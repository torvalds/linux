#!/bin/sh
#
# This script can be used to kill and restart the NTP daemon. Edit the
# /usr/local/bin/ntpd line to fit.
#
kill -INT `ps -ax | egrep "ntpd" | egrep -v "egrep" | sed 's/^\([ 0-9]*\) .*/\1'/`
sleep 10
/usr/local/bin/ntpd -g
/usr/local/bin/ntp-wait
exit 0
