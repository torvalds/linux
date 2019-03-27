# $Id: forloop.mk,v 1.1.1.1 2014/08/30 18:57:18 sjg Exp $

all: for-loop

LIST = one "two and three" four "five"

.if make(for-fail)
for-fail:

XTRA_LIST = xtra
.else

.for x in ${LIST}
X!= echo 'x=$x' >&2; echo
.endfor

CFL = -I/this -I"This or that" -Ithat "-DTHIS=\"this and that\""
cfl=
.for x in ${CFL}
X!= echo 'x=$x' >&2; echo
.if empty(cfl)
cfl= $x
.else
cfl+= $x
.endif
.endfor
X!= echo 'cfl=${cfl}' >&2; echo

.if ${cfl} != ${CFL}
.error ${.newline}'${cfl}' != ${.newline}'${CFL}'
.endif

.for a b in ${EMPTY}
X!= echo 'a=$a b=$b' >&2; echo
.endfor
.endif

.for a b in ${LIST} ${LIST:tu} ${XTRA_LIST}
X!= echo 'a=$a b=$b' >&2; echo
.endfor

for-loop:
	@echo We expect an error next:
	@(cd ${.CURDIR} && ${.MAKE} -f ${MAKEFILE} for-fail) && \
	{ echo "Oops that should have failed!"; exit 1; } || echo OK
