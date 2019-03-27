#	$OpenBSD: cfgmatchlisten.sh,v 1.3 2018/07/02 14:13:30 dtucker Exp $
#	Placed in the Public Domain.

tid="sshd_config matchlisten"

pidfile=$OBJ/remote_pid
fwdport=3301
fwdspec="localhost:${fwdport}"
fwd="-R $fwdport:127.0.0.1:$PORT"

echo "ExitOnForwardFailure=yes" >> $OBJ/ssh_config
echo "ExitOnForwardFailure=yes" >> $OBJ/ssh_proxy

start_client()
{
	rm -f $pidfile
	${SSH} -vvv $fwd "$@" somehost true >>$TEST_REGRESS_LOGFILE 2>&1
	r=$?
	if [ $r -ne 0 ]; then
		return $r
	fi
	${SSH} -vvv $fwd "$@" somehost \
	    exec sh -c \'"echo \$\$ > $pidfile; exec sleep 100"\' \
	    >>$TEST_REGRESS_LOGFILE 2>&1 &
	client_pid=$!
	# Wait for remote end
	n=0
	while test ! -f $pidfile ; do
		sleep 1
		n=`expr $n + 1`
		if test $n -gt 60; then
			kill $client_pid
			fatal "timeout waiting for background ssh"
		fi
	done
	return $r
}

expect_client_ok()
{
	start_client "$@" ||
	    fail "client did not start"
}

expect_client_fail()
{
	local failmsg="$1"
	shift
	start_client "$@" &&
	    fail $failmsg
}

stop_client()
{
	pid=`cat $pidfile`
	if [ ! -z "$pid" ]; then
		kill $pid
	fi
	wait
}

cp $OBJ/sshd_proxy $OBJ/sshd_proxy_bak
echo "PermitListen 127.0.0.1:1" >>$OBJ/sshd_config
echo "Match Address 127.0.0.1" >>$OBJ/sshd_config
echo "PermitListen 127.0.0.1:2 127.0.0.1:3 $fwdspec" >>$OBJ/sshd_config

grep -v AuthorizedKeysFile $OBJ/sshd_proxy_bak > $OBJ/sshd_proxy
echo "AuthorizedKeysFile /dev/null" >>$OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1" >>$OBJ/sshd_proxy
echo "Match user $USER" >>$OBJ/sshd_proxy
echo "AuthorizedKeysFile /dev/null $OBJ/authorized_keys_%u" >>$OBJ/sshd_proxy
echo "Match Address 127.0.0.1" >>$OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:2 127.0.0.1:3 $fwdspec" >>$OBJ/sshd_proxy

start_sshd

#set -x

# Test Match + PermitListen in sshd_config.  This should be permitted
trace "match permitlisten localhost"
expect_client_ok -F $OBJ/ssh_config
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "match permitlisten permit"
stop_client

# Same but from different source.  This should not be permitted
trace "match permitlisten proxy"
expect_client_fail "match permitlisten deny" \
    -F $OBJ/ssh_proxy

# Retry previous with key option, should also be denied.
cp /dev/null $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	printf 'permitlisten="'$fwdspec'" ' >> $OBJ/authorized_keys_$USER
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
done
trace "match permitlisten proxy w/key opts"
expect_client_fail "match permitlisten deny w/key opt"\
    -F $OBJ/ssh_proxy

# Test both sshd_config and key options permitting the same dst/port pair.
# Should be permitted.
trace "match permitlisten localhost"
expect_client_ok -F $OBJ/ssh_config
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "match permitlisten permit"
stop_client

# Test that a bare port number is accepted in PermitListen
cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 $fwdport 127.0.0.2:2" >>$OBJ/sshd_proxy
trace "match permitlisten bare"
expect_client_ok -F $OBJ/ssh_config
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "match permitlisten bare"
stop_client

# Test that an incorrect bare port number is denied as expected
cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 1 2 99" >>$OBJ/sshd_proxy
trace "match permitlisten bare"
expect_client_fail -F $OBJ/ssh_config

cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 $fwdspec 127.0.0.2:2" >>$OBJ/sshd_proxy
echo "Match User $USER" >>$OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 127.0.0.1:2" >>$OBJ/sshd_proxy

# Test that a Match overrides a PermitListen in the global section
trace "match permitlisten proxy w/key opts"
expect_client_fail "match override permitlisten" \
    -F $OBJ/ssh_proxy

cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 $fwdspec 127.0.0.2:2" >>$OBJ/sshd_proxy
echo "Match User NoSuchUser" >>$OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 127.0.0.1:2" >>$OBJ/sshd_proxy

# Test that a rule that doesn't match doesn't override, plus test a
# PermitListen entry that's not at the start of the list
trace "nomatch permitlisten proxy w/key opts"
expect_client_ok -F $OBJ/ssh_proxy
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "nomatch override permitlisten"
stop_client

# bind to 127.0.0.1 instead of default localhost
fwdspec2="127.0.0.1:${fwdport}"
fwd="-R ${fwdspec2}:127.0.0.1:$PORT"

# first try w/ old fwdspec both in server config and key opts
cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 $fwdspec 127.0.0.2:2" >>$OBJ/sshd_proxy
cp /dev/null $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	printf 'permitlisten="'$fwdspec'" ' >> $OBJ/authorized_keys_$USER
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
done
trace "nomatch permitlisten 127.0.0.1 server config and userkey"
expect_client_fail "nomatch 127.0.0.1 server config and userkey" \
    -F $OBJ/ssh_config

# correct server config, denied by key opts
cp $OBJ/sshd_proxy_bak $OBJ/sshd_proxy
echo "PermitListen 127.0.0.1:1 ${fwdspec2} 127.0.0.2:2" >>$OBJ/sshd_proxy
trace "nomatch permitlisten 127.0.0.1 w/key opts"
expect_client_fail "nomatch 127.0.0.1 w/key opts" \
    -F $OBJ/ssh_config

# fix key opts
cp /dev/null $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	printf 'permitlisten="'$fwdspec2'" ' >> $OBJ/authorized_keys_$USER
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
done
trace "match permitlisten 127.0.0.1 server config w/key opts"
expect_client_ok -F $OBJ/ssh_proxy
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "match 127.0.0.1 server config w/key opts"
stop_client

# key opts with bare port number
cp /dev/null $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	printf 'permitlisten="'$fwdport'" ' >> $OBJ/authorized_keys_$USER
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
done
trace "match permitlisten 127.0.0.1 server config w/key opts (bare)"
expect_client_ok -F $OBJ/ssh_proxy
${SSH} -q -p $fwdport -F $OBJ/ssh_config somehost true || \
    fail "match 127.0.0.1 server config w/key opts (bare)"
stop_client

# key opts with incorrect bare port number
cp /dev/null $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	printf 'permitlisten="99" ' >> $OBJ/authorized_keys_$USER
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
done
trace "match permitlisten 127.0.0.1 server config w/key opts (wrong bare)"
expect_client_fail "nomatch 127.0.0.1 w/key opts (wrong bare)" \
    -F $OBJ/ssh_config
