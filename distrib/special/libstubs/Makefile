#	$OpenBSD: Makefile,v 1.24 2025/08/14 09:31:21 kettenis Exp $

.include <bsd.own.mk>
LIB=    stubs
SRCS=	db.c setlocale.c sha1.c sha2.c getgrent.c getpwent.c \
	ethers.c \
	malloc.c mbrtowc_sb.c sscanf.c vfprintf.c vfscanf.c
SRCS+=	asr.c			\
	asr_utils.c		\
	getaddrinfo.c		\
	getaddrinfo_async.c	\
	gethostnamadr.c		\
	gethostnamadr_async.c	\
	getnameinfo.c		\
	getnameinfo_async.c	\
	getnetnamadr.c		\
	getnetnamadr_async.c	\
	getrrsetbyname.c	\
	getrrsetbyname_async.c	\
	res_debug.c		\
	res_init.c		\
	res_mkquery.c		\
	res_query.c		\
	res_search_async.c	\
	res_send.c		\
	res_send_async.c	\
	sethostent.c

.if ${MACHINE_ARCH} == "aarch64"
SRCS+=	aarch64_lse.c
.endif

NOPIC=	Yes
NOPROFILE=Yes

CFLAGS+=-DSHA2_SMALL -UYP -DNO_LOG_BAD_DNS_RESPONSES \
	-DFLOATING_POINT -DASRNODEBUG -D__LIBC__ -DMALLOC_SMALL \
	-I${LIBCSRCDIR}/include -I${LIBCSRCDIR}/hidden \
	-I${LIBCSRCDIR}/asr -I${LIBCSRCDIR}/locale -I${LIBCSRCDIR}/gdtoa \
	-I${LIBCSRCDIR}/arch/${MACHINE_CPU}/gdtoa -I${LIBCSRCDIR}/stdio \
	-include namespace.h
.if ${MACHINE_ARCH} == "hppa" || ${MACHINE_ARCH} == "m88k" || \
    ${MACHINE_ARCH} == "sh"
.else
CFLAGS+= -DFUTEX
.endif

LIBCSRCDIR=${.CURDIR}/../../../lib/libc

.PATH: ${.CURDIR}/../../../lib/libc/hash
.PATH: ${.CURDIR}/../../../lib/libc/gen
.PATH: ${.CURDIR}/../../../lib/libc/asr
.PATH: ${.CURDIR}/../../../lib/libc/net
.PATH: ${.CURDIR}/../../../lib/libc/stdio
.PATH: ${.CURDIR}/../../../lib/libc/stdlib

.include <bsd.lib.mk>
