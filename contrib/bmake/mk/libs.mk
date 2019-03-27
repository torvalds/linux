# $Id: libs.mk,v 1.3 2013/08/02 18:28:48 sjg Exp $
#
#	@(#) Copyright (c) 2006, Simon J. Gerraty
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

.MAIN: all

.if defined(LIBS)

# In meta mode, we can capture dependenices for _one_ of the progs.
# if makefile doesn't nominate one, we use the first.
.ifndef UPDATE_DEPENDFILE_LIB
UPDATE_DEPENDFILE_LIB = ${LIBS:[1]}
.export UPDATE_DEPENDFILE_LIB
.endif

.ifndef LIB
# They may have asked us to build just one
.for t in ${LIBS:R:T:S,^lib,,}
.if make(lib$t)
LIB?= $t
lib$t: all
.endif
.endfor
.endif

.if defined(LIB)
# just one of many
LIB_VARS += \
	LIBDIR \
	CFLAGS \
	COPTS \
	CPPFLAGS \
	CXXFLAGS \
	DPADD \
	DPLIBS \
	LDADD \
	LDFLAGS \
	MAN \
	SRCS

.for v in ${LIB_VARS:O:u}
.if defined(${v}.${LIB}) || defined(${v}_${LIB})
$v += ${${v}_${LIB}:U${${v}.${LIB}}}
.endif
.endfor

# for meta mode, there can be only one!
.if ${LIB} == ${UPDATE_DEPENDFILE_LIB:Uno}
UPDATE_DEPENDFILE ?= yes
.endif
UPDATE_DEPENDFILE ?= NO

# ensure that we don't clobber each other's dependencies
DEPENDFILE?= .depend.${LIB}
# lib.mk will do the rest
.else
all: ${LIBS:S,^lib,,:@t@lib$t.a@} .MAKE

# We cannot capture dependencies for meta mode here
UPDATE_DEPENDFILE = NO
# nor can we safely run in parallel.
.NOTPARALLEL:
.endif
.endif

# handle being called [bsd.]libs.mk
.include <${.PARSEFILE:S,libs,lib,}>

.ifndef LIB
# tell libs.mk we might want to install things
LIBS_TARGETS+= cleandepend cleandir cleanobj depend install

.for b in ${LIBS:R:T:S,^lib,,}
lib$b.a: ${SRCS} ${DPADD} ${SRCS_lib$b} ${DPADD_lib$b} 
	(cd ${.CURDIR} && ${.MAKE} -f ${MAKEFILE} LIB=$b)

.for t in ${LIBS_TARGETS:O:u}
$b.$t: .PHONY .MAKE
	(cd ${.CURDIR} && ${.MAKE} -f ${MAKEFILE} LIB=$b ${@:E})
.endfor
.endfor
.endif
