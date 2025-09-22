#	$OpenBSD: Makefile,v 1.4 2021/04/13 04:55:28 deraadt Exp $

SUBDIR= ramdisk

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
