#	$OpenBSD: keygen-moduli.sh,v 1.2 2016/09/14 00:45:31 dtucker Exp $
#	Placed in the Public Domain.

tid="keygen moduli"

# Try "start at the beginning and stop after 1", "skip 1 then stop after 1"
# and "skip 2 and run to the end with checkpointing".  Since our test data
# file has 3 lines, these should always result in 1 line of output.
for i in "-J1" "-j1 -J1" "-j2 -K $OBJ/moduli.ckpt"; do
	trace "keygen $i"
	rm -f $OBJ/moduli.out $OBJ/moduli.ckpt
	${SSHKEYGEN} -T $OBJ/moduli.out -f ${SRC}/moduli.in $i 2>/dev/null || \
	    fail "keygen screen failed $i"
	lines=`wc -l <$OBJ/moduli.out`
	test "$lines" -eq "1" || fail "expected 1 line, got $lines"
done

rm -f $OBJ/moduli.out $OBJ/moduli.ckpt
