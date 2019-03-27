# $Id: dpadd.mk,v 1.26 2018/02/12 21:54:26 sjg Exp $
#
#	@(#) Copyright (c) 2004, Simon J. Gerraty
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

.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

# sometimes we play games with .CURDIR etc
# _* hold the original values of .*
_OBJDIR?= ${.OBJDIR}
_CURDIR?= ${.CURDIR}

.if ${_CURDIR} == ${SRCTOP}
RELDIR=.
RELTOP=.
.else
RELDIR?= ${_CURDIR:S,${SRCTOP}/,,}
.if ${RELDIR} == ${_CURDIR}
RELDIR?= ${_OBJDIR:S,${OBJTOP}/,,}
.endif
RELTOP?= ${RELDIR:C,[^/]+,..,g}
.endif
RELOBJTOP?= ${OBJTOP}
RELSRCTOP?= ${SRCTOP}

# we get included just about everywhere so this is handy...
# C*DEBUG_XTRA are for defining on cmd line etc 
# so do not use in makefiles.
.ifdef CFLAGS_DEBUG_XTRA
CFLAGS_LAST += ${CFLAGS_DEBUG_XTRA}
.endif
.ifdef CXXFLAGS_DEBUG_XTRA
CXXFLAGS_LAST += ${CXXFLAGS_DEBUG_XTRA}
.endif

.-include <local.dpadd.mk>

