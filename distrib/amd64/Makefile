#	$OpenBSD: Makefile,v 1.5 2019/04/30 20:38:30 deraadt Exp $

SUBDIR= ramdisk_cd ramdiskA

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk_cd; ${MAKE} unconfig
	cd ramdiskA; ${MAKE} unconfig

.include <bsd.subdir.mk>
