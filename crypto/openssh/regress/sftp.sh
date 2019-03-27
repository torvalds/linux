#	$OpenBSD: sftp.sh,v 1.6 2017/10/30 21:59:43 djm Exp $
#	Placed in the Public Domain.

tid="basic sftp put/get"

SFTPCMDFILE=${OBJ}/batch
cat >$SFTPCMDFILE <<EOF
version
get $DATA ${COPY}.1
put $DATA ${COPY}.2
EOF

BUFFERSIZE="5 1000 32000 64000"
REQUESTS="1 2 10"

for B in ${BUFFERSIZE}; do
	for R in ${REQUESTS}; do
                verbose "test $tid: buffer_size $B num_requests $R"
		rm -f ${COPY}.1 ${COPY}.2
		${SFTP} -D ${SFTPSERVER} -B $B -R $R -b $SFTPCMDFILE \
		> /dev/null 2>&1
		r=$?
		if [ $r -ne 0 ]; then
			fail "sftp failed with $r"
		else
			cmp $DATA ${COPY}.1 || fail "corrupted copy after get"
			cmp $DATA ${COPY}.2 || fail "corrupted copy after put"
		fi
	done
done
rm -f ${COPY}.1 ${COPY}.2
rm -f $SFTPCMDFILE
