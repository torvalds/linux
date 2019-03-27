#	$OpenBSD: test-exec.sh,v 1.64 2018/08/10 01:35:49 dtucker Exp $
#	Placed in the Public Domain.

#SUDO=sudo

# Unbreak GNU head(1)
_POSIX2_VERSION=199209
export _POSIX2_VERSION

case `uname -s 2>/dev/null` in
OSF1*)
	BIN_SH=xpg4
	export BIN_SH
	;;
CYGWIN_NT-5.0)
	os=cygwin
	TEST_SSH_IPV6=no
	;;
CYGWIN*)
	os=cygwin
	;;
esac

if [ ! -z "$TEST_SSH_PORT" ]; then
	PORT="$TEST_SSH_PORT"
else
	PORT=4242
fi

if [ -x /usr/ucb/whoami ]; then
	USER=`/usr/ucb/whoami`
elif whoami >/dev/null 2>&1; then
	USER=`whoami`
elif logname >/dev/null 2>&1; then
	USER=`logname`
else
	USER=`id -un`
fi

OBJ=$1
if [ "x$OBJ" = "x" ]; then
	echo '$OBJ not defined'
	exit 2
fi
if [ ! -d $OBJ ]; then
	echo "not a directory: $OBJ"
	exit 2
fi
SCRIPT=$2
if [ "x$SCRIPT" = "x" ]; then
	echo '$SCRIPT not defined'
	exit 2
fi
if [ ! -f $SCRIPT ]; then
	echo "not a file: $SCRIPT"
	exit 2
fi
if $TEST_SHELL -n $SCRIPT; then
	true
else
	echo "syntax error in $SCRIPT"
	exit 2
fi
unset SSH_AUTH_SOCK

SRC=`dirname ${SCRIPT}`

# defaults
SSH=ssh
SSHD=sshd
SSHAGENT=ssh-agent
SSHADD=ssh-add
SSHKEYGEN=ssh-keygen
SSHKEYSCAN=ssh-keyscan
SFTP=sftp
SFTPSERVER=/usr/libexec/openssh/sftp-server
SCP=scp

# Set by make_tmpdir() on demand (below).
SSH_REGRESS_TMP=

# Interop testing
PLINK=plink
PUTTYGEN=puttygen
CONCH=conch

if [ "x$TEST_SSH_SSH" != "x" ]; then
	SSH="${TEST_SSH_SSH}"
fi
if [ "x$TEST_SSH_SSHD" != "x" ]; then
	SSHD="${TEST_SSH_SSHD}"
fi
if [ "x$TEST_SSH_SSHAGENT" != "x" ]; then
	SSHAGENT="${TEST_SSH_SSHAGENT}"
fi
if [ "x$TEST_SSH_SSHADD" != "x" ]; then
	SSHADD="${TEST_SSH_SSHADD}"
fi
if [ "x$TEST_SSH_SSHKEYGEN" != "x" ]; then
	SSHKEYGEN="${TEST_SSH_SSHKEYGEN}"
fi
if [ "x$TEST_SSH_SSHKEYSCAN" != "x" ]; then
	SSHKEYSCAN="${TEST_SSH_SSHKEYSCAN}"
fi
if [ "x$TEST_SSH_SFTP" != "x" ]; then
	SFTP="${TEST_SSH_SFTP}"
fi
if [ "x$TEST_SSH_SFTPSERVER" != "x" ]; then
	SFTPSERVER="${TEST_SSH_SFTPSERVER}"
fi
if [ "x$TEST_SSH_SCP" != "x" ]; then
	SCP="${TEST_SSH_SCP}"
