// SPDX-License-Identifier: GPL-2.0-only
/*
 * IOP Coprocessor-6 access handler
 * Copyright (c) 2006, Intel Corporation.
 */
#include <linux/init.h>
#include <asm/traps.h>
#include <asm/ptrace.h>

static int cp6_trap(struct pt_regs *regs, unsigned int instr)
{
	u32 temp;

        /* enable cp6 access */
        asm volatile (
		"mrc	p15, 0, %0, c15, c1, 0\n\t"
		"orr	%0, %0, #(1 << 6)\n\t"
		"mcr	p15, 0, %0, c15, c1, 0\n\t"
		: "=r"(temp));

	return 0;
}

/* permit kernel space cp6 access
 * deny user space cp6 access
 */
static struct undef_hook cp6_hook = {
	.instr_mask     = 0x0f000ff0,
	.instr_val      = 0x0e000610,
	.cpsr_mask      = MODE_MASK,
	.cpsr_val       = SVC_MODE,
	.fn             = cp6_trap,
};

void __init iop_init_cp6_handler(void)
{
	register_undef_hook(&cp6_hook);
}
