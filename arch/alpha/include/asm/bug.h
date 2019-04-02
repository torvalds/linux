/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA__H
#define _ALPHA__H

#include <linux/linkage.h>

#ifdef CONFIG_
#include <asm/pal.h>

/* ??? Would be nice to use .gprel32 here, but we can't be sure that the
   function loaded the GP, so this could fail in modules.  */
#define ()	do {							\
	__asm__ __volatile__(						\
		"call_pal %0  # chk\n\t"				\
		".long %1\n\t.8byte %2"					\
		: : "i"(PAL_chk), "i"(__LINE__), "i"(__FILE__));	\
	unreachable();							\
  } while (0)

#define HAVE_ARCH_
#endif

#include <asm-generic/.h>

#endif
