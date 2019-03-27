/* include/jemalloc/internal/jemalloc_internal_defs.h.  Generated from jemalloc_internal_defs.h.in by configure.  */
#ifndef JEMALLOC_INTERNAL_DEFS_H_
#define JEMALLOC_INTERNAL_DEFS_H_
/*
 * If JEMALLOC_PREFIX is defined via --with-jemalloc-prefix, it will cause all
 * public APIs to be prefixed.  This makes it possible, with some care, to use
 * multiple allocators simultaneously.
 */
/* #undef JEMALLOC_PREFIX */
/* #undef JEMALLOC_CPREFIX */

/*
 * Define overrides for non-standard allocator-related functions if they are
 * present on the system.
 */
/* #undef JEMALLOC_OVERRIDE___LIBC_CALLOC */
/* #undef JEMALLOC_OVERRIDE___LIBC_FREE */
/* #undef JEMALLOC_OVERRIDE___LIBC_MALLOC */
/* #undef JEMALLOC_OVERRIDE___LIBC_MEMALIGN */
/* #undef JEMALLOC_OVERRIDE___LIBC_REALLOC */
/* #undef JEMALLOC_OVERRIDE___LIBC_VALLOC */
#define JEMALLOC_OVERRIDE___POSIX_MEMALIGN 

/*
 * JEMALLOC_PRIVATE_NAMESPACE is used as a prefix for all library-private APIs.
 * For shared libraries, symbol visibility mechanisms prevent these symbols
 * from being exported, but for static libraries, naming collisions are a real
 * possibility.
 */
#define JEMALLOC_PRIVATE_NAMESPACE __je_

/*
 * Hyper-threaded CPUs may need a special instruction inside spin loops in
 * order to yield to another virtual CPU.
 */
#define CPU_SPINWAIT __asm__ volatile("pause")
/* 1 if CPU_SPINWAIT is defined, 0 otherwise. */
#define HAVE_CPU_SPINWAIT 1

/*
 * Number of significant bits in virtual addresses.  This may be less than the
 * total number of bits in a pointer, e.g. on x64, for which the uppermost 16
 * bits are the same as bit 47.
 */
#define LG_VADDR 48

/* Defined if C11 atomics are available. */
/* #undef JEMALLOC_C11_ATOMICS */

/* Defined if GCC __atomic atomics are available. */
/* #undef JEMALLOC_GCC_ATOMIC_ATOMICS */

/* Defined if GCC __sync atomics are available. */
#define JEMALLOC_GCC_SYNC_ATOMICS 1

/*
 * Defined if __sync_add_and_fetch(uint32_t *, uint32_t) and
 * __sync_sub_and_fetch(uint32_t *, uint32_t) are available, despite
 * __GCC_HAVE_SYNC_COMPARE_AND_SWAP_4 not being defined (which means the
 * functions are defined in libgcc instead of being inlines).
 */
#define JE_FORCE_SYNC_COMPARE_AND_SWAP_4 

/*
 * Defined if __sync_add_and_fetch(uint64_t *, uint64_t) and
 * __sync_sub_and_fetch(uint64_t *, uint64_t) are available, despite
 * __GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 not being defined (which means the
 * functions are defined in libgcc instead of being inlines).
 */
#define JE_FORCE_SYNC_COMPARE_AND_SWAP_8 

/*
 * Defined if __builtin_clz() and __builtin_clzl() are available.
 */
#define JEMALLOC_HAVE_BUILTIN_CLZ 

/*
 * Defined if os_unfair_lock_*() functions are available, as provided by Darwin.
 */
/* #undef JEMALLOC_OS_UNFAIR_LOCK */

/*
 * Defined if OSSpin*() functions are available, as provided by Darwin, and
 * documented in the spinlock(3) manual page.
 */
/* #undef JEMALLOC_OSSPIN */

/* Defined if syscall(2) is usable. */
#define JEMALLOC_USE_SYSCALL 

/*
 * Defined if secure_getenv(3) is available.
 */
/* #undef JEMALLOC_HAVE_SECURE_GETENV */

/*
 * Defined if issetugid(2) is available.
 */
#define JEMALLOC_HAVE_ISSETUGID 

/* Defined if pthread_atfork(3) is available. */
#define JEMALLOC_HAVE_PTHREAD_ATFORK 

/* Defined if pthread_setname_np(3) is available. */
/* #undef JEMALLOC_HAVE_PTHREAD_SETNAME_NP */

/*
 * Defined if clock_gettime(CLOCK_MONOTONIC_COARSE, ...) is available.
 */
/* #undef JEMALLOC_HAVE_CLOCK_MONOTONIC_COARSE */

/*
 * Defined if clock_gettime(CLOCK_MONOTONIC, ...) is available.
 */
