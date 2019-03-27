/*
 *  ntp_types.h - defines how int32 and u_int32 are treated.
 *
 * New style: Make sure C99 fixed width integer types are available:
 * intN_t and uintN_t

 * Old style: defines how int32 and u_int32 are treated.
 *  For 64 bit systems like the DEC Alpha, they have to be defined
 *  as int and u_int.
 *  For 32 bit systems, define them as long and u_long
 */
#ifndef NTP_TYPES_H
#define NTP_TYPES_H

#include <sys/types.h>
#if defined(HAVE_INTTYPES_H)
# include <inttypes.h>
#endif
#if defined(HAVE_STDINT_H)
# include <stdint.h>
#endif

/* Bug 2813 */
#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#include "ntp_machine.h"


#ifndef TRUE
# define	TRUE	1
#endif
#ifndef FALSE
# define	FALSE	0
#endif

#ifdef HAVE_STDBOOL_H
# include <stdbool.h>
#else
typedef int bool;	/* Can't use enum TRUE/FALSE because of above */
#endif


/*
 * This is another naming conflict.
 * On NetBSD for MAC the macro "mac" is defined as 1
 * this is fun for us as a packet structure contains an
 * optional "mac" member - severe confusion results 8-)
 * As we hopefully do not have to rely on that macro we
 * just undefine that.
 */
#ifdef mac
#undef mac
#endif

/*
 * used to quiet compiler warnings
 */
#ifndef UNUSED_ARG
#define UNUSED_ARG(arg)		((void)(arg))
#endif
#ifndef UNUSED_LOCAL
#define UNUSED_LOCAL(arg)	((void)(arg))
#endif

/*
 * COUNTOF(array) - size of array in elements
 */
#define COUNTOF(arr)	(sizeof(arr) / sizeof((arr)[0]))

/*
 * VMS DECC (v4.1), {u_char,u_short,u_long} are only in SOCKET.H,
 *			and u_int isn't defined anywhere
 */
#if defined(VMS)
#include <socket.h>
typedef unsigned int u_int;
#endif /* VMS */

#ifdef HAVE_UINT32_T
# ifndef HAVE_INT32
   typedef	int32_t		int32;
# endif
# ifndef HAVE_U_INT32
   typedef	uint32_t	u_int32;
#  if defined(UINT32_MAX) && !defined(U_INT32_MAX)
#   define U_INT32_MAX UINT32_MAX
#  endif
# endif
#elif (SIZEOF_INT == 4)
# if !defined(HAVE_INT32) && !defined(int32)
   typedef	int		int32;
#  ifndef INT32_MIN
#   define INT32_MIN INT_MIN
#  endif
#  ifndef INT32_MAX
#   define INT32_MAX INT_MAX
#  endif
# endif
# if !defined(HAVE_U_INT32) && !defined(u_int32)
   typedef	unsigned	u_int32;
#  if defined(UINT_MAX) && !defined(U_INT32_MAX)
#   define U_INT32_MAX UINT_MAX
#  endif
# endif
#else	/* SIZEOF_INT != 4 */
# if (SIZEOF_LONG == 4)
# if !defined(HAVE_INT32) && !defined(int32)
    typedef	long		int32;
#   ifndef INT32_MIN
#    define INT32_MIN LONG_MIN
#   endif
#   ifndef INT32_MAX
#    define INT32_MAX LONG_MAX
#   endif
#  endif
# if !defined(HAVE_U_INT32) && !defined(u_int32)
    typedef	unsigned long	u_int32;
#   if defined(ULONG_MAX) && !defined(U_INT32_MAX)
#    define U_INT32_MAX ULONG_MAX
#   endif
#  endif
# else	/* SIZEOF_LONG != 4 */
#  include "Bletch: what's 32 bits on this machine?"
# endif
#endif	/* !HAVE_UINT32_T && SIZEOF_INT != 4 */

#ifndef U_INT32_MAX
# define U_INT32_MAX	0xffffffff
#endif


/*
 * Ugly dance to find out if we have 64bit integer type.
 */
#if !defined(HAVE_INT64)

/* assume best for now, fix if frustrated later. */
# define HAVE_INT64
# define HAVE_U_INT64

/* now check the cascade. Feel free to add things. */
# ifdef INT64_MAX

typedef int64_t int64;
typedef uint64_t u_int64;

# elif SIZEOF_LONG == 8

typedef long int64;
typedef unsigned long u_int64;

# elif SIZEOF_LONG_LONG == 8

typedef long long int64;
typedef unsigned long long u_int64;

# else

/* no 64bit scalar, give it up. */
#  undef HAVE_INT64
#  undef HAVE_U_INT64

# endif

#endif

