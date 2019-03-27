#ifndef JEMALLOC_INTERNAL_TYPES_H
#define JEMALLOC_INTERNAL_TYPES_H

/* Page size index type. */
typedef unsigned pszind_t;

/* Size class index type. */
typedef unsigned szind_t;

/* Processor / core id type. */
typedef int malloc_cpuid_t;

/*
 * Flags bits:
 *
 * a: arena
 * t: tcache
 * 0: unused
 * z: zero
 * n: alignment
 *
 * aaaaaaaa aaaatttt tttttttt 0znnnnnn
 */
#define MALLOCX_ARENA_BITS	12
#define MALLOCX_TCACHE_BITS	12
#define MALLOCX_LG_ALIGN_BITS	6
#define MALLOCX_ARENA_SHIFT	20
#define MALLOCX_TCACHE_SHIFT	8
#define MALLOCX_ARENA_MASK \
    (((1 << MALLOCX_ARENA_BITS) - 1) << MALLOCX_ARENA_SHIFT)
/* NB: Arena index bias decreases the maximum number of arenas by 1. */
#define MALLOCX_ARENA_LIMIT	((1 << MALLOCX_ARENA_BITS) - 1)
#define MALLOCX_TCACHE_MASK \
    (((1 << MALLOCX_TCACHE_BITS) - 1) << MALLOCX_TCACHE_SHIFT)
#define MALLOCX_TCACHE_MAX	((1 << MALLOCX_TCACHE_BITS) - 3)
#define MALLOCX_LG_ALIGN_MASK	((1 << MALLOCX_LG_ALIGN_BITS) - 1)
/* Use MALLOCX_ALIGN_GET() if alignment may not be specified in flags. */
#define MALLOCX_ALIGN_GET_SPECIFIED(flags)				\
    (ZU(1) << (flags & MALLOCX_LG_ALIGN_MASK))
#define MALLOCX_ALIGN_GET(flags)					\
    (MALLOCX_ALIGN_GET_SPECIFIED(flags) & (SIZE_T_MAX-1))
#define MALLOCX_ZERO_GET(flags)						\
    ((bool)(flags & MALLOCX_ZERO))

#define MALLOCX_TCACHE_GET(flags)					\
    (((unsigned)((flags & MALLOCX_TCACHE_MASK) >> MALLOCX_TCACHE_SHIFT)) - 2)
#define MALLOCX_ARENA_GET(flags)					\
    (((unsigned)(((unsigned)flags) >> MALLOCX_ARENA_SHIFT)) - 1)

/* Smallest size class to support. */
#define TINY_MIN		(1U << LG_TINY_MIN)

/*
 * Minimum allocation alignment is 2^LG_QUANTUM bytes (ignoring tiny size
 * classes).
 */
#ifndef LG_QUANTUM
#  if (defined(__i386__) || defined(_M_IX86))
#    define LG_QUANTUM		4
#  endif
#  ifdef __ia64__
#    define LG_QUANTUM		4
#  endif
#  ifdef __alpha__
#    define LG_QUANTUM		4
#  endif
#  if (defined(__sparc64__) || defined(__sparcv9) || defined(__sparc_v9__))
#    define LG_QUANTUM		4
#  endif
#  if (defined(__amd64__) || defined(__x86_64__) || defined(_M_X64))
#    define LG_QUANTUM		4
#  endif
#  ifdef __arm__
#    define LG_QUANTUM		3
#  endif
#  ifdef __aarch64__
#    define LG_QUANTUM		4
#  endif
#  ifdef __hppa__
#    define LG_QUANTUM		4
#  endif
#  ifdef __m68k__
#    define LG_QUANTUM		3
#  endif
#  ifdef __mips__
#    define LG_QUANTUM		3
#  endif
#  ifdef __nios2__
#    define LG_QUANTUM		3
#  endif
#  ifdef __or1k__
#    define LG_QUANTUM		3
#  endif
#  ifdef __powerpc__
#    define LG_QUANTUM		4
#  endif
#  if defined(__riscv) || defined(__riscv__)
#    define LG_QUANTUM		4
#  endif
#  ifdef __s390__
#    define LG_QUANTUM		4
#  endif
#  if (defined (__SH3E__) || defined(__SH4_SINGLE__) || defined(__SH4__) || \
	defined(__SH4_SINGLE_ONLY__))
#    define LG_QUANTUM		4
#  endif
#  ifdef __tile__
#    define LG_QUANTUM		4
#  endif
#  ifdef __le32__
#    define LG_QUANTUM		4
#  endif
#  ifndef LG_QUANTUM
#    error "Unknown minimum alignment for architecture; specify via "
	 "--with-lg-quantum"
#  endif
#endif

#define QUANTUM			((size_t)(1U << LG_QUANTUM))
#define QUANTUM_MASK		(QUANTUM - 1)

/* Return the smallest quantum multiple that is >= a. */
#define QUANTUM_CEILING(a)						\
	(((a) + QUANTUM_MASK) & ~QUANTUM_MASK)

#define LONG			((size_t)(1U << LG_SIZEOF_LONG))
#define LONG_MASK		(LONG - 1)

/* Return the smallest long multiple that is >= a. */
#define LONG_CEILING(a)							\
	(((a) + LONG_MASK) & ~LONG_MASK)

#define SIZEOF_PTR		(1U << LG_SIZEOF_PTR)
#define PTR_MASK		(SIZEOF_PTR - 1)

/* Return the smallest (void *) multiple that is >= a. */
#define PTR_CEILING(a)							\
	(((a) + PTR_MASK) & ~PTR_MASK)

/*
 * Maximum size of L1 cache line.  This is used to avoid cache line aliasing.
 * In addition, this controls the spacing of cacheline-spaced size classes.
 *
 * CACHELINE cannot be based on LG_CACHELINE because __declspec(align()) can
 * only handle raw constants.
 */
#define LG_CACHELINE		6
#define CACHELINE		64
#define CACHELINE_MASK		(CACHELINE - 1)

/* Return the smallest cacheline multiple that is >= s. */
#define CACHELINE_CEILING(s)						\
	(((s) + CACHELINE_MASK) & ~CACHELINE_MASK)

/* Return the nearest aligned address at or below a. */
#define ALIGNMENT_ADDR2BASE(a, alignment)				\
	((void *)((uintptr_t)(a) & ((~(alignment)) + 1)))

/* Return the offset between a and the nearest aligned address at or below a. */
#define ALIGNMENT_ADDR2OFFSET(a, alignment)				\
	((size_t)((uintptr_t)(a) & (alignment - 1)))

/* Return the smallest alignment multiple that is >= s. */
#define ALIGNMENT_CEILING(s, alignment)					\
	(((s) + (alignment - 1)) & ((~(alignment)) + 1))

/* Declare a variable-length array. */
#if __STDC_VERSION__ < 199901L
#  ifdef _MSC_VER
#    include <malloc.h>
#    define alloca _alloca
#  else
#    ifdef JEMALLOC_HAS_ALLOCA_H
#      include <alloca.h>
#    else
#      include <stdlib.h>
#    endif
#  endif
#  define VARIABLE_ARRAY(type, name, count) \
	type *name = alloca(sizeof(type) * (count))
#else
#  define VARIABLE_ARRAY(type, name, count) type name[(count)]
#endif

#endif /* JEMALLOC_INTERNAL_TYPES_H */
