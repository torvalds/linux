#ifndef JEMALLOC_INTERNAL_PROF_TYPES_H
#define JEMALLOC_INTERNAL_PROF_TYPES_H

typedef struct prof_bt_s prof_bt_t;
typedef struct prof_accum_s prof_accum_t;
typedef struct prof_cnt_s prof_cnt_t;
typedef struct prof_tctx_s prof_tctx_t;
typedef struct prof_gctx_s prof_gctx_t;
typedef struct prof_tdata_s prof_tdata_t;

/* Option defaults. */
#ifdef JEMALLOC_PROF
#  define PROF_PREFIX_DEFAULT		"jeprof"
#else
#  define PROF_PREFIX_DEFAULT		""
#endif
#define LG_PROF_SAMPLE_DEFAULT		19
#define LG_PROF_INTERVAL_DEFAULT	-1

/*
 * Hard limit on stack backtrace depth.  The version of prof_backtrace() that
 * is based on __builtin_return_address() necessarily has a hard-coded number
 * of backtrace frame handlers, and should be kept in sync with this setting.
 */
#define PROF_BT_MAX			128

/* Initial hash table size. */
#define PROF_CKH_MINITEMS		64

/* Size of memory buffer to use when writing dump files. */
#define PROF_DUMP_BUFSIZE		65536

/* Size of stack-allocated buffer used by prof_printf(). */
#define PROF_PRINTF_BUFSIZE		128

/*
 * Number of mutexes shared among all gctx's.  No space is allocated for these
 * unless profiling is enabled, so it's okay to over-provision.
 */
#define PROF_NCTX_LOCKS			1024

/*
 * Number of mutexes shared among all tdata's.  No space is allocated for these
 * unless profiling is enabled, so it's okay to over-provision.
 */
#define PROF_NTDATA_LOCKS		256

/*
 * prof_tdata pointers close to NULL are used to encode state information that
 * is used for cleaning up during thread shutdown.
 */
#define PROF_TDATA_STATE_REINCARNATED	((prof_tdata_t *)(uintptr_t)1)
#define PROF_TDATA_STATE_PURGATORY	((prof_tdata_t *)(uintptr_t)2)
#define PROF_TDATA_STATE_MAX		PROF_TDATA_STATE_PURGATORY

#endif /* JEMALLOC_INTERNAL_PROF_TYPES_H */
