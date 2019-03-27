#	$OpenBSD: agent-pkcs11.sh,v 1.3 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="pkcs11 agent test"

TEST_SSH_PIN=""
TEST_SSH_PKCS11=/usr/local/lib/soft-pkcs11.so.0.0

test -f "$TEST_SSH_PKCS11" || fatal "$TEST_SSH_PKCS11 does not exist"

# setup environment for soft-pkcs11 token
SOFTPKCS11RC=$OBJ/pkcs11.info
export SOFTPKCS11RC
# prevent ssh-agent from calling ssh-askpass
SSH_ASKPASS=/usr/bin/true
export SSH_ASKPASS
unset DISPLAY

# start command w/o tty, so ssh-add accepts pin from stdin
notty() {
	perl -e 'use POSIX; POSIX::setsid(); 
	    if (fork) { wait; exit($? >> 8); } else { exec(@ARGV) }' "$@"
}

trace "start agent"
eval `${SSHAGENT} -s` > /dev/null
r=$?
if [ $r -ne 0 ]; then
	fail "could not start ssh-agent: exit code $r"
else
	trace "generating key/cert"
	rm -f $OBJ/pkcs11.key $OBJ/pkcs11.crt
	openssl genrsa -out $OBJ/pkcs11.key 2048 > /dev/null 2>&1
	chmod 600 $OBJ/pkcs11.key 
	openssl req -key $OBJ/pkcs11.key -new -x509 \
	    -out $OBJ/pkcs11.crt -text -subj '/CN=pkcs11 test' > /dev/null
	printf "a\ta\t$OBJ/pkcs11.crt\t$OBJ/pkcs11.key" > $SOFTPKCS11RC
	# add to authorized keys
	${SSHKEYGEN} -y -f $OBJ/pkcs11.key > $OBJ/authorized_keys_$USER

	trace "add pkcs11 key to agent"
	echo ${TEST_SSH_PIN} | notty ${SSHADD} -s ${TEST_SSH_PKCS11} > /dev/null 2>&1
	r=$?
	if [ $r -ne 0 ]; then
		fail "ssh-add -s failed: exit code $r"
	fi

	trace "pkcs11 list via agent"
	${SSHADD} -l > /dev/null 2>&1
	r=$?
	if [ $r -ne 0 ]; then
		fail "ssh-add -l failed: exit code $r"
	fi

	trace "pkcs11 connect via agent"
	${SSH} -F $OBJ/ssh_proxy somehost exit 5
	r=$?
	if [ $r -ne 5 ]; then
		fail "ssh connect failed (exit code $r)"
	fi

	trace "remove pkcs11 keys"
	echo ${TEST_SSH_PIN} | notty ${SSHADD} -e ${TEST_SSH_PKCS11} > /dev/null 2>&1
	r=$?
	if [ $r -ne 0 ]; then
		fail "ssh-add -e failed: exit code $r"
	fi

	trace "kill agent"
	${SSHAGENT} -k > /dev/null
fi
