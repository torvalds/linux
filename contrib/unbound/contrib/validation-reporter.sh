#!/bin/sh
# validation reporter - reports validation failures to a collection server.
# Copyright NLnet Labs, 2010
# BSD license.


###
# Here is the configuration for the validation reporter
# it greps the failure lines out of the log and sends them to a server.

# The pidfile for the reporter daemon.
pidfile="/var/run/validation-reporter.pid"

# The logfile to watch for logged validation failures.
logfile="/var/log/unbound.log"

# how to notify the upstream 
# nc is netcat, it sends tcp to given host port.  It makes a tcp connection
# and writes one log-line to it (grepped from the logfile).
# the notify command can be: "nc the.server.name.org 1234"
# the listening daemon could be:  nc -lk 127.0.0.1 1234 >> outputfile &
notify_cmd="nc localhost 1234"


###
# Below this line is the code for the validation reporter,
# first the daemon itself, then the controller for the daemon.
reporter_daemon() {
	trap "rm -f \"$pidfile\"" EXIT
	tail -F $logfile | grep --line-buffered "unbound.*info: validation failure" | \
	while read x; do
		echo "$x" | $notify_cmd
	done
}


###
# controller for daemon.
start_daemon() {
	echo "starting reporter"
	nohup $0 rundaemon </dev/null >/dev/null 2>&1 &
	echo $! > "$pidfile"
}

kill_daemon() {
	echo "stopping reporter"
	if test -s "$pidfile"; then
		kill `cat "$pidfile"`
		# check it is really dead
		if kill -0 `cat "$pidfile"` >/dev/null 2>&1; then
			sleep 1
			while kill -0 `cat "$pidfile"` >/dev/null 2>&1; do
				kill `cat "$pidfile"` >/dev/null 2>&1
				echo "waiting for reporter to stop"
				sleep 1
			done
		fi
	fi
}

get_status_daemon() {
	if test -s "$pidfile"; then
		if kill -0 `cat "$pidfile"`; then
			return 0;
		fi
	fi
	return 1;
}

restart_daemon() {
	kill_daemon
	start_daemon
}

condrestart_daemon() {
	if get_status_daemon; then
		echo "reporter ("`cat "$pidfile"`") is running"
		exit 0
	fi
	start_daemon
	exit 0
}

status_daemon() {
	if get_status_daemon; then
		echo "reporter ("`cat "$pidfile"`") is running"
		exit 0
	fi
	echo "reporter is not running"
	exit 1
}

case "$1" in
	rundaemon)
		reporter_daemon
	;;
	start)
		start_daemon
	;;
	stop)
		kill_daemon
	;;
	restart)
		restart_daemon
	;;
	condrestart)
		condrestart_daemon
	;;
	status)
		status_daemon
	;;
	*)
		echo "Usage: $0 {start|stop|restart|condrestart|status}"
		exit 2
	;;
esac
exit $?
