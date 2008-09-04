#ifndef _ALPHA_BUG_H
#define _ALPHA_BUG_H

#include <linux/linkage.h>

#ifdef CONFIG_BUG
#include <asm/pal.h>

/* ??? Would be nice to use .gprel32 here, but we can't be sure that the
   function loaded the GP, so this could fail in modules.  */
static inline void ATTRIB_NORET __BUG(const char *file, int line)
{
	__asm__ __volatile__(
		"call_pal %0  # bugchk\n\t"
		".long %1\n\t.8byte %2"
		       : : "i" (PAL_bugchk), "i"(line), "i"(file));
	for ( ; ; )
		;
}

#define BUG() __BUG(__FILE__, __LINE__)

#define HAVE_ARCH_BUG
#endif

#include <asm-generic/bug.h>

#endif
