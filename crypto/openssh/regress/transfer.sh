#	$OpenBSD: transfer.sh,v 1.4 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="transfer data"

rm -f ${COPY}
${SSH} -n -q -F $OBJ/ssh_proxy somehost cat ${DATA} > ${COPY}
if [ $? -ne 0 ]; then
	fail "ssh cat $DATA failed"
fi
cmp ${DATA} ${COPY}		|| fail "corrupted copy"

for s in 10 100 1k 32k 64k 128k 256k; do
	trace "dd-size ${s}"
	rm -f ${COPY}
	dd if=$DATA obs=${s} 2> /dev/null | \
		${SSH} -q -F $OBJ/ssh_proxy somehost "cat > ${COPY}"
	if [ $? -ne 0 ]; then
		fail "ssh cat $DATA failed"
	fi
	cmp $DATA ${COPY}		|| fail "corrupted copy"
done
rm -f ${COPY}
