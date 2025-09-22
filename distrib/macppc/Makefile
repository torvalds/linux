#	$OpenBSD: Makefile,v 1.4 2014/07/20 05:06:42 deraadt Exp $

SUBDIR= ramdisk

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
