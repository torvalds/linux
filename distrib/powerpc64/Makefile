#	$OpenBSD: Makefile,v 1.2 2020/08/05 05:00:03 deraadt Exp $

SUBDIR= ramdisk

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
