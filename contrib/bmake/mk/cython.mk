# RCSid:
#	$Id: cython.mk,v 1.7 2018/03/25 18:46:11 sjg Exp $
#
#	@(#) Copyright (c) 2014, Simon J. Gerraty
#
#	This file is provided in the hope that it will
#	be of use.  There is absolutely NO WARRANTY.
#	Permission to copy, redistribute or otherwise
#	use this file is hereby granted provided that 
#	the above copyright notice and this notice are
#	left intact. 
#      
#	Please send copies of changes and bug-fixes to:
#	sjg@crufty.net
#

# pyprefix is where python bits are
# which may not be where we want to put ours (prefix)
.if exists(/usr/pkg/include)
pyprefix?= /usr/pkg
.endif
pyprefix?= /usr/local

PYTHON_VERSION?= 2.7
PYTHON_H?= ${pyprefix}/include/python${PYTHON_VERSION}/Python.h
PYVERSION:= ${PYTHON_VERSION:C,\..*,,}

CFLAGS+= -I${PYTHON_H:H}

# conf.host_target() is limited to uname -m rather than uname -p
_HOST_MACHINE!= uname -m
.if ${HOST_TARGET:M*${_HOST_MACHINE}} == ""
PY_HOST_TARGET:= ${HOST_TARGET:S,${_HOST_ARCH:U${uname -p:L:sh}}$,${_HOST_MACHINE},}
.endif

COMPILE.c?= ${CC} -c ${CFLAGS}
PICO?= .pico

.SUFFIXES: ${PICO} .c

.c${PICO}:
	${COMPILE.c} ${PICFLAG} ${CC_PIC} ${.IMPSRC} -o ${.TARGET}

# this is what we build
.if !empty(CYTHON_MODULE_NAME)
CYTHON_MODULE = ${CYTHON_MODULE_NAME}${CYTHON_PYVERSION}.so

CYTHON_SRCS?= ${CYTHON_MODULE_NAME}.pyx

# this is where we save generated src
CYTHON_SAVEGENDIR?= ${.CURDIR}/gen

# set this empty if you don't want to handle multiple versions
.if !defined(CYTHON_PYVERSION)
CYTHON_PYVERSION:= ${PYVERSION}
.endif

CYTHON_GENSRCS= ${CYTHON_SRCS:R:S,$,${CYTHON_PYVERSION}.c,}
SRCS+= ${CYTHON_GENSRCS}

.SUFFIXES: .pyx .c .So

CYTHON?= ${pyprefix}/bin/cython

# if we don't have cython we can use pre-generated srcs
.if ${type ${CYTHON} 2> /dev/null || echo:L:sh:M/*} == ""
.PATH: ${CYTHON_SAVEGENDIR}
.else

.if !empty(CYTHON_PYVERSION)
.for c in ${CYTHON_SRCS}
${c:R}${CYTHON_PYVERSION}.${c:E}: $c
	ln -sf ${.ALLSRC:M*pyx} ${.TARGET}
.endfor
.endif

.pyx.c:
	${CYTHON} ${CYTHON_FLAGS} -${PYVERSION} -o ${.TARGET} ${.IMPSRC}


save-gen: ${CYTHON_GENSRCS}
	mkdir -p ${CYTHON_SAVEGENDIR}
	cp -p ${.ALLSRC} ${CYTHON_SAVEGENDIR}

.endif

${CYTHON_MODULE}: ${SRCS:S,.c,${PICO},}
	${CC} ${CC_SHARED:U-shared} -o ${.TARGET} ${.ALLSRC:M*${PICO}} ${LDADD}

MODULE_BINDIR?= ${.CURDIR:H}/${PY_HOST_TARGET:U${HOST_TARGET}}

build-cython-module: ${CYTHON_MODULE}

install-cython-module: ${CYTHON_MODULE}
	test -d ${DESTDIR}${MODULE_BINDIR} || \
	${INSTALL} -d ${DESTDIR}${MODULE_BINDIR}
	${INSTALL} -m 755 ${.ALLSRC} ${DESTDIR}${MODULE_BINDIR}

CLEANFILES+= *${PICO} ${CYTHON_MODULE}

.endif
