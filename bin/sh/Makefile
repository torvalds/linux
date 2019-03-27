#	@(#)Makefile	8.4 (Berkeley) 5/5/95
# $FreeBSD$

.include <src.opts.mk>

CONFGROUPS=	ETC ROOT
ETC=	profile
ROOT=	dot.shrc dot.profile
ROOTDIR=	/root
ROOTNAME_dot.shrc=	.shrc
ROOTNAME_dot.profile=	.profile
PACKAGE=runtime
PROG=	sh
INSTALLFLAGS= -S
SHSRCS=	alias.c arith_yacc.c arith_yylex.c cd.c echo.c error.c eval.c \
	exec.c expand.c \
	histedit.c input.c jobs.c kill.c mail.c main.c memalloc.c miscbltin.c \
	mystring.c options.c output.c parser.c printf.c redir.c show.c \
	test.c trap.c var.c
GENSRCS= builtins.c nodes.c syntax.c
GENHDRS= builtins.h nodes.h syntax.h token.h
SRCS= ${SHSRCS} ${GENSRCS} ${GENHDRS}

# MLINKS for Shell built in commands for which there are no userland
# utilities of the same name are handled with the associated manpage,
# builtin.1 in share/man/man1/.

LIBADD=	edit

CFLAGS+=-DSHELL -I. -I${.CURDIR}
# for debug:
# DEBUG_FLAGS+= -g -DDEBUG=2 -fno-inline
WARNS?=	2
WFORMAT=0

.PATH:	${.CURDIR}/bltin \
	${.CURDIR:H}/kill \
	${.CURDIR:H}/test \
	${SRCTOP}/usr.bin/printf

CLEANFILES+= mknodes mksyntax
CLEANFILES+= ${GENSRCS} ${GENHDRS}

build-tools: mknodes mksyntax

.ORDER: builtins.c builtins.h
builtins.h: .NOMETA
builtins.c builtins.h: mkbuiltins builtins.def
	sh ${.CURDIR}/mkbuiltins ${.CURDIR}

mknodes mksyntax: ${BUILD_TOOLS_META}

.ORDER: nodes.c nodes.h
nodes.h: .NOMETA
nodes.c nodes.h: mknodes nodetypes nodes.c.pat
	${BTOOLSPATH:U.}/mknodes ${.CURDIR}/nodetypes ${.CURDIR}/nodes.c.pat

.ORDER: syntax.c syntax.h
syntax.h: .NOMETA
syntax.c syntax.h: mksyntax
	${BTOOLSPATH:U.}/mksyntax

token.h: mktokens
	sh ${.CURDIR}/mktokens

HAS_TESTS=
SUBDIR.${MK_TESTS}+= tests

beforeinstallconfig:
	rm -f ${DESTDIR}/.profile

afterinstallconfig:
	${INSTALL_LINK} ${TAG_ARGS} ${DESTDIR}/root/.profile ${DESTDIR}/.profile

.include <bsd.prog.mk>
