#	$OpenBSD: sftp-uri.sh,v 1.1 2017/10/24 19:33:32 millert Exp $
#	Placed in the Public Domain.

tid="sftp-uri"

#set -x

COPY2=${OBJ}/copy2
DIR=${COPY}.dd
DIR2=${COPY}.dd2
SRC=`dirname ${SCRIPT}`

sftpclean() {
	rm -rf ${COPY} ${COPY2} ${DIR} ${DIR2}
	mkdir ${DIR} ${DIR2}
}

start_sshd -oForceCommand="internal-sftp -d /"

# Remove Port and User from ssh_config, we want to rely on the URI
cp $OBJ/ssh_config $OBJ/ssh_config.orig
egrep -v '^	+(Port|User)	+.*$' $OBJ/ssh_config.orig > $OBJ/ssh_config

verbose "$tid: non-interactive fetch to local file"
sftpclean
${SFTP} -q -S "$SSH" -F $OBJ/ssh_config "sftp://${USER}@somehost:${PORT}/${DATA}" ${COPY} || fail "copy failed"
cmp ${DATA} ${COPY} || fail "corrupted copy"

verbose "$tid: non-interactive fetch to local dir"
sftpclean
cp ${DATA} ${COPY}
${SFTP} -q -S "$SSH" -F $OBJ/ssh_config "sftp://${USER}@somehost:${PORT}/${COPY}" ${DIR} || fail "copy failed"
cmp ${COPY} ${DIR}/copy || fail "corrupted copy"

verbose "$tid: put to remote directory (trailing slash)"
sftpclean
${SFTP} -q -S "$SSH" -F $OBJ/ssh_config -b - \
    "sftp://${USER}@somehost:${PORT}/${DIR}/" > /dev/null 2>&1 << EOF
	version
	put ${DATA} copy
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "sftp failed with $r"
else
	cmp ${DATA} ${DIR}/copy || fail "corrupted copy"
fi

verbose "$tid: put to remote directory (no slash)"
sftpclean
${SFTP} -q -S "$SSH" -F $OBJ/ssh_config -b - \
    "sftp://${USER}@somehost:${PORT}/${DIR}" > /dev/null 2>&1 << EOF
	version
	put ${DATA} copy
EOF
r=$?
if [ $r -ne 0 ]; then
	fail "sftp failed with $r"
else
	cmp ${DATA} ${DIR}/copy || fail "corrupted copy"
fi

sftpclean