#define JEMALLOC_HAVE_CLOCK_MONOTONIC 1

/*
 * Defined if mach_absolute_time() is available.
 */
/* #undef JEMALLOC_HAVE_MACH_ABSOLUTE_TIME */

/*
 * Defined if _malloc_thread_cleanup() exists.  At least in the case of
 * FreeBSD, pthread_key_create() allocates, which if used during malloc
 * bootstrapping will cause recursion into the pthreads library.  Therefore, if
 * _malloc_thread_cleanup() exists, use it as the basis for thread cleanup in
 * malloc_tsd.
 */
#define JEMALLOC_MALLOC_THREAD_CLEANUP 

/*
 * Defined if threaded initialization is known to be safe on this platform.
 * Among other things, it must be possible to initialize a mutex without
 * triggering allocation in order for threaded allocation to be safe.
 */
/* #undef JEMALLOC_THREADED_INIT */

/*
 * Defined if the pthreads implementation defines
 * _pthread_mutex_init_calloc_cb(), in which case the function is used in order
 * to avoid recursive allocation during mutex initialization.
 */
#define JEMALLOC_MUTEX_INIT_CB 1

/* Non-empty if the tls_model attribute is supported. */
#define JEMALLOC_TLS_MODEL __attribute__((tls_model("initial-exec")))

/*
 * JEMALLOC_DEBUG enables assertions and other sanity checks, and disables
 * inline functions.
 */
/* #undef JEMALLOC_DEBUG */

/* JEMALLOC_STATS enables statistics calculation. */
#define JEMALLOC_STATS 

/* JEMALLOC_PROF enables allocation profiling. */
/* #undef JEMALLOC_PROF */

/* Use libunwind for profile backtracing if defined. */
/* #undef JEMALLOC_PROF_LIBUNWIND */

/* Use libgcc for profile backtracing if defined. */
/* #undef JEMALLOC_PROF_LIBGCC */

/* Use gcc intrinsics for profile backtracing if defined. */
/* #undef JEMALLOC_PROF_GCC */

/*
 * JEMALLOC_DSS enables use of sbrk(2) to allocate extents from the data storage
 * segment (DSS).
 */
#define JEMALLOC_DSS 

/* Support memory filling (junk/zero). */
#define JEMALLOC_FILL 

/* Support utrace(2)-based tracing. */
#define JEMALLOC_UTRACE 

/* Support optional abort() on OOM. */
#define JEMALLOC_XMALLOC 

/* Support lazy locking (avoid locking unless a second thread is launched). */
#define JEMALLOC_LAZY_LOCK 

/*
 * Minimum allocation alignment is 2^LG_QUANTUM bytes (ignoring tiny size
 * classes).
 */
/* #undef LG_QUANTUM */

/* One page is 2^LG_PAGE bytes. */
#define LG_PAGE 12

/*
 * One huge page is 2^LG_HUGEPAGE bytes.  Note that this is defined even if the
 * system does not explicitly support huge pages; system calls that require
 * explicit huge page support are separately configured.
 */
#define LG_HUGEPAGE 21

/*
 * If defined, adjacent virtual memory mappings with identical attributes
 * automatically coalesce, and they fragment when changes are made to subranges.
 * This is the normal order of things for mmap()/munmap(), but on Windows
 * VirtualAlloc()/VirtualFree() operations must be precisely matched, i.e.
 * mappings do *not* coalesce/fragment.
 */
#define JEMALLOC_MAPS_COALESCE 

/*
 * If defined, retain memory for later reuse by default rather than using e.g.
 * munmap() to unmap freed extents.  This is enabled on 64-bit Linux because
 * common sequences of mmap()/munmap() calls will cause virtual memory map
 * holes.
 */
/* #undef JEMALLOC_RETAIN */

/* TLS is used to map arenas and magazine caches to threads. */
#define JEMALLOC_TLS 

/*
 * Used to mark unreachable code to quiet "end of non-void" compiler warnings.
 * Don't use this directly; instead use unreachable() from util.h
 */
#define JEMALLOC_INTERNAL_UNREACHABLE abort

/*
 * ffs*() functions to use for bitmapping.  Don't use these directly; instead,
 * use ffs_*() from util.h.
 */
#define JEMALLOC_INTERNAL_FFSLL __builtin_ffsll
#define JEMALLOC_INTERNAL_FFSL __builtin_ffsl
#define JEMALLOC_INTERNAL_FFS __builtin_ffs

/*
 * If defined, explicitly attempt to more uniformly distribute large allocation
 * pointer alignments across all cache indices.
 */
#define JEMALLOC_CACHE_OBLIVIOUS 

/*
 * If defined, enable logging facilities.  We make this a configure option to
 * avoid taking extra branches everywhere.
 */
/* #undef JEMALLOC_LOG */

