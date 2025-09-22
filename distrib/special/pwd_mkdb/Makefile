#	$OpenBSD: Makefile,v 1.1 2015/04/13 21:27:07 deraadt Exp $

# static because it's used by the boot floppy through a chroot.

PROG=	pwd_mkdb
LDADD+=	-lutil
DPADD+=	${LIBUTIL}

.PATH:  ${.CURDIR}/../../../usr.sbin/pwd_mkdb
.include <bsd.prog.mk>
