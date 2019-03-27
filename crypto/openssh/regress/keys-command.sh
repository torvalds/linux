#	$OpenBSD: keys-command.sh,v 1.4 2016/09/26 21:34:38 bluhm Exp $
#	Placed in the Public Domain.

tid="authorized keys from command"

if [ -z "$SUDO" -a ! -w /var/run ]; then
	echo "skipped (SUDO not set)"
	echo "need SUDO to create file in /var/run, test won't work without"
	exit 0
fi

rm -f $OBJ/keys-command-args

touch $OBJ/keys-command-args
chmod a+rw $OBJ/keys-command-args

expected_key_text=`awk '{ print $2 }' < $OBJ/rsa.pub`
expected_key_fp=`$SSHKEYGEN -lf $OBJ/rsa.pub | awk '{ print $2 }'`

# Establish a AuthorizedKeysCommand in /var/run where it will have
# acceptable directory permissions.
KEY_COMMAND="/var/run/keycommand_${LOGNAME}"
cat << _EOF | $SUDO sh -c "rm -f '$KEY_COMMAND' ; cat > '$KEY_COMMAND'"
#!/bin/sh
echo args: "\$@" >> $OBJ/keys-command-args
echo "$PATH" | grep -q mekmitasdigoat && exit 7
test "x\$1" != "x${LOGNAME}" && exit 1
if test $# -eq 6 ; then
	test "x\$2" != "xblah" && exit 2
	test "x\$3" != "x${expected_key_text}" && exit 3
	test "x\$4" != "xssh-rsa" && exit 4
	test "x\$5" != "x${expected_key_fp}" && exit 5
	test "x\$6" != "xblah" && exit 6
fi
exec cat "$OBJ/authorized_keys_${LOGNAME}"
_EOF
$SUDO chmod 0755 "$KEY_COMMAND"

if ! $OBJ/check-perm -m keys-command $KEY_COMMAND ; then
	echo "skipping: $KEY_COMMAND is unsuitable as AuthorizedKeysCommand"
	$SUDO rm -f $KEY_COMMAND
	exit 0
fi

if [ -x $KEY_COMMAND ]; then
	cp $OBJ/sshd_proxy $OBJ/sshd_proxy.bak

	verbose "AuthorizedKeysCommand with arguments"
	(
		grep -vi AuthorizedKeysFile $OBJ/sshd_proxy.bak
		echo AuthorizedKeysFile none
		echo AuthorizedKeysCommand $KEY_COMMAND %u blah %k %t %f blah
		echo AuthorizedKeysCommandUser ${LOGNAME}
	) > $OBJ/sshd_proxy

	# Ensure that $PATH is sanitised in sshd
	env PATH=$PATH:/sbin/mekmitasdigoat \
	    ${SSH} -F $OBJ/ssh_proxy somehost true
	if [ $? -ne 0 ]; then
		fail "connect failed"
	fi

	verbose "AuthorizedKeysCommand without arguments"
	# Check legacy behavior of no-args resulting in username being passed.
	(
		grep -vi AuthorizedKeysFile $OBJ/sshd_proxy.bak
		echo AuthorizedKeysFile none
		echo AuthorizedKeysCommand $KEY_COMMAND
		echo AuthorizedKeysCommandUser ${LOGNAME}
	) > $OBJ/sshd_proxy

	# Ensure that $PATH is sanitised in sshd
	env PATH=$PATH:/sbin/mekmitasdigoat \
	    ${SSH} -F $OBJ/ssh_proxy somehost true
	if [ $? -ne 0 ]; then
		fail "connect failed"
	fi
else
	echo "SKIPPED: $KEY_COMMAND not executable (/var/run mounted noexec?)"
fi

$SUDO rm -f $KEY_COMMAND
