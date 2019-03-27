#!/bin/sh

NTPWAIT=/usr/sbin/ntpwait

ntpwait_start() {
    $NTPWAIT -v
}

case "$1" in
    'start')
        ntpwait_start
        ;;
    *)
        echo "Usage: $0 (start)"
esac
