#	$OpenBSD: Makefile.inc,v 1.6 2024/10/29 21:03:09 volker Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/BOOTAA64.EFI ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso BOOTAA64.EFI miniroot${OSrev}.img
