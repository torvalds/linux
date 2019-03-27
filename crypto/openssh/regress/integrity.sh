#	$OpenBSD: integrity.sh,v 1.23 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="integrity"
cp $OBJ/sshd_proxy $OBJ/sshd_proxy_bak

# start at byte 2900 (i.e. after kex) and corrupt at different offsets
tries=10
startoffset=2900
macs=`${SSH} -Q mac`
# The following are not MACs, but ciphers with integrated integrity. They are
# handled specially below.
macs="$macs `${SSH} -Q cipher-auth`"

# avoid DH group exchange as the extra traffic makes it harder to get the
# offset into the stream right.
echo "KexAlgorithms diffie-hellman-group14-sha1,diffie-hellman-group1-sha1" \
	>> $OBJ/ssh_proxy

# sshd-command for proxy (see test-exec.sh)
cmd="$SUDO sh ${SRC}/sshd-log-wrapper.sh ${TEST_SSHD_LOGFILE} ${SSHD} -i -f $OBJ/sshd_proxy"

for m in $macs; do
	trace "test $tid: mac $m"
	elen=0
	epad=0
	emac=0
	etmo=0
	ecnt=0
	skip=0
	for off in `jot $tries $startoffset`; do
		skip=`expr $skip - 1`
		if [ $skip -gt 0 ]; then
			# avoid modifying the high bytes of the length
			continue
		fi
		cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
		# modify output from sshd at offset $off
		pxy="proxycommand=$cmd | $OBJ/modpipe -wm xor:$off:1"
		if ${SSH} -Q cipher-auth | grep "^${m}\$" >/dev/null 2>&1 ; then
			echo "Ciphers=$m" >> $OBJ/sshd_proxy
			macopt="-c $m"
		else
			echo "Ciphers=aes128-ctr" >> $OBJ/sshd_proxy
			echo "MACs=$m" >> $OBJ/sshd_proxy
			macopt="-m $m -c aes128-ctr"
		fi
		verbose "test $tid: $m @$off"
		${SSH} $macopt -F $OBJ/ssh_proxy -o "$pxy" \
		    -oServerAliveInterval=1 -oServerAliveCountMax=30 \
		    999.999.999.999 'printf "%4096s" " "' >/dev/null
		if [ $? -eq 0 ]; then
			fail "ssh -m $m succeeds with bit-flip at $off"
		fi
		ecnt=`expr $ecnt + 1`
		out=$(egrep -v "^debug" $TEST_SSH_LOGFILE | tail -2 | \
		     tr -s '\r\n' '.')
		case "$out" in
		Bad?packet*)	elen=`expr $elen + 1`; skip=3;;
		Corrupted?MAC* | *message?authentication?code?incorrect*)
				emac=`expr $emac + 1`; skip=0;;
		padding*)	epad=`expr $epad + 1`; skip=0;;
		*Timeout,?server*)
				etmo=`expr $etmo + 1`; skip=0;;
		*)		fail "unexpected error mac $m at $off: $out";;
		esac
	done
	verbose "test $tid: $ecnt errors: mac $emac padding $epad length $elen timeout $etmo"
	if [ $emac -eq 0 ]; then
		fail "$m: no mac errors"
	fi
	expect=`expr $ecnt - $epad - $elen - $etmo`
	if [ $emac -ne $expect ]; then
		fail "$m: expected $expect mac errors, got $emac"
	fi
done
