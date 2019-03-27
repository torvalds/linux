# $Id: Makefile.inc,v 1.11 2002/04/16 02:57:46 bp Exp $

.ifmake !clean && !cleandepend
.if !defined(PREFIX)
.include "config.int"
.endif
.endif

BINDIR?=${PREFIX}/bin
SMBSBINDIR=${PREFIX}/sbin
MANDIR=${PREFIX}/man/man

CFLAGS	+= -Wall -I../kernel/mysys

.include <bsd.libnames.mk>

.if !defined(LIBSMB) || defined(COMPLETEBUILD)
CFLAGS	+= -L../lib/smb -L../lib/nb
CFLAGS	+= -I../include
LIBSMB	= ../lib/smb/libsmb.a
.endif

.if !exists(${SYSDIR}/netsmb/smb.h)
CFLAGS	+= -I../kernel
.else
CFLAGS	+= -I${SYSDIR}
.endif

CFLAGS += -I../include

.if defined(SMBGDB)
CFLAGS+= -ggdb
.endif

deinstall:
	@(if test -f ${BINDIR}/${PROG}; then \
		echo removing ${BINDIR}/${PROG}; \
		rm -f ${BINDIR}/${PROG}; \
	fi;)
	@(if test -f ${MANDIR}1/${MAN1}.gz ; then \
		echo removing ${MANDIR}1/${MAN1}.gz; \
		rm -f ${MANDIR}1/${MAN1}.gz; \
	fi;)
	@(if test -f ${MANDIR}8/${MAN8}.gz ; then \
		echo removing ${MANDIR}8/${MAN8}.gz; \
		rm -f ${MANDIR}8/${MAN8}.gz; \
	fi;)