/*
 * and here the trouble starts: We need a representation with more than
 * 64 bits. If a scalar of that size is not available, we need a struct
 * that holds the value in split representation.
 *
 * To ease the usage a bit, we alwys use a union that is in processor
 * byte order and might or might not contain a 64bit scalar.
 */

#if SIZEOF_SHORT != 2
# error short is not 2 bytes -- what is 16 bit integer on this target?
#endif

typedef union {
#   ifdef WORDS_BIGENDIAN
	struct {
		int16_t	hh; uint16_t hl; uint16_t lh; uint16_t ll;
	} w_s;
	struct {
		uint16_t hh; uint16_t hl; uint16_t lh; uint16_t ll;
	} W_s;
	struct {
		  int32 hi; u_int32 lo;
	} d_s;
	struct {
		u_int32	hi; u_int32 lo;
	} D_s;
#   else
	struct {
		uint16_t ll; uint16_t lh; uint16_t hl;   int16_t hh;
	} w_s;
	struct {
		uint16_t ll; uint16_t lh; uint16_t hl; uint16_t hh;
	} W_s;
	struct {
		u_int32 lo;   int32 hi;
	} d_s;
	struct {
		u_int32 lo; u_int32 hi;
	} D_s;
#   endif

#   ifdef HAVE_INT64
	int64	q_s;	/*   signed quad scalar */
	u_int64 Q_s;	/* unsigned quad scalar */
#   endif
} vint64; /* variant int 64 */


typedef uint8_t		ntp_u_int8_t;
typedef uint16_t	ntp_u_int16_t;
typedef uint32_t	ntp_u_int32_t;

typedef struct ntp_uint64_t { u_int32 val[2]; } ntp_uint64_t;

typedef uint16_t	associd_t; /* association ID */
#define ASSOCID_MAX	USHRT_MAX
typedef u_int32 keyid_t;	/* cryptographic key ID */
#define KEYID_T_MAX	(0xffffffff)

typedef u_int32 tstamp_t;	/* NTP seconds timestamp */

/*
 * Cloning malloc()'s behavior of always returning pointers suitably
 * aligned for the strictest alignment requirement of any type is not
 * easy to do portably, as the maximum alignment required is not
 * exposed.  Use the size of a union of the types known to represent the
 * strictest alignment on some platform.
 */
typedef union max_alignment_tag {
	double		d;
} max_alignment;

#define MAXALIGN		sizeof(max_alignment)
#define ALIGN_UNITS(sz)		(((sz) + MAXALIGN - 1) / MAXALIGN)
#define ALIGNED_SIZE(sz)	(MAXALIGN * ALIGN_UNITS(sz))
#define INC_ALIGNED_PTR(b, m)	((void *)aligned_ptr((void *)(b), m))

static inline
max_alignment *
aligned_ptr(
	max_alignment *	base,
	size_t		minsize
	)
{
	return base + ALIGN_UNITS((minsize < 1) ? 1 : minsize);
}

/*
 * Macro to use in otherwise-empty source files to comply with ANSI C
 * requirement that each translation unit (source file) contain some
 * declaration.  This has commonly been done by declaring an unused
 * global variable of type int or char.  An extern reference to exit()
 * serves the same purpose without bloat.
 */
#define	NONEMPTY_TRANSLATION_UNIT	extern void exit(int);

/*
 * On Unix struct sock_timeval is equivalent to struct timeval.
 * On Windows built with 64-bit time_t, sock_timeval.tv_sec is a long
 * as required by Windows' socket() interface timeout argument, while
 * timeval.tv_sec is time_t for the more common use as a UTC time 
 * within NTP.
 */
#ifndef SYS_WINNT
#define	sock_timeval	timeval
#endif

/*
 * On Unix open() works for tty (serial) devices just fine, while on
 * Windows refclock serial devices are opened using CreateFile, a lower
 * level than the CRT-provided descriptors, because the C runtime lacks
 * tty APIs.  For refclocks which wish to use open() as well as or 
 * instead of refclock_open(), tty_open() is equivalent to open() on
 * Unix and  implemented in the Windows port similarly to
 * refclock_open().
 * Similarly, the termios emulation in the Windows code needs to know
 * about serial ports being closed, while the Posix systems do not.
 */
#ifndef SYS_WINNT
# define tty_open(f, a, m)	open(f, a, m)
# define closeserial(fd)	close(fd)
# define closesocket(fd)	close(fd)
typedef int SOCKET;
# define INVALID_SOCKET		(-1)
# define SOCKET_ERROR		(-1)
# define socket_errno()		(errno)
#else	/* SYS_WINNT follows */
# define socket_errno()		(errno = WSAGetLastError())
#endif



#endif	/* NTP_TYPES_H */
