/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Based on arch/arm/kvm/coproc_a15.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Rusty Russell <rusty@rustcorp.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kvm_host.h>
#include <asm/cputype.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <asm/sysreg.h>
#include <linux/init.h>

#include "sys_regs.h"

static bool access_actlr(struct kvm_vcpu *vcpu,
			 struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	p->regval = vcpu_sys_reg(vcpu, ACTLR_EL1);
	return true;
}

static void reset_actlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	vcpu_sys_reg(vcpu, ACTLR_EL1) = read_sysreg(actlr_el1);
}

/*
 * Implementation specific sys-reg registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 */
static const struct sys_reg_desc genericv8_sys_regs[] = {
	{ SYS_DESC(SYS_ACTLR_EL1), access_actlr, reset_actlr, ACTLR_EL1 },
};

static const struct sys_reg_desc genericv8_cp15_regs[] = {
	/* ACTLR */
	{ Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b001),
	  access_actlr },
};

static struct kvm_sys_reg_target_table genericv8_target_table = {
	.table64 = {
		.table = genericv8_sys_regs,
		.num = ARRAY_SIZE(genericv8_sys_regs),
	},
	.table32 = {
		.table = genericv8_cp15_regs,
		.num = ARRAY_SIZE(genericv8_cp15_regs),
	},
};

static int __init sys_reg_genericv8_init(void)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(genericv8_sys_regs); i++)
		BUG_ON(cmp_sys_reg(&genericv8_sys_regs[i-1],
			       &genericv8_sys_regs[i]) >= 0);

	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_AEM_V8,
					  &genericv8_target_table);
	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_FOUNDATION_V8,
					  &genericv8_target_table);
	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_CORTEX_A53,
					  &genericv8_target_table);
	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_CORTEX_A57,
					  &genericv8_target_table);
	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_XGENE_POTENZA,
					  &genericv8_target_table);
	kvm_register_target_sys_reg_table(KVM_ARM_TARGET_GENERIC_V8,
					  &genericv8_target_table);

	return 0;
}
late_initcall(sys_reg_genericv8_init);
