# $Id: whats.mk,v 1.3 2017/10/19 06:09:14 sjg Exp $
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

.if ${MK_WHATSTRING:Uno} != "no"
what_build_exts?= o
# it can be useful to embed a what(1) string in binaries 
# so that the build location can be seen from a core file.
.if defined(PROG) && ${.MAKE.MAKEFILES:M*prog.mk} != ""
what_thing?= ${PROGNAME:U${PROG}}
what_build_thing?= ${PROG}
.elif defined(LIB) && ${.MAKE.MAKEFILES:M*lib.mk} != ""
# probably only makes sense for shared libs
# and the plumbing needed varies depending on *lib.mk
what_thing?= lib${LIB}
.if !empty(SOBJS)
_soe:= ${SOBJS:E:[1]}
what_build_exts= ${_soe}
SOBJS+= ${what_uuid}.${_soe}
.endif
.elif defined(KMOD) && ${.MAKE.MAKEFILES:M*kmod.mk} != ""
what_thing?= ${KMOD}
what_build_thing?= ${KMOD}.ko
.endif

.if !empty(what_thing)
# a unique name that won't conflict with anything
what_uuid = what_${what_thing}_${.CURDIR:T:hash}
what_var = what_${.CURDIR:T:hash}

.if !empty(what_build_thing)
${what_build_thing}: ${what_build_exts:@e@${what_uuid}.$e@}
.endif
OBJS+= ${what_uuid}.o
CLEANFILES+= ${what_uuid}.c

# we do not need to capture this
SUPPRESS_DEPEND+= *${what_uuid}.c

SB?= ${SRCTOP:H}
SB_LOCATION?= ${HOST}:${SB}
what_location:= ${.OBJDIR:S,${SB},${SB_LOCATION},}

# this works with clang and gcc
_what_t= const char __attribute__ ((section(".data")))
_what1:= @(\#)${what_thing:tu} built ${%Y%m%d:L:localtime} by ${USER}
_what2:= @(\#)${what_location}

${what_uuid}.c:
	echo '${_what_t} ${what_var}1[] = "${_what1}";' > $@ ${.OODATE:MNO_META_CMP}
	echo '${_what_t} ${what_var}2[] = "${_what2}";' >> $@
.endif
.endif
