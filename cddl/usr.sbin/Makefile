# $FreeBSD$

.include <src.opts.mk>

SUBDIR=	${_dtrace} \
	${_dwatch} \
	${_lockstat} \
	${_plockstat} \
	${_zdb} \
	${_zfsd} \
	${_zhack}

SUBDIR.${MK_TESTS}+= tests

.if ${MK_ZFS} != "no"
.if ${MK_LIBTHR} != "no"
_zdb=	zdb
_zhack=	zhack
.endif
. if ${MK_CXX} != "no"
_zfsd=	zfsd
. endif
.endif

.if ${MACHINE_ARCH} == "amd64" || ${MACHINE_ARCH} == "i386"
_dtrace=	dtrace
_dwatch=	dwatch
_lockstat=	lockstat
_plockstat=	plockstat
.endif

.if ${MACHINE_CPUARCH} == "aarch64" || ${MACHINE_CPUARCH} == "arm" || \
    ${MACHINE_CPUARCH} == "riscv"
_dtrace=	dtrace
_dwatch=	dwatch
_lockstat=	lockstat
.endif

.if ${MACHINE_CPUARCH} == "mips"
_dtrace=	dtrace
_dwatch=	dwatch
.endif

.if ${MACHINE_CPUARCH} == "powerpc"
_dtrace=	dtrace
_dwatch=	dwatch
_lockstat=	lockstat
.endif

SUBDIR_PARALLEL=

.include <bsd.subdir.mk>
