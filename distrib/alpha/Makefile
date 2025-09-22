#	$OpenBSD: Makefile,v 1.14 2019/04/30 17:46:00 deraadt Exp $

SUBDIR= miniroot

.if	make(obj) || make(cleandir) || make(clean)
SUBDIR+= iso
.endif

unconfig:
	cd miniroot; ${MAKE} unconfig

.include <bsd.subdir.mk>
