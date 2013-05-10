/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_TRAPS_H
#define __ASM_AVR32_TRAPS_H

#include <linux/list.h>

struct undef_hook {
	struct list_head node;
	u32 insn_mask;
	u32 insn_val;
	int (*fn)(struct pt_regs *regs, u32 insn);
};

void register_undef_hook(struct undef_hook *hook);
void unregister_undef_hook(struct undef_hook *hook);

#endif /* __ASM_AVR32_TRAPS_H */
