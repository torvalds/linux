/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68K_BUG_H
#define _M68K_BUG_H

#ifdef CONFIG_MMU
#ifdef CONFIG_BUG
#ifdef CONFIG_DEBUG_BUGVERBOSE
#ifndef CONFIG_SUN3
#define BUG() do { \
	pr_crit("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	__builtin_trap(); \
} while (0)
#else
#define BUG() do { \
	pr_crit("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	panic("BUG!"); \
} while (0)
#endif
#else
#define BUG() do { \
	__builtin_trap(); \
} while (0)
#endif

#define HAVE_ARCH_BUG
#endif
#endif /* CONFIG_MMU */

#include <asm-generic/bug.h>

#endif