fi
if [ "x$TEST_SSH_PLINK" != "x" ]; then
	# Find real binary, if it exists
	case "${TEST_SSH_PLINK}" in
	/*) PLINK="${TEST_SSH_PLINK}" ;;
	*) PLINK=`which ${TEST_SSH_PLINK} 2>/dev/null` ;;
	esac
fi
if [ "x$TEST_SSH_PUTTYGEN" != "x" ]; then
	# Find real binary, if it exists
	case "${TEST_SSH_PUTTYGEN}" in
	/*) PUTTYGEN="${TEST_SSH_PUTTYGEN}" ;;
	*) PUTTYGEN=`which ${TEST_SSH_PUTTYGEN} 2>/dev/null` ;;
	esac
fi
if [ "x$TEST_SSH_CONCH" != "x" ]; then
	# Find real binary, if it exists
	case "${TEST_SSH_CONCH}" in
	/*) CONCH="${TEST_SSH_CONCH}" ;;
	*) CONCH=`which ${TEST_SSH_CONCH} 2>/dev/null` ;;
	esac
fi

# Path to sshd must be absolute for rexec
case "$SSHD" in
/*) ;;
*) SSHD=`which $SSHD` ;;
esac

case "$SSHAGENT" in
/*) ;;
*) SSHAGENT=`which $SSHAGENT` ;;
esac

# Record the actual binaries used.
SSH_BIN=${SSH}
SSHD_BIN=${SSHD}
SSHAGENT_BIN=${SSHAGENT}
SSHADD_BIN=${SSHADD}
SSHKEYGEN_BIN=${SSHKEYGEN}
SSHKEYSCAN_BIN=${SSHKEYSCAN}
SFTP_BIN=${SFTP}
SFTPSERVER_BIN=${SFTPSERVER}
SCP_BIN=${SCP}

if [ "x$USE_VALGRIND" != "x" ]; then
	mkdir -p $OBJ/valgrind-out
	VG_TEST=`basename $SCRIPT .sh`

	# Some tests are difficult to fix.
	case "$VG_TEST" in
	connect-privsep|reexec)
		VG_SKIP=1 ;;
	esac

	if [ x"$VG_SKIP" = "x" ]; then
		VG_LEAK="--leak-check=no"
		if [ x"$VALGRIND_CHECK_LEAKS" != "x" ]; then
			VG_LEAK="--leak-check=full"
		fi
		VG_IGNORE="/bin/*,/sbin/*,/usr/*,/var/*"
		VG_LOG="$OBJ/valgrind-out/${VG_TEST}."
		VG_OPTS="--track-origins=yes $VG_LEAK"
		VG_OPTS="$VG_OPTS --trace-children=yes"
		VG_OPTS="$VG_OPTS --trace-children-skip=${VG_IGNORE}"
		VG_PATH="valgrind"
		if [ "x$VALGRIND_PATH" != "x" ]; then
			VG_PATH="$VALGRIND_PATH"
		fi
		VG="$VG_PATH $VG_OPTS"
		SSH="$VG --log-file=${VG_LOG}ssh.%p $SSH"
		SSHD="$VG --log-file=${VG_LOG}sshd.%p $SSHD"
		SSHAGENT="$VG --log-file=${VG_LOG}ssh-agent.%p $SSHAGENT"
		SSHADD="$VG --log-file=${VG_LOG}ssh-add.%p $SSHADD"
		SSHKEYGEN="$VG --log-file=${VG_LOG}ssh-keygen.%p $SSHKEYGEN"
		SSHKEYSCAN="$VG --log-file=${VG_LOG}ssh-keyscan.%p $SSHKEYSCAN"
		SFTP="$VG --log-file=${VG_LOG}sftp.%p ${SFTP}"
		SCP="$VG --log-file=${VG_LOG}scp.%p $SCP"
		cat > $OBJ/valgrind-sftp-server.sh << EOF
#!/bin/sh
exec $VG --log-file=${VG_LOG}sftp-server.%p $SFTPSERVER "\$@"
EOF
		chmod a+rx $OBJ/valgrind-sftp-server.sh
		SFTPSERVER="$OBJ/valgrind-sftp-server.sh"
	fi
fi

# Logfiles.
# SSH_LOGFILE should be the debug output of ssh(1) only
# SSHD_LOGFILE should be the debug output of sshd(8) only
# REGRESS_LOGFILE is the output of the test itself stdout and stderr
if [ "x$TEST_SSH_LOGFILE" = "x" ]; then
	TEST_SSH_LOGFILE=$OBJ/ssh.log
fi
if [ "x$TEST_SSHD_LOGFILE" = "x" ]; then
	TEST_SSHD_LOGFILE=$OBJ/sshd.log
fi
if [ "x$TEST_REGRESS_LOGFILE" = "x" ]; then
	TEST_REGRESS_LOGFILE=$OBJ/regress.log
fi

# truncate logfiles
>$TEST_SSH_LOGFILE
>$TEST_SSHD_LOGFILE
>$TEST_REGRESS_LOGFILE

# Create wrapper ssh with logging.  We can't just specify "SSH=ssh -E..."
# because sftp and scp don't handle spaces in arguments.
SSHLOGWRAP=$OBJ/ssh-log-wrapper.sh
echo "#!/bin/sh" > $SSHLOGWRAP
echo "exec ${SSH} -E${TEST_SSH_LOGFILE} "'"$@"' >>$SSHLOGWRAP

chmod a+rx $OBJ/ssh-log-wrapper.sh
REAL_SSH="$SSH"
SSH="$SSHLOGWRAP"

# Some test data.  We make a copy because some tests will overwrite it.
# The tests may assume that $DATA exists and is writable and $COPY does
# not exist.  Tests requiring larger data files can call increase_datafile_size
# [kbytes] to ensure the file is at least that large.
DATANAME=data
DATA=$OBJ/${DATANAME}
cat ${SSHAGENT_BIN} >${DATA}
chmod u+w ${DATA}
COPY=$OBJ/copy
rm -f ${COPY}

increase_datafile_size()
{
	while [ `du -k ${DATA} | cut -f1` -lt $1 ]; do
		cat ${SSHAGENT_BIN} >>${DATA}
	done
}

# these should be used in tests
export SSH SSHD SSHAGENT SSHADD SSHKEYGEN SSHKEYSCAN SFTP SFTPSERVER SCP
#echo $SSH $SSHD $SSHAGENT $SSHADD $SSHKEYGEN $SSHKEYSCAN $SFTP $SFTPSERVER $SCP

# Portable specific functions
have_prog()
{
	saved_IFS="$IFS"
	IFS=":"
	for i in $PATH
	do
		if [ -x $i/$1 ]; then
			IFS="$saved_IFS"
			return 0
		fi
	done
	IFS="$saved_IFS"
	return 1
}

jot() {
	awk "BEGIN { for (i = $2; i < $2 + $1; i++) { printf \"%d\n\", i } exit }"
}

# Check whether preprocessor symbols are defined in config.h.
config_defined ()
{
	str=$1
	while test "x$2" != "x" ; do
		str="$str|$2"
		shift
	done
	egrep "^#define.*($str)" ${BUILDDIR}/config.h >/dev/null 2>&1
}

md5 () {
	if have_prog md5sum; then
		md5sum
	elif have_prog openssl; then
		openssl md5
	elif have_prog cksum; then
		cksum
	elif have_prog sum; then
		sum
	else
		wc -c
	fi
}
# End of portable specific functions

stop_sshd ()
{
	if [ -f $PIDFILE ]; then
		pid=`$SUDO cat $PIDFILE`
		if [ "X$pid" = "X" ]; then
			echo no sshd running
		else
			if [ $pid -lt 2 ]; then
				echo bad pid for sshd: $pid
			else
				$SUDO kill $pid
				trace "wait for sshd to exit"
				i=0;
				while [ -f $PIDFILE -a $i -lt 5 ]; do
					i=`expr $i + 1`
					sleep $i
				done
				if test -f $PIDFILE; then
					if $SUDO kill -0 $pid; then
						echo "sshd didn't exit " \
						    "port $PORT pid $pid"
					else
						echo "sshd died without cleanup"
					fi
					exit 1
				fi
			fi
		fi
	fi
}

make_tmpdir ()
{
	SSH_REGRESS_TMP="$($OBJ/mkdtemp openssh-XXXXXXXX)" || \
	    fatal "failed to create temporary directory"
}

# helper
cleanup ()
{
	if [ "x$SSH_PID" != "x" ]; then
		if [ $SSH_PID -lt 2 ]; then
			echo bad pid for ssh: $SSH_PID
		else
			kill $SSH_PID
		fi
	fi
	if [ "x$SSH_REGRESS_TMP" != "x" ]; then
		rm -rf "$SSH_REGRESS_TMP"
	fi
	stop_sshd
}

start_debug_log ()
{
	echo "trace: $@" >$TEST_REGRESS_LOGFILE
	echo "trace: $@" >$TEST_SSH_LOGFILE
	echo "trace: $@" >$TEST_SSHD_LOGFILE
}

save_debug_log ()
{
	echo $@ >>$TEST_REGRESS_LOGFILE
	echo $@ >>$TEST_SSH_LOGFILE
	echo $@ >>$TEST_SSHD_LOGFILE
	(cat $TEST_REGRESS_LOGFILE; echo) >>$OBJ/failed-regress.log
	(cat $TEST_SSH_LOGFILE; echo) >>$OBJ/failed-ssh.log
	(cat $TEST_SSHD_LOGFILE; echo) >>$OBJ/failed-sshd.log
}

trace ()
{
	start_debug_log $@
	if [ "X$TEST_SSH_TRACE" = "Xyes" ]; then
		echo "$@"
	fi
}

verbose ()
{
	start_debug_log $@
	if [ "X$TEST_SSH_QUIET" != "Xyes" ]; then
		echo "$@"
	fi
}

warn ()
{
	echo "WARNING: $@" >>$TEST_SSH_LOGFILE
	echo "WARNING: $@"
}

fail ()
{
	save_debug_log "FAIL: $@"
	RESULT=1
	echo "$@"
	if test "x$TEST_SSH_FAIL_FATAL" != "x" ; then
		cleanup
		exit $RESULT
	fi
}

fatal ()
{
	save_debug_log "FATAL: $@"
	printf "FATAL: "
	fail "$@"
	cleanup
	exit $RESULT
}

RESULT=0
PIDFILE=$OBJ/pidfile

trap fatal 3 2

# create server config
cat << EOF > $OBJ/sshd_config
	StrictModes		no
	Port			$PORT
	AddressFamily		inet
	ListenAddress		127.0.0.1
	#ListenAddress		::1
	PidFile			$PIDFILE
	AuthorizedKeysFile	$OBJ/authorized_keys_%u
	LogLevel		DEBUG3
	AcceptEnv		_XXX_TEST_*
	AcceptEnv		_XXX_TEST
	Subsystem	sftp	$SFTPSERVER
EOF

# This may be necessary if /usr/src and/or /usr/obj are group-writable,
# but if you aren't careful with permissions then the unit tests could
# be abused to locally escalate privileges.
if [ ! -z "$TEST_SSH_UNSAFE_PERMISSIONS" ]; then
	echo "StrictModes no" >> $OBJ/sshd_config
fi

if [ ! -z "$TEST_SSH_SSHD_CONFOPTS" ]; then
	trace "adding sshd_config option $TEST_SSH_SSHD_CONFOPTS"
	echo "$TEST_SSH_SSHD_CONFOPTS" >> $OBJ/sshd_config
fi

# server config for proxy connects
cp $OBJ/sshd_config $OBJ/sshd_proxy

# allow group-writable directories in proxy-mode
echo 'StrictModes no' >> $OBJ/sshd_proxy

# create client config
cat << EOF > $OBJ/ssh_config
Host *
	Hostname		127.0.0.1
	HostKeyAlias		localhost-with-alias
	Port			$PORT
	User			$USER
	GlobalKnownHostsFile	$OBJ/known_hosts
	UserKnownHostsFile	$OBJ/known_hosts
	PubkeyAuthentication	yes
	ChallengeResponseAuthentication	no
	HostbasedAuthentication	no
	PasswordAuthentication	no
	BatchMode		yes
	StrictHostKeyChecking	yes
	LogLevel		DEBUG3
EOF

if [ ! -z "$TEST_SSH_SSH_CONFOPTS" ]; then
	trace "adding ssh_config option $TEST_SSH_SSH_CONFOPTS"
	echo "$TEST_SSH_SSH_CONFOPTS" >> $OBJ/ssh_config
fi

rm -f $OBJ/known_hosts $OBJ/authorized_keys_$USER

SSH_KEYTYPES="rsa ed25519"

trace "generate keys"
for t in ${SSH_KEYTYPES}; do
	# generate user key
	if [ ! -f $OBJ/$t ] || [ ${SSHKEYGEN_BIN} -nt $OBJ/$t ]; then
		rm -f $OBJ/$t
		${SSHKEYGEN} -q -N '' -t $t  -f $OBJ/$t ||\
			fail "ssh-keygen for $t failed"
	fi

	# known hosts file for client
	(
		printf 'localhost-with-alias,127.0.0.1,::1 '
		cat $OBJ/$t.pub
	) >> $OBJ/known_hosts

	# setup authorized keys
	cat $OBJ/$t.pub >> $OBJ/authorized_keys_$USER
	echo IdentityFile $OBJ/$t >> $OBJ/ssh_config

	# use key as host key, too
	$SUDO cp $OBJ/$t $OBJ/host.$t
	echo HostKey $OBJ/host.$t >> $OBJ/sshd_config

	# don't use SUDO for proxy connect
	echo HostKey $OBJ/$t >> $OBJ/sshd_proxy
done
chmod 644 $OBJ/authorized_keys_$USER

# Activate Twisted Conch tests if the binary is present
REGRESS_INTEROP_CONCH=no
if test -x "$CONCH" ; then
	REGRESS_INTEROP_CONCH=yes
fi

# If PuTTY is present and we are running a PuTTY test, prepare keys and
# configuration
REGRESS_INTEROP_PUTTY=no
if test -x "$PUTTYGEN" -a -x "$PLINK" ; then
	REGRESS_INTEROP_PUTTY=yes
fi
case "$SCRIPT" in
*putty*)	;;
*)		REGRESS_INTEROP_PUTTY=no ;;
esac

if test "$REGRESS_INTEROP_PUTTY" = "yes" ; then
	mkdir -p ${OBJ}/.putty

	# Add a PuTTY key to authorized_keys
	rm -f ${OBJ}/putty.rsa2
	if ! puttygen -t rsa -o ${OBJ}/putty.rsa2 \
	    --random-device=/dev/urandom \
	    --new-passphrase /dev/null < /dev/null > /dev/null; then
		echo "Your installed version of PuTTY is too old to support --new-passphrase; trying without (may require manual interaction) ..." >&2
		puttygen -t rsa -o ${OBJ}/putty.rsa2 < /dev/null > /dev/null
	fi
	puttygen -O public-openssh ${OBJ}/putty.rsa2 \
	    >> $OBJ/authorized_keys_$USER

	# Convert rsa2 host key to PuTTY format
	cp $OBJ/rsa $OBJ/rsa_oldfmt
	${SSHKEYGEN} -p -N '' -m PEM -f $OBJ/rsa_oldfmt >/dev/null
	${SRC}/ssh2putty.sh 127.0.0.1 $PORT $OBJ/rsa_oldfmt > \
	    ${OBJ}/.putty/sshhostkeys
	${SRC}/ssh2putty.sh 127.0.0.1 22 $OBJ/rsa_oldfmt >> \
	    ${OBJ}/.putty/sshhostkeys
	rm -f $OBJ/rsa_oldfmt

	# Setup proxied session
	mkdir -p ${OBJ}/.putty/sessions
	rm -f ${OBJ}/.putty/sessions/localhost_proxy
	echo "Protocol=ssh" >> ${OBJ}/.putty/sessions/localhost_proxy
	echo "HostName=127.0.0.1" >> ${OBJ}/.putty/sessions/localhost_proxy
	echo "PortNumber=$PORT" >> ${OBJ}/.putty/sessions/localhost_proxy
	echo "ProxyMethod=5" >> ${OBJ}/.putty/sessions/localhost_proxy
	echo "ProxyTelnetCommand=sh ${SRC}/sshd-log-wrapper.sh ${TEST_SSHD_LOGFILE} ${SSHD} -i -f $OBJ/sshd_proxy" >> ${OBJ}/.putty/sessions/localhost_proxy
	echo "ProxyLocalhost=1" >> ${OBJ}/.putty/sessions/localhost_proxy

	PUTTYDIR=${OBJ}/.putty
	export PUTTYDIR

	REGRESS_INTEROP_PUTTY=yes
fi

# create a proxy version of the client config
(
	cat $OBJ/ssh_config
	echo proxycommand ${SUDO} sh ${SRC}/sshd-log-wrapper.sh ${TEST_SSHD_LOGFILE} ${SSHD} -i -f $OBJ/sshd_proxy
) > $OBJ/ssh_proxy

# check proxy config
${SSHD} -t -f $OBJ/sshd_proxy	|| fatal "sshd_proxy broken"

start_sshd ()
{
	# start sshd
	$SUDO ${SSHD} -f $OBJ/sshd_config "$@" -t || fatal "sshd_config broken"
	$SUDO ${SSHD} -f $OBJ/sshd_config "$@" -E$TEST_SSHD_LOGFILE

	trace "wait for sshd"
	i=0;
	while [ ! -f $PIDFILE -a $i -lt 10 ]; do
		i=`expr $i + 1`
		sleep $i
	done

	test -f $PIDFILE || fatal "no sshd running on port $PORT"
}

# source test body
. $SCRIPT

# kill sshd
cleanup
if [ $RESULT -eq 0 ]; then
	verbose ok $tid
else
	echo failed $tid
fi
exit $RESULT
