#!/bin/sh

NTPD=/usr/sbin/ntpd
PIDFILE=/var/run/ntpd.pid
USER=ntp
GROUP=ntp
NTPD_OPTS="-g -u $USER:$GROUP -p $PIDFILE"

ntpd_start() {
    if [ -r $PIDFILE ]; then
        echo "ntpd seems to be already running under pid `cat $PIDFILE`."
        echo "Delete $PIDFILE if this is not the case.";
        return 1;
    fi
    echo -n "Starting NTP daemon... "

    $NTPD $NTPD_OPTS

    # You can't always rely on the ntpd exit code, see Bug #2420
    # case "$?" in
    #     0) echo "OK!"
    #         return 0;;
    #     *) echo "FAILED!"
    #         return 1;;
    # esac

    sleep 1

    if ps -Ao args|grep -q "^$NTPD $NTPD_OPTS"; then
        echo "OK!"
        return 0
    else
        echo "FAILED!"
        [ -e $PIDFILE ] && rm $PIDFILE
        return 1
    fi
}

ntpd_stop() {
    if [ ! -r $PIDFILE ]; then
        echo "ntpd doesn't seem to be running, cannot read the pid file."
        return 1;
    fi
    echo -n "Stopping NTP daemon...";
    PID=`cat $PIDFILE`

    if kill -TERM $PID 2> /dev/null;then
        # Give ntp 15 seconds to exit
        for i in `seq 1 15`; do
            if [ -n "`ps -p $PID|grep -v PID`" ]; then
                echo -n .
                sleep 1
            else
                echo " OK!"
                rm $PIDFILE
                return 0
            fi
        done
    fi

    echo " FAILED! ntpd is still running";
    return 1
}

ntpd_status() {
    if [ -r $PIDFILE ]; then
        echo "NTP daemon is running as `cat $PIDFILE`"
    else
        echo "NTP daemon is not running"
    fi
}

case "$1" in
    'start')
        ntpd_start
        ;;
    'stop')
        ntpd_stop
        ;;
    'restart')
        ntpd_stop && ntpd_start
        ;;
    'status')
        ntpd_status
        ;;
    *)
        echo "Usage: $0 (start|stop|restart|status)"
esac
