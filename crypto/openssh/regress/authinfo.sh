#	$OpenBSD: authinfo.sh,v 1.3 2018/04/10 00:13:27 djm Exp $
#	Placed in the Public Domain.

tid="authinfo"

# Ensure the environment variable doesn't leak when ExposeAuthInfo=no.
verbose "ExposeAuthInfo=no"
env SSH_USER_AUTH=blah ${SSH} -F $OBJ/ssh_proxy x \
	'env | grep SSH_USER_AUTH >/dev/null' && fail "SSH_USER_AUTH present"

verbose "ExposeAuthInfo=yes"
echo ExposeAuthInfo=yes >> $OBJ/sshd_proxy
${SSH} -F $OBJ/ssh_proxy x \
	'grep ^publickey "$SSH_USER_AUTH" /dev/null >/dev/null' ||
	fail "ssh with ExposeAuthInfo failed"

# XXX test multiple auth and key contents