# DPLIBS helps us ensure we keep DPADD and LDADD in sync
DPLIBS+= ${DPLIBS_LAST}
DPADD+= ${DPLIBS:N-*}
.for __lib in ${DPLIBS}
.if "${_lib:M-*}" != ""
LDADD += ${__lib}
.else
LDADD += ${LDADD_${__lib:T:R}:U${__lib:T:R:S/lib/-l/:C/\.so.*//}}
.endif
.endfor

# DPADD can contain things other than libs
__dpadd_libs := ${DPADD:M*/lib*}

.if defined(PROG) && ${MK_PROG_LDORDER_MK:Uno} != "no"
# some libs have dependencies...
# DPLIBS_* allows bsd.libnames.mk to flag libs which must be included
# in DPADD for a given library.
# Gather all such dependencies into __ldadd_all_xtras
# dups will be dealt with later.
# Note: libfoo_pic uses DPLIBS_libfoo
__ldadd_all_xtras=
.for __lib in ${__dpadd_libs:@d@${DPLIBS_${d:T:R:S,_pic,,}}@}
__ldadd_all_xtras+= ${LDADD_${__lib}:U${__lib:T:R:S/lib/-l/:C/\.so.*//}}
.if "${DPADD:M${__lib}}" == ""
DPADD+= ${__lib}
.endif
.endfor
.endif
# Last of all... for libc and libgcc
DPADD+= ${DPADD_LAST}

# de-dupuplicate __ldadd_all_xtras into __ldadd_xtras
# in reverse order so that libs end up listed after all that needed them.
__ldadd_xtras=
.for __lib in ${__ldadd_all_xtras:[-1..1]}
.if "${__ldadd_xtras:M${__lib}}" == "" || ${NEED_IMPLICIT_LDADD:tl:Uno} != "no"
__ldadd_xtras+= ${__lib}
.endif
.endfor

.if !empty(__ldadd_xtras)
# now back to the original order
__ldadd_xtras:= ${__ldadd_xtras:[-1..1]}
LDADD+= ${__ldadd_xtras}
.endif

# Convert DPADD into -I and -L options and add them to CPPFLAGS and LDADD
# For the -I's convert the path to a relative one.  For separate objdirs
# the DPADD paths will be to the obj tree so we need to subst anyway.

# update this
__dpadd_libs := ${DPADD:M*/lib*}

# Order -L's to search ours first.
# Avoids picking up old versions already installed.
__dpadd_libdirs := ${__dpadd_libs:R:H:S/^/-L/g:O:u:N-L}
LDADD += ${__dpadd_libdirs:M-L${OBJTOP}/*}
LDADD += ${__dpadd_libdirs:N-L${OBJTOP}/*:N-L${HOST_LIBDIR:U/usr/lib}}
.if defined(HOST_LIBDIR) && ${HOST_LIBDIR} != "/usr/lib"
LDADD+= -L${HOST_LIBDIR}
.endif

.if !make(dpadd)
.ifdef LIB
# Each lib is its own src_lib, we want to include it in SRC_LIBS
# so that the correct INCLUDES_* will be picked up automatically.
SRC_LIBS+= ${_OBJDIR}/lib${LIB}.a
.endif
.endif

# 
# This little bit of magic, assumes that SRC_libfoo will be
# set if it cannot be correctly derrived from ${LIBFOO}
# Note that SRC_libfoo and INCLUDES_libfoo should be named for the
# actual library name not the variable name that might refer to it.
# 99% of the time the two are the same, but the DPADD logic
# only has the library name available, so stick to that.
# 

SRC_LIBS?=
# magic_libs includes those we want to link with
# as well as those we might look at
__dpadd_magic_libs += ${__dpadd_libs} ${SRC_LIBS}
DPMAGIC_LIBS += ${__dpadd_magic_libs} \
	${__dpadd_magic_libs:@d@${DPMAGIC_LIBS_${d:T:R}}@}

# we skip this for staged libs
.for __lib in ${DPMAGIC_LIBS:O:u:N${STAGE_OBJTOP:Unot}*/lib/*}
# 
# if SRC_libfoo is not set, then we assume that the srcdir corresponding
# to where we found the library is correct.
#
SRC_${__lib:T:R} ?= ${__lib:H:S,${OBJTOP},${RELSRCTOP},}
#
# This is a no-brainer but just to be complete...
#
OBJ_${__lib:T:R} ?= ${__lib:H:S,${OBJTOP},${RELOBJTOP},}
#
# If INCLUDES_libfoo is not set, then we'll use ${SRC_libfoo}/h if it exists,
# else just ${SRC_libfoo}.
#
INCLUDES_${__lib:T:R}?= -I${exists(${SRC_${__lib:T:R}}/h):?${SRC_${__lib:T:R}}/h:${SRC_${__lib:T:R}}}

.endfor

# even for staged libs we sometimes 
# need to allow direct -I to avoid cicular dependencies 
.for __lib in ${DPMAGIC_LIBS:O:u:T:R}
.if !empty(SRC_${__lib}) && empty(INCLUDES_${__lib})
# must be a staged lib
.if exists(${SRC_${__lib}}/h)
INCLUDES_${__lib} = -I${SRC_${__lib}}/h
.else
INCLUDES_${__lib} = -I${SRC_${__lib}}
.endif
.endif
.endfor

# when linking a shared lib, avoid non pic libs
SHLDADD+= ${LDADD:N-[lL]*}
.for __lib in ${__dpadd_libs:u}
.if defined(SHLIB_NAME) && ${LDADD:M-l${__lib:T:R:S,lib,,}} != ""
.if ${__lib:T:N*_pic.a:N*.so} == "" || exists(${__lib:R}.so)
SHLDADD+= -l${__lib:T:R:S,lib,,}
.elif exists(${__lib:R}_pic.a)
SHLDADD+= -l${__lib:T:R:S,lib,,}_pic
.else
.warning ${RELDIR}.${TARGET_SPEC} needs ${__lib:T:R}_pic.a
SHLDADD+= -l${__lib:T:R:S,lib,,}
.endif
SHLDADD+= -L${__lib:H}
.endif
.endfor

# Now for the bits we actually need
__dpadd_incs=
.for __lib in ${__dpadd_libs:u}
.if (make(${PROG}_p) || defined(NEED_GPROF)) && exists(${__lib:R}_p.a)
__ldadd=-l${__lib:T:R:S,lib,,}
LDADD := ${LDADD:S,^${__ldadd}$,${__ldadd}_p,g}
.endif
.endfor

#
# We take care of duplicate suppression later.
# don't apply :T:R too early
__dpadd_incs += ${__dpadd_magic_libs:u:@x@${INCLUDES_${x:T:R}}@}
__dpadd_incs += ${__dpadd_magic_libs:O:u:@s@${SRC_LIBS_${s:T:R}:U}@:@x@${INCLUDES_${x:T:R}}@}

__dpadd_last_incs += ${__dpadd_magic_libs:u:@x@${INCLUDES_LAST_${x:T:R}}@}
__dpadd_last_incs += ${__dpadd_magic_libs:O:u:@s@${SRC_LIBS_${s:T:R}:U}@:@x@${INCLUDES_LAST_${x:T:R}}@}

.if defined(HOSTPROG) || ${MACHINE:Nhost*} == ""
# we want any -I/usr/* last
__dpadd_last_incs := \
	${__dpadd_last_incs:N-I/usr/*} \
	${__dpadd_incs:M-I/usr/*} \
	${__dpadd_last_incs:M-I/usr/*} 
__dpadd_incs := ${__dpadd_incs:N-I/usr/*}
.endif

#
# eliminate any duplicates - but don't mess with the order
# force evaluation now - to avoid giving make a headache
#
.for t in CFLAGS CXXFLAGS
# avoid duplicates
__$t_incs:=${$t:M-I*:O:u}
.for i in ${__dpadd_incs}
.if "${__$t_incs:M$i}" == ""
$t+= $i
__$t_incs+= $i
.endif
.endfor
.endfor

.for t in CFLAGS_LAST CXXFLAGS_LAST
# avoid duplicates
__$t_incs:=${$t:M-I*:u}
.for i in ${__dpadd_last_incs}
.if "${__$t_incs:M$i}" == ""
$t+= $i
__$t_incs+= $i
.endif
.endfor
.endfor

# This target is used to gather a list of
# dir: ${DPADD}
# entries
.if make(*dpadd*)
.if !target(dpadd)
dpadd:	.NOTMAIN
.if defined(DPADD) && ${DPADD} != ""
	@echo "${RELDIR}: ${DPADD:S,${OBJTOP}/,,}"
.endif
.endif
.endif

.ifdef SRC_PATHADD
# We don't want to assume that we need to .PATH every element of 
# SRC_LIBS, but the Makefile cannot do
# .PATH: ${SRC_libfoo}
# since the value of SRC_libfoo must be available at the time .PATH:
# is read - and we only just worked it out.  
# Further, they can't wait until after include of {lib,prog}.mk as 
# the .PATH is needed before then.
# So we let the Makefile do
# SRC_PATHADD+= ${SRC_libfoo}
# and we defer the .PATH: until now so that SRC_libfoo will be available.
.PATH: ${SRC_PATHADD}
.endif

# after all that, if doing -n we don't care
.if ${.MAKEFLAGS:Ux:M-n} != ""
DPADD =
.elif ${.MAKE.MODE:Mmeta*} != "" && exists(${.MAKE.DEPENDFILE})
DPADD_CLEAR_DPADD ?= yes
.if ${DPADD_CLEAR_DPADD} == "yes"
# save this
__dpadd_libs := ${__dpadd_libs}
# we have made what use of it we can of DPADD
DPADD =
.endif
.endif

.endif
