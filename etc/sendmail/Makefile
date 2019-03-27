#	@(#)Makefile	8.19 (Berkeley) 1/14/97
# $FreeBSD$

M4=		m4
CHMOD=		chmod
ROMODE=		444
RM=		rm -f

SENDMAIL_DIR=	${SRCTOP}/contrib/sendmail
SMDIR=		${SENDMAIL_DIR}/src
SENDMAIL_CF_DIR?=${SENDMAIL_DIR}/cf

# this is overkill, but....
M4FILES!=	find ${SENDMAIL_CF_DIR} -type f -name '*.m4' -print

.SUFFIXES: .mc .cf

.mc.cf: ${M4FILES}
	${RM} ${.TARGET}
	${M4} -D_CF_DIR_=${SENDMAIL_CF_DIR}/ -D_NO_MAKEINFO_ \
	    ${SENDMAIL_M4_FLAGS} \
	    ${SENDMAIL_CF_DIR}/m4/cf.m4 ${.IMPSRC} > ${.TARGET}
	${CHMOD} ${ROMODE} ${.TARGET}

DEST_CF=	${DESTDIR}/etc/mail/sendmail.cf
DEST_SUBMIT_CF=	${DESTDIR}/etc/mail/submit.cf

ALL=		freebsd.cf freebsd.submit.cf
CLEANFILES=	freebsd.cf freebsd.submit.cf

# Local SENDMAIL_MC or SENDMAIL_CF may be set in /etc/make.conf.
# Warning!  If set, this causes 'make install' to always copy it
# over /etc/mail/sendmail.cf!!!
# Caveat emptor!  Be sure you want this before you enable it.
.if defined(SENDMAIL_MC) && defined(SENDMAIL_CF)
.error Both SENDMAIL_MC and SENDMAIL_CF cannot be set.
.elif defined(SENDMAIL_MC)
INSTALL_CF=	${SENDMAIL_MC:T:R}.cf
ALL+=		${INSTALL_CF}
CLEANFILES+=	${SENDMAIL_MC:T:R}.cf
${INSTALL_CF}: ${SENDMAIL_MC}
.elif defined(SENDMAIL_CF)
ALL+=		${SENDMAIL_CF}
INSTALL_CF=	${SENDMAIL_CF}
.endif

.if !defined(SENDMAIL_SET_USER_ID) && defined(SENDMAIL_SUBMIT_MC)
INSTALL_SUBMIT_CF= ${SENDMAIL_SUBMIT_MC:T:R}.cf
ALL+=		${INSTALL_SUBMIT_CF}
CLEANFILES+=	${INSTALL_SUBMIT_CF}
${INSTALL_SUBMIT_CF}: ${SENDMAIL_SUBMIT_MC}
.endif

# Additional .cf files to build.
.if defined(SENDMAIL_ADDITIONAL_MC)
SENDMAIL_ADDITIONAL_CF= ${SENDMAIL_ADDITIONAL_MC:T:S/.mc$/.cf/}
ALL+=		${SENDMAIL_ADDITIONAL_CF}
CLEANFILES+=	${SENDMAIL_ADDITIONAL_CF}
.for mc in ${SENDMAIL_ADDITIONAL_MC}
${mc:T:R}.cf: ${mc}
.endfor
.endif

all: ${ALL}

distribution:
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${.CURDIR}/freebsd.mc freebsd.cf ${DESTDIR}/etc/mail
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${.CURDIR}/freebsd.submit.mc freebsd.submit.cf ${DESTDIR}/etc/mail
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${SMDIR}/helpfile ${DESTDIR}/etc/mail
.if defined(INSTALL_CF)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${INSTALL_CF} ${DEST_CF}
.else
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    freebsd.cf ${DEST_CF}
.endif
.if defined(SENDMAIL_ADDITIONAL_CF)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 644 \
	    ${SENDMAIL_ADDITIONAL_CF} ${DESTDIR}/etc/mail
.endif
.if !defined(SENDMAIL_SET_USER_ID)
.if defined(INSTALL_SUBMIT_CF)
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    ${INSTALL_SUBMIT_CF} ${DEST_SUBMIT_CF}
.else
	${INSTALL} -o ${BINOWN} -g ${BINGRP} -m 444 \
	    freebsd.submit.cf ${DEST_SUBMIT_CF}
.endif
.endif

.include <bsd.prog.mk>
