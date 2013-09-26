/*
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
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <linux/kvm_host.h>
#include <asm/cputype.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_coproc.h>
#include <linux/init.h>

static void reset_mpidr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	/*
	 * Compute guest MPIDR. No need to mess around with different clusters
	 * but we read the 'U' bit from the underlying hardware directly.
	 */
	vcpu->arch.cp15[c0_MPIDR] = (read_cpuid_mpidr() & MPIDR_SMP_BITMASK)
					| vcpu->vcpu_id;
}

#include "coproc.h"

/* A15 TRM 4.3.28: RO WI */
static bool access_actlr(struct kvm_vcpu *vcpu,
			 const struct coproc_params *p,
			 const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = vcpu->arch.cp15[c1_ACTLR];
	return true;
}

/* A15 TRM 4.3.60: R/O. */
static bool access_cbar(struct kvm_vcpu *vcpu,
			const struct coproc_params *p,
			const struct coproc_reg *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p);
	return read_zero(vcpu, p);
}

/* A15 TRM 4.3.48: R/O WI. */
static bool access_l2ctlr(struct kvm_vcpu *vcpu,
			  const struct coproc_params *p,
			  const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = vcpu->arch.cp15[c9_L2CTLR];
	return true;
}

static void reset_l2ctlr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	u32 l2ctlr, ncores;

	asm volatile("mrc p15, 1, %0, c9, c0, 2\n" : "=r" (l2ctlr));
	l2ctlr &= ~(3 << 24);
	ncores = atomic_read(&vcpu->kvm->online_vcpus) - 1;
	l2ctlr |= (ncores & 3) << 24;

	vcpu->arch.cp15[c9_L2CTLR] = l2ctlr;
}

static void reset_actlr(struct kvm_vcpu *vcpu, const struct coproc_reg *r)
{
	u32 actlr;

	/* ACTLR contains SMP bit: make sure you create all cpus first! */
	asm volatile("mrc p15, 0, %0, c1, c0, 1\n" : "=r" (actlr));
	/* Make the SMP bit consistent with the guest configuration */
	if (atomic_read(&vcpu->kvm->online_vcpus) > 1)
		actlr |= 1U << 6;
	else
		actlr &= ~(1U << 6);

	vcpu->arch.cp15[c1_ACTLR] = actlr;
}

/* A15 TRM 4.3.49: R/O WI (even if NSACR.NS_L2ERR, a write of 1 is ignored). */
static bool access_l2ectlr(struct kvm_vcpu *vcpu,
			   const struct coproc_params *p,
			   const struct coproc_reg *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	*vcpu_reg(vcpu, p->Rt1) = 0;
	return true;
}

/*
 * A15-specific CP15 registers.
 * CRn denotes the primary register number, but is copied to the CRm in the
 * user space API for 64-bit register access in line with the terminology used
 * in the ARM ARM.
 * Important: Must be sorted ascending by CRn, CRM, Op1, Op2 and with 64-bit
 *            registers preceding 32-bit ones.
 */
static const struct coproc_reg a15_regs[] = {
	/* MPIDR: we use VMPIDR for guest access. */
	{ CRn( 0), CRm( 0), Op1( 0), Op2( 5), is32,
			NULL, reset_mpidr, c0_MPIDR },

	/* SCTLR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 0), is32,
			NULL, reset_val, c1_SCTLR, 0x00C50078 },
	/* ACTLR: trapped by HCR.TAC bit. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 1), is32,
			access_actlr, reset_actlr, c1_ACTLR },
	/* CPACR: swapped by interrupt.S. */
	{ CRn( 1), CRm( 0), Op1( 0), Op2( 2), is32,
			NULL, reset_val, c1_CPACR, 0x00000000 },

	/*
	 * L2CTLR access (guest wants to know #CPUs).
	 */
	{ CRn( 9), CRm( 0), Op1( 1), Op2( 2), is32,
			access_l2ctlr, reset_l2ctlr, c9_L2CTLR },
	{ CRn( 9), CRm( 0), Op1( 1), Op2( 3), is32, access_l2ectlr},

	/* The Configuration Base Address Register. */
	{ CRn(15), CRm( 0), Op1( 4), Op2( 0), is32, access_cbar},
};

static struct kvm_coproc_target_table a15_target_table = {
	.target = KVM_ARM_TARGET_CORTEX_A15,
	.table = a15_regs,
	.num = ARRAY_SIZE(a15_regs),
};

static int __init coproc_a15_init(void)
{
	unsigned int i;

	for (i = 1; i < ARRAY_SIZE(a15_regs); i++)
		BUG_ON(cmp_reg(&a15_regs[i-1],
			       &a15_regs[i]) >= 0);

	kvm_register_target_coproc_table(&a15_target_table);
	return 0;
}
late_initcall(coproc_a15_init);
