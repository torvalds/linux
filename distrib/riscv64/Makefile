#	$OpenBSD: Makefile,v 1.2 2021/05/11 15:18:48 deraadt Exp $

SUBDIR= ramdisk

.if     make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
