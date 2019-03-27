# $FreeBSD$

.include "${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/Makefile.files"

# ZFS_COMMON_SRCS
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
# LUA_SRCS
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lua
# ZFS_SHARED_SRCS
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
# KERNEL_SRCS
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
# LIST_SRCS
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/os
# ATOMIC_SRCS
.if exists(${SRCTOP}/sys/cddl/contrib/opensolaris/common/atomic/${MACHINE_ARCH}/opensolaris_atomic.S)
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/atomic/${MACHINE_ARCH}
ATOMIC_SRCS=	opensolaris_atomic.S
.if ${MACHINE_ARCH} != "sparc64"
ACFLAGS+=	-Wa,--noexecstack
.endif
.else
.PATH: ${SRCTOP}/sys/cddl/compat/opensolaris/kern
ATOMIC_SRCS=	opensolaris_atomic.c
.endif
# UNICODE_SRCS
.PATH: ${SRCTOP}/sys/cddl/contrib/opensolaris/common/unicode
# LIBCMDUTILS_SRCS
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils/common

LIB=		zpool

ZFS_COMMON_SRCS= ${ZFS_COMMON_OBJS:C/.o$/.c/} trim_map.c
ZFS_SHARED_SRCS= ${ZFS_SHARED_OBJS:C/.o$/.c/}
LUA_SRCS=	${LUA_OBJS:C/.o$/.c/}
KERNEL_SRCS=	kernel.c taskq.c util.c
LIST_SRCS=	list.c
UNICODE_SRCS=	u8_textprep.c
LIBCMDUTILS_SRCS=nicenum.c

SRCS=		${ZFS_COMMON_SRCS} ${ZFS_SHARED_SRCS} ${LUA_SRCS} \
		${KERNEL_SRCS} ${LIST_SRCS} ${ATOMIC_SRCS} \
		${UNICODE_SRCS} ${LIBCMDUTILS_SRCS}

WARNS?=		0
CFLAGS+=	-I${SRCTOP}/sys/cddl/compat/opensolaris
CFLAGS+=	-I${SRCTOP}/cddl/compat/opensolaris/include
CFLAGS+=	-I${SRCTOP}/cddl/compat/opensolaris/lib/libumem
CFLAGS+=	-I${SRCTOP}/cddl/contrib/opensolaris/lib/libzpool/common
CFLAGS+=	-I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs
CFLAGS+=	-I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common/fs/zfs/lua
CFLAGS+=	-I${SRCTOP}/sys/cddl/contrib/opensolaris/common/zfs
CFLAGS+=	-I${SRCTOP}/sys/cddl/contrib/opensolaris/uts/common
CFLAGS+=	-I${SRCTOP}/cddl/contrib/opensolaris/head
CFLAGS+=	-I${SRCTOP}/cddl/contrib/opensolaris/lib/libnvpair
CFLAGS+=	-I${SRCTOP}/cddl/contrib/opensolaris/lib/libcmdutils
# XXX: pthread doesn't have mutex_owned() equivalent, so we need to look
#      into libthr private structures. That's sooo evil, but it's only for
#      ZFS debugging tools needs.
CFLAGS+=	-DWANTS_MUTEX_OWNED
CFLAGS+=	-I${SRCTOP}/lib/libpthread/thread
CFLAGS+=	-I${SRCTOP}/lib/libpthread/sys
CFLAGS+=	-I${SRCTOP}/lib/libthr/arch/${MACHINE_CPUARCH}/include
CFLAGS.gcc+=	-fms-extensions

LIBADD=		md pthread z nvpair avl umem

# atomic.S doesn't like profiling.
MK_PROFILE=	no

CSTD=	c99

# Since there are many asserts in this library, it makes no sense to compile
# it without debugging.

CFLAGS+=	-g -DDEBUG=1

.include <bsd.lib.mk>
