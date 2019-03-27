#	$OpenBSD: agent.sh,v 1.13 2017/12/19 00:49:30 djm Exp $
#	Placed in the Public Domain.

tid="simple agent test"

SSH_AUTH_SOCK=/nonexistent ${SSHADD} -l > /dev/null 2>&1
if [ $? -ne 2 ]; then
	fail "ssh-add -l did not fail with exit code 2"
fi

trace "start agent"
eval `${SSHAGENT} -s` > /dev/null
r=$?
if [ $r -ne 0 ]; then
	fatal "could not start ssh-agent: exit code $r"
fi

${SSHADD} -l > /dev/null 2>&1
if [ $? -ne 1 ]; then
	fail "ssh-add -l did not fail with exit code 1"
fi

rm -f $OBJ/user_ca_key $OBJ/user_ca_key.pub
${SSHKEYGEN} -q -N '' -t ed25519 -f $OBJ/user_ca_key \
	|| fatal "ssh-keygen failed"

trace "overwrite authorized keys"
printf '' > $OBJ/authorized_keys_$USER

for t in ${SSH_KEYTYPES}; do
	# generate user key for agent
	rm -f $OBJ/$t-agent $OBJ/$t-agent.pub*
	${SSHKEYGEN} -q -N '' -t $t -f $OBJ/$t-agent ||\
		 fatal "ssh-keygen for $t-agent failed"
	# Make a certificate for each too.
	${SSHKEYGEN} -qs $OBJ/user_ca_key -I "$t cert" \
		-n estragon $OBJ/$t-agent.pub || fatal "ca sign failed"

	# add to authorized keys
	cat $OBJ/$t-agent.pub >> $OBJ/authorized_keys_$USER
	# add privat key to agent
	${SSHADD} $OBJ/$t-agent > /dev/null 2>&1
	if [ $? -ne 0 ]; then
		fail "ssh-add did succeed exit code 0"
	fi
	# Remove private key to ensure that we aren't accidentally using it.
	rm -f $OBJ/$t-agent
done

# Remove explicit identity directives from ssh_proxy
mv $OBJ/ssh_proxy $OBJ/ssh_proxy_bak
grep -vi identityfile $OBJ/ssh_proxy_bak > $OBJ/ssh_proxy

${SSHADD} -l > /dev/null 2>&1
r=$?
if [ $r -ne 0 ]; then
	fail "ssh-add -l failed: exit code $r"
fi
# the same for full pubkey output
${SSHADD} -L > /dev/null 2>&1
r=$?
if [ $r -ne 0 ]; then
	fail "ssh-add -L failed: exit code $r"
fi

trace "simple connect via agent"
${SSH} -F $OBJ/ssh_proxy somehost exit 52
r=$?
if [ $r -ne 52 ]; then
	fail "ssh connect with failed (exit code $r)"
fi

for t in ${SSH_KEYTYPES}; do
	trace "connect via agent using $t key"
	${SSH} -F $OBJ/ssh_proxy -i $OBJ/$t-agent.pub -oIdentitiesOnly=yes \
		somehost exit 52
	r=$?
	if [ $r -ne 52 ]; then
		fail "ssh connect with failed (exit code $r)"
	fi
done

trace "agent forwarding"
${SSH} -A -F $OBJ/ssh_proxy somehost ${SSHADD} -l > /dev/null 2>&1
r=$?
if [ $r -ne 0 ]; then
	fail "ssh-add -l via agent fwd failed (exit code $r)"
fi
${SSH} -A -F $OBJ/ssh_proxy somehost \
	"${SSH} -F $OBJ/ssh_proxy somehost exit 52"
r=$?
if [ $r -ne 52 ]; then
	fail "agent fwd failed (exit code $r)"
fi

(printf 'cert-authority,principals="estragon" '; cat $OBJ/user_ca_key.pub) \
	> $OBJ/authorized_keys_$USER
for t in ${SSH_KEYTYPES}; do
	trace "connect via agent using $t key"
	${SSH} -F $OBJ/ssh_proxy -i $OBJ/$t-agent.pub \
		-oCertificateFile=$OBJ/$t-agent-cert.pub \
		-oIdentitiesOnly=yes somehost exit 52
	r=$?
	if [ $r -ne 52 ]; then
		fail "ssh connect with failed (exit code $r)"
	fi
done

trace "delete all agent keys"
${SSHADD} -D > /dev/null 2>&1
r=$?
if [ $r -ne 0 ]; then
	fail "ssh-add -D failed: exit code $r"
fi

trace "kill agent"
${SSHAGENT} -k > /dev/null
