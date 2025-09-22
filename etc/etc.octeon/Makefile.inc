#	$OpenBSD: Makefile.inc,v 1.7 2020/05/17 17:04:29 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/boot ${RELEASEDIR}

MDEXT=	boot bsd.rd miniroot${OSrev}.img
