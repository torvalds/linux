#	$OpenBSD: brokenkeys.sh,v 1.2 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="broken keys"

KEYS="$OBJ/authorized_keys_${USER}"

start_sshd

mv ${KEYS} ${KEYS}.bak

# Truncated key
echo "ssh-rsa AAAAB3NzaC1yc2EAAAABIwAAAIEABTM= bad key" > $KEYS
cat ${KEYS}.bak >> ${KEYS}
cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER

${SSH} -F $OBJ/ssh_config somehost true
if [ $? -ne 0 ]; then
	fail "ssh connect with failed"
fi

mv ${KEYS}.bak ${KEYS}

