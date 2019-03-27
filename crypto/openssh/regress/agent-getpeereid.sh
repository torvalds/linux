#	$OpenBSD: agent-getpeereid.sh,v 1.10 2018/02/09 03:40:22 dtucker Exp $
#	Placed in the Public Domain.

tid="disallow agent attach from other uid"

UNPRIV=nobody
ASOCK=${OBJ}/agent
SSH_AUTH_SOCK=/nonexistent

if config_defined HAVE_GETPEEREID HAVE_GETPEERUCRED HAVE_SO_PEERCRED ; then
	:
else
	echo "skipped (not supported on this platform)"
	exit 0
fi
case "x$SUDO" in
	xsudo) sudo=1;;
	xdoas) ;;
	x)
		echo "need SUDO to switch to uid $UNPRIV"
		echo SKIPPED
		exit 0 ;;
	*)
		echo "unsupported $SUDO - "doas" and "sudo" are allowed"
		exit 0 ;;
esac

trace "start agent"
eval `${SSHAGENT} -s -a ${ASOCK}` > /dev/null
r=$?
if [ $r -ne 0 ]; then
	fail "could not start ssh-agent: exit code $r"
else
	chmod 644 ${SSH_AUTH_SOCK}

	${SSHADD} -l > /dev/null 2>&1
	r=$?
	if [ $r -ne 1 ]; then
		fail "ssh-add failed with $r != 1"
	fi
	if test -z "$sudo" ; then
		# doas
		${SUDO} -n -u ${UNPRIV} ${SSHADD} -l 2>/dev/null
	else
		# sudo
		< /dev/null ${SUDO} -S -u ${UNPRIV} ${SSHADD} -l 2>/dev/null
	fi
	r=$?
	if [ $r -lt 2 ]; then
		fail "ssh-add did not fail for ${UNPRIV}: $r < 2"
	fi

	trace "kill agent"
	${SSHAGENT} -k > /dev/null
fi

rm -f ${OBJ}/agent
