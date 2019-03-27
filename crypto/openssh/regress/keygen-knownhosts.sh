#	$OpenBSD: keygen-knownhosts.sh,v 1.4 2018/06/01 03:52:37 djm Exp $
#	Placed in the Public Domain.

tid="ssh-keygen known_hosts"

rm -f $OBJ/kh.*

# Generate some keys for testing (just ed25519 for speed) and make a hosts file.
for x in host-a host-b host-c host-d host-e host-f host-a2 host-b2; do
	${SSHKEYGEN} -qt ed25519 -f $OBJ/kh.$x -C "$x" -N "" || \
		fatal "ssh-keygen failed"
	# Add a comment that we expect should be preserved.
	echo "# $x" >> $OBJ/kh.hosts
	(
		case "$x" in
		host-a|host-b)	printf "$x " ;;
		host-c)		printf "@cert-authority $x " ;;
		host-d)		printf "@revoked $x " ;;
		host-e)		printf "host-e* " ;;
		host-f)		printf "host-f,host-g,host-h " ;;
		host-a2)	printf "host-a " ;;
		host-b2)	printf "host-b " ;;
		esac
		cat $OBJ/kh.${x}.pub
		# Blank line should be preserved.
		echo "" >> $OBJ/kh.hosts
	) >> $OBJ/kh.hosts
done

# Generate a variant with an invalid line. We'll use this for most tests,
# because keygen should be able to cope and it should be preserved in any
# output file.
cat $OBJ/kh.hosts >> $OBJ/kh.invalid
echo "host-i " >> $OBJ/kh.invalid

cp $OBJ/kh.invalid $OBJ/kh.invalid.orig
cp $OBJ/kh.hosts $OBJ/kh.hosts.orig

expect_key() {
	_host=$1
	_hosts=$2
	_key=$3
	_line=$4
	_mark=$5
	_marker=""
	test "x$_mark" = "xCA" && _marker="@cert-authority "
	test "x$_mark" = "xREVOKED" && _marker="@revoked "
	test "x$_line" != "x" &&
	    echo "# Host $_host found: line $_line $_mark" >> $OBJ/kh.expect
	printf "${_marker}$_hosts " >> $OBJ/kh.expect
	cat $OBJ/kh.${_key}.pub >> $OBJ/kh.expect ||
	    fatal "${_key}.pub missing"
}

check_find() {
	_host=$1
	_name=$2
	shift; shift
	${SSHKEYGEN} "$@" -f $OBJ/kh.invalid -F $_host > $OBJ/kh.result
	if ! diff -w $OBJ/kh.expect $OBJ/kh.result ; then
		fail "didn't find $_name"
	fi
}

check_find_exit_code() {
	_host=$1
	_name=$2
	_keygenopt=$3
	_exp_exit_code=$4
	${SSHKEYGEN} $_keygenopt -f $OBJ/kh.invalid -F $_host > /dev/null
	if [ "$?" != "$_exp_exit_code" ] ; then
	    fail "Unexpected exit code $_name"
	fi
}

# Find key
rm -f $OBJ/kh.expect
expect_key host-a host-a host-a 2
expect_key host-a host-a host-a2 20
check_find host-a "simple find"

# find CA key
rm -f $OBJ/kh.expect
expect_key host-c host-c host-c 8 CA
check_find host-c "find CA key"

# find revoked key
rm -f $OBJ/kh.expect
expect_key host-d host-d host-d 11 REVOKED
check_find host-d "find revoked key"

# find key with wildcard
rm -f $OBJ/kh.expect
expect_key host-e.somedomain "host-e*" host-e 14
check_find host-e.somedomain "find wildcard key"

# find key among multiple hosts
rm -f $OBJ/kh.expect
expect_key host-h "host-f,host-g,host-h " host-f 17
check_find host-h "find multiple hosts"

# Check exit code, known host
check_find_exit_code host-a "known host" "-q" "0"

# Check exit code, unknown host
check_find_exit_code host-aa "unknown host" "-q" "1"

# Check exit code, the hash mode, known host
check_find_exit_code host-a "known host" "-q -H" "0"

# Check exit code, the hash mode, unknown host
check_find_exit_code host-aa "unknown host" "-q -H" "1"

