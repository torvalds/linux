#	$OpenBSD: agent-timeout.sh,v 1.3 2015/03/03 22:35:19 markus Exp $
#	Placed in the Public Domain.

tid="agent timeout test"

SSHAGENT_TIMEOUT=10

trace "start agent"
eval `${SSHAGENT} -s` > /dev/null
r=$?
if [ $r -ne 0 ]; then
	fail "could not start ssh-agent: exit code $r"
else
	trace "add keys with timeout"
	for t in ${SSH_KEYTYPES}; do
		${SSHADD} -t ${SSHAGENT_TIMEOUT} $OBJ/$t > /dev/null 2>&1
		if [ $? -ne 0 ]; then
			fail "ssh-add did succeed exit code 0"
		fi
	done
	n=`${SSHADD} -l 2> /dev/null | wc -l`
	trace "agent has $n keys"
	if [ $n -ne 2 ]; then
		fail "ssh-add -l did not return 2 keys: $n"
	fi
	trace "sleeping 2*${SSHAGENT_TIMEOUT} seconds"
	sleep ${SSHAGENT_TIMEOUT}
	sleep ${SSHAGENT_TIMEOUT}
	${SSHADD} -l 2> /dev/null | grep 'The agent has no identities.' >/dev/null
	if [ $? -ne 0 ]; then
		fail "ssh-add -l still returns keys after timeout"
	fi

	trace "kill agent"
	${SSHAGENT} -k > /dev/null
fi
