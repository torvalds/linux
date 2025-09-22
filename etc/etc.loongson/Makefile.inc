#	$OpenBSD: Makefile.inc,v 1.8 2023/11/05 16:27:18 miod Exp $

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/boot ${RELEASEDIR}

MDEXT=	bsd.rd boot cd${OSrev}.iso miniroot${OSrev}.img
