#	$OpenBSD: hostkey-rotate.sh,v 1.5 2015/09/04 04:23:10 djm Exp $
#	Placed in the Public Domain.

tid="hostkey rotate"

# Need full names here since they are used in HostKeyAlgorithms
HOSTKEY_TYPES="ecdsa-sha2-nistp256 ssh-ed25519 ssh-rsa ssh-dss"

rm -f $OBJ/hkr.* $OBJ/ssh_proxy.orig

grep -vi 'hostkey' $OBJ/sshd_proxy > $OBJ/sshd_proxy.orig
echo "UpdateHostkeys=yes" >> $OBJ/ssh_proxy
rm $OBJ/known_hosts

trace "prepare hostkeys"
nkeys=0
all_algs=""
for k in `${SSH} -Q key-plain` ; do
	${SSHKEYGEN} -qt $k -f $OBJ/hkr.$k -N '' || fatal "ssh-keygen $k"
	echo "Hostkey $OBJ/hkr.${k}" >> $OBJ/sshd_proxy.orig
	nkeys=`expr $nkeys + 1`
	test "x$all_algs" = "x" || all_algs="${all_algs},"
	all_algs="${all_algs}$k"
done

dossh() {
	# All ssh should succeed in this test
	${SSH} -F $OBJ/ssh_proxy "$@" x true || fail "ssh $@ failed"
}

expect_nkeys() {
	_expected=$1
	_message=$2
	_n=`wc -l $OBJ/known_hosts | awk '{ print $1 }'` || fatal "wc failed"
	[ "x$_n" = "x$_expected" ] || fail "$_message (got $_n wanted $_expected)"
}

check_key_present() {
	_type=$1
	_kfile=$2
	test "x$_kfile" = "x" && _kfile="$OBJ/hkr.${_type}.pub"
	_kpub=`awk "/$_type /"' { print $2 }' < $_kfile` || \
		fatal "awk failed"
	fgrep "$_kpub" $OBJ/known_hosts > /dev/null
}

cp $OBJ/sshd_proxy.orig $OBJ/sshd_proxy

# Connect to sshd with StrictHostkeyChecking=no
verbose "learn hostkey with StrictHostKeyChecking=no"
>$OBJ/known_hosts
dossh -oHostKeyAlgorithms=ssh-ed25519 -oStrictHostKeyChecking=no
# Verify no additional keys learned
expect_nkeys 1 "unstrict connect keys"
check_key_present ssh-ed25519 || fail "unstrict didn't learn key"

# Connect to sshd as usual
verbose "learn additional hostkeys"
dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=$all_algs
# Check that other keys learned
expect_nkeys $nkeys "learn hostkeys"
check_key_present ssh-rsa || fail "didn't learn keys"

# Check each key type
for k in `${SSH} -Q key-plain` ; do
	verbose "learn additional hostkeys, type=$k"
	dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=$k,$all_algs
	expect_nkeys $nkeys "learn hostkeys $k"
	check_key_present $k || fail "didn't learn $k"
done

# Change one hostkey (non primary) and relearn
verbose "learn changed non-primary hostkey"
mv $OBJ/hkr.ssh-rsa.pub $OBJ/hkr.ssh-rsa.pub.old
rm -f $OBJ/hkr.ssh-rsa
${SSHKEYGEN} -qt ssh-rsa -f $OBJ/hkr.ssh-rsa -N '' || fatal "ssh-keygen $k"
dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=$all_algs
# Check that the key was replaced
expect_nkeys $nkeys "learn hostkeys"
check_key_present ssh-rsa $OBJ/hkr.ssh-rsa.pub.old && fail "old key present"
check_key_present ssh-rsa || fail "didn't learn changed key"

# Add new hostkey (primary type) to sshd and connect
verbose "learn new primary hostkey"
${SSHKEYGEN} -qt ssh-rsa -f $OBJ/hkr.ssh-rsa-new -N '' || fatal "ssh-keygen $k"
( cat $OBJ/sshd_proxy.orig ; echo HostKey $OBJ/hkr.ssh-rsa-new ) \
    > $OBJ/sshd_proxy
# Check new hostkey added
dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=ssh-rsa,$all_algs
expect_nkeys `expr $nkeys + 1` "learn hostkeys"
check_key_present ssh-rsa || fail "current key missing"
check_key_present ssh-rsa $OBJ/hkr.ssh-rsa-new.pub || fail "new key missing"

# Remove old hostkey (primary type) from sshd
verbose "rotate primary hostkey"
cp $OBJ/sshd_proxy.orig $OBJ/sshd_proxy
mv $OBJ/hkr.ssh-rsa.pub $OBJ/hkr.ssh-rsa.pub.old
mv $OBJ/hkr.ssh-rsa-new.pub $OBJ/hkr.ssh-rsa.pub
mv $OBJ/hkr.ssh-rsa-new $OBJ/hkr.ssh-rsa
# Check old hostkey removed
dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=ssh-rsa,$all_algs
expect_nkeys $nkeys "learn hostkeys"
check_key_present ssh-rsa $OBJ/hkr.ssh-rsa.pub.old && fail "old key present"
check_key_present ssh-rsa || fail "didn't learn changed key"

# Connect again, forcing rotated key
verbose "check rotate primary hostkey"
dossh -oStrictHostKeyChecking=yes -oHostKeyAlgorithms=ssh-rsa
expect_nkeys 1 "learn hostkeys"
check_key_present ssh-rsa || fail "didn't learn changed key"
