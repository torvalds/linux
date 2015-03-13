#ifndef __BACKPORT_LINUX_COMPILER_H
#define __BACKPORT_LINUX_COMPILER_H
#include_next <linux/compiler.h>

#ifndef __rcu
#define __rcu
#endif

#ifndef __always_unused
#ifdef __GNUC__
#define __always_unused			__attribute__((unused))
#else
#define __always_unused			/* unimplemented */
#endif
#endif

#endif /* __BACKPORT_LINUX_COMPILER_H */
