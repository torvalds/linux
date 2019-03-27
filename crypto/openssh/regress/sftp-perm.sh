#	$OpenBSD: sftp-perm.sh,v 1.2 2013/10/17 22:00:18 djm Exp $
#	Placed in the Public Domain.

tid="sftp permissions"

SERVER_LOG=${OBJ}/sftp-server.log
CLIENT_LOG=${OBJ}/sftp.log
TEST_SFTP_SERVER=${OBJ}/sftp-server.sh

prepare_server() {
	printf "#!/bin/sh\nexec $SFTPSERVER -el debug3 $* 2>$SERVER_LOG\n" \
	> $TEST_SFTP_SERVER
	chmod a+x $TEST_SFTP_SERVER
}

run_client() {
	echo "$@" | ${SFTP} -D ${TEST_SFTP_SERVER} -vvvb - >$CLIENT_LOG 2>&1
}

prepare_files() {
	_prep="$1"
	rm -f ${COPY} ${COPY}.1
	test -d ${COPY}.dd && { rmdir ${COPY}.dd || fatal "rmdir ${COPY}.dd"; }
	test -z "$_prep" && return
	sh -c "$_prep" || fail "preparation failed: \"$_prep\""
}

postcondition() {
	_title="$1"
	_check="$2"
	test -z "$_check" && return
	${TEST_SHELL} -c "$_check" || fail "postcondition check failed: $_title"
}

ro_test() {
	_desc=$1
	_cmd="$2"
	_prep="$3"
	_expect_success_post="$4"
	_expect_fail_post="$5"
	verbose "$tid: read-only $_desc"
	# Plain (no options, mostly to test that _cmd is good)
	prepare_files "$_prep"
	prepare_server
	run_client "$_cmd" || fail "plain $_desc failed"
	postcondition "$_desc no-readonly" "$_expect_success_post"
	# Read-only enabled
	prepare_files "$_prep"
	prepare_server -R
	run_client "$_cmd" && fail "read-only $_desc succeeded"
	postcondition "$_desc readonly" "$_expect_fail_post"
}

perm_test() {
	_op=$1
	_whitelist_ops=$2
	_cmd="$3"
	_prep="$4"
	_expect_success_post="$5"
	_expect_fail_post="$6"
	verbose "$tid: explicit $_op"
	# Plain (no options, mostly to test that _cmd is good)
	prepare_files "$_prep"
	prepare_server
	run_client "$_cmd" || fail "plain $_op failed"
	postcondition "$_op no white/blacklists" "$_expect_success_post"
	# Whitelist
	prepare_files "$_prep"
	prepare_server -p $_op,$_whitelist_ops
	run_client "$_cmd" || fail "whitelisted $_op failed"
	postcondition "$_op whitelisted" "$_expect_success_post"
	# Blacklist
	prepare_files "$_prep"
	prepare_server -P $_op
	run_client "$_cmd" && fail "blacklisted $_op succeeded"
	postcondition "$_op blacklisted" "$_expect_fail_post"
	# Whitelist with op missing.
	prepare_files "$_prep"
	prepare_server -p $_whitelist_ops
	run_client "$_cmd" && fail "no whitelist $_op succeeded"
	postcondition "$_op not in whitelist" "$_expect_fail_post"
}

ro_test \
	"upload" \
	"put $DATA $COPY" \
	"" \
	"cmp $DATA $COPY" \
	"test ! -f $COPY"

ro_test \
	"setstat" \
	"chmod 0700 $COPY" \
	"touch $COPY; chmod 0400 $COPY" \
	"test -x $COPY" \
	"test ! -x $COPY"

ro_test \
	"rm" \
	"rm $COPY" \
	"touch $COPY" \
	"test ! -f $COPY" \
	"test -f $COPY"

ro_test \
	"mkdir" \
	"mkdir ${COPY}.dd" \
	"" \
	"test -d ${COPY}.dd" \
	"test ! -d ${COPY}.dd"

