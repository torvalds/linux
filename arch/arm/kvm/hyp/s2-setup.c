// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2016 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#include <linux/types.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_hyp.h>

void __hyp_text __init_stage2_translation(void)
{
	u64 val;

	val = read_sysreg(VTCR) & ~VTCR_MASK;

	val |= read_sysreg(HTCR) & VTCR_HTCR_SH;
	val |= KVM_VTCR_SL0 | KVM_VTCR_T0SZ | KVM_VTCR_S;

	write_sysreg(val, VTCR);
}
