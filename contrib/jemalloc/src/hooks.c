#include "jemalloc/internal/jemalloc_preamble.h"

/*
 * The hooks are a little bit screwy -- they're not genuinely exported in the
 * sense that we want them available to end-users, but we do want them visible
 * from outside the generated library, so that we can use them in test code.
 */
JEMALLOC_EXPORT
void (*hooks_arena_new_hook)() = NULL;

JEMALLOC_EXPORT
void (*hooks_libc_hook)() = NULL;
