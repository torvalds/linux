#	$OpenBSD: Makefile,v 1.13 2018/09/13 12:33:43 millert Exp $

WARNINGS=Yes
PROG=   pax
SRCS=	ar_io.c ar_subs.c buf_subs.c cpio.c file_subs.c ftree.c\
	gen_subs.c getoldopt.c options.c pat_rep.c pax.c sel_subs.c tables.c\
	tar.c tty_subs.c
MAN=	pax.1 tar.1 cpio.1
LINKS=	${BINDIR}/pax ${BINDIR}/tar ${BINDIR}/pax ${BINDIR}/cpio

.include <bsd.prog.mk>
