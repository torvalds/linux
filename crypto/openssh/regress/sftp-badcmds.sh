#	$OpenBSD: sftp-badcmds.sh,v 1.6 2013/05/17 10:26:26 dtucker Exp $
#	Placed in the Public Domain.

tid="sftp invalid commands"

DATA2=/bin/sh${EXEEXT}
NONEXIST=/NONEXIST.$$
GLOBFILES=`(cd /bin;echo l*)`

rm -rf ${COPY} ${COPY}.1 ${COPY}.2 ${COPY}.dd

rm -f ${COPY}
verbose "$tid: get nonexistent"
echo "get $NONEXIST $COPY" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
	|| fail "get nonexistent failed"
test -f ${COPY} && fail "existing copy after get nonexistent"

rm -f ${COPY}.dd/*
verbose "$tid: glob get to nonexistent directory"
echo "get /bin/l* $NONEXIST" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
        || fail "get nonexistent failed"
for x in $GLOBFILES; do
        test -f ${COPY}.dd/$x && fail "existing copy after get nonexistent"
done

rm -f ${COPY}
verbose "$tid: put nonexistent"
echo "put $NONEXIST $COPY" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
	|| fail "put nonexistent failed"
test -f ${COPY} && fail "existing copy after put nonexistent"

rm -f ${COPY}.dd/*
verbose "$tid: glob put to nonexistent directory"
echo "put /bin/l* ${COPY}.dd" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
        || fail "put nonexistent failed"
for x in $GLOBFILES; do
        test -f ${COPY}.dd/$x && fail "existing copy after nonexistent"
done

rm -f ${COPY}
verbose "$tid: rename nonexistent"
echo "rename $NONEXIST ${COPY}.1" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
	|| fail "rename nonexist failed"
test -f ${COPY}.1 && fail "file exists after rename nonexistent"

rm -rf ${COPY} ${COPY}.dd
cp $DATA $COPY
mkdir ${COPY}.dd
verbose "$tid: rename target exists (directory)"
echo "rename $COPY ${COPY}.dd" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 \
	|| fail "rename target exists (directory) failed"
test -f ${COPY} || fail "oldname missing after rename target exists (directory)"
test -d ${COPY}.dd || fail "newname missing after rename target exists (directory)"
cmp $DATA ${COPY} >/dev/null 2>&1 || fail "corrupted oldname after rename target exists (directory)"

rm -f ${COPY}.dd/*
rm -rf ${COPY}
cp ${DATA2} ${COPY}
verbose "$tid: glob put files to local file"
echo "put /bin/l* $COPY" | ${SFTP} -D ${SFTPSERVER} >/dev/null 2>&1 
cmp ${DATA2} ${COPY} || fail "put successed when it should have failed"

rm -rf ${COPY} ${COPY}.1 ${COPY}.2 ${COPY}.dd


