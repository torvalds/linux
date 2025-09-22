#	$OpenBSD: Makefile.inc,v 1.29 2020/05/17 17:04:28 deraadt Exp $

KERNELS += GENERIC.MP bsd.mp

bootblocks:
	cp -p ${DESTDIR}/usr/mdec/boot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/bootxx ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/netboot ${RELEASEDIR}
	cp -p ${DESTDIR}/usr/mdec/netboot.mop ${RELEASEDIR}

MDEXT=	bsd.rd cd${OSrev}.iso \
	miniroot${OSrev}.img \
	boot bootxx netboot netboot.mop
