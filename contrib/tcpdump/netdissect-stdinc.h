/*
 * Copyright (c) 2002 - 2003
 * NetGroup, Politecnico di Torino (Italy)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the Politecnico di Torino nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Include the appropriate OS header files on Windows and various flavors
 * of UNIX, include various non-OS header files on Windows, and define
 * various items as needed, to isolate most of netdissect's platform
 * differences to this one file.
 */

#ifndef netdissect_stdinc_h
#define netdissect_stdinc_h

#include <errno.h>

#ifdef _WIN32

/*
 * Includes and definitions for Windows.
 */

#include <stdint.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ctype.h>
#include <time.h>
#include <io.h>
#include <fcntl.h>
#include <sys/types.h>

#ifndef uint8_t
#define uint8_t		unsigned char
#endif

#ifndef int8_t
#define int8_t		signed char
#endif

#ifndef uint16_t
#define uint16_t	unsigned short
#endif

#ifndef int16_t
#define int16_t		signed short
#endif

#ifndef uint32_t
#define uint32_t	unsigned int
#endif

#ifndef int32_t
#define int32_t		signed int
#endif

#ifdef _MSC_EXTENSIONS

#ifndef uint64_t
#define uint64_t	unsigned _int64
#endif

#ifndef int64_t
#define int64_t		_int64
#endif

#ifndef PRId64
#define PRId64		"I64d"
#endif

#ifndef PRIo64
#define PRIo64		"I64o"
#endif

#ifndef PRIu64
#define PRIu64		"I64u"
#endif

#ifndef PRIx64
#define PRIx64		"I64x"
#endif

#else /* _MSC_EXTENSIONS */

#ifndef uint64_t
#define uint64_t	unsigned long long
#endif

#ifndef int64_t
#define int64_t		long long
#endif

#ifndef PRId64
#define PRId64		"lld"
#endif

#ifndef PRIo64
#define PRIo64		"llo"
#endif

#ifndef PRIu64
#define PRIu64		"llu"
#endif

#ifndef PRIx64
#define PRIx64		"llx"
#endif

#endif /* _MSC_EXTENSIONS */

/*
 * Suppress definition of intN_t in bittypes.h, as included by <pcap/pcap.h>
 * on Windows.
 * (Yes, HAVE_U_INTn_T, as the definition guards are UN*X-oriented, and
 * we check for u_intN_t in the UN*X configure script.)
 */
#define HAVE_U_INT8_T
#define HAVE_U_INT16_T
#define HAVE_U_INT32_T
#define HAVE_U_INT64_T

#ifdef _MSC_VER
#define stat _stat
#define open _open
#define fstat _fstat
#define read _read
#define close _close
#define O_RDONLY _O_RDONLY
#endif  /* _MSC_VER */

/*
 * With MSVC, for C, __inline is used to make a function an inline.
 */
#ifdef _MSC_VER
#define inline __inline
#endif

#ifdef AF_INET6
#define HAVE_OS_IPV6_SUPPORT
#endif

#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif

/* It is in MSVC's <errno.h>, but not defined in MingW+Watcom.
 */
#ifndef EAFNOSUPPORT
#define EAFNOSUPPORT WSAEAFNOSUPPORT
#endif

#ifndef caddr_t
typedef char* caddr_t;
#endif /* caddr_t */

#define MAXHOSTNAMELEN	64
#define snprintf _snprintf
#define vsnprintf _vsnprintf
#define RETSIGTYPE void

#else /* _WIN32 */

/*
 * Includes and definitions for various flavors of UN*X.
 */

#include <ctype.h>
#include <unistd.h>
#include <netdb.h>
#if HAVE_INTTYPES_H
#include <inttypes.h>
#elif HAVE_STDINT_H
#include <stdint.h>
#endif
#include <sys/param.h>
#include <sys/types.h>			/* concession to AIX */
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>

#ifdef TIME_WITH_SYS_TIME
#include <time.h>
#endif

#include <arpa/inet.h>

#endif /* _WIN32 */

#ifndef HAVE___ATTRIBUTE__
#define __attribute__(x)
#endif

/*
 * Used to declare a structure unaligned, so that the C compiler,
 * if necessary, generates code that doesn't assume alignment.
 * This is required because there is no guarantee that the packet
 * data we get from libpcap/WinPcap is properly aligned.
 *
 * This assumes that, for all compilers that support __attribute__:
 *
 *	1) they support __attribute__((packed));
 *
 *	2) for all instruction set architectures requiring strict
 *	   alignment, declaring a structure with that attribute
 *	   causes the compiler to generate code that handles
 *	   misaligned 2-byte, 4-byte, and 8-byte integral
 *	   quantities.
 *
 * It does not (yet) handle compilers where you can get the compiler
 * to generate code of that sort by some other means.
 *
 * This is required in order to, for example, keep the compiler from
 * generating, for
 *
 *	if (bp->bp_htype == 1 && bp->bp_hlen == 6 && bp->bp_op == BOOTPREQUEST) {
 *
 * in print-bootp.c, code that loads the first 4-byte word of a
 * "struct bootp", masking out the bp_hops field, and comparing the result
 * against 0x01010600.
 *
 * Note: this also requires that padding be put into the structure,
 * at least for compilers where it's implemented as __attribute__((packed)).
 */
#if !(defined(_MSC_VER) && defined(UNALIGNED))
/* MSVC may have its own macro defined with the same name and purpose. */
#undef UNALIGNED
#define UNALIGNED	__attribute__((packed))
#endif

