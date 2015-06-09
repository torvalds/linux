/*
 * livepatch.h - s390-specific Kernel Live Patching Core
 *
 *  Copyright (c) 2013-2015 SUSE
 *   Authors: Jiri Kosina
 *	      Vojtech Pavlik
 *	      Jiri Slaby
 */

/*
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 */

#ifndef ASM_LIVEPATCH_H
#define ASM_LIVEPATCH_H

#include <linux/module.h>

#ifdef CONFIG_LIVEPATCH
static inline int klp_check_compiler_support(void)
{
	return 0;
}

static inline int klp_write_module_reloc(struct module *mod, unsigned long
		type, unsigned long loc, unsigned long value)
{
	/* not supported yet */
	return -ENOSYS;
}

static inline void klp_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->psw.addr = ip;
}
#else
#error Live patching support is disabled; check CONFIG_LIVEPATCH
#endif

#endif
