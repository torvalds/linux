#ifndef JEMALLOC_INTERNAL_INCLUDES_H
#define JEMALLOC_INTERNAL_INCLUDES_H

/*
 * jemalloc can conceptually be broken into components (arena, tcache, etc.),
 * but there are circular dependencies that cannot be broken without
 * substantial performance degradation.
 *
 * Historically, we dealt with this by each header into four sections (types,
 * structs, externs, and inlines), and included each header file multiple times
 * in this file, picking out the portion we want on each pass using the
 * following #defines:
 *   JEMALLOC_H_TYPES   : Preprocessor-defined constants and psuedo-opaque data
 *                        types.
 *   JEMALLOC_H_STRUCTS : Data structures.
 *   JEMALLOC_H_EXTERNS : Extern data declarations and function prototypes.
 *   JEMALLOC_H_INLINES : Inline functions.
 *
 * We're moving toward a world in which the dependencies are explicit; each file
 * will #include the headers it depends on (rather than relying on them being
 * implicitly available via this file including every header file in the
 * project).
 *
 * We're now in an intermediate state: we've broken up the header files to avoid
 * having to include each one multiple times, but have not yet moved the
 * dependency information into the header files (i.e. we still rely on the
 * ordering in this file to ensure all a header's dependencies are available in
 * its translation unit).  Each component is now broken up into multiple header
 * files, corresponding to the sections above (e.g. instead of "foo.h", we now
 * have "foo_types.h", "foo_structs.h", "foo_externs.h", "foo_inlines.h").
 *
 * Those files which have been converted to explicitly include their
 * inter-component dependencies are now in the initial HERMETIC HEADERS
 * section.  All headers may still rely on jemalloc_preamble.h (which, by fiat,
 * must be included first in every translation unit) for system headers and
 * global jemalloc definitions, however.
 */

/******************************************************************************/
/* TYPES */
/******************************************************************************/

#include "jemalloc/internal/extent_types.h"
#include "jemalloc/internal/base_types.h"
#include "jemalloc/internal/arena_types.h"
#include "jemalloc/internal/tcache_types.h"
#include "jemalloc/internal/prof_types.h"

/******************************************************************************/
/* STRUCTS */
/******************************************************************************/

#include "jemalloc/internal/arena_structs_a.h"
#include "jemalloc/internal/extent_structs.h"
#include "jemalloc/internal/base_structs.h"
#include "jemalloc/internal/prof_structs.h"
#include "jemalloc/internal/arena_structs_b.h"
#include "jemalloc/internal/tcache_structs.h"
#include "jemalloc/internal/background_thread_structs.h"

/******************************************************************************/
/* EXTERNS */
/******************************************************************************/

#include "jemalloc/internal/jemalloc_internal_externs.h"
#include "jemalloc/internal/extent_externs.h"
#include "jemalloc/internal/base_externs.h"
#include "jemalloc/internal/arena_externs.h"
#include "jemalloc/internal/large_externs.h"
#include "jemalloc/internal/tcache_externs.h"
#include "jemalloc/internal/prof_externs.h"
#include "jemalloc/internal/background_thread_externs.h"

/******************************************************************************/
/* INLINES */
/******************************************************************************/

#include "jemalloc/internal/jemalloc_internal_inlines_a.h"
#include "jemalloc/internal/base_inlines.h"
/*
 * Include portions of arena code interleaved with tcache code in order to
 * resolve circular dependencies.
 */
#include "jemalloc/internal/prof_inlines_a.h"
#include "jemalloc/internal/arena_inlines_a.h"
#include "jemalloc/internal/extent_inlines.h"
#include "jemalloc/internal/jemalloc_internal_inlines_b.h"
#include "jemalloc/internal/tcache_inlines.h"
#include "jemalloc/internal/arena_inlines_b.h"
#include "jemalloc/internal/jemalloc_internal_inlines_c.h"
#include "jemalloc/internal/prof_inlines_b.h"
#include "jemalloc/internal/background_thread_inlines.h"

#endif /* JEMALLOC_INTERNAL_INCLUDES_H */
