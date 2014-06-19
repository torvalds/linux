/*
 * Blackfin ftrace code
 *
 * Copyright 2009 Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#ifndef __ASM_BFIN_FTRACE_H__
#define __ASM_BFIN_FTRACE_H__

#define MCOUNT_INSN_SIZE	6 /* sizeof "[++sp] = rets; call __mcount;" */

#ifndef __ASSEMBLY__

#ifdef CONFIG_DYNAMIC_FTRACE

extern void _mcount(void);
#define MCOUNT_ADDR ((unsigned long)_mcount)

static inline unsigned long ftrace_call_adjust(unsigned long addr)
{
	return addr;
}

struct dyn_arch_ftrace {
	/* No extra data needed for Blackfin */
};

#endif

#ifdef CONFIG_FRAME_POINTER
#include <linux/mm.h>

extern inline void *return_address(unsigned int level)
{
	unsigned long *endstack, *fp, *ret_addr;
	unsigned int current_level = 0;

	if (level == 0)
		return __builtin_return_address(0);

	fp = (unsigned long *)__builtin_frame_address(0);
	endstack = (unsigned long *)PAGE_ALIGN((unsigned long)&level);

	while (((unsigned long)fp & 0x3) == 0 && fp &&
	       (fp + 1) < endstack && current_level < level) {
		fp = (unsigned long *)*fp;
		current_level++;
	}

	if (((unsigned long)fp & 0x3) == 0 && fp &&
	    (fp + 1) < endstack)
		ret_addr = (unsigned long *)*(fp + 1);
	else
		ret_addr = NULL;

	return ret_addr;
}

#else

extern inline void *return_address(unsigned int level)
{
	return NULL;
}

#endif /* CONFIG_FRAME_POINTER */

#define ftrace_return_address(n) return_address(n)

#endif /* __ASSEMBLY__ */

#endif
