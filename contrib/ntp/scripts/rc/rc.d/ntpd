#!/bin/sh

# PROVIDE: ntpd
# REQUIRE: syslogd cleanvar devfs
# BEFORE: SERVERS

. /etc/rc.subr

name="ntpd"
rcvar="ntpd_enable"
command="/usr/sbin/${name}"
pidfile="/var/run/${name}.pid"
start_precmd="ntpd_precmd"

load_rc_config $name

ntpd_precmd()
{
        rc_flags="-c ${ntpd_config} ${ntpd_flags}"

        if checkyesno ntpd_sync_on_start; then
                rc_flags="-g $rc_flags"
        fi

        if [ -z "$ntpd_chrootdir" ]; then
                return 0;
        fi

        rc_flags="-u ntpd:ntpd -i ${ntpd_chrootdir} $rc_flags"
}

run_rc_command "$1"
