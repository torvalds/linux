# $Id: yacc.mk,v 1.6 2011/06/10 22:45:46 sjg Exp $

#
#	@(#) Copyright (c) 1999-2011, Simon J. Gerraty
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

# this file contains rules to DTRT when SRCS contains foo.y or foo.c
# when only a foo.y exists.

YACC?= yacc
YFLAGS?= -v -t
RM?= rm

YACC.y?= ${YACC} ${YFLAGS}

.if ${YACC.y:M-d} == "" || defined(NO_RENAME_Y_TAB_H)

.y.c:
	${YACC.y} ${.IMPSRC}
	[ ! -s y.tab.c ] || mv y.tab.c ${.TARGET}
	${RM} -f y.tab.[!h]

.else

# the touch of the .c is to ensure it is newer than .h (paranoia)
.y.h:
	${YACC.y} ${.IMPSRC}
	[ ! -s y.tab.c ] || mv y.tab.c ${.TARGET:T:R}.c
	[ ! -s y.tab.h ] || cmp -s y.tab.h ${.TARGET:T:R}.h \
		|| mv y.tab.h ${.TARGET:T:R}.h
	touch ${.TARGET:T:R}.c
	${RM} -f y.tab.*

# Normally the .y.h rule does the work - to avoid races.
# If for any reason the .c is lost but the .h remains,
# regenerate the .c
.y.c:	${.TARGET:T:R}.h
	[ -s ${.TARGET} ] || { \
		${YACC.y} ${.IMPSRC} && \
		{ [ ! -s y.tab.c ] || mv y.tab.c ${.TARGET}; \
		${RM} y.tab.*; }; }
.endif

beforedepend:	${SRCS:T:M*.y:S/.y/.c/g}

CLEANFILES+= ${SRCS:T:M*.y:S/.y/.[ch]/g}
CLEANFILES+= y.tab.[ch]
