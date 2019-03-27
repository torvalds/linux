#	$OpenBSD: forward-control.sh,v 1.7 2018/06/07 14:29:43 djm Exp $
#	Placed in the Public Domain.

tid="sshd control of local and remote forwarding"

LFWD_PORT=3320
RFWD_PORT=3321
CTL=$OBJ/ctl-sock
READY=$OBJ/ready

wait_for_file_to_appear() {
	_path=$1
	_n=0
	while test ! -f $_path ; do
		test $_n -eq 1 && trace "waiting for $_path to appear"
		_n=`expr $_n + 1`
		test $_n -ge 20 && return 1
		sleep 1
	done
	return 0
}

wait_for_process_to_exit() {
	_pid=$1
	_n=0
	while kill -0 $_pid 2>/dev/null ; do
		test $_n -eq 1 && trace "waiting for $_pid to exit"
		_n=`expr $_n + 1`
		test $_n -ge 20 && return 1
		sleep 1
	done
	return 0
}

# usage: check_lfwd Y|N message
check_lfwd() {
	_expected=$1
	_message=$2
	rm -f $READY
	${SSH} -F $OBJ/ssh_proxy \
	    -L$LFWD_PORT:127.0.0.1:$PORT \
	    -o ExitOnForwardFailure=yes \
	    -n host exec sh -c \'"sleep 60 & echo \$! > $READY ; wait "\' \
	    >/dev/null 2>&1 &
	_sshpid=$!
	wait_for_file_to_appear $READY || \
		fatal "check_lfwd ssh fail: $_message"
	${SSH} -F $OBJ/ssh_config -p $LFWD_PORT \
	    -oConnectionAttempts=4 host true >/dev/null 2>&1
	_result=$?
	kill $_sshpid `cat $READY` 2>/dev/null
	wait_for_process_to_exit $_sshpid
	if test "x$_expected" = "xY" -a $_result -ne 0 ; then
		fail "check_lfwd failed (expecting success): $_message"
	elif test "x$_expected" = "xN" -a $_result -eq 0 ; then
		fail "check_lfwd succeeded (expecting failure): $_message"
	elif test "x$_expected" != "xY" -a "x$_expected" != "xN" ; then
		fatal "check_lfwd invalid argument \"$_expected\""
	else
		verbose "check_lfwd done (expecting $_expected): $_message"
	fi
}

# usage: check_rfwd Y|N message
check_rfwd() {
	_expected=$1
	_message=$2
	rm -f $READY
	${SSH} -F $OBJ/ssh_proxy \
	    -R127.0.0.1:$RFWD_PORT:127.0.0.1:$PORT \
	    -o ExitOnForwardFailure=yes \
	    -n host exec sh -c \'"sleep 60 & echo \$! > $READY ; wait "\' \
	    >/dev/null 2>&1 &
	_sshpid=$!
	wait_for_file_to_appear $READY
	_result=$?
	if test $_result -eq 0 ; then
		${SSH} -F $OBJ/ssh_config -p $RFWD_PORT \
		    -oConnectionAttempts=4 host true >/dev/null 2>&1
		_result=$?
		kill $_sshpid `cat $READY` 2>/dev/null
		wait_for_process_to_exit $_sshpid
	fi
	if test "x$_expected" = "xY" -a $_result -ne 0 ; then
		fail "check_rfwd failed (expecting success): $_message"
	elif test "x$_expected" = "xN" -a $_result -eq 0 ; then
		fail "check_rfwd succeeded (expecting failure): $_message"
	elif test "x$_expected" != "xY" -a "x$_expected" != "xN" ; then
		fatal "check_rfwd invalid argument \"$_expected\""
	else
		verbose "check_rfwd done (expecting $_expected): $_message"
	fi
}

start_sshd
cp ${OBJ}/sshd_proxy ${OBJ}/sshd_proxy.bak
cp ${OBJ}/authorized_keys_${USER} ${OBJ}/authorized_keys_${USER}.bak

# Sanity check: ensure the default config allows forwarding
check_lfwd Y "default configuration"
check_rfwd Y "default configuration"

