#	$OpenBSD: Makefile,v 1.4 2009/04/17 03:58:54 deraadt Exp $

SUBDIR=ramdisk

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd ramdisk; ${MAKE} unconfig

.include <bsd.subdir.mk>
