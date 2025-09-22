#	$OpenBSD: Makefile,v 1.55 2021/05/01 16:10:33 visa Exp $

SUBDIR=	special notes

.if	make(obj)
SUBDIR+=alpha amd64 armv7 arm64 hppa i386 \
	landisk loongson luna88k macppc \
	octeon powerpc64 riscv64 sparc64
.elif exists(${MACHINE})
SUBDIR+=${MACHINE}
.endif

.include <bsd.subdir.mk>
