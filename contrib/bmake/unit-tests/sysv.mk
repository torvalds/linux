# $Id: sysv.mk,v 1.2 2014/08/30 22:25:14 sjg Exp $

FOO ?=
FOOBAR = ${FOO:=bar}

_this := ${.PARSEDIR}/${.PARSEFILE}

B = /b
S = /
FUN = ${B}${S}fun
SUN = the Sun

# we expect nothing when FOO is empty
all: foo fun

foo:
	@echo FOOBAR = ${FOOBAR}
.if empty(FOO)
	@FOO="foo fu" ${.MAKE} -f ${_this} foo
.endif

fun:
	@echo ${FUN:T}
	@echo ${FUN:${B}${S}fun=fun}
	@echo ${FUN:${B}${S}%=%}
	@echo ${In:L:%=% ${SUN}}
