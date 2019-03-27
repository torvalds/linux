#	$OpenBSD: connect-privsep.sh,v 1.9 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="proxy connect with privsep"

cp $OBJ/sshd_proxy $OBJ/sshd_proxy.orig
echo 'UsePrivilegeSeparation yes' >> $OBJ/sshd_proxy

${SSH} -F $OBJ/ssh_proxy 999.999.999.999 true
if [ $? -ne 0 ]; then
	fail "ssh privsep+proxyconnect failed"
fi

cp $OBJ/sshd_proxy.orig $OBJ/sshd_proxy
echo 'UsePrivilegeSeparation sandbox' >> $OBJ/sshd_proxy

${SSH} -F $OBJ/ssh_proxy 999.999.999.999 true
if [ $? -ne 0 ]; then
	# XXX replace this with fail once sandbox has stabilised
	warn "ssh privsep/sandbox+proxyconnect failed"
fi

# Because sandbox is sensitive to changes in libc, especially malloc, retest
# with every malloc.conf option (and none).
if [ -z "TEST_MALLOC_OPTIONS" ]; then
	mopts="C F G J R S U X < >"
else
	mopts=`echo $TEST_MALLOC_OPTIONS | sed 's/./& /g'`
fi
for m in '' $mopts ; do
	env MALLOC_OPTIONS="$m" ${SSH} -F $OBJ/ssh_proxy 999.999.999.999 true
	if [ $? -ne 0 ]; then
		fail "ssh privsep/sandbox+proxyconnect mopt '$m' failed"
	fi
done
