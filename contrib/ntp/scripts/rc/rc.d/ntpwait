#!/bin/sh
# This script, when run, runs ntp-wait if ntpd is enabled.

# PROVIDE: ntpwait

. /etc/rc.subr

name="ntpwait"
rcvar="ntpwait_enable"
start_cmd="ntpwait_start"
ntp_wait="/usr/sbin/ntp-wait"

load_rc_config "$name"

ntpwait_start() {
        if checkyesno ntpd_enable; then
                $ntp_wait -v
        fi
}

run_rc_command "$1"
