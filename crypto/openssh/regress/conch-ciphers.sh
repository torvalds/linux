#	$OpenBSD: conch-ciphers.sh,v 1.3 2013/05/17 04:29:14 dtucker Exp $
#	Placed in the Public Domain.

tid="conch ciphers"

if test "x$REGRESS_INTEROP_CONCH" != "xyes" ; then
	echo "conch interop tests not enabled"
	exit 0
fi

start_sshd

for c in aes256-ctr aes256-cbc aes192-ctr aes192-cbc aes128-ctr aes128-cbc \
         cast128-cbc blowfish 3des-cbc ; do
	verbose "$tid: cipher $c"
	rm -f ${COPY}
	# XXX the 2nd "cat" seems to be needed because of buggy FD handling
	# in conch
	${CONCH} --identity $OBJ/rsa --port $PORT --user $USER  -e none \
	    --known-hosts $OBJ/known_hosts --notty --noagent --nox11 -n \
	    127.0.0.1 "cat ${DATA}" 2>/dev/null | cat > ${COPY}
	if [ $? -ne 0 ]; then
		fail "ssh cat $DATA failed"
	fi
	cmp ${DATA} ${COPY}		|| fail "corrupted copy"
done
rm -f ${COPY}

