#	$OpenBSD: multiplex.sh,v 1.28 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

make_tmpdir
CTL=${SSH_REGRESS_TMP}/ctl-sock

tid="connection multiplexing"

NC=$OBJ/netcat

trace "will use ProxyCommand $proxycmd"
if config_defined DISABLE_FD_PASSING ; then
	echo "skipped (not supported on this platform)"
	exit 0
fi

P=3301  # test port

wait_for_mux_master_ready()
{
	for i in 1 2 3 4 5; do
		${SSH} -F $OBJ/ssh_config -S $CTL -Ocheck otherhost \
		    >/dev/null 2>&1 && return 0
		sleep $i
	done
	fatal "mux master never becomes ready"
}

start_sshd

start_mux_master()
{
	trace "start master, fork to background"
	${SSH} -Nn2 -MS$CTL -F $OBJ/ssh_config -oSendEnv="_XXX_TEST" somehost \
	    -E $TEST_REGRESS_LOGFILE 2>&1 &
	# NB. $SSH_PID will be killed by test-exec.sh:cleanup on fatal errors.
	SSH_PID=$!
	wait_for_mux_master_ready
}

start_mux_master

verbose "test $tid: envpass"
trace "env passing over multiplexed connection"
_XXX_TEST=blah ${SSH} -F $OBJ/ssh_config -oSendEnv="_XXX_TEST" -S$CTL otherhost sh << 'EOF'
	test X"$_XXX_TEST" = X"blah"
EOF
if [ $? -ne 0 ]; then
	fail "environment not found"
fi

verbose "test $tid: transfer"
rm -f ${COPY}
trace "ssh transfer over multiplexed connection and check result"
${SSH} -F $OBJ/ssh_config -S$CTL otherhost cat ${DATA} > ${COPY}
test -f ${COPY}			|| fail "ssh -Sctl: failed copy ${DATA}" 
cmp ${DATA} ${COPY}		|| fail "ssh -Sctl: corrupted copy of ${DATA}"

rm -f ${COPY}
trace "ssh transfer over multiplexed connection and check result"
${SSH} -F $OBJ/ssh_config -S $CTL otherhost cat ${DATA} > ${COPY}
test -f ${COPY}			|| fail "ssh -S ctl: failed copy ${DATA}" 
cmp ${DATA} ${COPY}		|| fail "ssh -S ctl: corrupted copy of ${DATA}"

rm -f ${COPY}
trace "sftp transfer over multiplexed connection and check result"
echo "get ${DATA} ${COPY}" | \
	${SFTP} -S ${SSH} -F $OBJ/ssh_config -oControlPath=$CTL otherhost >>$TEST_REGRESS_LOGFILE 2>&1
test -f ${COPY}			|| fail "sftp: failed copy ${DATA}" 
cmp ${DATA} ${COPY}		|| fail "sftp: corrupted copy of ${DATA}"

rm -f ${COPY}
trace "scp transfer over multiplexed connection and check result"
${SCP} -S ${SSH} -F $OBJ/ssh_config -oControlPath=$CTL otherhost:${DATA} ${COPY} >>$TEST_REGRESS_LOGFILE 2>&1
test -f ${COPY}			|| fail "scp: failed copy ${DATA}" 
cmp ${DATA} ${COPY}		|| fail "scp: corrupted copy of ${DATA}"

rm -f ${COPY}
verbose "test $tid: forward"
trace "forward over TCP/IP and check result"
$NC -N -l 127.0.0.1 $((${PORT} + 1)) < ${DATA} > /dev/null &
netcat_pid=$!
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -L127.0.0.1:$((${PORT} + 2)):127.0.0.1:$((${PORT} + 1)) otherhost >>$TEST_SSH_LOGFILE 2>&1
$NC 127.0.0.1 $((${PORT} + 2)) < /dev/null > ${COPY}
cmp ${DATA} ${COPY}		|| fail "ssh: corrupted copy of ${DATA}"
kill $netcat_pid 2>/dev/null
rm -f ${COPY} $OBJ/unix-[123].fwd

trace "forward over UNIX and check result"
$NC -N -Ul $OBJ/unix-1.fwd < ${DATA} > /dev/null &
netcat_pid=$!
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -L$OBJ/unix-2.fwd:$OBJ/unix-1.fwd otherhost >>$TEST_SSH_LOGFILE 2>&1
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -R$OBJ/unix-3.fwd:$OBJ/unix-2.fwd otherhost >>$TEST_SSH_LOGFILE 2>&1
$NC -U $OBJ/unix-3.fwd < /dev/null > ${COPY} 2>/dev/null
cmp ${DATA} ${COPY}		|| fail "ssh: corrupted copy of ${DATA}"
kill $netcat_pid 2>/dev/null
rm -f ${COPY} $OBJ/unix-[123].fwd

