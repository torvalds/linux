// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
 */

#include <linux/spinlock.h>
#include <asm/barrier.h>
#include <asm/sysreg.h>
#include <soc/qcom/kryo-l2-accessors.h>

#define L2CPUSRSELR_EL1         sys_reg(3, 3, 15, 0, 6)
#define L2CPUSRDR_EL1           sys_reg(3, 3, 15, 0, 7)

static DEFINE_RAW_SPINLOCK(l2_access_lock);

/**
 * kryo_l2_set_indirect_reg() - write value to an L2 register
 * @reg: Address of L2 register.
 * @val: Value to be written to register.
 *
 * Use architecturally required barriers for ordering between system register
 * accesses, and system registers with respect to device memory
 */
void kryo_l2_set_indirect_reg(u64 reg, u64 val)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&l2_access_lock, flags);
	write_sysreg_s(reg, L2CPUSRSELR_EL1);
	isb();
	write_sysreg_s(val, L2CPUSRDR_EL1);
	isb();
	raw_spin_unlock_irqrestore(&l2_access_lock, flags);
}
EXPORT_SYMBOL(kryo_l2_set_indirect_reg);

/**
 * kryo_l2_get_indirect_reg() - read an L2 register value
 * @reg: Address of L2 register.
 *
 * Use architecturally required barriers for ordering between system register
 * accesses, and system registers with respect to device memory
 */
u64 kryo_l2_get_indirect_reg(u64 reg)
{
	u64 val;
	unsigned long flags;

	raw_spin_lock_irqsave(&l2_access_lock, flags);
	write_sysreg_s(reg, L2CPUSRSELR_EL1);
	isb();
	val = read_sysreg_s(L2CPUSRDR_EL1);
	raw_spin_unlock_irqrestore(&l2_access_lock, flags);

	return val;
}
EXPORT_SYMBOL(kryo_l2_get_indirect_reg);
