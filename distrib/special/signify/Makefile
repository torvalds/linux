#	$OpenBSD: Makefile,v 1.4 2017/05/28 21:59:56 tedu Exp $

.PATH: ${.CURDIR}/../../../usr.bin/signify

SRCS=	signify.c
SRCS+=	zsig.c
SRCS+=	fe25519.c sc25519.c
SRCS+=	mod_ed25519.c mod_ge25519.c
SRCS+=	crypto_api.c

PROG=	signify

LDADD=	-lutil
DPADD=	${LIBUTIL}

COPTS+=	-Wall -DVERIFYONLY

.include <bsd.prog.mk>
