#	$OpenBSD: reconfigure.sh,v 1.6 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="simple connect after reconfigure"

# we need the full path to sshd for -HUP
if test "x$USE_VALGRIND" = "x" ; then
	case $SSHD in
	/*)
		# full path is OK
		;;
	*)
		# otherwise make fully qualified
		SSHD=$OBJ/$SSHD
	esac
fi

start_sshd

trace "connect before restart"
${SSH} -F $OBJ/ssh_config somehost true
if [ $? -ne 0 ]; then
	fail "ssh connect with failed before reconfigure"
fi

PID=`$SUDO cat $PIDFILE`
rm -f $PIDFILE
$SUDO kill -HUP $PID

trace "wait for sshd to restart"
i=0;
while [ ! -f $PIDFILE -a $i -lt 10 ]; do
	i=`expr $i + 1`
	sleep $i
done

test -f $PIDFILE || fatal "sshd did not restart"

trace "connect after restart"
${SSH} -F $OBJ/ssh_config somehost true
if [ $? -ne 0 ]; then
	fail "ssh connect with failed after reconfigure"
fi
