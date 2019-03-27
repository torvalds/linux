# $FreeBSD$

.include <src.opts.mk>

SUBDIR=	${_drti} \
	libavl \
	libctf \
	${_libdtrace} \
	libnvpair \
	libumem \
	libuutil \
	${_libzfs_core} \
	${_libzfs} \
	${_libzpool} \

SUBDIR.${MK_TESTS}+= tests

.if ${MK_ZFS} != "no"
_libzfs_core=	libzfs_core
_libzfs=	libzfs
.if ${MK_LIBTHR} != "no"
_libzpool=	libzpool
.endif
.endif

.if ${MACHINE_CPUARCH} != "sparc64"
_drti=		drti
_libdtrace=	libdtrace
.endif

SUBDIR_DEPEND_libdtrace=	libctf
SUBDIR_DEPEND_libzfs_core=	libnvpair
SUBDIR_DEPEND_libzfs=	libavl libnvpair libumem libuutil libzfs_core
SUBDIR_DEPEND_libzpool=	libavl libnvpair libumem

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