check_hashed_find() {
	_host=$1
	_name=$2
	_file=$3
	test "x$_file" = "x" && _file=$OBJ/kh.invalid
	${SSHKEYGEN} -f $_file -HF $_host | grep '|1|' | \
	    sed "s/^[^ ]*/$_host/" > $OBJ/kh.result
	if ! diff -w $OBJ/kh.expect $OBJ/kh.result ; then
		fail "didn't find $_name"
	fi
}

# Find key and hash
rm -f $OBJ/kh.expect
expect_key host-a host-a host-a
expect_key host-a host-a host-a2
check_hashed_find host-a "find simple and hash"

# Find CA key and hash
rm -f $OBJ/kh.expect
expect_key host-c host-c host-c "" CA
# CA key output is not hashed.
check_find host-c "find simple and hash" -Hq

# Find revoked key and hash
rm -f $OBJ/kh.expect
expect_key host-d host-d host-d "" REVOKED
# Revoked key output is not hashed.
check_find host-d "find simple and hash" -Hq

# find key with wildcard and hash
rm -f $OBJ/kh.expect
expect_key host-e "host-e*" host-e ""
# Key with wildcard hostname should not be hashed.
check_find host-e "find wildcard key" -Hq

# find key among multiple hosts
rm -f $OBJ/kh.expect
# Comma-separated hostnames should be expanded and hashed.
expect_key host-f "host-h " host-f
expect_key host-g "host-h " host-f
expect_key host-h "host-h " host-f
check_hashed_find host-h "find multiple hosts"

# Attempt remove key on invalid file.
cp $OBJ/kh.invalid.orig $OBJ/kh.invalid
${SSHKEYGEN} -qf $OBJ/kh.invalid -R host-a 2>/dev/null
diff $OBJ/kh.invalid $OBJ/kh.invalid.orig || fail "remove on invalid succeeded"

# Remove key
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -R host-a 2>/dev/null
grep -v "^host-a " $OBJ/kh.hosts.orig > $OBJ/kh.expect
diff $OBJ/kh.hosts $OBJ/kh.expect || fail "remove simple"

# Remove CA key
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -R host-c 2>/dev/null
# CA key should not be removed.
diff $OBJ/kh.hosts $OBJ/kh.hosts.orig || fail "remove CA"

# Remove revoked key
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -R host-d 2>/dev/null
# revoked key should not be removed.
diff $OBJ/kh.hosts $OBJ/kh.hosts.orig || fail "remove revoked"

# Remove wildcard
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -R host-e.blahblah 2>/dev/null
grep -v "^host-e[*] " $OBJ/kh.hosts.orig > $OBJ/kh.expect
diff $OBJ/kh.hosts $OBJ/kh.expect || fail "remove wildcard"

# Remove multiple
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -R host-h 2>/dev/null
grep -v "^host-f," $OBJ/kh.hosts.orig > $OBJ/kh.expect
diff $OBJ/kh.hosts $OBJ/kh.expect || fail "remove wildcard"

# Attempt hash on invalid file
cp $OBJ/kh.invalid.orig $OBJ/kh.invalid
${SSHKEYGEN} -qf $OBJ/kh.invalid -H 2>/dev/null && fail "hash invalid succeeded"
diff $OBJ/kh.invalid $OBJ/kh.invalid.orig || fail "invalid file modified"

# Hash valid file
cp $OBJ/kh.hosts.orig $OBJ/kh.hosts
${SSHKEYGEN} -qf $OBJ/kh.hosts -H 2>/dev/null || fail "hash failed"
diff $OBJ/kh.hosts.old $OBJ/kh.hosts.orig || fail "backup differs"
grep "^host-[abfgh]" $OBJ/kh.hosts && fail "original hostnames persist"

cp $OBJ/kh.hosts $OBJ/kh.hashed.orig

# Test lookup
rm -f $OBJ/kh.expect
expect_key host-a host-a host-a
expect_key host-a host-a host-a2
check_hashed_find host-a "find simple in hashed" $OBJ/kh.hosts

# Test multiple expanded
rm -f $OBJ/kh.expect
expect_key host-h host-h host-f
check_hashed_find host-h "find simple in hashed" $OBJ/kh.hosts

# Test remove
cp $OBJ/kh.hashed.orig $OBJ/kh.hashed
${SSHKEYGEN} -qf $OBJ/kh.hashed -R host-a 2>/dev/null
${SSHKEYGEN} -qf $OBJ/kh.hashed -F host-a && fail "found key after hashed remove"
