#
# RCSid:
#	$Id: auto.dep.mk,v 1.5 2016/04/05 15:58:37 sjg Exp $
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

# This module provides automagic dependency generation along the
# lines suggested in the GNU make.info

# set MKDEP_MK=auto.dep.mk and dep.mk will include us

# This version differs from autodep.mk, in that 
# we use ${.TARGET:T}.d rather than ${.TARGET:T:R}.d
# this makes it simpler to get the args to -MF and -MT right
# and ensure we can simply include all the .d files.
# 
# However suffix rules do not work with something like .o.d so we
# don't even try to handle 'make depend' gracefully.
# dep.mk will handle that itself.
#
.if !target(__${.PARSEFILE}__)
__${.PARSEFILE}__:

# this what bmake > 20100401 will look for
.MAKE.DEPENDFILE ?= .depend

# set this to -MMD to ignore /usr/include
# actually it ignores <> so may not be a great idea
CFLAGS_MD ?= -MD 
# -MF etc not available on all gcc versions.
CFLAGS_MF ?= -MF ${.TARGET:T}.d -MT ${.TARGET:T}
CFLAGS += ${CFLAGS_MD} ${CFLAGS_MF}
CXXFLAGS += ${CFLAGS_MD} ${CFLAGS_MF}

CLEANFILES += .depend ${.MAKE.DEPENDFILE} *.d

.if ${MAKE_VERSION} < 20160218
# skip generating dependfile for misc targets
.if ${.TARGETS:Uall:M*all} != ""
.END:	${.MAKE.DEPENDFILE}
.endif

# doing 'make depend' isn't a big win with this model
.if !target(depend)
depend: ${.MAKE.DEPENDFILE}
.endif

# this is trivial
${.MAKE.DEPENDFILE}: ${OBJS} ${POBJS} ${SOBJS}
	-@for f in ${.ALLSRC:M*o:T:O:u:%=%.d}; do \
		echo ".-include \"$$f\""; \
	done > $@
.else
# we have .dinclude
.if empty(_SKIP_BUILD)
_all_objs = ${OBJS} ${POBJS} ${SOBJS}
.for d in ${_all_objs:M*o:T:O:u:%=%.d}
.dinclude <$d>
.endfor
.endif

.endif
.endif
