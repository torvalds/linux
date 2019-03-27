#	@(#)Makefile	8.1 (Berkeley) 5/31/93
# $FreeBSD$

PACKAGE=sendmail
SENDMAIL_DIR=${SRCTOP}/contrib/sendmail
.PATH:	${SENDMAIL_DIR}/rmail

# Not much point this being static. It calls a shared sendmail...
NO_SHARED?= NO

PROG=	rmail
SRCS=	rmail.c
MAN=	rmail.8

WARNS?=	2
CFLAGS+=-I${SENDMAIL_DIR}/include -I.

LIBADD=	sm

SRCS+=	sm_os.h
CLEANFILES+=sm_os.h

# User customizations to the sendmail build environment
CFLAGS+=${SENDMAIL_CFLAGS}
DPADD+=${SENDMAIL_DPADD}
LDADD+=${SENDMAIL_LDADD}
LDFLAGS+=${SENDMAIL_LDFLAGS}

# If you want to have your rmail queuing the mail only, uncomment the
# following:
# CFLAGS+= -DQUEUE_ONLY

sm_os.h: ${SENDMAIL_DIR}/include/sm/os/sm_os_freebsd.h .NOMETA
	ln -sf ${.ALLSRC} ${.TARGET}

.include <bsd.prog.mk>
