#	$OpenBSD: connect-uri.sh,v 1.1 2017/10/24 19:33:32 millert Exp $
#	Placed in the Public Domain.

tid="uri connect"

# Remove Port and User from ssh_config, we want to rely on the URI
cp $OBJ/ssh_config $OBJ/ssh_config.orig
egrep -v '^	+(Port|User)	+.*$' $OBJ/ssh_config.orig > $OBJ/ssh_config

start_sshd

verbose "$tid: no trailing slash"
${SSH} -F $OBJ/ssh_config "ssh://${USER}@somehost:${PORT}" true
if [ $? -ne 0 ]; then
	fail "ssh connection failed"
fi

verbose "$tid: trailing slash"
${SSH} -F $OBJ/ssh_config "ssh://${USER}@somehost:${PORT}/" true
if [ $? -ne 0 ]; then
	fail "ssh connection failed"
fi

verbose "$tid: with path name"
${SSH} -F $OBJ/ssh_config "ssh://${USER}@somehost:${PORT}/${DATA}" true \
    > /dev/null 2>&1
if [ $? -eq 0 ]; then
	fail "ssh connection succeeded, expected failure"
fi
