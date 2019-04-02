/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K__H
#define _M68K__H

#ifdef CONFIG_MMU
#ifdef CONFIG_
#ifdef CONFIG_DE_VERBOSE
#ifndef CONFIG_SUN3
#define () do { \
	pr_crit("kernel  at %s:%d!\n", __FILE__, __LINE__); \
	barrier_before_unreachable(); \
	__builtin_trap(); \
} while (0)
#else
#define () do { \
	pr_crit("kernel  at %s:%d!\n", __FILE__, __LINE__); \
	barrier_before_unreachable(); \
	panic("!"); \
} while (0)
#endif
#else
#define () do { \
	barrier_before_unreachable(); \
	__builtin_trap(); \
} while (0)
#endif

#define HAVE_ARCH_
#endif
#endif /* CONFIG_MMU */

#include <asm-generic/.h>

#endif
