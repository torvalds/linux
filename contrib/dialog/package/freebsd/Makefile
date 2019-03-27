# New ports collection makefile for:	ncdialog
# Date created:		2003-01-11
# Whom:			AlanE <alane@freebsd.org>
#
# $Carpetsmoker: ports/cdialog/Makefile,v 1.4 2007/04/22 11:30:53 carpetsmoker Exp $
# $FreeBSD$
#

PORTNAME=	cdialog
PORTVERSION=	1.3.20180621
PORTEPOCH=	1
CATEGORIES=	devel
MASTER_SITES=	ftp://ftp.invisible-island.net/${PORTNAME:S|^c||}/
DISTNAME=	${PORTNAME:S|^c||}-${PORTVERSION:R}-${PORTVERSION:E}
EXTRACT_SUFX=	.tgz

MAINTAINER=	sylvio@FreeBSD.org
COMMENT=	An enhanced version of 'dialog' to work with ncurses

LICENSE=	LGPL21

USE_AUTOTOOLS=	libtool
GNU_CONFIGURE=	yes
USE_LDCONFIG=	yes
CONFIGURE_ARGS=	--enable-widec \
		--includedir=${PREFIX}/include/${PORTNAME} \
		--with-libtool \
		--with-ncursesw \
		--with-package=${PORTNAME}
MAKEFILE=	makefile

MAN1=		cdialog.1
MAN3=		cdialog.3
MANCOMPRESSED=	no

INSTALL_TARGET=	install-strip install-man install-lib

.if !defined(NOPORTEXAMPLES)
post-patch:
	@${FIND} ${WRKSRC}/samples/ -type f -exec ${REINPLACE_CMD} -i "" "s|dialog|cdialog|g" {} \;

post-install:
	@${MKDIR} ${EXAMPLESDIR}
	cd ${WRKSRC}/samples && ${FIND} . | \
		${CPIO} -pdm -L -R ${SHAREOWN}:${SHAREGRP} ${EXAMPLESDIR}
.endif

.include <bsd.port.mk>
