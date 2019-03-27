/* $FreeBSD$ */

#define	min_t(type, x, y) ({			\
	type __min1 = (x);			\
	type __min2 = (y);			\
	__min1 < __min2 ? __min1 : __min2; })

#define	max_t(type, x, y) ({			\
	type __max1 = (x);			\
	type __max2 = (y);			\
	__max1 > __max2 ? __max1 : __max2; })

#define	min(a, b) ((a) > (b) ? (b) : (a))
#define	max(a, b) ((a) < (b) ? (b) : (a))
#define	SWITCH_FALLTHROUGH (void)0
#define	ALWAYS_INLINE __attribute__ ((__always_inline__))
#define	VALGRIND_MAKE_MEM_DEFINED(...)	0
#define	likely(x) __predict_true(x)
#define	unlikely(x) __predict_false(x)
#define	SHM_HUGETLB 0
