/*
 * Copyright (c) 2014      BalaBit S.a.r.l., Luxembourg, Luxembourg
 * Copyright (c) 2002-2014 BalaBit IT Ltd, Budapest, Hungary
 * Copyright (c) 2013-2014 Benke Tibor
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */


#include "systemd-syslog-source.h"
#include "afunix-source.h"
#include "afsocket-systemd-override.h"
#include "service-management.h"

#if ENABLE_SYSTEMD
#include <systemd/sd-daemon.h>
#include <stdlib.h>
#include <unistd.h>
#include "misc.h"


static gboolean
systemd_syslog_sd_acquire_socket(AFSocketSourceDriver *s,
                          gint *acquired_fd)
{
  gint fd, number_of_fds;

  *acquired_fd = -1;
  fd = -1;

  number_of_fds = sd_listen_fds(0);

  if (number_of_fds > 1)
    {
      msg_error("Systemd socket activation failed: got more than one fd",
                evt_tag_int("number", number_of_fds),
                NULL);

      return TRUE;
    }
  else if (number_of_fds < 1)
    {
      msg_error("Failed to acquire systemd sockets, disabling systemd-syslog source",
                NULL);
      return TRUE;
    }
  else
    {
      fd = SD_LISTEN_FDS_START;
      msg_debug("Systemd socket activation",
                evt_tag_int("file-descriptor", fd),
                NULL);

      if (sd_is_socket_unix(fd, SOCK_DGRAM, -1, NULL, 0))
        {
          *acquired_fd = fd;
        }
      else
        {
          msg_error("The systemd supplied UNIX domain socket is of a"
                    " different type, check the configured driver and"
                    " the matching systemd unit file",
                    evt_tag_int("systemd-sock-fd", fd),
                    evt_tag_str("expecting", "unix-dgram()"),
                    NULL);
          *acquired_fd = -1;  
          return TRUE;
        }
    }

  if (*acquired_fd != -1)
    {
      g_fd_set_nonblock(*acquired_fd, TRUE);
      msg_verbose("Acquired systemd syslog socket",
                  evt_tag_int("systemd-syslog-sock-fd", *acquired_fd),
                  NULL);
      return TRUE;
    }

  return TRUE;
}


#else

static gboolean
systemd_syslog_sd_acquire_socket(AFSocketSourceDriver *s, gint *acquired_fd)
{
  return TRUE;
}
#endif

static gboolean
systemd_syslog_sd_fallback_init_method(LogPipe *s)
{
  SystemDSyslogSourceDriver *self = (SystemDSyslogSourceDriver*) s;

  msg_warning("systemd-syslog() source ignores configuration options. "
              "Please, do not set anything on it",
              NULL);

  socket_options_free(self->super.socket_options);
  self->super.socket_options = socket_options_new();
  socket_options_init_instance(self->super.socket_options);

  return afsocket_sd_init_method((LogPipe*) &self->super);
}

SystemDSyslogSourceDriver*
systemd_syslog_sd_new(GlobalConfig *cfg, gboolean fallback)
{
  SystemDSyslogSourceDriver *self;
  TransportMapper *transport_mapper;

#if ! ENABLE_SYSTEMD
  msg_error("systemd-syslog() source cannot be enabled and it is not"
            " functioning. Please compile your syslog-ng with --enable-systemd"
            " flag",
            NULL);
#endif

  self = g_new0(SystemDSyslogSourceDriver, 1);
  transport_mapper = transport_mapper_unix_dgram_new();

  afsocket_sd_init_instance(&self->super, socket_options_new(), transport_mapper, cfg);

  if (fallback)
    self->super.super.super.super.init = systemd_syslog_sd_fallback_init_method;

  self->super.acquire_socket = systemd_syslog_sd_acquire_socket;
  self->super.max_connections = 256;
  self->super.recvd_messages_are_local = TRUE;

  if (!self->super.bind_addr)
    self->super.bind_addr = g_sockaddr_unix_new(NULL);

  return self;
}

AFSocketSourceDriver*
create_and_set_unix_dgram_or_systemd_source(gchar *filename, GlobalConfig *cfg)
{
  SystemDSyslogSourceDriver *sd;
  AFUnixSourceDriver *ud;

  if (service_management_get_type() == SMT_SYSTEMD && strncmp("/dev/log", filename, 9) == 0)
    {
      msg_warning("Using /dev/log Unix dgram socket with systemd is not"
                " possible. Changing to systemd source, which supports"
                " socket activation.",
                NULL);
      
      sd = systemd_syslog_sd_new(configuration, TRUE);
      systemd_syslog_grammar_set_source_driver(sd);
      return &sd->super;
    }
  else
    {
      ud = afunix_sd_new_dgram(filename, configuration);
      afunix_grammar_set_source_driver(ud);
      return &ud->super;
    }
}

