#	$OpenBSD: Makefile,v 1.13 2019/12/21 21:40:00 espie Exp $

PROG=	more
CFLAGS+= -I. -I${.CURDIR}/
SRCS=	more.c tputs.c termcap.c tgoto.c

CLEANFILES+=morehelp.h

morehelp.h: ${.CURDIR}/more.help
	echo -n 'const char *more_help = "' > morehelp.h
	sed -e 's/[\\"]/\\&/g' -e 's/$$/\\n\\/' ${.CURDIR}/more.help >> morehelp.h
	echo '";' >> morehelp.h

more.o: morehelp.h

.include <bsd.prog.mk>
