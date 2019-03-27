#ifndef JEMALLOC_INTERNAL_HOOKS_H
#define JEMALLOC_INTERNAL_HOOKS_H

extern JEMALLOC_EXPORT void (*hooks_arena_new_hook)();
extern JEMALLOC_EXPORT void (*hooks_libc_hook)();

#define JEMALLOC_HOOK(fn, hook) ((void)(hook != NULL && (hook(), 0)), fn)

/* Note that this is undef'd and re-define'd in src/prof.c. */
#define _Unwind_Backtrace JEMALLOC_HOOK(_Unwind_Backtrace, hooks_libc_hook)

#endif /* JEMALLOC_INTERNAL_HOOKS_H */
