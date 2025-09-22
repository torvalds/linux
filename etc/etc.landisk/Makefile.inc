#	$OpenBSD: Makefile.inc,v 1.10 2020/05/17 17:04:29 deraadt Exp $

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/mbr ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/xxboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/boot ${RELEASEDIR}

MDEXT=	mbr bsd.rd xxboot boot miniroot${OSrev}.img
