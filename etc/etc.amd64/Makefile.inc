#	$OpenBSD: Makefile.inc,v 1.19 2020/05/17 17:04:28 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/pxeboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/cdboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/cdbr ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/BOOTX64.EFI ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/BOOTIA32.EFI ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso floppy${OSrev}.img miniroot${OSrev}.img \
	pxeboot cdboot cdbr BOOTX64.EFI BOOTIA32.EFI
