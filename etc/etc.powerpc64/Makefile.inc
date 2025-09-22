#	$OpenBSD: Makefile.inc,v 1.4 2020/07/22 14:06:08 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/boot ${RELEASEDIR}

MDEXT=	boot bsd.rd miniroot${OSrev}.img
