#	$OpenBSD: Makefile.inc,v 1.16 2020/05/17 17:04:29 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/{bootblk,ofwboot,ofwboot.net} ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso \
	miniroot${OSrev}.img floppy${OSrev}.img floppyB${OSrev}.img \
	bootblk ofwboot ofwboot.net
