# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libuutil/common
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/avl

LIB=	uutil
SRCS=	avl.c \
	uu_alloc.c \
	uu_avl.c \
	uu_dprintf.c \
	uu_ident.c \
	uu_list.c \
	uu_misc.c \
	uu_open.c \
	uu_pname.c \
	uu_strtoint.c

WARNS?=	1
CFLAGS+= -DNATIVE_BUILD
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libuutil/common
CFLAGS+= -I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/head

.include <bsd.lib.mk>
