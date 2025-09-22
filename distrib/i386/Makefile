#	$OpenBSD: Makefile,v 1.13 2019/04/30 21:11:17 deraadt Exp $

SUBDIR= ramdisk_cd ramdisk

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk_cd; ${MAKE} unconfig
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
