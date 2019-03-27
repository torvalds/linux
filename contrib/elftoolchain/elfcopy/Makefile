# $Id: Makefile 3608 2018-04-14 21:23:04Z jkoshy $

TOP=	..

.include "${TOP}/mk/elftoolchain.components.mk"

PROG=	elfcopy

SRCS=	archive.c ascii.c binary.c main.c sections.c segments.c \
	symbols.c

WARNS?=	5

DPADD=	${LIBELF} ${LIBELFTC}
LDADD=	-lelf -lelftc

.if !defined(LIBELF_AR)
LDADD+= -larchive
.endif

.if defined(WITH_PE) && ${WITH_PE} == "yes"
SRCS+=	pe.c
CFLAGS+= -DWITH_PE=1

DPADD+=	${LIBPE}
LDADD+=	-lpe
.endif

MAN=	elfcopy.1 mcs.1 strip.1
MLINKS=	elfcopy.1 objcopy.1

NO_SHARED?=	yes

LINKS=	${BINDIR}/elfcopy ${BINDIR}/mcs		\
	${BINDIR}/elfcopy ${BINDIR}/objcopy 	\
	${BINDIR}/elfcopy ${BINDIR}/strip

EXTRA_TARGETS=	mcs strip objcopy

CLEANFILES+=	${EXTRA_TARGETS}

# Create in-place symbolic links to "elfcopy" at build time.

all:	${EXTRA_TARGETS}

${EXTRA_TARGETS}:	${PROG}
	ln -s ${PROG} ${.TARGET}

.include "${TOP}/mk/elftoolchain.prog.mk"

.if ${OS_HOST} == "OpenBSD"
CFLAGS+=	-I/usr/local/include
LDFLAGS+=	-L/usr/local/lib
.elif ${OS_HOST} == "DragonFly"
LDADD+=		-lbz2
.endif
