#!/bin/bash
#
# Init file for OpenSSH server daemon
#
# chkconfig: 2345 55 25
# description: OpenSSH server daemon
#
# processname: sshd
# config: /etc/ssh/ssh_host_key
# config: /etc/ssh/ssh_host_key.pub
# config: /etc/ssh/ssh_random_seed
# config: /etc/ssh/sshd_config
# pidfile: /var/run/sshd.pid

# source function library
. /etc/rc.d/init.d/functions

# pull in sysconfig settings
[ -f /etc/sysconfig/sshd ] && . /etc/sysconfig/sshd

RETVAL=0
prog="sshd"

# Some functions to make the below more readable
SSHD=/usr/sbin/sshd
PID_FILE=/var/run/sshd.pid

do_restart_sanity_check()
{
	$SSHD -t
	RETVAL=$?
	if [ $RETVAL -ne 0 ]; then
		failure $"Configuration file or keys are invalid"
		echo
	fi
}

start()
{
	# Create keys if necessary
	/usr/bin/ssh-keygen -A
	if [ -x /sbin/restorecon ]; then
		/sbin/restorecon /etc/ssh/ssh_host_rsa_key.pub
		/sbin/restorecon /etc/ssh/ssh_host_dsa_key.pub
		/sbin/restorecon /etc/ssh/ssh_host_ecdsa_key.pub
	fi

	echo -n $"Starting $prog:"
	$SSHD $OPTIONS && success || failure
	RETVAL=$?
	[ $RETVAL -eq 0 ] && touch /var/lock/subsys/sshd
	echo
}

stop()
{
	echo -n $"Stopping $prog:"
	killproc $SSHD -TERM
	RETVAL=$?
	[ $RETVAL -eq 0 ] && rm -f /var/lock/subsys/sshd
	echo
}

reload()
{
	echo -n $"Reloading $prog:"
	killproc $SSHD -HUP
	RETVAL=$?
	echo
}

case "$1" in
	start)
		start
		;;
	stop)
		stop
		;;
	restart)
		stop
		start
		;;
	reload)
		reload
		;;
	condrestart)
		if [ -f /var/lock/subsys/sshd ] ; then
			do_restart_sanity_check
			if [ $RETVAL -eq 0 ] ; then
				stop
				# avoid race
				sleep 3
				start
			fi
		fi
		;;
	status)
		status $SSHD
		RETVAL=$?
		;;
	*)
		echo $"Usage: $0 {start|stop|restart|reload|condrestart|status}"
		RETVAL=1
esac
exit $RETVAL
