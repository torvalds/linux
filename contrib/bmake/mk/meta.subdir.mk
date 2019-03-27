# $Id: meta.subdir.mk,v 1.11 2015/11/24 22:26:51 sjg Exp $

#
#	@(#) Copyright (c) 2010, Simon J. Gerraty
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

.if !defined(NO_SUBDIR) && !empty(SUBDIR)
.if make(destroy*) || make(clean*)
.MAKE.MODE = compat
.if !commands(destroy)
.-include <bsd.obj.mk>
.endif
.elif ${.MAKE.LEVEL} == 0

.MAIN: all

.if !exists(${.CURDIR}/${.MAKE.DEPENDFILE:T}) || make(gendirdeps)
# start with this
DIRDEPS = ${SUBDIR:N.WAIT:O:u:@d@${RELDIR}/$d@}

.if make(gendirdeps)
.include <meta.autodep.mk>
.else
# this is the cunning bit
# actually it is probably a bit risky 
# since we may pickup subdirs which are not relevant
# the alternative is a walk through the tree though
# which is difficult without a sub-make.

.if defined(BOOTSTRAP_DEPENDFILES)
_find_name = ${.MAKE.MAKEFILE_PREFERENCE:@m@-o -name $m@:S,^-o,,1}
DIRDEPS = ${_subdeps:H:O:u:@d@${RELDIR}/$d@}
.elif ${.MAKE.DEPENDFILE:E} == ${MACHINE} && defined(ALL_MACHINES)
# we want to find Makefile.depend.* ie for all machines
# and turn the dirs into dir.<machine>
_find_name = -name '${.MAKE.DEPENDFILE:T:R}*'
DIRDEPS = ${_subdeps:O:u:${NIgnoreFiles}:@d@${RELDIR}/${d:H}.${d:E}@:S,.${MACHINE}$,,:S,.depend$,,}
.else
# much simpler
_find_name = -name ${.MAKE.DEPENDFILE:T}
.if ${.MAKE.DEPENDFILE:E} == ${MACHINE}
_find_name += -o -name ${.MAKE.DEPENDFILE:T:R}
.endif
DIRDEPS = ${_subdeps:H:O:u:@d@${RELDIR}/$d@}
.endif

_subdeps != cd ${.CURDIR} && \
	find ${SUBDIR:N.WAIT} -type f \( ${_find_name} \) -print -o \
	-name .svn -prune 2> /dev/null; echo

.if empty(_subdeps)
DIRDEPS =
.else
# clean up if needed
DIRDEPS := ${DIRDEPS:S,^./,,:S,/./,/,g:${SUBDIRDEPS_FILTER:Uu}}
.endif
# we just dealt with it, if we leave it defined,
# dirdeps.mk will compute some interesting combinations.
.undef ALL_MACHINES

DEP_RELDIR = ${RELDIR}
.include <dirdeps.mk>
.endif
.endif
.else
all: .PHONY
.endif

.endif
