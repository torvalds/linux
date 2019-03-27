#!/bin/sh
### quick sanity test for the binutils.
###
### This file was written and is maintained by K. Richard Pixley,
### rich@cygnus.com.

### fail on errors
set -e

### first arg is directory in which binaries to be tested reside.
case "$1" in
"") BIN=. ;;
*)  BIN="$1" ;;
esac

### size
for i in size objdump nm ar strip ranlib ; do
	${BIN}/size ${BIN}/$i > /dev/null
done

### objdump
for i in size objdump nm ar strip ranlib ; do
	${BIN}/objdump -ahifdrtxsl ${BIN}/$i > /dev/null
done

### nm
for i in size objdump nm ar strip ranlib ; do
	${BIN}/nm ${BIN}/$i > /dev/null
done

### strip
TMPDIR=./binutils-$$
mkdir ${TMPDIR}

cp ${BIN}/strip ${TMPDIR}/strip

for i in size objdump nm ar ranlib ; do
	cp ${BIN}/$i ${TMPDIR}/$i
	${BIN}/strip ${TMPDIR}/$i
	cp ${BIN}/$i ${TMPDIR}/$i
	${TMPDIR}/strip ${TMPDIR}/$i
done

### ar

### ranlib

rm -rf ${TMPDIR}

exit 0
