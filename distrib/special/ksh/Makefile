#	$OpenBSD: Makefile,v 1.8 2023/08/14 08:56:20 kn Exp $

PROG=	ksh
SRCS=	alloc.c c_ksh.c c_sh.c c_test.c c_ulimit.c edit.c emacs.c eval.c \
	exec.c expr.c history.c io.c jobs.c lex.c main.c \
	misc.c path.c shf.c syn.c table.c trap.c tree.c tty.c var.c \
	vi.c

DEFS=	-Wall -DEMACS -DSMALL
CFLAGS+=${DEFS} -I. -I${.CURDIR}/../../../bin/ksh -I${.CURDIR}/../../../lib/libc/gen

LINKS=	${BINDIR}/ksh ${BINDIR}/rksh
LINKS+=	${BINDIR}/ksh ${BINDIR}/sh

.PATH:  ${.CURDIR}/../../../bin/ksh
.include <bsd.prog.mk>
