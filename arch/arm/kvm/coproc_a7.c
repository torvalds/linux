/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Copyright (C) 2013 - ARM Ltd
 *
 * Authors: Rusty Russell <rusty@rustcorp.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
 *          Jonathan Austin <jonathan.austin@arm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/kvm_host.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <linux/init.h>

#include "coproc.h"

/*
 * Cortex-A7 specific CP15 registers.
 * CRn denotes the primary register number, but is copied to the CRm in the
 * user space API for 64-bit register access in line with the terminology used
 * in the ARM ARM.
 * Important: Must be sorted ascending by CRn, CRM, Op1, Op2 and with 64-bit
 *            registers preceding 32-bit ones.
 */
static const struct coproc_reg a7_regs[] = {
	/* SCTLR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 0), is32,
			access_vm_reg, reset_val, c1_SCTLR, 0x00C50878 },
};

static struct kvm_coproc_target_table a7_target_table = {
	.target = KVM_ARM_TARGET_CORTEX_A7,
	.table = a7_regs,
	.num = ARRAY_SIZE(a7_regs),
};

static int __init coproc_a7_init(void)
{
	kvm_register_target_coproc_table(&a7_target_table);
	return 0;
}
late_initcall(coproc_a7_init);
