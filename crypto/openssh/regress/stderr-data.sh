#	$OpenBSD: stderr-data.sh,v 1.5 2017/04/30 23:34:55 djm Exp $
#	Placed in the Public Domain.

tid="stderr data transfer"

for n in '' -n; do
	verbose "test $tid: ($n)"
	${SSH} $n -F $OBJ/ssh_proxy otherhost exec \
	    sh -c \'"exec > /dev/null; sleep 3; cat ${DATA} 1>&2 $s"\' \
		2> ${COPY}
	r=$?
	if [ $r -ne 0 ]; then
		fail "ssh failed with exit code $r"
	fi
	cmp ${DATA} ${COPY}	|| fail "stderr corrupt"
	rm -f ${COPY}

	${SSH} $n -F $OBJ/ssh_proxy otherhost exec \
	    sh -c \'"echo a; exec > /dev/null; sleep 3; cat ${DATA} 1>&2 $s"\' \
		> /dev/null 2> ${COPY}
	r=$?
	if [ $r -ne 0 ]; then
		fail "ssh failed with exit code $r"
	fi
	cmp ${DATA} ${COPY}	|| fail "stderr corrupt"
	rm -f ${COPY}
done
