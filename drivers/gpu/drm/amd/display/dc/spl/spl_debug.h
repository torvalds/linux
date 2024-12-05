/* SPDX-License-Identifier: MIT */

/* Copyright 2024 Advanced Micro Devices, Inc. */

#ifndef SPL_DEBUG_H
#define SPL_DEBUG_H

#if defined(CONFIG_HAVE_KGDB) || defined(CONFIG_KGDB)
#define SPL_ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		kgdb_breakpoint(); \
	} \
} while (0)
#else
#define SPL_ASSERT_CRITICAL(expr) do {	\
	if (WARN_ON(!(expr))) { \
		; \
	} \
} while (0)
#endif /* CONFIG_HAVE_KGDB || CONFIG_KGDB */

#if defined(CONFIG_DEBUG_KERNEL_DC)
#define SPL_ASSERT(expr) SPL_ASSERT_CRITICAL(expr)
#else
#define SPL_ASSERT(expr) WARN_ON(!(expr))
#endif /* CONFIG_DEBUG_KERNEL_DC */

#define SPL_BREAK_TO_DEBUGGER() SPL_ASSERT(0)

#endif  // SPL_DEBUG_H
