#	$OpenBSD: hostkey-agent.sh,v 1.7 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="hostkey agent"

rm -f $OBJ/agent-key.* $OBJ/ssh_proxy.orig $OBJ/known_hosts.orig

trace "start agent"
eval `${SSHAGENT} -s` > /dev/null
r=$?
[ $r -ne 0 ] && fatal "could not start ssh-agent: exit code $r"

grep -vi 'hostkey' $OBJ/sshd_proxy > $OBJ/sshd_proxy.orig
echo "HostKeyAgent $SSH_AUTH_SOCK" >> $OBJ/sshd_proxy.orig

trace "load hostkeys"
for k in `${SSH} -Q key-plain` ; do
	${SSHKEYGEN} -qt $k -f $OBJ/agent-key.$k -N '' || fatal "ssh-keygen $k"
	(
		printf 'localhost-with-alias,127.0.0.1,::1 '
		cat $OBJ/agent-key.$k.pub
	) >> $OBJ/known_hosts.orig
	${SSHADD} $OBJ/agent-key.$k >/dev/null 2>&1 || \
		fatal "couldn't load key $OBJ/agent-key.$k"
	echo "Hostkey $OBJ/agent-key.${k}" >> $OBJ/sshd_proxy.orig
	# Remove private key so the server can't use it.
	rm $OBJ/agent-key.$k || fatal "couldn't rm $OBJ/agent-key.$k"
done
cp $OBJ/known_hosts.orig $OBJ/known_hosts

unset SSH_AUTH_SOCK

for ps in no yes; do
	for k in `${SSH} -Q key-plain` ; do
		verbose "key type $k privsep=$ps"
		cp $OBJ/sshd_proxy.orig $OBJ/sshd_proxy
		echo "UsePrivilegeSeparation $ps" >> $OBJ/sshd_proxy
		echo "HostKeyAlgorithms $k" >> $OBJ/sshd_proxy
		opts="-oHostKeyAlgorithms=$k -F $OBJ/ssh_proxy"
		cp $OBJ/known_hosts.orig $OBJ/known_hosts
		SSH_CONNECTION=`${SSH} $opts host 'echo $SSH_CONNECTION'`
		if [ $? -ne 0 ]; then
			fail "privsep=$ps failed"
		fi
		if [ "$SSH_CONNECTION" != "UNKNOWN 65535 UNKNOWN 65535" ]; then
			fail "bad SSH_CONNECTION key type $k privsep=$ps"
		fi
	done
done

trace "kill agent"
${SSHAGENT} -k > /dev/null

