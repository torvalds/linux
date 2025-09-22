#	$OpenBSD: Makefile.inc,v 1.26 2020/05/17 17:04:29 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/pxeboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/cdboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/cdbr ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso floppy${OSrev}.img miniroot${OSrev}.img \
	pxeboot cdboot cdbr
