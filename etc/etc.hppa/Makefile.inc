#	$OpenBSD: Makefile.inc,v 1.14 2020/05/17 17:04:29 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/xxboot ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso lif${OSrev}.img xxboot
