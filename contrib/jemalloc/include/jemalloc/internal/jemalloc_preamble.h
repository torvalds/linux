#ifndef JEMALLOC_PREAMBLE_H
#define JEMALLOC_PREAMBLE_H

#include "jemalloc_internal_defs.h"
#include "jemalloc/internal/jemalloc_internal_decls.h"

#ifdef JEMALLOC_UTRACE
#include <sys/ktrace.h>
#endif

#include "un-namespace.h"
#include "libc_private.h"

#define JEMALLOC_NO_DEMANGLE
#ifdef JEMALLOC_JET
#  undef JEMALLOC_IS_MALLOC
#  define JEMALLOC_N(n) jet_##n
#  include "jemalloc/internal/public_namespace.h"
#  define JEMALLOC_NO_RENAME
#  include "../jemalloc.h"
#  undef JEMALLOC_NO_RENAME
#else
#  define JEMALLOC_N(n) __je_##n
#  include "../jemalloc.h"
#endif

#if (defined(JEMALLOC_OSATOMIC) || defined(JEMALLOC_OSSPIN))
#include <libkern/OSAtomic.h>
#endif

#ifdef JEMALLOC_ZONE
#include <mach/mach_error.h>
#include <mach/mach_init.h>
#include <mach/vm_map.h>
#endif

#include "jemalloc/internal/jemalloc_internal_macros.h"

/*
 * Note that the ordering matters here; the hook itself is name-mangled.  We
 * want the inclusion of hooks to happen early, so that we hook as much as
 * possible.
 */
#ifndef JEMALLOC_NO_PRIVATE_NAMESPACE
#  ifndef JEMALLOC_JET
#    include "jemalloc/internal/private_namespace.h"
#  else
#    include "jemalloc/internal/private_namespace_jet.h"
#  endif
#endif
#include "jemalloc/internal/hooks.h"

#ifdef JEMALLOC_DEFINE_MADVISE_FREE
#  define JEMALLOC_MADV_FREE 8
#endif

static const bool config_debug =
#ifdef JEMALLOC_DEBUG
    true
#else
    false
#endif
    ;
static const bool have_dss =
#ifdef JEMALLOC_DSS
    true
#else
    false
#endif
    ;
static const bool have_madvise_huge =
#ifdef JEMALLOC_HAVE_MADVISE_HUGE
    true
#else
    false
#endif
    ;
static const bool config_fill =
#ifdef JEMALLOC_FILL
    true
#else
    false
#endif
    ;
static const bool config_lazy_lock = true;
static const char * const config_malloc_conf = JEMALLOC_CONFIG_MALLOC_CONF;
static const bool config_prof =
#ifdef JEMALLOC_PROF
    true
#else
    false
#endif
    ;
static const bool config_prof_libgcc =
#ifdef JEMALLOC_PROF_LIBGCC
    true
#else
    false
#endif
    ;
static const bool config_prof_libunwind =
#ifdef JEMALLOC_PROF_LIBUNWIND
    true
#else
    false
#endif
    ;
static const bool maps_coalesce =
#ifdef JEMALLOC_MAPS_COALESCE
    true
#else
    false
#endif
    ;
static const bool config_stats =
#ifdef JEMALLOC_STATS
    true
#else
    false
#endif
    ;
static const bool config_tls =
#ifdef JEMALLOC_TLS
    true
#else
    false
#endif
    ;
static const bool config_utrace =
#ifdef JEMALLOC_UTRACE
    true
#else
    false
#endif
    ;
static const bool config_xmalloc =
#ifdef JEMALLOC_XMALLOC
    true
#else
    false
#endif
    ;
static const bool config_cache_oblivious =
#ifdef JEMALLOC_CACHE_OBLIVIOUS
    true
#else
    false
#endif
    ;
/*
 * Undocumented, for jemalloc development use only at the moment.  See the note
 * in jemalloc/internal/log.h.
 */
static const bool config_log =
#ifdef JEMALLOC_LOG
    true
#else
    false
#endif
    ;
#ifdef JEMALLOC_HAVE_SCHED_GETCPU
/* Currently percpu_arena depends on sched_getcpu. */
#define JEMALLOC_PERCPU_ARENA
#endif
static const bool have_percpu_arena =
#ifdef JEMALLOC_PERCPU_ARENA
    true
#else
    false
#endif
    ;
/*
 * Undocumented, and not recommended; the application should take full
 * responsibility for tracking provenance.
 */
static const bool force_ivsalloc =
#ifdef JEMALLOC_FORCE_IVSALLOC
    true
#else
    false
#endif
    ;
static const bool have_background_thread =
#ifdef JEMALLOC_BACKGROUND_THREAD
    true
#else
    false
#endif
    ;

#endif /* JEMALLOC_PREAMBLE_H */