for s in 0 1 4 5 44; do
	trace "exit status $s over multiplexed connection"
	verbose "test $tid: status $s"
	${SSH} -F $OBJ/ssh_config -S $CTL otherhost exit $s
	r=$?
	if [ $r -ne $s ]; then
		fail "exit code mismatch: $r != $s"
	fi

	# same with early close of stdout/err
	trace "exit status $s with early close over multiplexed connection"
	${SSH} -F $OBJ/ssh_config -S $CTL -n otherhost \
                exec sh -c \'"sleep 2; exec > /dev/null 2>&1; sleep 3; exit $s"\'
	r=$?
	if [ $r -ne $s ]; then
		fail "exit code (with sleep) mismatch: $r != $s"
	fi
done

verbose "test $tid: cmd check"
${SSH} -F $OBJ/ssh_config -S $CTL -Ocheck otherhost >>$TEST_REGRESS_LOGFILE 2>&1 \
    || fail "check command failed" 

verbose "test $tid: cmd forward local (TCP)"
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -L $P:localhost:$PORT otherhost \
     || fail "request local forward failed"
${SSH} -F $OBJ/ssh_config -p$P otherhost true \
     || fail "connect to local forward port failed"
${SSH} -F $OBJ/ssh_config -S $CTL -Ocancel -L $P:localhost:$PORT otherhost \
     || fail "cancel local forward failed"
${SSH} -F $OBJ/ssh_config -p$P otherhost true \
     && fail "local forward port still listening"

verbose "test $tid: cmd forward remote (TCP)"
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -R $P:localhost:$PORT otherhost \
     || fail "request remote forward failed"
${SSH} -F $OBJ/ssh_config -p$P otherhost true \
     || fail "connect to remote forwarded port failed"
${SSH} -F $OBJ/ssh_config -S $CTL -Ocancel -R $P:localhost:$PORT otherhost \
     || fail "cancel remote forward failed"
${SSH} -F $OBJ/ssh_config -p$P otherhost true \
     && fail "remote forward port still listening"

verbose "test $tid: cmd forward local (UNIX)"
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -L $OBJ/unix-1.fwd:localhost:$PORT otherhost \
     || fail "request local forward failed"
echo "" | $NC -U $OBJ/unix-1.fwd | grep "Protocol mismatch" >/dev/null 2>&1 \
     || fail "connect to local forward path failed"
${SSH} -F $OBJ/ssh_config -S $CTL -Ocancel -L $OBJ/unix-1.fwd:localhost:$PORT otherhost \
     || fail "cancel local forward failed"
N=$(echo "xyzzy" | $NC -U $OBJ/unix-1.fwd 2>&1 | grep "xyzzy" | wc -l)
test ${N} -eq 0 || fail "local forward path still listening"
rm -f $OBJ/unix-1.fwd

verbose "test $tid: cmd forward remote (UNIX)"
${SSH} -F $OBJ/ssh_config -S $CTL -Oforward -R $OBJ/unix-1.fwd:localhost:$PORT otherhost \
     || fail "request remote forward failed"
echo "" | $NC -U $OBJ/unix-1.fwd | grep "Protocol mismatch" >/dev/null 2>&1 \
     || fail "connect to remote forwarded path failed"
${SSH} -F $OBJ/ssh_config -S $CTL -Ocancel -R $OBJ/unix-1.fwd:localhost:$PORT otherhost \
     || fail "cancel remote forward failed"
N=$(echo "xyzzy" | $NC -U $OBJ/unix-1.fwd 2>&1 | grep "xyzzy" | wc -l)
test ${N} -eq 0 || fail "remote forward path still listening"
rm -f $OBJ/unix-1.fwd

verbose "test $tid: cmd exit"
${SSH} -F $OBJ/ssh_config -S $CTL -Oexit otherhost >>$TEST_REGRESS_LOGFILE 2>&1 \
    || fail "send exit command failed" 

# Wait for master to exit
wait $SSH_PID
kill -0 $SSH_PID >/dev/null 2>&1 && fail "exit command failed"

# Restart master and test -O stop command with master using -N
verbose "test $tid: cmd stop"
trace "restart master, fork to background"
start_mux_master

# start a long-running command then immediately request a stop
${SSH} -F $OBJ/ssh_config -S $CTL otherhost "sleep 10; exit 0" \
     >>$TEST_REGRESS_LOGFILE 2>&1 &
SLEEP_PID=$!
${SSH} -F $OBJ/ssh_config -S $CTL -Ostop otherhost >>$TEST_REGRESS_LOGFILE 2>&1 \
    || fail "send stop command failed"

# wait until both long-running command and master have exited.
wait $SLEEP_PID
[ $! != 0 ] || fail "waiting for concurrent command"
wait $SSH_PID
[ $! != 0 ] || fail "waiting for master stop"
kill -0 $SSH_PID >/dev/null 2>&1 && fatal "stop command failed"
SSH_PID="" # Already gone, so don't kill in cleanup

