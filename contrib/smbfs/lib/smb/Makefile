# $Id: Makefile,v 1.12 2001/04/16 14:27:35 bp Exp $

LIB=		smb

NOPROFILE=	yes

#SHLIB_MAJOR=	1
#SHLIB_MINOR=	0

NOMAN=

SRCS=	rcfile.c ctx.c cfopt.c subr.c nls.c rap.c mbuf.c rq.c file.c \
	print.c \
	kiconv.c \
	nb.c nb_name.c nb_net.c nbns_rq.c

.ifmake !clean && !cleandepend
.if !defined(PREFIX)
.include "../../config.int"
.endif
.endif

DESTDIR=${PREFIX}/
LIBDIR=lib

.if !defined(USE_SHAREDLIBS)
NOPIC=		yes
.endif

install-src: install-lib

deinstall-src: deinstall-lib

.include <bsd.lib.mk>

install-lib: afterinstall _SUBDIR
.if !defined(NOMAN)
afterinstall: realinstall maninstall
.else
afterinstall: realinstall
.endif

deinstall-lib:
	rm -f ${DESTDIR}/${LIBDIR}/lib${LIB}.a 
.if defined(SHLIB_NAME)
	rm -f ${DESTDIR}/${SHLIBDIR}/${SHLIB_NAME}
.endif
.if defined(SHLIB_LINK)
	rm -f ${DESTDIR}/${SHLIBDIR}/${SHLIB_LINK}
.endif
