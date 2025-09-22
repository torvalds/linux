#	$OpenBSD: Makefile.inc,v 1.18 2020/01/26 04:26:46 tedu Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/ofwboot ${RELEASEDIR}/ofwboot
	cp -p ${DESTDIR}/usr/mdec/bsd.tbxi ${RELEASEDIR}/bsd.tbxi

MDEXT=	bsd.rd cd${OSrev}.iso ofwboot bsd.tbxi
