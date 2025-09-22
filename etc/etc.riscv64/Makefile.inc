#	$OpenBSD: Makefile.inc,v 1.2 2021/06/29 22:03:30 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/BOOTRISCV64.EFI ${RELEASEDIR}

MDEXT=	bsd.rd BOOTRISCV64.EFI miniroot${OSrev}.img
