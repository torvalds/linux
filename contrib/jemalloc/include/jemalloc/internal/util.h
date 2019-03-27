#ifndef JEMALLOC_INTERNAL_UTIL_H
#define JEMALLOC_INTERNAL_UTIL_H

#define UTIL_INLINE static inline

/* Junk fill patterns. */
#ifndef JEMALLOC_ALLOC_JUNK
#  define JEMALLOC_ALLOC_JUNK	((uint8_t)0xa5)
#endif
#ifndef JEMALLOC_FREE_JUNK
#  define JEMALLOC_FREE_JUNK	((uint8_t)0x5a)
#endif

/*
 * Wrap a cpp argument that contains commas such that it isn't broken up into
 * multiple arguments.
 */
#define JEMALLOC_ARG_CONCAT(...) __VA_ARGS__

/* cpp macro definition stringification. */
#define STRINGIFY_HELPER(x) #x
#define STRINGIFY(x) STRINGIFY_HELPER(x)

/*
 * Silence compiler warnings due to uninitialized values.  This is used
 * wherever the compiler fails to recognize that the variable is never used
 * uninitialized.
 */
#define JEMALLOC_CC_SILENCE_INIT(v) = v

#ifdef __GNUC__
#  define likely(x)   __builtin_expect(!!(x), 1)
#  define unlikely(x) __builtin_expect(!!(x), 0)
#else
#  define likely(x)   !!(x)
#  define unlikely(x) !!(x)
#endif

#if !defined(JEMALLOC_INTERNAL_UNREACHABLE)
#  error JEMALLOC_INTERNAL_UNREACHABLE should have been defined by configure
#endif

#define unreachable() JEMALLOC_INTERNAL_UNREACHABLE()

/* Set error code. */
UTIL_INLINE void
set_errno(int errnum) {
#ifdef _WIN32
	SetLastError(errnum);
#else
	errno = errnum;
#endif
}

/* Get last error code. */
UTIL_INLINE int
get_errno(void) {
#ifdef _WIN32
	return GetLastError();
#else
	return errno;
#endif
}

#undef UTIL_INLINE

#endif /* JEMALLOC_INTERNAL_UTIL_H */
