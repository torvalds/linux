// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Rusty Russell <rusty@rustcorp.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
 */
#include <linux/kvm_host.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <linux/init.h>

#include "coproc.h"

/*
 * A15-specific CP15 registers.
 * CRn denotes the primary register number, but is copied to the CRm in the
 * user space API for 64-bit register access in line with the terminology used
 * in the ARM ARM.
 * Important: Must be sorted ascending by CRn, CRM, Op1, Op2 and with 64-bit
 *            registers preceding 32-bit ones.
 */
static const struct coproc_reg a15_regs[] = {
	/* SCTLR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_val, c1_SCTLR, 0x00C50078 },
};

static struct kvm_coproc_target_table a15_target_table = {
	.target = KVM_ARM_TARGET_CORTEX_A15,
	.table = a15_regs,
	.num = ARRAY_SIZE(a15_regs),
};

static int __init coproc_a15_init(void)
{
	kvm_register_target_coproc_table(&a15_target_table);
	return 0;
}
late_initcall(coproc_a15_init);
