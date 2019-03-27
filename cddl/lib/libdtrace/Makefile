# $FreeBSD$

.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/common
.PATH: ${SRCTOP}/cddl/contrib/opensolaris/lib/libgen/common

LIB=		dtrace
SRCS=		dt_aggregate.c \
		dt_as.c \
		dt_buf.c \
		dt_cc.c \
		dt_cg.c \
		dt_consume.c \
		dt_decl.c \
		dt_dis.c \
		dt_dof.c \
		dt_error.c \
		dt_errtags.c \
		dt_grammar.y \
		dt_handle.c \
		dt_ident.c \
		dt_isadep.c \
		dt_inttab.c \
		dt_lex.l \
		dt_link.c \
		dt_list.c \
		dt_map.c \
		dt_module.c \
		dt_names.c \
		dt_open.c \
		dt_options.c \
		dt_parser.c \
		dt_pcb.c \
		dt_pid.c \
		dt_pq.c \
		dt_pragma.c \
		dt_print.c \
		dt_printf.c \
		dt_proc.c \
		dt_program.c \
		dt_provider.c \
		dt_regset.c \
		dt_string.c \
		dt_strtab.c \
		dt_subr.c \
		dt_sugar.c \
		dt_work.c \
		dt_xlator.c \
		gmatch.c

DSRCS=		errno.d			\
		io.d			\
		ip.d			\
		psinfo.d		\
		sctp.d			\
		siftr.d			\
		signal.d		\
		tcp.d			\
		socket.d		\
		udp.d			\
		udplite.d		\
		unistd.d

FILES=		${DSRCS}
FILESDIR=	/usr/lib/dtrace
FILESMODE=	${NOBINMODE}

WARNS?=		1

CFLAGS+=	-I${.OBJDIR} -I${.CURDIR} \
		-I${SRCTOP}/sys/cddl/dev/dtrace/${MACHINE_ARCH} \
		-I${SRCTOP}/sys/cddl/compat/opensolaris \
		-I${SRCTOP}/cddl/compat/opensolaris/include \
		-I${OPENSOLARIS_USR_DISTDIR}/head \
		-I${OPENSOLARIS_USR_DISTDIR}/lib/libctf/common \
		-I${OPENSOLARIS_USR_DISTDIR}/lib/libdtrace/common \
		-I${OPENSOLARIS_SYS_DISTDIR}/uts/common

#CFLAGS+=	-DYYDEBUG

.if ${MACHINE_CPUARCH} == "aarch64"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/aarch64
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/aarch64
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/aarch64
.elif ${MACHINE_CPUARCH} == "amd64" || ${MACHINE_CPUARCH} == "i386"
CFLAGS+=	-I${SRCTOP}/sys/cddl/dev/dtrace/x86
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/intel -DDIS_MEM
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/i386
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/${MACHINE_ARCH}
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/x86
.elif ${MACHINE_CPUARCH} == "arm"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/arm
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/arm
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/arm
.elif ${MACHINE_CPUARCH} == "mips"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/mips
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/mips
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/mips
.elif ${MACHINE_CPUARCH} == "powerpc"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/powerpc
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/powerpc
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/powerpc
.elif ${MACHINE_CPUARCH} == "riscv"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/riscv
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/riscv
.PATH:		${SRCTOP}/sys/cddl/dev/dtrace/riscv
.elif ${MACHINE_CPUARCH} == "sparc64"
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/sparc
.PATH:		${SRCTOP}/cddl/contrib/opensolaris/lib/libdtrace/sparc
.else
# temporary hack
CFLAGS+=	-I${OPENSOLARIS_SYS_DISTDIR}/uts/intel
.endif

.if ${MACHINE_ARCH} == "i386" || ${MACHINE_ARCH} == "amd64"
SRCS+=		dis_tables.c
DSRCS+=		regs_x86.d
.endif

LFLAGS+=-l

YFLAGS+=-d

LIBADD=	ctf elf proc pthread rtld_db

CLEANFILES=	dt_errtags.c dt_names.c

.include <bsd.lib.mk>

dt_errtags.c: ${OPENSOLARIS_USR_DISTDIR}/lib/libdtrace/common/dt_errtags.h
	sh ${OPENSOLARIS_USR_DISTDIR}/lib/libdtrace/common/mkerrtags.sh < ${.ALLSRC} > ${.TARGET}

dt_names.c: ${OPENSOLARIS_SYS_DISTDIR}/uts/common/sys/dtrace.h
	sh ${OPENSOLARIS_USR_DISTDIR}/lib/libdtrace/common/mknames.sh < ${.ALLSRC} > ${.TARGET}
