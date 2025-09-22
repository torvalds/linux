#	$OpenBSD: Makefile,v 1.39 2018/06/18 17:03:58 millert Exp $

PROG=	ksh
DPADD+=	${LIBCURSES}
LDADD+=	-lcurses

SRCS=	alloc.c c_ksh.c c_sh.c c_test.c c_ulimit.c edit.c emacs.c eval.c \
	exec.c expr.c history.c io.c jobs.c lex.c mail.c main.c \
	misc.c path.c shf.c syn.c table.c trap.c tree.c tty.c var.c \
	version.c vi.c

WARNINGS=yes
DEFS=	-DEMACS -DVI
CFLAGS+=${DEFS} -I. -I${.CURDIR} -I${.CURDIR}/../../lib/libc/gen
MAN=	ksh.1 sh.1

LINKS=	${BINDIR}/ksh ${BINDIR}/rksh
LINKS+=	${BINDIR}/ksh ${BINDIR}/sh

.include <bsd.prog.mk>
