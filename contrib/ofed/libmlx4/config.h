/* $FreeBSD$ */

#ifdef	__LP64__
#define	SIZEOF_LONG 8
#else
#define	SIZEOF_LONG 4
#endif

#define	VALGRIND_MAKE_MEM_DEFINED(...)	0
#define	SWITCH_FALLTHROUGH (void)0
#define	ALWAYS_INLINE __attribute__ ((__always_inline__))
#define	likely(x) __predict_true(x)
#define	unlikely(x) __predict_false(x)
