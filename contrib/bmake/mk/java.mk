#
# RCSid:
#	$Id: java.mk,v 1.14 2007/11/22 08:16:25 sjg Exp $

#	@(#) Copyright (c) 1998-2001, Simon J. Gerraty
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

.include <init.mk>

CLASSPATH?=.

.if defined(PROG)
SRCS?=	${PROG:.class=.java}
.endif
.if !defined(SRCS) || empty(SRCS)
SRCS!=cd ${.CURDIR} && echo *.java
.endif
.SUFFIXES:	.class .java

CLEANFILES+= *.class

JAVAC?=   javac
JAVADOC?= javadoc

.if !target(docs)
docs:
	${JAVADOC} ${JAVADOC_FLAGS} ${SRCS}
.endif

.if defined(JAVADESTDIR) && !empty(JAVADESTDIR)
JAVASRCDIR?=${JAVADESTDIR:H}/src
__classdest:=${JAVADESTDIR}${.CURDIR:S,${JAVASRCDIR},,}/
CLASSPATH:=${CLASSPATH}:${JAVADESTDIR}
JAVAC_FLAGS+= -d ${JAVADESTDIR}
.else
__classdest=
.endif

JAVAC_FLAGS+= ${JAVAC_DBG}

.if defined(MAKE_VERSION) && !defined(NO_CLASSES_COOKIE)
# java works best by compiling a bunch of classes at once.
# this lot does that but needs a recent netbsd make or 
# or its portable cousin bmake.
.for __s in ${SRCS}
__c:= ${__classdest}${__s:.java=.class}
.if !target(${__c})
# We need to do something to force __c's parent to be made.
${__c}:	${__s}
	@rm -f ${.TARGET}
.endif
SRCS_${__c}=${__s}
__classes:= ${__classes} ${__c}
.endfor
__classes_cookie=${__classdest}.classes.done
CLEANFILES+= ${__classes} ${__classes_cookie}

${__classes_cookie}:	${__classes}
	CLASSPATH=${CLASSPATH} ${JAVAC} ${JAVAC_FLAGS} ${.OODATE:@c@${SRCS_$c}@}
	@touch ${.TARGET}

all:	${__classes_cookie}

.else
# this will work with other BSD make's
.for __s in ${SRCS}
__c:= ${__classdest}${__s:.java=.class}
${__c}:	${__s}
	CLASSPATH=${CLASSPATH} ${JAVAC} ${JAVAC_FLAGS} ${.OODATE}
.endfor

all:	${SRCS:%.java=${__classdest}%.class}

.endif

.if !target(cleanjava)
cleanjava:
	rm -f [Ee]rrs mklog core *.core ${PROG} ${CLEANFILES}

clean: cleanjava
cleandir: cleanjava
.endif

.endif
