#	$OpenBSD: forwarding.sh,v 1.20 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="local and remote forwarding"

DATA=/bin/ls${EXEEXT}

start_sshd

base=33
last=$PORT
fwd=""
make_tmpdir
CTL=${SSH_REGRESS_TMP}/ctl-sock

for j in 0 1 2; do
	for i in 0 1 2; do
		a=$base$j$i
		b=`expr $a + 50`
		c=$last
		# fwd chain: $a -> $b -> $c
		fwd="$fwd -L$a:127.0.0.1:$b -R$b:127.0.0.1:$c"
		last=$a
	done
done

trace "start forwarding, fork to background"
rm -f $CTL
${SSH} -S $CTL -M -F $OBJ/ssh_config -f $fwd somehost sleep 10

trace "transfer over forwarded channels and check result"
${SSH} -F $OBJ/ssh_config -p$last -o 'ConnectionAttempts=4' \
	somehost cat ${DATA} > ${COPY}
test -s ${COPY}		|| fail "failed copy of ${DATA}"
cmp ${DATA} ${COPY}	|| fail "corrupted copy of ${DATA}"

${SSH} -F $OBJ/ssh_config -S $CTL -O exit somehost

for d in L R; do
	trace "exit on -$d forward failure"

	# this one should succeed
	${SSH}  -F $OBJ/ssh_config \
	    -$d ${base}01:127.0.0.1:$PORT \
	    -$d ${base}02:127.0.0.1:$PORT \
	    -$d ${base}03:127.0.0.1:$PORT \
	    -$d ${base}04:127.0.0.1:$PORT \
	    -oExitOnForwardFailure=yes somehost true
	if [ $? != 0 ]; then
		fatal "connection failed, should not"
	else
		# this one should fail
		${SSH} -q -F $OBJ/ssh_config \
		    -$d ${base}01:127.0.0.1:$PORT \
		    -$d ${base}02:127.0.0.1:$PORT \
		    -$d ${base}03:127.0.0.1:$PORT \
		    -$d ${base}01:localhost:$PORT \
		    -$d ${base}04:127.0.0.1:$PORT \
		    -oExitOnForwardFailure=yes somehost true
		r=$?
		if [ $r != 255 ]; then
			fail "connection not termintated, but should ($r)"
		fi
	fi
done

trace "simple clear forwarding"
${SSH} -F $OBJ/ssh_config -oClearAllForwardings=yes somehost true

trace "clear local forward"
rm -f $CTL
${SSH} -S $CTL -M -f -F $OBJ/ssh_config -L ${base}01:127.0.0.1:$PORT \
    -oClearAllForwardings=yes somehost sleep 10
if [ $? != 0 ]; then
	fail "connection failed with cleared local forwarding"
else
	# this one should fail
	${SSH} -F $OBJ/ssh_config -p ${base}01 somehost true \
	     >>$TEST_REGRESS_LOGFILE 2>&1 && \
		fail "local forwarding not cleared"
fi
${SSH} -F $OBJ/ssh_config -S $CTL -O exit somehost

trace "clear remote forward"
rm -f $CTL
${SSH} -S $CTL -M -f -F $OBJ/ssh_config -R ${base}01:127.0.0.1:$PORT \
    -oClearAllForwardings=yes somehost sleep 10
if [ $? != 0 ]; then
	fail "connection failed with cleared remote forwarding"
else
	# this one should fail
	${SSH} -F $OBJ/ssh_config -p ${base}01 somehost true \
	     >>$TEST_REGRESS_LOGFILE 2>&1 && \
		fail "remote forwarding not cleared"
fi
${SSH} -F $OBJ/ssh_config -S $CTL -O exit somehost

trace "stdio forwarding"
cmd="${SSH} -F $OBJ/ssh_config"
$cmd -o "ProxyCommand $cmd -q -W localhost:$PORT somehost" somehost true
if [ $? != 0 ]; then
	fail "stdio forwarding"
fi

echo "LocalForward ${base}01 127.0.0.1:$PORT" >> $OBJ/ssh_config
echo "RemoteForward ${base}02 127.0.0.1:${base}01" >> $OBJ/ssh_config

trace "config file: start forwarding, fork to background"
rm -f $CTL
${SSH} -S $CTL -M -F $OBJ/ssh_config -f somehost sleep 10

trace "config file: transfer over forwarded channels and check result"
${SSH} -F $OBJ/ssh_config -p${base}02 -o 'ConnectionAttempts=4' \
	somehost cat ${DATA} > ${COPY}
test -s ${COPY}		|| fail "failed copy of ${DATA}"
cmp ${DATA} ${COPY}	|| fail "corrupted copy of ${DATA}"

${SSH} -F $OBJ/ssh_config -S $CTL -O exit somehost

trace "transfer over chained unix domain socket forwards and check result"
rm -f $OBJ/unix-[123].fwd
rm -f $CTL $CTL.[123]
${SSH} -S $CTL -M -f -F $OBJ/ssh_config -R${base}01:[$OBJ/unix-1.fwd] somehost sleep 10
${SSH} -S $CTL.1 -M -f -F $OBJ/ssh_config -L[$OBJ/unix-1.fwd]:[$OBJ/unix-2.fwd] somehost sleep 10
${SSH} -S $CTL.2 -M -f -F $OBJ/ssh_config -R[$OBJ/unix-2.fwd]:[$OBJ/unix-3.fwd] somehost sleep 10
${SSH} -S $CTL.3 -M -f -F $OBJ/ssh_config -L[$OBJ/unix-3.fwd]:127.0.0.1:$PORT somehost sleep 10
${SSH} -F $OBJ/ssh_config -p${base}01 -o 'ConnectionAttempts=4' \
	somehost cat ${DATA} > ${COPY}
test -s ${COPY}			|| fail "failed copy ${DATA}"
cmp ${DATA} ${COPY}		|| fail "corrupted copy of ${DATA}"

${SSH} -F $OBJ/ssh_config -S $CTL -O exit somehost
${SSH} -F $OBJ/ssh_config -S $CTL.1 -O exit somehost
${SSH} -F $OBJ/ssh_config -S $CTL.2 -O exit somehost
${SSH} -F $OBJ/ssh_config -S $CTL.3 -O exit somehost