/*
 * fopen() read and write modes for text files and binary files.
 */
#if defined(_WIN32) || defined(MSDOS)
  #define FOPEN_READ_TXT   "rt"
  #define FOPEN_READ_BIN   "rb"
  #define FOPEN_WRITE_TXT  "wt"
  #define FOPEN_WRITE_BIN  "wb"
#else
  #define FOPEN_READ_TXT   "r"
  #define FOPEN_READ_BIN   FOPEN_READ_TXT
  #define FOPEN_WRITE_TXT  "w"
  #define FOPEN_WRITE_BIN  FOPEN_WRITE_TXT
#endif

/*
 * Inline x86 assembler-language versions of ntoh[ls]() and hton[ls](),
 * defined if the OS doesn't provide them.  These assume no more than
 * an 80386, so, for example, it avoids the bswap instruction added in
 * the 80486.
 *
 * (We don't use them on OS X; Apple provides their own, which *doesn't*
 * avoid the bswap instruction, as OS X only supports machines that
 * have it.)
 */
#if defined(__GNUC__) && defined(__i386__) && !defined(__APPLE__) && !defined(__ntohl)
  #undef ntohl
  #undef ntohs
  #undef htonl
  #undef htons

  static __inline__ unsigned long __ntohl (unsigned long x);
  static __inline__ unsigned short __ntohs (unsigned short x);

  #define ntohl(x)  __ntohl(x)
  #define ntohs(x)  __ntohs(x)
  #define htonl(x)  __ntohl(x)
  #define htons(x)  __ntohs(x)

  static __inline__ unsigned long __ntohl (unsigned long x)
  {
    __asm__ ("xchgb %b0, %h0\n\t"   /* swap lower bytes  */
             "rorl  $16, %0\n\t"    /* swap words        */
             "xchgb %b0, %h0"       /* swap higher bytes */
            : "=q" (x) : "0" (x));
    return (x);
  }

  static __inline__ unsigned short __ntohs (unsigned short x)
  {
    __asm__ ("xchgb %b0, %h0"       /* swap bytes */
            : "=q" (x) : "0" (x));
    return (x);
  }
#endif

/*
 * If the OS doesn't define AF_INET6 and struct in6_addr:
 *
 * define AF_INET6, so we can use it internally as a "this is an
 * IPv6 address" indication;
 *
 * define struct in6_addr so that we can use it for IPv6 addresses.
 */
#ifndef HAVE_OS_IPV6_SUPPORT
#ifndef AF_INET6
#define AF_INET6	24

struct in6_addr {
	union {
		__uint8_t   __u6_addr8[16];
		__uint16_t  __u6_addr16[8];
		__uint32_t  __u6_addr32[4];
	} __u6_addr;			/* 128-bit IP6 address */
};
#endif
#endif

#ifndef NI_MAXHOST
#define	NI_MAXHOST	1025
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/*
 * The Apple deprecation workaround macros below were adopted from the
 * FreeRADIUS server code under permission of Alan DeKok and Arran Cudbard-Bell.
 */

#define XSTRINGIFY(x) #x

/*
 *	Macros for controlling warnings in GCC >= 4.2 and clang >= 2.8
 */
#define DIAG_JOINSTR(x,y) XSTRINGIFY(x ## y)
#define DIAG_DO_PRAGMA(x) _Pragma (#x)

#if defined(__GNUC__) && ((__GNUC__ * 100) + __GNUC_MINOR__) >= 402
#  define DIAG_PRAGMA(x) DIAG_DO_PRAGMA(GCC diagnostic x)
#  if ((__GNUC__ * 100) + __GNUC_MINOR__) >= 406
#    define DIAG_OFF(x) DIAG_PRAGMA(push) DIAG_PRAGMA(ignored DIAG_JOINSTR(-W,x))
#    define DIAG_ON(x) DIAG_PRAGMA(pop)
#  else
#    define DIAG_OFF(x) DIAG_PRAGMA(ignored DIAG_JOINSTR(-W,x))
#    define DIAG_ON(x)  DIAG_PRAGMA(warning DIAG_JOINSTR(-W,x))
#  endif
#elif defined(__clang__) && ((__clang_major__ * 100) + __clang_minor__ >= 208)
#  define DIAG_PRAGMA(x) DIAG_DO_PRAGMA(clang diagnostic x)
#  define DIAG_OFF(x) DIAG_PRAGMA(push) DIAG_PRAGMA(ignored DIAG_JOINSTR(-W,x))
#  define DIAG_ON(x) DIAG_PRAGMA(pop)
#else
#  define DIAG_OFF(x)
#  define DIAG_ON(x)
#endif

/*
 *	For dealing with APIs which are only deprecated in OSX (like the OpenSSL API)
 */
#ifdef __APPLE__
#  define USES_APPLE_DEPRECATED_API DIAG_OFF(deprecated-declarations)
#  define USES_APPLE_RST DIAG_ON(deprecated-declarations)
#else
#  define USES_APPLE_DEPRECATED_API
#  define USES_APPLE_RST
#endif

/*
 * end of Apple deprecation workaround macros
 */

/*
 * Function attributes, for various compilers.
 */
#include "funcattrs.h"

#ifndef min
#define min(a,b) ((a)>(b)?(b):(a))
#endif
#ifndef max
#define max(a,b) ((b)>(a)?(b):(a))
#endif

#endif /* netdissect_stdinc_h */
