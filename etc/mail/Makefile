# $OpenBSD: Makefile,v 1.13 2015/12/22 07:39:00 sunil Exp $

all clean:

distribution:
	${INSTALL} -c -o root -g wheel -m 644 aliases \
	    ${DESTDIR}/etc/mail/aliases
	${INSTALL} -c -o root -g wheel -m 644 smtpd.conf \
	    ${DESTDIR}/etc/mail/smtpd.conf
	${INSTALL} -c -o root -g wheel -m 644 spamd.conf \
	    ${DESTDIR}/etc/mail/spamd.conf

.PHONY: distribution
.include <bsd.own.mk>
.include <bsd.sys.mk>
