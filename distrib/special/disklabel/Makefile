#	$OpenBSD: Makefile,v 1.14 2023/08/14 08:56:20 kn Exp $

PROG=	disklabel
SRCS=	disklabel.c dkcksum.c editor.c manual.c
DPADD=	${LIBUTIL}
LDADD=	-lutil

CLEANFILES += disklabel.cat8 manual.c

.include <bsd.own.mk>

.ifdef NOMAN
manual.c:
	(echo 'const unsigned char manpage[] = {'; \
	echo 'no manual' | gzip -9c | hexdump -ve '"0x" 1/1 "%02x,"'; \
	echo '};'; echo 'const int manpage_sz = sizeof(manpage);') > manual.c
.else
disklabel.cat8:	disklabel.8
	mandoc -Tascii ${.ALLSRC} > ${.TARGET}

manual.c:	disklabel.cat8
	(echo 'const unsigned char manpage[] = {'; \
	cat disklabel.cat8 | gzip -9c | hexdump -ve '"0x" 1/1 "%02x,"'; \
	echo '};'; echo 'const int manpage_sz = sizeof(manpage);') > manual.c
.endif

.if ${MACHINE} == "sparc64"
CFLAGS+= -DSUN_CYLCHECK -DSUN_AAT0
.endif

.ifdef NOPIC
CFLAGS+= -DSTATICLINKING
.endif

.PATH:	${.CURDIR}/../../../sbin/disklabel
.include <bsd.prog.mk>
