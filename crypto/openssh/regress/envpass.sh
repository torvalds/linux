#	$OpenBSD: envpass.sh,v 1.4 2005/03/04 08:48:46 djm Exp $
#	Placed in the Public Domain.

tid="environment passing"

# NB accepted env vars are in test-exec.sh (_XXX_TEST_* and _XXX_TEST)

# Prepare a custom config to test for a configuration parsing bug fixed in 4.0
cat << EOF > $OBJ/ssh_proxy_envpass
Host test-sendenv-confparse-bug
	SendEnv *
EOF
cat $OBJ/ssh_proxy >> $OBJ/ssh_proxy_envpass

trace "pass env, don't accept"
verbose "test $tid: pass env, don't accept"
_TEST_ENV=blah ${SSH} -oSendEnv="*" -F $OBJ/ssh_proxy_envpass otherhost \
	sh << 'EOF'
	test -z "$_TEST_ENV"
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "environment found"
fi

trace "don't pass env, accept"
verbose "test $tid: don't pass env, accept"
_XXX_TEST_A=1 _XXX_TEST_B=2 ${SSH} -F $OBJ/ssh_proxy_envpass otherhost \
	sh << 'EOF'
	test -z "$_XXX_TEST_A" && test -z "$_XXX_TEST_B"
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "environment found"
fi

trace "pass single env, accept single env"
verbose "test $tid: pass single env, accept single env"
_XXX_TEST=blah ${SSH} -oSendEnv="_XXX_TEST" -F $OBJ/ssh_proxy_envpass \
    otherhost sh << 'EOF'
	test X"$_XXX_TEST" = X"blah"
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "environment not found"
fi

trace "pass multiple env, accept multiple env"
verbose "test $tid: pass multiple env, accept multiple env"
_XXX_TEST_A=1 _XXX_TEST_B=2 ${SSH} -oSendEnv="_XXX_TEST_*" \
    -F $OBJ/ssh_proxy_envpass otherhost \
	sh << 'EOF'
	test X"$_XXX_TEST_A" = X"1" -a X"$_XXX_TEST_B" = X"2"
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "environment not found"
fi

rm -f $OBJ/ssh_proxy_envpass
