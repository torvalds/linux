# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/nvpair

LIB=	nvpair

INCS=	libnvpair.h
SRCS=	libnvpair.c \
	nvpair_alloc_system.c \
	nvpair_json.c \
	opensolaris_fnvpair.c \
	opensolaris_nvpair.c \
	opensolaris_nvpair_alloc_fixed.c

WARNS?=	1
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+= -I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+= -I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+= -I${SRCTOP}/sys
CFLAGS+= -I${SRCTOP}/cddl/contrib/opensolaris/head
CFLAGS+= -I${SRCTOP}/cddl/compat/opensolaris/lib/libumem

# This library uses macros to define fprintf behavior for several object types
# The compiler will see the non-string literal arguments to the fprintf calls and
# omit warnings for them. Quiesce these warnings in contrib code:
#
# cddl/contrib/opensolaris/lib/libnvpair/libnvpair.c:743:12: warning: format
#   string is not a string literal (potentially insecure) [-Wformat-security]
#    ARENDER(pctl, nvlist_array, nvl, name, val, nelem);
#
CFLAGS+= -Wno-format-security
.include <bsd.lib.mk>
