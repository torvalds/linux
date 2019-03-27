#ifndef JEMALLOC_INTERNAL_BIT_UTIL_H
#define JEMALLOC_INTERNAL_BIT_UTIL_H

#include "jemalloc/internal/assert.h"

#define BIT_UTIL_INLINE static inline

/* Sanity check. */
#if !defined(JEMALLOC_INTERNAL_FFSLL) || !defined(JEMALLOC_INTERNAL_FFSL) \
    || !defined(JEMALLOC_INTERNAL_FFS)
#  error JEMALLOC_INTERNAL_FFS{,L,LL} should have been defined by configure
#endif


BIT_UTIL_INLINE unsigned
ffs_llu(unsigned long long bitmap) {
	return JEMALLOC_INTERNAL_FFSLL(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_lu(unsigned long bitmap) {
	return JEMALLOC_INTERNAL_FFSL(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_u(unsigned bitmap) {
	return JEMALLOC_INTERNAL_FFS(bitmap);
}

BIT_UTIL_INLINE unsigned
ffs_zu(size_t bitmap) {
#if LG_SIZEOF_PTR == LG_SIZEOF_INT
	return ffs_u(bitmap);
#elif LG_SIZEOF_PTR == LG_SIZEOF_LONG
	return ffs_lu(bitmap);
#elif LG_SIZEOF_PTR == LG_SIZEOF_LONG_LONG
	return ffs_llu(bitmap);
#else
#error No implementation for size_t ffs()
#endif
}

BIT_UTIL_INLINE unsigned
ffs_u64(uint64_t bitmap) {
#if LG_SIZEOF_LONG == 3
	return ffs_lu(bitmap);
#elif LG_SIZEOF_LONG_LONG == 3
	return ffs_llu(bitmap);
#else
#error No implementation for 64-bit ffs()
#endif
}

BIT_UTIL_INLINE unsigned
ffs_u32(uint32_t bitmap) {
#if LG_SIZEOF_INT == 2
	return ffs_u(bitmap);
#else
#error No implementation for 32-bit ffs()
#endif
	return ffs_u(bitmap);
}

BIT_UTIL_INLINE uint64_t
pow2_ceil_u64(uint64_t x) {
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x |= x >> 32;
	x++;
	return x;
}

BIT_UTIL_INLINE uint32_t
pow2_ceil_u32(uint32_t x) {
	x--;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	x++;
	return x;
}

/* Compute the smallest power of 2 that is >= x. */
BIT_UTIL_INLINE size_t
pow2_ceil_zu(size_t x) {
#if (LG_SIZEOF_PTR == 3)
	return pow2_ceil_u64(x);
#else
	return pow2_ceil_u32(x);
#endif
}

#if (defined(__i386__) || defined(__amd64__) || defined(__x86_64__))
BIT_UTIL_INLINE unsigned
lg_floor(size_t x) {
	size_t ret;
	assert(x != 0);

	asm ("bsr %1, %0"
	    : "=r"(ret) // Outputs.
	    : "r"(x)    // Inputs.
	    );
	assert(ret < UINT_MAX);
	return (unsigned)ret;
}
#elif (defined(_MSC_VER))
BIT_UTIL_INLINE unsigned
lg_floor(size_t x) {
	unsigned long ret;

	assert(x != 0);

#if (LG_SIZEOF_PTR == 3)
	_BitScanReverse64(&ret, x);
#elif (LG_SIZEOF_PTR == 2)
	_BitScanReverse(&ret, x);
#else
#  error "Unsupported type size for lg_floor()"
#endif
	assert(ret < UINT_MAX);
	return (unsigned)ret;
}
#elif (defined(JEMALLOC_HAVE_BUILTIN_CLZ))
BIT_UTIL_INLINE unsigned
lg_floor(size_t x) {
	assert(x != 0);

#if (LG_SIZEOF_PTR == LG_SIZEOF_INT)
	return ((8 << LG_SIZEOF_PTR) - 1) - __builtin_clz(x);
#elif (LG_SIZEOF_PTR == LG_SIZEOF_LONG)
	return ((8 << LG_SIZEOF_PTR) - 1) - __builtin_clzl(x);
#else
#  error "Unsupported type size for lg_floor()"
#endif
}
#else
BIT_UTIL_INLINE unsigned
lg_floor(size_t x) {
	assert(x != 0);

	x |= (x >> 1);
	x |= (x >> 2);
	x |= (x >> 4);
	x |= (x >> 8);
	x |= (x >> 16);
#if (LG_SIZEOF_PTR == 3)
	x |= (x >> 32);
#endif
	if (x == SIZE_T_MAX) {
		return (8 << LG_SIZEOF_PTR) - 1;
	}
	x++;
	return ffs_zu(x) - 2;
}
#endif

#undef BIT_UTIL_INLINE

#endif /* JEMALLOC_INTERNAL_BIT_UTIL_H */