ro_test \
	"rmdir" \
	"rmdir ${COPY}.dd" \
	"mkdir ${COPY}.dd" \
	"test ! -d ${COPY}.dd" \
	"test -d ${COPY}.dd"

ro_test \
	"posix-rename" \
	"rename $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1 -a ! -f $COPY" \
	"test -f $COPY -a ! -f ${COPY}.1"

ro_test \
	"oldrename" \
	"rename -l $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1 -a ! -f $COPY" \
	"test -f $COPY -a ! -f ${COPY}.1"

ro_test \
	"symlink" \
	"ln -s $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -h ${COPY}.1" \
	"test ! -h ${COPY}.1"

ro_test \
	"hardlink" \
	"ln $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1" \
	"test ! -f ${COPY}.1"

# Test explicit permissions

perm_test \
	"open" \
	"realpath,stat,lstat,read,close" \
	"get $DATA $COPY" \
	"" \
	"cmp $DATA $COPY" \
	"! cmp $DATA $COPY 2>/dev/null"

perm_test \
	"read" \
	"realpath,stat,lstat,open,close" \
	"get $DATA $COPY" \
	"" \
	"cmp $DATA $COPY" \
	"! cmp $DATA $COPY 2>/dev/null"

perm_test \
	"write" \
	"realpath,stat,lstat,open,close" \
	"put $DATA $COPY" \
	"" \
	"cmp $DATA $COPY" \
	"! cmp $DATA $COPY 2>/dev/null"

perm_test \
	"lstat" \
	"realpath,stat,open,read,close" \
	"get $DATA $COPY" \
	"" \
	"cmp $DATA $COPY" \
	"! cmp $DATA $COPY 2>/dev/null"

perm_test \
	"opendir" \
	"realpath,readdir,stat,lstat" \
	"ls -ln $OBJ"

perm_test \
	"readdir" \
	"realpath,opendir,stat,lstat" \
	"ls -ln $OBJ"

perm_test \
	"setstat" \
	"realpath,stat,lstat" \
	"chmod 0700 $COPY" \
	"touch $COPY; chmod 0400 $COPY" \
	"test -x $COPY" \
	"test ! -x $COPY"

perm_test \
	"remove" \
	"realpath,stat,lstat" \
	"rm $COPY" \
	"touch $COPY" \
	"test ! -f $COPY" \
	"test -f $COPY"

perm_test \
	"mkdir" \
	"realpath,stat,lstat" \
	"mkdir ${COPY}.dd" \
	"" \
	"test -d ${COPY}.dd" \
	"test ! -d ${COPY}.dd"

perm_test \
	"rmdir" \
	"realpath,stat,lstat" \
	"rmdir ${COPY}.dd" \
	"mkdir ${COPY}.dd" \
	"test ! -d ${COPY}.dd" \
	"test -d ${COPY}.dd"

perm_test \
	"posix-rename" \
	"realpath,stat,lstat" \
	"rename $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1 -a ! -f $COPY" \
	"test -f $COPY -a ! -f ${COPY}.1"

perm_test \
	"rename" \
	"realpath,stat,lstat" \
	"rename -l $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1 -a ! -f $COPY" \
	"test -f $COPY -a ! -f ${COPY}.1"

perm_test \
	"symlink" \
	"realpath,stat,lstat" \
	"ln -s $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -h ${COPY}.1" \
	"test ! -h ${COPY}.1"

perm_test \
	"hardlink" \
	"realpath,stat,lstat" \
	"ln $COPY ${COPY}.1" \
	"touch $COPY" \
	"test -f ${COPY}.1" \
	"test ! -f ${COPY}.1"

perm_test \
	"statvfs" \
	"realpath,stat,lstat" \
	"df /"

# XXX need good tests for:
# fstat
# fsetstat
# realpath
# stat
# readlink
# fstatvfs

rm -rf ${COPY} ${COPY}.1 ${COPY}.dd

