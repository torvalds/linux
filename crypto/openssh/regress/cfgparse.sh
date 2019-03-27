#	$OpenBSD: cfgparse.sh,v 1.7 2018/05/11 03:51:06 dtucker Exp $
#	Placed in the Public Domain.

tid="sshd config parse"

# This is a reasonable proxy for IPv6 support.
if ! config_defined HAVE_STRUCT_IN6_ADDR ; then
	SKIP_IPV6=yes
fi

# We need to use the keys generated for the regression test because sshd -T
# will fail if we're not running with SUDO (no permissions for real keys) or
# if we are running tests on a system that has never had sshd installed
# because the keys won't exist.

grep "HostKey " $OBJ/sshd_config > $OBJ/sshd_config_minimal
SSHD_KEYS="`cat $OBJ/sshd_config_minimal`"

verbose "reparse minimal config"
($SUDO ${SSHD} -T -f $OBJ/sshd_config_minimal >$OBJ/sshd_config.1 &&
 $SUDO ${SSHD} -T -f $OBJ/sshd_config.1 >$OBJ/sshd_config.2 &&
 diff $OBJ/sshd_config.1 $OBJ/sshd_config.2) || fail "reparse minimal config"

verbose "reparse regress config"
($SUDO ${SSHD} -T -f $OBJ/sshd_config >$OBJ/sshd_config.1 &&
 $SUDO ${SSHD} -T -f $OBJ/sshd_config.1 >$OBJ/sshd_config.2 &&
 diff $OBJ/sshd_config.1 $OBJ/sshd_config.2) || fail "reparse regress config"

verbose "listenaddress order"
# expected output
cat > $OBJ/sshd_config.0 <<EOD
listenaddress 1.2.3.4:1234
listenaddress 1.2.3.4:5678
EOD
[ X${SKIP_IPV6} = Xyes ] || cat >> $OBJ/sshd_config.0 <<EOD
listenaddress [::1]:1234
listenaddress [::1]:5678
EOD

# test input sets.  should all result in the output above.
# test 1: addressfamily and port first
cat > $OBJ/sshd_config.1 <<EOD
${SSHD_KEYS}
addressfamily any
port 1234
port 5678
listenaddress 1.2.3.4
EOD
[ X${SKIP_IPV6} = Xyes ] || cat >> $OBJ/sshd_config.1 <<EOD
listenaddress ::1
EOD

($SUDO ${SSHD} -T -f $OBJ/sshd_config.1 | \
 grep 'listenaddress ' >$OBJ/sshd_config.2 &&
 diff $OBJ/sshd_config.0 $OBJ/sshd_config.2) || \
 fail "listenaddress order 1"
# test 2: listenaddress first
cat > $OBJ/sshd_config.1 <<EOD
${SSHD_KEYS}
listenaddress 1.2.3.4
port 1234
port 5678
addressfamily any
EOD
[ X${SKIP_IPV6} = Xyes ] || cat >> $OBJ/sshd_config.1 <<EOD
listenaddress ::1
EOD

($SUDO ${SSHD} -T -f $OBJ/sshd_config.1 | \
 grep 'listenaddress ' >$OBJ/sshd_config.2 &&
 diff $OBJ/sshd_config.0 $OBJ/sshd_config.2) || \
 fail "listenaddress order 2"

# cleanup
rm -f $OBJ/sshd_config.[012]
