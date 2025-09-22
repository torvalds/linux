#	$OpenBSD: Makefile.inc,v 1.18 2024/04/09 11:13:51 kettenis Exp $

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/BOOTARM.EFI ${RELEASEDIR}

MDEXT=	bsd.rd BOOTARM.EFI \
	miniroot-am335x-${OSrev}.img miniroot-cubox-${OSrev}.img \
	miniroot-nitrogen-${OSrev}.img miniroot-panda-${OSrev}.img \
	miniroot-wandboard-${OSrev}.img
