# $Id: srctop.mk,v 1.3 2012/11/11 23:20:18 sjg Exp $
#
#	@(#) Copyright (c) 2012, Simon J. Gerraty
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

.if !defined(SRCTOP)
# if using mk(1) SB will be set.
.ifdef SB
.if ${.CURDIR:S,${SB},,} != ${.CURDIR}
# we are actually within SB
.ifdef SB_SRC
SRCTOP:= ${SB_SRC}
.elif exists(${SB}/src)
SRCTOP:= ${SB}/src
.else
SRCTOP:= ${SB}
.endif
.endif
.endif

.if !defined(SRCTOP)
.for rd in share/mk build/mk mk
.if ${_this_mk_dir:M*${rd}} != ""
.if ${.CURDIR:S,${_this_mk_dir:${rd:C,[^/]+,H,g:S,/, ,g:ts:}},,} != ${.CURDIR}
SRCTOP:= ${_this_mk_dir:${rd:C,[^/]+,H,g:S,/, ,g:ts:}}
.endif
.endif
.endfor
.endif

.if !defined(SRCTOP)
_SRCTOP_TEST_?= [ -f ../.sandbox-env -o -d share/mk ]
# Linux at least has a bug where attempting to check an automounter
# directory will hang.  So avoid looking above /a/b
SRCTOP!= cd ${.CURDIR}; while :; do \
		here=`pwd`; \
		${_SRCTOP_TEST_} && { echo $$here; break; }; \
		case $$here in /*/*/*) cd ..;; *) echo ""; break;; esac; \
		done 
.endif
.if defined(SRCTOP) && exists(${SRCTOP}/.)
.export SRCTOP
.endif
.endif

.if !defined(OBJTOP) && !empty(SRCTOP)
.if defined(MAKEOBJDIRPREFIX) && exists(${MAKEOBJDIRPREFIX}${SRCTOP})
OBJTOP= ${MAKEOBJDIRPREFIX}${SRCTOP}
.elif (exists(${SRCTOP}/Makefile) || exists(${SRCTOP}/makefile))
OBJTOP!= cd ${SRCTOP} && ${PRINTOBJDIR}
.endif
.if empty(OBJTOP)
OBJTOP= ${SRCTOP}
.endif
.export OBJTOP
.endif
