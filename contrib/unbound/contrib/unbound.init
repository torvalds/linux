#!/bin/sh
#
# unbound	This shell script takes care of starting and stopping
#		unbound (DNS server).
#
# chkconfig:   - 14 86
# description:	unbound is a Domain Name Server (DNS) \
#		that is used to resolve host names to IP addresses.

### BEGIN INIT INFO
# Provides: $named unbound
# Required-Start: $network $local_fs
# Required-Stop: $network $local_fs
# Should-Start: $syslog
# Should-Stop: $syslog
# Short-Description: unbound recursive Domain Name Server.
# Description:  unbound is a Domain Name Server (DNS) 
#		that is used to resolve host names to IP addresses.
### END INIT INFO

# Source function library.
. /etc/rc.d/init.d/functions

exec="/usr/sbin/unbound"
prog="unbound"
config="/var/unbound/unbound.conf"
pidfile="/var/unbound/unbound.pid"
rootdir="/var/unbound"

[ -e /etc/sysconfig/$prog ] && . /etc/sysconfig/$prog

lockfile=/var/lock/subsys/$prog

start() {
    [ -x $exec ] || exit 5
    [ -f $config ] || exit 6
    echo -n $"Starting $prog: "

    # setup root jail
    if [ -s /etc/localtime ]; then 
	[ -d ${rootdir}/etc ] || mkdir -p ${rootdir}/etc ;
	if [ ! -e ${rootdir}/etc/localtime ] || /usr/bin/cmp -s /etc/localtime ${rootdir}/etc/localtime; then
	    cp -fp /etc/localtime ${rootdir}/etc/localtime
	fi;
    fi;
    if [ -s /etc/resolv.conf ]; then
	[ -d ${rootdir}/etc ] || mkdir -p ${rootdir}/etc ;
	if [ ! -e ${rootdir}/etc/resolv.conf ] || /usr/bin/cmp -s /etc/resolv.conf ${rootdir}/etc/resolv.conf; then
	    cp -fp /etc/resolv.conf ${rootdir}/etc/resolv.conf
	fi;
    fi;
    if ! egrep -q '^/[^[:space:]]+[[:space:]]+'${rootdir}'/dev/log' /proc/mounts; then
	[ -d ${rootdir}/dev ] || mkdir -p ${rootdir}/dev ;
	[ -e ${rootdir}/dev/log ] || touch ${rootdir}/dev/log
	mount --bind -n /dev/log ${rootdir}/dev/log >/dev/null 2>&1;
    fi;
    if ! egrep -q '^/[^[:space:]]+[[:space:]]+'${rootdir}'/dev/random' /proc/mounts; then
	[ -d ${rootdir}/dev ] || mkdir -p ${rootdir}/dev ;
	[ -e ${rootdir}/dev/random ] || touch ${rootdir}/dev/random
	mount --bind -n /dev/random ${rootdir}/dev/random >/dev/null 2>&1;
    fi;

    # if not running, start it up here
    daemon $exec
    retval=$?
    echo
    [ $retval -eq 0 ] && touch $lockfile
    return $retval
}

stop() {
    echo -n $"Stopping $prog: "
    # stop it here, often "killproc $prog"
    killproc -p $pidfile $prog
    retval=$?
    echo
    [ $retval -eq 0 ] && rm -f $lockfile
    if egrep -q '^/[^[:space:]]+[[:space:]]+'${rootdir}'/dev/log' /proc/mounts; then
	umount ${rootdir}/dev/log >/dev/null 2>&1
    fi;
    if egrep -q '^/[^[:space:]]+[[:space:]]+'${rootdir}'/dev/random' /proc/mounts; then
	umount ${rootdir}/dev/random >/dev/null 2>&1
    fi;
    return $retval
}

restart() {
    stop
    start
}

reload() {
    kill -HUP `cat $pidfile`
}

force_reload() {
    restart
}

rh_status() {
    # run checks to determine if the service is running or use generic status
    status -p $pidfile $prog
}

rh_status_q() {
    rh_status -p $pidfile >/dev/null 2>&1
}

case "$1" in
    start)
        rh_status_q && exit 0
        $1
        ;;
    stop)
        rh_status_q || exit 0
        $1
        ;;
    restart)
        $1
        ;;
    reload)
        rh_status_q || exit 7
        $1
        ;;
    force-reload)
        force_reload
        ;;
    status)
        rh_status
        ;;
    condrestart|try-restart)
        rh_status_q || exit 0
        restart
        ;;
    *)
        echo $"Usage: $0 {start|stop|status|restart|condrestart|try-restart|reload|force-reload}"
        exit 2
esac
exit $?