/*
 * Darwin (OS X) uses zones to work around Mach-O symbol override shortcomings.
 */
/* #undef JEMALLOC_ZONE */

/*
 * Methods for determining whether the OS overcommits.
 * JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY: Linux's
 *                                         /proc/sys/vm.overcommit_memory file.
 * JEMALLOC_SYSCTL_VM_OVERCOMMIT: FreeBSD's vm.overcommit sysctl.
 */
#define JEMALLOC_SYSCTL_VM_OVERCOMMIT 
/* #undef JEMALLOC_PROC_SYS_VM_OVERCOMMIT_MEMORY */

/* Defined if madvise(2) is available. */
#define JEMALLOC_HAVE_MADVISE 

/*
 * Defined if transparent huge pages are supported via the MADV_[NO]HUGEPAGE
 * arguments to madvise(2).
 */
/* #undef JEMALLOC_HAVE_MADVISE_HUGE */

/*
 * Methods for purging unused pages differ between operating systems.
 *
 *   madvise(..., MADV_FREE) : This marks pages as being unused, such that they
 *                             will be discarded rather than swapped out.
 *   madvise(..., MADV_DONTNEED) : If JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS is
 *                                 defined, this immediately discards pages,
 *                                 such that new pages will be demand-zeroed if
 *                                 the address region is later touched;
 *                                 otherwise this behaves similarly to
 *                                 MADV_FREE, though typically with higher
 *                                 system overhead.
 */
#define JEMALLOC_PURGE_MADVISE_FREE 
#define JEMALLOC_PURGE_MADVISE_DONTNEED 
/* #undef JEMALLOC_PURGE_MADVISE_DONTNEED_ZEROS */

/* Defined if madvise(2) is available but MADV_FREE is not (x86 Linux only). */
/* #undef JEMALLOC_DEFINE_MADVISE_FREE */

/*
 * Defined if MADV_DO[NT]DUMP is supported as an argument to madvise.
 */
/* #undef JEMALLOC_MADVISE_DONTDUMP */

/*
 * Defined if transparent huge pages (THPs) are supported via the
 * MADV_[NO]HUGEPAGE arguments to madvise(2), and THP support is enabled.
 */
/* #undef JEMALLOC_THP */

/* Define if operating system has alloca.h header. */
/* #undef JEMALLOC_HAS_ALLOCA_H */

/* C99 restrict keyword supported. */
#define JEMALLOC_HAS_RESTRICT 1

/* For use by hash code. */
/* #undef JEMALLOC_BIG_ENDIAN */

/* sizeof(int) == 2^LG_SIZEOF_INT. */
#define LG_SIZEOF_INT 2

/* sizeof(long) == 2^LG_SIZEOF_LONG. */
#define LG_SIZEOF_LONG 3

/* sizeof(long long) == 2^LG_SIZEOF_LONG_LONG. */
#define LG_SIZEOF_LONG_LONG 3

/* sizeof(intmax_t) == 2^LG_SIZEOF_INTMAX_T. */
#define LG_SIZEOF_INTMAX_T 3

/* glibc malloc hooks (__malloc_hook, __realloc_hook, __free_hook). */
/* #undef JEMALLOC_GLIBC_MALLOC_HOOK */

/* glibc memalign hook. */
/* #undef JEMALLOC_GLIBC_MEMALIGN_HOOK */

/* pthread support */
#define JEMALLOC_HAVE_PTHREAD 

/* dlsym() support */
#define JEMALLOC_HAVE_DLSYM 

/* Adaptive mutex support in pthreads. */
#define JEMALLOC_HAVE_PTHREAD_MUTEX_ADAPTIVE_NP 

/* GNU specific sched_getcpu support */
/* #undef JEMALLOC_HAVE_SCHED_GETCPU */

/* GNU specific sched_setaffinity support */
/* #undef JEMALLOC_HAVE_SCHED_SETAFFINITY */

/*
 * If defined, all the features necessary for background threads are present.
 */
#define JEMALLOC_BACKGROUND_THREAD 1

/*
 * If defined, jemalloc symbols are not exported (doesn't work when
 * JEMALLOC_PREFIX is not defined).
 */
/* #undef JEMALLOC_EXPORT */

/* config.malloc_conf options string. */
#define JEMALLOC_CONFIG_MALLOC_CONF "abort_conf:false"

/* If defined, jemalloc takes the malloc/free/etc. symbol names. */
#define JEMALLOC_IS_MALLOC 1

/*
 * Defined if strerror_r returns char * if _GNU_SOURCE is defined.
 */
/* #undef JEMALLOC_STRERROR_R_RETURNS_CHAR_WITH_GNU_SOURCE */

#endif /* JEMALLOC_INTERNAL_DEFS_H_ */
