/*
 * Override settings that were generated in jemalloc_defs.h as necessary.
 */

#undef JEMALLOC_OVERRIDE_VALLOC

#ifndef MALLOC_PRODUCTION
#define	JEMALLOC_DEBUG
#endif

#undef JEMALLOC_DSS

#undef JEMALLOC_BACKGROUND_THREAD

/*
 * The following are architecture-dependent, so conditionally define them for
 * each supported architecture.
 */
#undef JEMALLOC_TLS_MODEL
#undef LG_PAGE
#undef LG_VADDR
#undef LG_SIZEOF_PTR
#undef LG_SIZEOF_INT
#undef LG_SIZEOF_LONG
#undef LG_SIZEOF_INTMAX_T

#ifdef __i386__
#  define LG_VADDR		32
#  define LG_SIZEOF_PTR		2
#  define JEMALLOC_TLS_MODEL	__attribute__((tls_model("initial-exec")))
#endif
#ifdef __ia64__
#  define LG_VADDR		64
#  define LG_SIZEOF_PTR		3
#endif
#ifdef __sparc64__
#  define LG_VADDR		64
#  define LG_SIZEOF_PTR		3
#  define JEMALLOC_TLS_MODEL	__attribute__((tls_model("initial-exec")))
#endif
#ifdef __amd64__
#  define LG_VADDR		48
#  define LG_SIZEOF_PTR		3
#  define JEMALLOC_TLS_MODEL	__attribute__((tls_model("initial-exec")))
#endif
#ifdef __arm__
#  define LG_VADDR		32
#  define LG_SIZEOF_PTR		2
#endif
#ifdef __aarch64__
#  define LG_VADDR		48
#  define LG_SIZEOF_PTR		3
#endif
#ifdef __mips__
#ifdef __mips_n64
#  define LG_VADDR		64
#  define LG_SIZEOF_PTR		3
#else
#  define LG_VADDR		32
#  define LG_SIZEOF_PTR		2
#endif
#endif
#ifdef __powerpc64__
#  define LG_VADDR		64
#  define LG_SIZEOF_PTR		3
#elif defined(__powerpc__)
#  define LG_VADDR		32
#  define LG_SIZEOF_PTR		2
#endif
#ifdef __riscv
#  define LG_VADDR		64
#  define LG_SIZEOF_PTR		3
#endif

#ifndef JEMALLOC_TLS_MODEL
#  define JEMALLOC_TLS_MODEL	/* Default. */
#endif

#define	LG_PAGE			PAGE_SHIFT
#define	LG_SIZEOF_INT		2
#define	LG_SIZEOF_LONG		LG_SIZEOF_PTR
#define	LG_SIZEOF_INTMAX_T	3

#undef CPU_SPINWAIT
#include <machine/cpu.h>
#include <machine/cpufunc.h>
#define	CPU_SPINWAIT		cpu_spinwait()

/* Disable lazy-lock machinery, mangle isthreaded, and adjust its type. */
#undef JEMALLOC_LAZY_LOCK
extern int __isthreaded;
#define	isthreaded		((bool)__isthreaded)

/* Mangle. */
#undef je_malloc
#undef je_calloc
#undef je_posix_memalign
#undef je_aligned_alloc
#undef je_realloc
#undef je_free
#undef je_malloc_usable_size
#undef je_mallocx
#undef je_rallocx
#undef je_xallocx
#undef je_sallocx
#undef je_dallocx
#undef je_sdallocx
#undef je_nallocx
#undef je_mallctl
#undef je_mallctlnametomib
#undef je_mallctlbymib
#undef je_malloc_stats_print
#undef je_allocm
#undef je_rallocm
#undef je_sallocm
#undef je_dallocm
#undef je_nallocm
#define	je_malloc		__malloc
#define	je_calloc		__calloc
#define	je_posix_memalign	__posix_memalign
#define	je_aligned_alloc	__aligned_alloc
#define	je_realloc		__realloc
#define	je_free			__free
#define	je_malloc_usable_size	__malloc_usable_size
#define	je_mallocx		__mallocx
#define	je_rallocx		__rallocx
#define	je_xallocx		__xallocx
#define	je_sallocx		__sallocx
#define	je_dallocx		__dallocx
#define	je_sdallocx		__sdallocx
#define	je_nallocx		__nallocx
#define	je_mallctl		__mallctl
#define	je_mallctlnametomib	__mallctlnametomib
#define	je_mallctlbymib		__mallctlbymib
#define	je_malloc_stats_print	__malloc_stats_print
#define	je_allocm		__allocm
#define	je_rallocm		__rallocm
#define	je_sallocm		__sallocm
#define	je_dallocm		__dallocm
#define	je_nallocm		__nallocm
#define	open			_open
#define	read			_read
#define	write			_write
#define	close			_close
#define	pthread_join		_pthread_join
#define	pthread_once		_pthread_once
#define	pthread_self		_pthread_self
#define	pthread_equal		_pthread_equal
#define	pthread_mutex_lock	_pthread_mutex_lock
#define	pthread_mutex_trylock	_pthread_mutex_trylock
#define	pthread_mutex_unlock	_pthread_mutex_unlock
#define	pthread_cond_init	_pthread_cond_init
#define	pthread_cond_wait	_pthread_cond_wait
#define	pthread_cond_timedwait	_pthread_cond_timedwait
#define	pthread_cond_signal	_pthread_cond_signal

#ifdef JEMALLOC_C_
/*
 * Define 'weak' symbols so that an application can have its own versions
 * of malloc, calloc, realloc, free, et al.
 */
__weak_reference(__malloc, malloc);
__weak_reference(__calloc, calloc);
__weak_reference(__posix_memalign, posix_memalign);
__weak_reference(__aligned_alloc, aligned_alloc);
__weak_reference(__realloc, realloc);
__weak_reference(__free, free);
__weak_reference(__malloc_usable_size, malloc_usable_size);
__weak_reference(__mallocx, mallocx);
__weak_reference(__rallocx, rallocx);
__weak_reference(__xallocx, xallocx);
__weak_reference(__sallocx, sallocx);
__weak_reference(__dallocx, dallocx);
__weak_reference(__sdallocx, sdallocx);
__weak_reference(__nallocx, nallocx);
__weak_reference(__mallctl, mallctl);
__weak_reference(__mallctlnametomib, mallctlnametomib);
__weak_reference(__mallctlbymib, mallctlbymib);
__weak_reference(__malloc_stats_print, malloc_stats_print);
__weak_reference(__allocm, allocm);
__weak_reference(__rallocm, rallocm);
__weak_reference(__sallocm, sallocm);
__weak_reference(__dallocm, dallocm);
__weak_reference(__nallocm, nallocm);
#endif