# Usage: lperm_tests yes|local|remote|no Y|N Y|N Y|N Y|N Y|N Y|N
lperm_tests() {
	_tcpfwd=$1
	_plain_lfwd=$2
	_plain_rfwd=$3
	_nopermit_lfwd=$4
	_nopermit_rfwd=$5
	_permit_lfwd=$6
	_permit_rfwd=$7
	_badfwd1=127.0.0.1:22
	_badfwd2=127.0.0.2:22
	_goodfwd=127.0.0.1:${PORT}
	cp ${OBJ}/authorized_keys_${USER}.bak  ${OBJ}/authorized_keys_${USER}
	_prefix="AllowTcpForwarding=$_tcpfwd"

	# No PermitOpen
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_plain_lfwd "$_prefix"
	check_rfwd $_plain_rfwd "$_prefix"

	# PermitOpen via sshd_config that doesn't match
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ;
	  echo "PermitOpen $_badfwd1 $_badfwd2" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_nopermit_lfwd "$_prefix, !PermitOpen"
	check_rfwd $_nopermit_rfwd "$_prefix, !PermitOpen"
	# PermitOpen via sshd_config that does match
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ;
	  echo "PermitOpen $_badfwd1 $_goodfwd $_badfwd2" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_plain_lfwd "$_prefix, PermitOpen"
	check_rfwd $_plain_rfwd "$_prefix, PermitOpen"

	# permitopen keys option.
	# NB. permitopen via authorized_keys should have same
	# success/fail as via sshd_config
	# permitopen via authorized_keys that doesn't match
	sed "s/^/permitopen=\"$_badfwd1\",permitopen=\"$_badfwd2\" /" \
	    < ${OBJ}/authorized_keys_${USER}.bak \
	    > ${OBJ}/authorized_keys_${USER} || fatal "sed 1 fail"
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_nopermit_lfwd "$_prefix, !permitopen"
	check_rfwd $_nopermit_rfwd "$_prefix, !permitopen"
	# permitopen via authorized_keys that does match
	sed "s/^/permitopen=\"$_badfwd1\",permitopen=\"$_goodfwd\" /" \
	    < ${OBJ}/authorized_keys_${USER}.bak \
	    > ${OBJ}/authorized_keys_${USER} || fatal "sed 2 fail"
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_permit_lfwd "$_prefix, permitopen"
	check_rfwd $_permit_rfwd "$_prefix, permitopen"

	# Check port-forwarding flags in authorized_keys.
	# These two should refuse all.
	sed "s/^/no-port-forwarding /" \
	    < ${OBJ}/authorized_keys_${USER}.bak \
	    > ${OBJ}/authorized_keys_${USER} || fatal "sed 3 fail"
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd N "$_prefix, no-port-forwarding"
	check_rfwd N "$_prefix, no-port-forwarding"
	sed "s/^/restrict /" \
	    < ${OBJ}/authorized_keys_${USER}.bak \
	    > ${OBJ}/authorized_keys_${USER} || fatal "sed 4 fail"
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd N "$_prefix, restrict"
	check_rfwd N "$_prefix, restrict"
	# This should pass the same cases as _nopermit*
	sed "s/^/restrict,port-forwarding /" \
	    < ${OBJ}/authorized_keys_${USER}.bak \
	    > ${OBJ}/authorized_keys_${USER} || fatal "sed 5 fail"
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_plain_lfwd "$_prefix, restrict,port-forwarding"
	check_rfwd $_plain_rfwd "$_prefix, restrict,port-forwarding"
}

#          permit-open      none          mismatch         match
#   AllowTcpForwarding  local remote    local remote    local remote
lperm_tests     yes     Y     Y         N     Y         Y     Y
lperm_tests   local     Y     N         N     N         Y     N
lperm_tests  remote     N     Y         N     Y         N     Y
lperm_tests      no     N     N         N     N         N     N

# Usage: rperm_tests yes|local|remote|no Y|N Y|N Y|N Y|N Y|N Y|N
rperm_tests() {
	_tcpfwd=$1
	_plain_lfwd=$2
	_plain_rfwd=$3
	_nopermit_lfwd=$4
	_nopermit_rfwd=$5
	_permit_lfwd=$6
	_permit_rfwd=$7
	_badfwd1=127.0.0.1:22
	_badfwd2=127.0.0.2:${RFWD_PORT}
	_goodfwd=127.0.0.1:${RFWD_PORT}
	cp ${OBJ}/authorized_keys_${USER}.bak  ${OBJ}/authorized_keys_${USER}
	_prefix="AllowTcpForwarding=$_tcpfwd"

	# PermitListen via sshd_config that doesn't match
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ;
	  echo "PermitListen $_badfwd1 $_badfwd2" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_nopermit_lfwd "$_prefix, !PermitListen"
	check_rfwd $_nopermit_rfwd "$_prefix, !PermitListen"
	# PermitListen via sshd_config that does match
	( cat ${OBJ}/sshd_proxy.bak ;
	  echo "AllowTcpForwarding $_tcpfwd" ;
	  echo "PermitListen $_badfwd1 $_goodfwd $_badfwd2" ) \
	    > ${OBJ}/sshd_proxy
	check_lfwd $_plain_lfwd "$_prefix, PermitListen"
	check_rfwd $_plain_rfwd "$_prefix, PermitListen"
}

#   permit-remote-open      none          mismatch         match
#   AllowTcpForwarding  local remote    local remote    local remote
rperm_tests     yes     Y     Y         Y     N         Y     Y
rperm_tests   local     Y     N         Y     N         Y     N
rperm_tests  remote     N     Y         N     N         N     Y
rperm_tests      no     N     N         N     N         N     N

