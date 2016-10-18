/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/coproc.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Rusty Russell <rusty@rustcorp.com.au>
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

#include <linux/bsearch.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_mmu.h>
#include <asm/perf_event.h>
#include <asm/sysreg.h>

#include <trace/events/kvm.h>

#include "sys_regs.h"

#include "trace.h"

/*
 * All of this file is extremly similar to the ARM coproc.c, but the
 * types are different. My gut feeling is that it should be pretty
 * easy to merge, but that would be an ABI breakage -- again. VFP
 * would also need to be abstracted.
 *
 * For AArch32, we only take care of what is being trapped. Anything
 * that has to do with init and userspace access has to go via the
 * 64bit interface.
 */

/* 3 bits per cache level, as per CLIDR, but non-existent caches always 0 */
static u32 cache_levels;

/* CSSELR values; used to index KVM_REG_ARM_DEMUX_ID_CCSIDR */
#define CSSELR_MAX 12

/* Which cache CCSIDR represents depends on CSSELR value. */
static u32 get_ccsidr(u32 csselr)
{
	u32 ccsidr;

	/* Make sure noone else changes CSSELR during this! */
	local_irq_disable();
	write_sysreg(csselr, csselr_el1);
	isb();
	ccsidr = read_sysreg(ccsidr_el1);
	local_irq_enable();

	return ccsidr;
}

/*
 * See note at ARMv7 ARM B1.14.4 (TL;DR: S/W ops are not easily virtualized).
 */
static bool access_dcsw(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (!p->is_write)
		return read_from_write_only(vcpu, p);

	kvm_set_way_flush(vcpu);
	return true;
}

/*
 * Generic accessor for VM registers. Only called as long as HCR_TVM
 * is set. If the guest enables the MMU, we stop trapping the VM
 * sys_regs and leave it in complete control of the caches.
 */
static bool access_vm_reg(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	bool was_enabled = vcpu_has_cache_enabled(vcpu);

	BUG_ON(!p->is_write);

	if (!p->is_aarch32) {
		vcpu_sys_reg(vcpu, r->reg) = p->regval;
	} else {
		if (!p->is_32bit)
			vcpu_cp15_64_high(vcpu, r->reg) = upper_32_bits(p->regval);
		vcpu_cp15_64_low(vcpu, r->reg) = lower_32_bits(p->regval);
	}

	kvm_toggle_cache(vcpu, was_enabled);
	return true;
}

/*
 * Trap handler for the GICv3 SGI generation system register.
 * Forward the request to the VGIC emulation.
 * The cp15_64 code makes sure this automatically works
 * for both AArch64 and AArch32 accesses.
 */
static bool access_gic_sgi(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (!p->is_write)
		return read_from_write_only(vcpu, p);

	vgic_v3_dispatch_sgi(vcpu, p->regval);

	return true;
}

static bool access_gic_sre(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);

	p->regval = vcpu->arch.vgic_cpu.vgic_v3.vgic_sre;
	return true;
}

static bool trap_raz_wi(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write)
		return ignore_write(vcpu, p);
	else
		return read_zero(vcpu, p);
}

static bool trap_oslsr_el1(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		p->regval = (1 << 3);
		return true;
	}
}

static bool trap_dbgauthstatus_el1(struct kvm_vcpu *vcpu,
				   struct sys_reg_params *p,
				   const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		p->regval = read_sysreg(dbgauthstatus_el1);
		return true;
	}
}

/*
 * We want to avoid world-switching all the DBG registers all the
 * time:
 * 
 * - If we've touched any debug register, it is likely that we're
 *   going to touch more of them. It then makes sense to disable the
 *   traps and start doing the save/restore dance
 * - If debug is active (DBG_MDSCR_KDE or DBG_MDSCR_MDE set), it is
 *   then mandatory to save/restore the registers, as the guest
 *   depends on them.
 * 
 * For this, we use a DIRTY bit, indicating the guest has modified the
 * debug registers, used as follow:
 *
 * On guest entry:
 * - If the dirty bit is set (because we're coming back from trapping),
 *   disable the traps, save host registers, restore guest registers.
 * - If debug is actively in use (DBG_MDSCR_KDE or DBG_MDSCR_MDE set),
 *   set the dirty bit, disable the traps, save host registers,
 *   restore guest registers.
 * - Otherwise, enable the traps
 *
 * On guest exit:
 * - If the dirty bit is set, save guest registers, restore host
 *   registers and clear the dirty bit. This ensure that the host can
 *   now use the debug registers.
 */
static bool trap_debug_regs(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	if (p->is_write) {
		vcpu_sys_reg(vcpu, r->reg) = p->regval;
		vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;
	} else {
		p->regval = vcpu_sys_reg(vcpu, r->reg);
	}

	trace_trap_reg(__func__, r->reg, p->is_write, p->regval);

	return true;
}

/*
 * reg_to_dbg/dbg_to_reg
 *
 * A 32 bit write to a debug register leave top bits alone
 * A 32 bit read from a debug register only returns the bottom bits
 *
 * All writes will set the KVM_ARM64_DEBUG_DIRTY flag to ensure the
 * hyp.S code switches between host and guest values in future.
 */
static void reg_to_dbg(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       u64 *dbg_reg)
{
	u64 val = p->regval;

	if (p->is_32bit) {
		val &= 0xffffffffUL;
		val |= ((*dbg_reg >> 32) << 32);
	}

	*dbg_reg = val;
	vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;
}

static void dbg_to_reg(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       u64 *dbg_reg)
{
	p->regval = *dbg_reg;
	if (p->is_32bit)
		p->regval &= 0xffffffffUL;
}

static bool trap_bvr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_bvr[rd->reg];

	if (p->is_write)
		reg_to_dbg(vcpu, p, dbg_reg);
	else
		dbg_to_reg(vcpu, p, dbg_reg);

	trace_trap_reg(__func__, rd->reg, p->is_write, *dbg_reg);

	return true;
}

static int set_bvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_bvr[rd->reg];

	if (copy_from_user(r, uaddr, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_bvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
	const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_bvr[rd->reg];

	if (copy_to_user(uaddr, r, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static void reset_bvr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_bvr[rd->reg] = rd->val;
}

static bool trap_bcr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_bcr[rd->reg];

	if (p->is_write)
		reg_to_dbg(vcpu, p, dbg_reg);
	else
		dbg_to_reg(vcpu, p, dbg_reg);

	trace_trap_reg(__func__, rd->reg, p->is_write, *dbg_reg);

	return true;
}

static int set_bcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_bcr[rd->reg];

	if (copy_from_user(r, uaddr, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;

	return 0;
}

static int get_bcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
	const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_bcr[rd->reg];

	if (copy_to_user(uaddr, r, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static void reset_bcr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_bcr[rd->reg] = rd->val;
}

static bool trap_wvr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_wvr[rd->reg];

	if (p->is_write)
		reg_to_dbg(vcpu, p, dbg_reg);
	else
		dbg_to_reg(vcpu, p, dbg_reg);

	trace_trap_reg(__func__, rd->reg, p->is_write,
		vcpu->arch.vcpu_debug_state.dbg_wvr[rd->reg]);

	return true;
}

static int set_wvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_wvr[rd->reg];

	if (copy_from_user(r, uaddr, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_wvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
	const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_wvr[rd->reg];

	if (copy_to_user(uaddr, r, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static void reset_wvr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_wvr[rd->reg] = rd->val;
}

static bool trap_wcr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_wcr[rd->reg];

	if (p->is_write)
		reg_to_dbg(vcpu, p, dbg_reg);
	else
		dbg_to_reg(vcpu, p, dbg_reg);

	trace_trap_reg(__func__, rd->reg, p->is_write, *dbg_reg);

	return true;
}

static int set_wcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_wcr[rd->reg];

	if (copy_from_user(r, uaddr, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_wcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
	const struct kvm_one_reg *reg, void __user *uaddr)
{
	__u64 *r = &vcpu->arch.vcpu_debug_state.dbg_wcr[rd->reg];

	if (copy_to_user(uaddr, r, KVM_REG_SIZE(reg->id)) != 0)
		return -EFAULT;
	return 0;
}

static void reset_wcr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_wcr[rd->reg] = rd->val;
}

static void reset_amair_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	vcpu_sys_reg(vcpu, AMAIR_EL1) = read_sysreg(amair_el1);
}

static void reset_mpidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 mpidr;

	/*
	 * Map the vcpu_id into the first three affinity level fields of
	 * the MPIDR. We limit the number of VCPUs in level 0 due to a
	 * limitation to 16 CPUs in that level in the ICC_SGIxR registers
	 * of the GICv3 to be able to address each CPU directly when
	 * sending IPIs.
	 */
	mpidr = (vcpu->vcpu_id & 0x0f) << MPIDR_LEVEL_SHIFT(0);
	mpidr |= ((vcpu->vcpu_id >> 4) & 0xff) << MPIDR_LEVEL_SHIFT(1);
	mpidr |= ((vcpu->vcpu_id >> 12) & 0xff) << MPIDR_LEVEL_SHIFT(2);
	vcpu_sys_reg(vcpu, MPIDR_EL1) = (1ULL << 31) | mpidr;
}

static void reset_pmcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 pmcr, val;

	pmcr = read_sysreg(pmcr_el0);
	/*
	 * Writable bits of PMCR_EL0 (ARMV8_PMU_PMCR_MASK) are reset to UNKNOWN
	 * except PMCR.E resetting to zero.
	 */
	val = ((pmcr & ~ARMV8_PMU_PMCR_MASK)
	       | (ARMV8_PMU_PMCR_MASK & 0xdecafbad)) & (~ARMV8_PMU_PMCR_E);
	vcpu_sys_reg(vcpu, PMCR_EL0) = val;
}

static bool pmu_access_el0_disabled(struct kvm_vcpu *vcpu)
{
	u64 reg = vcpu_sys_reg(vcpu, PMUSERENR_EL0);

	return !((reg & ARMV8_PMU_USERENR_EN) || vcpu_mode_priv(vcpu));
}

static bool pmu_write_swinc_el0_disabled(struct kvm_vcpu *vcpu)
{
	u64 reg = vcpu_sys_reg(vcpu, PMUSERENR_EL0);

	return !((reg & (ARMV8_PMU_USERENR_SW | ARMV8_PMU_USERENR_EN))
		 || vcpu_mode_priv(vcpu));
}

static bool pmu_access_cycle_counter_el0_disabled(struct kvm_vcpu *vcpu)
{
	u64 reg = vcpu_sys_reg(vcpu, PMUSERENR_EL0);

	return !((reg & (ARMV8_PMU_USERENR_CR | ARMV8_PMU_USERENR_EN))
		 || vcpu_mode_priv(vcpu));
}

static bool pmu_access_event_counter_el0_disabled(struct kvm_vcpu *vcpu)
{
	u64 reg = vcpu_sys_reg(vcpu, PMUSERENR_EL0);

	return !((reg & (ARMV8_PMU_USERENR_ER | ARMV8_PMU_USERENR_EN))
		 || vcpu_mode_priv(vcpu));
}

static bool access_pmcr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	u64 val;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		/* Only update writeable bits of PMCR */
		val = vcpu_sys_reg(vcpu, PMCR_EL0);
		val &= ~ARMV8_PMU_PMCR_MASK;
		val |= p->regval & ARMV8_PMU_PMCR_MASK;
		vcpu_sys_reg(vcpu, PMCR_EL0) = val;
		kvm_pmu_handle_pmcr(vcpu, val);
	} else {
		/* PMCR.P & PMCR.C are RAZ */
		val = vcpu_sys_reg(vcpu, PMCR_EL0)
		      & ~(ARMV8_PMU_PMCR_P | ARMV8_PMU_PMCR_C);
		p->regval = val;
	}

	return true;
}

static bool access_pmselr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_event_counter_el0_disabled(vcpu))
		return false;

	if (p->is_write)
		vcpu_sys_reg(vcpu, PMSELR_EL0) = p->regval;
	else
		/* return PMSELR.SEL field */
		p->regval = vcpu_sys_reg(vcpu, PMSELR_EL0)
			    & ARMV8_PMU_COUNTER_MASK;

	return true;
}

static bool access_pmceid(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u64 pmceid;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	BUG_ON(p->is_write);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (!(p->Op2 & 1))
		pmceid = read_sysreg(pmceid0_el0);
	else
		pmceid = read_sysreg(pmceid1_el0);

	p->regval = pmceid;

	return true;
}

static bool pmu_counter_idx_valid(struct kvm_vcpu *vcpu, u64 idx)
{
	u64 pmcr, val;

	pmcr = vcpu_sys_reg(vcpu, PMCR_EL0);
	val = (pmcr >> ARMV8_PMU_PMCR_N_SHIFT) & ARMV8_PMU_PMCR_N_MASK;
	if (idx >= val && idx != ARMV8_PMU_CYCLE_IDX)
		return false;

	return true;
}

static bool access_pmu_evcntr(struct kvm_vcpu *vcpu,
			      struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	u64 idx;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (r->CRn == 9 && r->CRm == 13) {
		if (r->Op2 == 2) {
			/* PMXEVCNTR_EL0 */
			if (pmu_access_event_counter_el0_disabled(vcpu))
				return false;

			idx = vcpu_sys_reg(vcpu, PMSELR_EL0)
			      & ARMV8_PMU_COUNTER_MASK;
		} else if (r->Op2 == 0) {
			/* PMCCNTR_EL0 */
			if (pmu_access_cycle_counter_el0_disabled(vcpu))
				return false;

			idx = ARMV8_PMU_CYCLE_IDX;
		} else {
			BUG();
		}
	} else if (r->CRn == 14 && (r->CRm & 12) == 8) {
		/* PMEVCNTRn_EL0 */
		if (pmu_access_event_counter_el0_disabled(vcpu))
			return false;

		idx = ((r->CRm & 3) << 3) | (r->Op2 & 7);
	} else {
		BUG();
	}

	if (!pmu_counter_idx_valid(vcpu, idx))
		return false;

	if (p->is_write) {
		if (pmu_access_el0_disabled(vcpu))
			return false;

		kvm_pmu_set_counter_value(vcpu, idx, p->regval);
	} else {
		p->regval = kvm_pmu_get_counter_value(vcpu, idx);
	}

	return true;
}

static bool access_pmu_evtyper(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			       const struct sys_reg_desc *r)
{
	u64 idx, reg;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (r->CRn == 9 && r->CRm == 13 && r->Op2 == 1) {
		/* PMXEVTYPER_EL0 */
		idx = vcpu_sys_reg(vcpu, PMSELR_EL0) & ARMV8_PMU_COUNTER_MASK;
		reg = PMEVTYPER0_EL0 + idx;
	} else if (r->CRn == 14 && (r->CRm & 12) == 12) {
		idx = ((r->CRm & 3) << 3) | (r->Op2 & 7);
		if (idx == ARMV8_PMU_CYCLE_IDX)
			reg = PMCCFILTR_EL0;
		else
			/* PMEVTYPERn_EL0 */
			reg = PMEVTYPER0_EL0 + idx;
	} else {
		BUG();
	}

	if (!pmu_counter_idx_valid(vcpu, idx))
		return false;

	if (p->is_write) {
		kvm_pmu_set_counter_event_type(vcpu, p->regval, idx);
		vcpu_sys_reg(vcpu, reg) = p->regval & ARMV8_PMU_EVTYPE_MASK;
	} else {
		p->regval = vcpu_sys_reg(vcpu, reg) & ARMV8_PMU_EVTYPE_MASK;
	}

	return true;
}

static bool access_pmcnten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 val, mask;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	mask = kvm_pmu_valid_counter_mask(vcpu);
	if (p->is_write) {
		val = p->regval & mask;
		if (r->Op2 & 0x1) {
			/* accessing PMCNTENSET_EL0 */
			vcpu_sys_reg(vcpu, PMCNTENSET_EL0) |= val;
			kvm_pmu_enable_counter(vcpu, val);
		} else {
			/* accessing PMCNTENCLR_EL0 */
			vcpu_sys_reg(vcpu, PMCNTENSET_EL0) &= ~val;
			kvm_pmu_disable_counter(vcpu, val);
		}
	} else {
		p->regval = vcpu_sys_reg(vcpu, PMCNTENSET_EL0) & mask;
	}

	return true;
}

static bool access_pminten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (!vcpu_mode_priv(vcpu))
		return false;

	if (p->is_write) {
		u64 val = p->regval & mask;

		if (r->Op2 & 0x1)
			/* accessing PMINTENSET_EL1 */
			vcpu_sys_reg(vcpu, PMINTENSET_EL1) |= val;
		else
			/* accessing PMINTENCLR_EL1 */
			vcpu_sys_reg(vcpu, PMINTENSET_EL1) &= ~val;
	} else {
		p->regval = vcpu_sys_reg(vcpu, PMINTENSET_EL1) & mask;
	}

	return true;
}

static bool access_pmovs(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		if (r->CRm & 0x2)
			/* accessing PMOVSSET_EL0 */
			kvm_pmu_overflow_set(vcpu, p->regval & mask);
		else
			/* accessing PMOVSCLR_EL0 */
			vcpu_sys_reg(vcpu, PMOVSSET_EL0) &= ~(p->regval & mask);
	} else {
		p->regval = vcpu_sys_reg(vcpu, PMOVSSET_EL0) & mask;
	}

	return true;
}

static bool access_pmswinc(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_write_swinc_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		mask = kvm_pmu_valid_counter_mask(vcpu);
		kvm_pmu_software_increment(vcpu, p->regval & mask);
		return true;
	}

	return false;
}

static bool access_pmuserenr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (p->is_write) {
		if (!vcpu_mode_priv(vcpu))
			return false;

		vcpu_sys_reg(vcpu, PMUSERENR_EL0) = p->regval
						    & ARMV8_PMU_USERENR_MASK;
	} else {
		p->regval = vcpu_sys_reg(vcpu, PMUSERENR_EL0)
			    & ARMV8_PMU_USERENR_MASK;
	}

	return true;
}

/* Silly macro to expand the DBG{BCR,BVR,WVR,WCR}n_EL1 registers in one go */
#define DBG_BCR_BVR_WCR_WVR_EL1(n)					\
	/* DBGBVRn_EL1 */						\
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm((n)), Op2(0b100),	\
	  trap_bvr, reset_bvr, n, 0, get_bvr, set_bvr },		\
	/* DBGBCRn_EL1 */						\
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm((n)), Op2(0b101),	\
	  trap_bcr, reset_bcr, n, 0, get_bcr, set_bcr },		\
	/* DBGWVRn_EL1 */						\
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm((n)), Op2(0b110),	\
	  trap_wvr, reset_wvr, n, 0,  get_wvr, set_wvr },		\
	/* DBGWCRn_EL1 */						\
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm((n)), Op2(0b111),	\
	  trap_wcr, reset_wcr, n, 0,  get_wcr, set_wcr }

/* Macro to expand the PMEVCNTRn_EL0 register */
#define PMU_PMEVCNTR_EL0(n)						\
	/* PMEVCNTRn_EL0 */						\
	{ Op0(0b11), Op1(0b011), CRn(0b1110),				\
	  CRm((0b1000 | (((n) >> 3) & 0x3))), Op2(((n) & 0x7)),		\
	  access_pmu_evcntr, reset_unknown, (PMEVCNTR0_EL0 + n), }

/* Macro to expand the PMEVTYPERn_EL0 register */
#define PMU_PMEVTYPER_EL0(n)						\
	/* PMEVTYPERn_EL0 */						\
	{ Op0(0b11), Op1(0b011), CRn(0b1110),				\
	  CRm((0b1100 | (((n) >> 3) & 0x3))), Op2(((n) & 0x7)),		\
	  access_pmu_evtyper, reset_unknown, (PMEVTYPER0_EL0 + n), }

/*
 * Architected system registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 *
 * Debug handling: We do trap most, if not all debug related system
 * registers. The implementation is good enough to ensure that a guest
 * can use these with minimal performance degradation. The drawback is
 * that we don't implement any of the external debug, none of the
 * OSlock protocol. This should be revisited if we ever encounter a
 * more demanding guest...
 */
static const struct sys_reg_desc sys_reg_descs[] = {
	/* DC ISW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b0110), Op2(0b010),
	  access_dcsw },
	/* DC CSW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b1010), Op2(0b010),
	  access_dcsw },
	/* DC CISW */
	{ Op0(0b01), Op1(0b000), CRn(0b0111), CRm(0b1110), Op2(0b010),
	  access_dcsw },

	DBG_BCR_BVR_WCR_WVR_EL1(0),
	DBG_BCR_BVR_WCR_WVR_EL1(1),
	/* MDCCINT_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b000),
	  trap_debug_regs, reset_val, MDCCINT_EL1, 0 },
	/* MDSCR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b010),
	  trap_debug_regs, reset_val, MDSCR_EL1, 0 },
	DBG_BCR_BVR_WCR_WVR_EL1(2),
	DBG_BCR_BVR_WCR_WVR_EL1(3),
	DBG_BCR_BVR_WCR_WVR_EL1(4),
	DBG_BCR_BVR_WCR_WVR_EL1(5),
	DBG_BCR_BVR_WCR_WVR_EL1(6),
	DBG_BCR_BVR_WCR_WVR_EL1(7),
	DBG_BCR_BVR_WCR_WVR_EL1(8),
	DBG_BCR_BVR_WCR_WVR_EL1(9),
	DBG_BCR_BVR_WCR_WVR_EL1(10),
	DBG_BCR_BVR_WCR_WVR_EL1(11),
	DBG_BCR_BVR_WCR_WVR_EL1(12),
	DBG_BCR_BVR_WCR_WVR_EL1(13),
	DBG_BCR_BVR_WCR_WVR_EL1(14),
	DBG_BCR_BVR_WCR_WVR_EL1(15),

	/* MDRAR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b000),
	  trap_raz_wi },
	/* OSLAR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b100),
	  trap_raz_wi },
	/* OSLSR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0001), CRm(0b0001), Op2(0b100),
	  trap_oslsr_el1 },
	/* OSDLR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0001), CRm(0b0011), Op2(0b100),
	  trap_raz_wi },
	/* DBGPRCR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0001), CRm(0b0100), Op2(0b100),
	  trap_raz_wi },
	/* DBGCLAIMSET_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0111), CRm(0b1000), Op2(0b110),
	  trap_raz_wi },
	/* DBGCLAIMCLR_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0111), CRm(0b1001), Op2(0b110),
	  trap_raz_wi },
	/* DBGAUTHSTATUS_EL1 */
	{ Op0(0b10), Op1(0b000), CRn(0b0111), CRm(0b1110), Op2(0b110),
	  trap_dbgauthstatus_el1 },

	/* MDCCSR_EL1 */
	{ Op0(0b10), Op1(0b011), CRn(0b0000), CRm(0b0001), Op2(0b000),
	  trap_raz_wi },
	/* DBGDTR_EL0 */
	{ Op0(0b10), Op1(0b011), CRn(0b0000), CRm(0b0100), Op2(0b000),
	  trap_raz_wi },
	/* DBGDTR[TR]X_EL0 */
	{ Op0(0b10), Op1(0b011), CRn(0b0000), CRm(0b0101), Op2(0b000),
	  trap_raz_wi },

	/* DBGVCR32_EL2 */
	{ Op0(0b10), Op1(0b100), CRn(0b0000), CRm(0b0111), Op2(0b000),
	  NULL, reset_val, DBGVCR32_EL2, 0 },

	/* MPIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b101),
	  NULL, reset_mpidr, MPIDR_EL1 },
	/* SCTLR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b000),
	  access_vm_reg, reset_val, SCTLR_EL1, 0x00C50078 },
	/* CPACR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0001), CRm(0b0000), Op2(0b010),
	  NULL, reset_val, CPACR_EL1, 0 },
	/* TTBR0_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b000),
	  access_vm_reg, reset_unknown, TTBR0_EL1 },
	/* TTBR1_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b001),
	  access_vm_reg, reset_unknown, TTBR1_EL1 },
	/* TCR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0010), CRm(0b0000), Op2(0b010),
	  access_vm_reg, reset_val, TCR_EL1, 0 },

	/* AFSR0_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0001), Op2(0b000),
	  access_vm_reg, reset_unknown, AFSR0_EL1 },
	/* AFSR1_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0001), Op2(0b001),
	  access_vm_reg, reset_unknown, AFSR1_EL1 },
	/* ESR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0101), CRm(0b0010), Op2(0b000),
	  access_vm_reg, reset_unknown, ESR_EL1 },
	/* FAR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0110), CRm(0b0000), Op2(0b000),
	  access_vm_reg, reset_unknown, FAR_EL1 },
	/* PAR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b0111), CRm(0b0100), Op2(0b000),
	  NULL, reset_unknown, PAR_EL1 },

	/* PMINTENSET_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1001), CRm(0b1110), Op2(0b001),
	  access_pminten, reset_unknown, PMINTENSET_EL1 },
	/* PMINTENCLR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1001), CRm(0b1110), Op2(0b010),
	  access_pminten, NULL, PMINTENSET_EL1 },

	/* MAIR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1010), CRm(0b0010), Op2(0b000),
	  access_vm_reg, reset_unknown, MAIR_EL1 },
	/* AMAIR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1010), CRm(0b0011), Op2(0b000),
	  access_vm_reg, reset_amair_el1, AMAIR_EL1 },

	/* VBAR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1100), CRm(0b0000), Op2(0b000),
	  NULL, reset_val, VBAR_EL1, 0 },

	/* ICC_SGI1R_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1100), CRm(0b1011), Op2(0b101),
	  access_gic_sgi },
	/* ICC_SRE_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1100), CRm(0b1100), Op2(0b101),
	  access_gic_sre },

	/* CONTEXTIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1101), CRm(0b0000), Op2(0b001),
	  access_vm_reg, reset_val, CONTEXTIDR_EL1, 0 },
	/* TPIDR_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1101), CRm(0b0000), Op2(0b100),
	  NULL, reset_unknown, TPIDR_EL1 },

	/* CNTKCTL_EL1 */
	{ Op0(0b11), Op1(0b000), CRn(0b1110), CRm(0b0001), Op2(0b000),
	  NULL, reset_val, CNTKCTL_EL1, 0},

	/* CSSELR_EL1 */
	{ Op0(0b11), Op1(0b010), CRn(0b0000), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, CSSELR_EL1 },

	/* PMCR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b000),
	  access_pmcr, reset_pmcr, },
	/* PMCNTENSET_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b001),
	  access_pmcnten, reset_unknown, PMCNTENSET_EL0 },
	/* PMCNTENCLR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b010),
	  access_pmcnten, NULL, PMCNTENSET_EL0 },
	/* PMOVSCLR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b011),
	  access_pmovs, NULL, PMOVSSET_EL0 },
	/* PMSWINC_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b100),
	  access_pmswinc, reset_unknown, PMSWINC_EL0 },
	/* PMSELR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b101),
	  access_pmselr, reset_unknown, PMSELR_EL0 },
	/* PMCEID0_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b110),
	  access_pmceid },
	/* PMCEID1_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1100), Op2(0b111),
	  access_pmceid },
	/* PMCCNTR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b000),
	  access_pmu_evcntr, reset_unknown, PMCCNTR_EL0 },
	/* PMXEVTYPER_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b001),
	  access_pmu_evtyper },
	/* PMXEVCNTR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1101), Op2(0b010),
	  access_pmu_evcntr },
	/* PMUSERENR_EL0
	 * This register resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1110), Op2(0b000),
	  access_pmuserenr, reset_val, PMUSERENR_EL0, 0 },
	/* PMOVSSET_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1001), CRm(0b1110), Op2(0b011),
	  access_pmovs, reset_unknown, PMOVSSET_EL0 },

	/* TPIDR_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1101), CRm(0b0000), Op2(0b010),
	  NULL, reset_unknown, TPIDR_EL0 },
	/* TPIDRRO_EL0 */
	{ Op0(0b11), Op1(0b011), CRn(0b1101), CRm(0b0000), Op2(0b011),
	  NULL, reset_unknown, TPIDRRO_EL0 },

	/* PMEVCNTRn_EL0 */
	PMU_PMEVCNTR_EL0(0),
	PMU_PMEVCNTR_EL0(1),
	PMU_PMEVCNTR_EL0(2),
	PMU_PMEVCNTR_EL0(3),
	PMU_PMEVCNTR_EL0(4),
	PMU_PMEVCNTR_EL0(5),
	PMU_PMEVCNTR_EL0(6),
	PMU_PMEVCNTR_EL0(7),
	PMU_PMEVCNTR_EL0(8),
	PMU_PMEVCNTR_EL0(9),
	PMU_PMEVCNTR_EL0(10),
	PMU_PMEVCNTR_EL0(11),
	PMU_PMEVCNTR_EL0(12),
	PMU_PMEVCNTR_EL0(13),
	PMU_PMEVCNTR_EL0(14),
	PMU_PMEVCNTR_EL0(15),
	PMU_PMEVCNTR_EL0(16),
	PMU_PMEVCNTR_EL0(17),
	PMU_PMEVCNTR_EL0(18),
	PMU_PMEVCNTR_EL0(19),
	PMU_PMEVCNTR_EL0(20),
	PMU_PMEVCNTR_EL0(21),
	PMU_PMEVCNTR_EL0(22),
	PMU_PMEVCNTR_EL0(23),
	PMU_PMEVCNTR_EL0(24),
	PMU_PMEVCNTR_EL0(25),
	PMU_PMEVCNTR_EL0(26),
	PMU_PMEVCNTR_EL0(27),
	PMU_PMEVCNTR_EL0(28),
	PMU_PMEVCNTR_EL0(29),
	PMU_PMEVCNTR_EL0(30),
	/* PMEVTYPERn_EL0 */
	PMU_PMEVTYPER_EL0(0),
	PMU_PMEVTYPER_EL0(1),
	PMU_PMEVTYPER_EL0(2),
	PMU_PMEVTYPER_EL0(3),
	PMU_PMEVTYPER_EL0(4),
	PMU_PMEVTYPER_EL0(5),
	PMU_PMEVTYPER_EL0(6),
	PMU_PMEVTYPER_EL0(7),
	PMU_PMEVTYPER_EL0(8),
	PMU_PMEVTYPER_EL0(9),
	PMU_PMEVTYPER_EL0(10),
	PMU_PMEVTYPER_EL0(11),
	PMU_PMEVTYPER_EL0(12),
	PMU_PMEVTYPER_EL0(13),
	PMU_PMEVTYPER_EL0(14),
	PMU_PMEVTYPER_EL0(15),
	PMU_PMEVTYPER_EL0(16),
	PMU_PMEVTYPER_EL0(17),
	PMU_PMEVTYPER_EL0(18),
	PMU_PMEVTYPER_EL0(19),
	PMU_PMEVTYPER_EL0(20),
	PMU_PMEVTYPER_EL0(21),
	PMU_PMEVTYPER_EL0(22),
	PMU_PMEVTYPER_EL0(23),
	PMU_PMEVTYPER_EL0(24),
	PMU_PMEVTYPER_EL0(25),
	PMU_PMEVTYPER_EL0(26),
	PMU_PMEVTYPER_EL0(27),
	PMU_PMEVTYPER_EL0(28),
	PMU_PMEVTYPER_EL0(29),
	PMU_PMEVTYPER_EL0(30),
	/* PMCCFILTR_EL0
	 * This register resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ Op0(0b11), Op1(0b011), CRn(0b1110), CRm(0b1111), Op2(0b111),
	  access_pmu_evtyper, reset_val, PMCCFILTR_EL0, 0 },

	/* DACR32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0011), CRm(0b0000), Op2(0b000),
	  NULL, reset_unknown, DACR32_EL2 },
	/* IFSR32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0101), CRm(0b0000), Op2(0b001),
	  NULL, reset_unknown, IFSR32_EL2 },
	/* FPEXC32_EL2 */
	{ Op0(0b11), Op1(0b100), CRn(0b0101), CRm(0b0011), Op2(0b000),
	  NULL, reset_val, FPEXC32_EL2, 0x70 },
};

static bool trap_dbgidr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		u64 dfr = read_system_reg(SYS_ID_AA64DFR0_EL1);
		u64 pfr = read_system_reg(SYS_ID_AA64PFR0_EL1);
		u32 el3 = !!cpuid_feature_extract_unsigned_field(pfr, ID_AA64PFR0_EL3_SHIFT);

		p->regval = ((((dfr >> ID_AA64DFR0_WRPS_SHIFT) & 0xf) << 28) |
			     (((dfr >> ID_AA64DFR0_BRPS_SHIFT) & 0xf) << 24) |
			     (((dfr >> ID_AA64DFR0_CTX_CMPS_SHIFT) & 0xf) << 20)
			     | (6 << 16) | (el3 << 14) | (el3 << 12));
		return true;
	}
}

static bool trap_debug32(struct kvm_vcpu *vcpu,
			 struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	if (p->is_write) {
		vcpu_cp14(vcpu, r->reg) = p->regval;
		vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;
	} else {
		p->regval = vcpu_cp14(vcpu, r->reg);
	}

	return true;
}

/* AArch32 debug register mappings
 *
 * AArch32 DBGBVRn is mapped to DBGBVRn_EL1[31:0]
 * AArch32 DBGBXVRn is mapped to DBGBVRn_EL1[63:32]
 *
 * All control registers and watchpoint value registers are mapped to
 * the lower 32 bits of their AArch64 equivalents. We share the trap
 * handlers with the above AArch64 code which checks what mode the
 * system is in.
 */

static bool trap_xvr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_bvr[rd->reg];

	if (p->is_write) {
		u64 val = *dbg_reg;

		val &= 0xffffffffUL;
		val |= p->regval << 32;
		*dbg_reg = val;

		vcpu->arch.debug_flags |= KVM_ARM64_DEBUG_DIRTY;
	} else {
		p->regval = *dbg_reg >> 32;
	}

	trace_trap_reg(__func__, rd->reg, p->is_write, *dbg_reg);

	return true;
}

#define DBG_BCR_BVR_WCR_WVR(n)						\
	/* DBGBVRn */							\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 4), trap_bvr, NULL, n }, 	\
	/* DBGBCRn */							\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 5), trap_bcr, NULL, n },	\
	/* DBGWVRn */							\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 6), trap_wvr, NULL, n },	\
	/* DBGWCRn */							\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 7), trap_wcr, NULL, n }

#define DBGBXVR(n)							\
	{ Op1( 0), CRn( 1), CRm((n)), Op2( 1), trap_xvr, NULL, n }

/*
 * Trapped cp14 registers. We generally ignore most of the external
 * debug, on the principle that they don't really make sense to a
 * guest. Revisit this one day, would this principle change.
 */
static const struct sys_reg_desc cp14_regs[] = {
	/* DBGIDR */
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 0), trap_dbgidr },
	/* DBGDTRRXext */
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 2), trap_raz_wi },

	DBG_BCR_BVR_WCR_WVR(0),
	/* DBGDSCRint */
	{ Op1( 0), CRn( 0), CRm( 1), Op2( 0), trap_raz_wi },
	DBG_BCR_BVR_WCR_WVR(1),
	/* DBGDCCINT */
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 0), trap_debug32 },
	/* DBGDSCRext */
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 2), trap_debug32 },
	DBG_BCR_BVR_WCR_WVR(2),
	/* DBGDTR[RT]Xint */
	{ Op1( 0), CRn( 0), CRm( 3), Op2( 0), trap_raz_wi },
	/* DBGDTR[RT]Xext */
	{ Op1( 0), CRn( 0), CRm( 3), Op2( 2), trap_raz_wi },
	DBG_BCR_BVR_WCR_WVR(3),
	DBG_BCR_BVR_WCR_WVR(4),
	DBG_BCR_BVR_WCR_WVR(5),
	/* DBGWFAR */
	{ Op1( 0), CRn( 0), CRm( 6), Op2( 0), trap_raz_wi },
	/* DBGOSECCR */
	{ Op1( 0), CRn( 0), CRm( 6), Op2( 2), trap_raz_wi },
	DBG_BCR_BVR_WCR_WVR(6),
	/* DBGVCR */
	{ Op1( 0), CRn( 0), CRm( 7), Op2( 0), trap_debug32 },
	DBG_BCR_BVR_WCR_WVR(7),
	DBG_BCR_BVR_WCR_WVR(8),
	DBG_BCR_BVR_WCR_WVR(9),
	DBG_BCR_BVR_WCR_WVR(10),
	DBG_BCR_BVR_WCR_WVR(11),
	DBG_BCR_BVR_WCR_WVR(12),
	DBG_BCR_BVR_WCR_WVR(13),
	DBG_BCR_BVR_WCR_WVR(14),
	DBG_BCR_BVR_WCR_WVR(15),

	/* DBGDRAR (32bit) */
	{ Op1( 0), CRn( 1), CRm( 0), Op2( 0), trap_raz_wi },

	DBGBXVR(0),
	/* DBGOSLAR */
	{ Op1( 0), CRn( 1), CRm( 0), Op2( 4), trap_raz_wi },
	DBGBXVR(1),
	/* DBGOSLSR */
	{ Op1( 0), CRn( 1), CRm( 1), Op2( 4), trap_oslsr_el1 },
	DBGBXVR(2),
	DBGBXVR(3),
	/* DBGOSDLR */
	{ Op1( 0), CRn( 1), CRm( 3), Op2( 4), trap_raz_wi },
	DBGBXVR(4),
	/* DBGPRCR */
	{ Op1( 0), CRn( 1), CRm( 4), Op2( 4), trap_raz_wi },
	DBGBXVR(5),
	DBGBXVR(6),
	DBGBXVR(7),
	DBGBXVR(8),
	DBGBXVR(9),
	DBGBXVR(10),
	DBGBXVR(11),
	DBGBXVR(12),
	DBGBXVR(13),
	DBGBXVR(14),
	DBGBXVR(15),

	/* DBGDSAR (32bit) */
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 0), trap_raz_wi },

	/* DBGDEVID2 */
	{ Op1( 0), CRn( 7), CRm( 0), Op2( 7), trap_raz_wi },
	/* DBGDEVID1 */
	{ Op1( 0), CRn( 7), CRm( 1), Op2( 7), trap_raz_wi },
	/* DBGDEVID */
	{ Op1( 0), CRn( 7), CRm( 2), Op2( 7), trap_raz_wi },
	/* DBGCLAIMSET */
	{ Op1( 0), CRn( 7), CRm( 8), Op2( 6), trap_raz_wi },
	/* DBGCLAIMCLR */
	{ Op1( 0), CRn( 7), CRm( 9), Op2( 6), trap_raz_wi },
	/* DBGAUTHSTATUS */
	{ Op1( 0), CRn( 7), CRm(14), Op2( 6), trap_dbgauthstatus_el1 },
};

/* Trapped cp14 64bit registers */
static const struct sys_reg_desc cp14_64_regs[] = {
	/* DBGDRAR (64bit) */
	{ Op1( 0), CRm( 1), .access = trap_raz_wi },

	/* DBGDSAR (64bit) */
	{ Op1( 0), CRm( 2), .access = trap_raz_wi },
};

/* Macro to expand the PMEVCNTRn register */
#define PMU_PMEVCNTR(n)							\
	/* PMEVCNTRn */							\
	{ Op1(0), CRn(0b1110),						\
	  CRm((0b1000 | (((n) >> 3) & 0x3))), Op2(((n) & 0x7)),		\
	  access_pmu_evcntr }

/* Macro to expand the PMEVTYPERn register */
#define PMU_PMEVTYPER(n)						\
	/* PMEVTYPERn */						\
	{ Op1(0), CRn(0b1110),						\
	  CRm((0b1100 | (((n) >> 3) & 0x3))), Op2(((n) & 0x7)),		\
	  access_pmu_evtyper }

/*
 * Trapped cp15 registers. TTBR0/TTBR1 get a double encoding,
 * depending on the way they are accessed (as a 32bit or a 64bit
 * register).
 */
static const struct sys_reg_desc cp15_regs[] = {
	{ Op1( 0), CRn( 0), CRm(12), Op2( 0), access_gic_sgi },

	{ Op1( 0), CRn( 1), CRm( 0), Op2( 0), access_vm_reg, NULL, c1_SCTLR },
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 0), access_vm_reg, NULL, c2_TTBR0 },
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 1), access_vm_reg, NULL, c2_TTBR1 },
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 2), access_vm_reg, NULL, c2_TTBCR },
	{ Op1( 0), CRn( 3), CRm( 0), Op2( 0), access_vm_reg, NULL, c3_DACR },
	{ Op1( 0), CRn( 5), CRm( 0), Op2( 0), access_vm_reg, NULL, c5_DFSR },
	{ Op1( 0), CRn( 5), CRm( 0), Op2( 1), access_vm_reg, NULL, c5_IFSR },
	{ Op1( 0), CRn( 5), CRm( 1), Op2( 0), access_vm_reg, NULL, c5_ADFSR },
	{ Op1( 0), CRn( 5), CRm( 1), Op2( 1), access_vm_reg, NULL, c5_AIFSR },
	{ Op1( 0), CRn( 6), CRm( 0), Op2( 0), access_vm_reg, NULL, c6_DFAR },
	{ Op1( 0), CRn( 6), CRm( 0), Op2( 2), access_vm_reg, NULL, c6_IFAR },

	/*
	 * DC{C,I,CI}SW operations:
	 */
	{ Op1( 0), CRn( 7), CRm( 6), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(10), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(14), Op2( 2), access_dcsw },

	/* PMU */
	{ Op1( 0), CRn( 9), CRm(12), Op2( 0), access_pmcr },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 1), access_pmcnten },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 2), access_pmcnten },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 3), access_pmovs },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 4), access_pmswinc },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 5), access_pmselr },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 6), access_pmceid },
	{ Op1( 0), CRn( 9), CRm(12), Op2( 7), access_pmceid },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 0), access_pmu_evcntr },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 1), access_pmu_evtyper },
	{ Op1( 0), CRn( 9), CRm(13), Op2( 2), access_pmu_evcntr },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 0), access_pmuserenr },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 1), access_pminten },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 2), access_pminten },
	{ Op1( 0), CRn( 9), CRm(14), Op2( 3), access_pmovs },

	{ Op1( 0), CRn(10), CRm( 2), Op2( 0), access_vm_reg, NULL, c10_PRRR },
	{ Op1( 0), CRn(10), CRm( 2), Op2( 1), access_vm_reg, NULL, c10_NMRR },
	{ Op1( 0), CRn(10), CRm( 3), Op2( 0), access_vm_reg, NULL, c10_AMAIR0 },
	{ Op1( 0), CRn(10), CRm( 3), Op2( 1), access_vm_reg, NULL, c10_AMAIR1 },

	/* ICC_SRE */
	{ Op1( 0), CRn(12), CRm(12), Op2( 5), access_gic_sre },

	{ Op1( 0), CRn(13), CRm( 0), Op2( 1), access_vm_reg, NULL, c13_CID },

	/* PMEVCNTRn */
	PMU_PMEVCNTR(0),
	PMU_PMEVCNTR(1),
	PMU_PMEVCNTR(2),
	PMU_PMEVCNTR(3),
	PMU_PMEVCNTR(4),
	PMU_PMEVCNTR(5),
	PMU_PMEVCNTR(6),
	PMU_PMEVCNTR(7),
	PMU_PMEVCNTR(8),
	PMU_PMEVCNTR(9),
	PMU_PMEVCNTR(10),
	PMU_PMEVCNTR(11),
	PMU_PMEVCNTR(12),
	PMU_PMEVCNTR(13),
	PMU_PMEVCNTR(14),
	PMU_PMEVCNTR(15),
	PMU_PMEVCNTR(16),
	PMU_PMEVCNTR(17),
	PMU_PMEVCNTR(18),
	PMU_PMEVCNTR(19),
	PMU_PMEVCNTR(20),
	PMU_PMEVCNTR(21),
	PMU_PMEVCNTR(22),
	PMU_PMEVCNTR(23),
	PMU_PMEVCNTR(24),
	PMU_PMEVCNTR(25),
	PMU_PMEVCNTR(26),
	PMU_PMEVCNTR(27),
	PMU_PMEVCNTR(28),
	PMU_PMEVCNTR(29),
	PMU_PMEVCNTR(30),
	/* PMEVTYPERn */
	PMU_PMEVTYPER(0),
	PMU_PMEVTYPER(1),
	PMU_PMEVTYPER(2),
	PMU_PMEVTYPER(3),
	PMU_PMEVTYPER(4),
	PMU_PMEVTYPER(5),
	PMU_PMEVTYPER(6),
	PMU_PMEVTYPER(7),
	PMU_PMEVTYPER(8),
	PMU_PMEVTYPER(9),
	PMU_PMEVTYPER(10),
	PMU_PMEVTYPER(11),
	PMU_PMEVTYPER(12),
	PMU_PMEVTYPER(13),
	PMU_PMEVTYPER(14),
	PMU_PMEVTYPER(15),
	PMU_PMEVTYPER(16),
	PMU_PMEVTYPER(17),
	PMU_PMEVTYPER(18),
	PMU_PMEVTYPER(19),
	PMU_PMEVTYPER(20),
	PMU_PMEVTYPER(21),
	PMU_PMEVTYPER(22),
	PMU_PMEVTYPER(23),
	PMU_PMEVTYPER(24),
	PMU_PMEVTYPER(25),
	PMU_PMEVTYPER(26),
	PMU_PMEVTYPER(27),
	PMU_PMEVTYPER(28),
	PMU_PMEVTYPER(29),
	PMU_PMEVTYPER(30),
	/* PMCCFILTR */
	{ Op1(0), CRn(14), CRm(15), Op2(7), access_pmu_evtyper },
};

static const struct sys_reg_desc cp15_64_regs[] = {
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, c2_TTBR0 },
	{ Op1( 0), CRn( 0), CRm( 9), Op2( 0), access_pmu_evcntr },
	{ Op1( 0), CRn( 0), CRm(12), Op2( 0), access_gic_sgi },
	{ Op1( 1), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, c2_TTBR1 },
};

/* Target specific emulation tables */
static struct kvm_sys_reg_target_table *target_tables[KVM_ARM_NUM_TARGETS];

void kvm_register_target_sys_reg_table(unsigned int target,
				       struct kvm_sys_reg_target_table *table)
{
	target_tables[target] = table;
}

/* Get specific register table for this target. */
static const struct sys_reg_desc *get_target_table(unsigned target,
						   bool mode_is_64,
						   size_t *num)
{
	struct kvm_sys_reg_target_table *table;

	table = target_tables[target];
	if (mode_is_64) {
		*num = table->table64.num;
		return table->table64.table;
	} else {
		*num = table->table32.num;
		return table->table32.table;
	}
}

#define reg_to_match_value(x)						\
	({								\
		unsigned long val;					\
		val  = (x)->Op0 << 14;					\
		val |= (x)->Op1 << 11;					\
		val |= (x)->CRn << 7;					\
		val |= (x)->CRm << 3;					\
		val |= (x)->Op2;					\
		val;							\
	 })

static int match_sys_reg(const void *key, const void *elt)
{
	const unsigned long pval = (unsigned long)key;
	const struct sys_reg_desc *r = elt;

	return pval - reg_to_match_value(r);
}

static const struct sys_reg_desc *find_reg(const struct sys_reg_params *params,
					 const struct sys_reg_desc table[],
					 unsigned int num)
{
	unsigned long pval = reg_to_match_value(params);

	return bsearch((void *)pval, table, num, sizeof(table[0]), match_sys_reg);
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

/*
 * emulate_cp --  tries to match a sys_reg access in a handling table, and
 *                call the corresponding trap handler.
 *
 * @params: pointer to the descriptor of the access
 * @table: array of trap descriptors
 * @num: size of the trap descriptor array
 *
 * Return 0 if the access has been handled, and -1 if not.
 */
static int emulate_cp(struct kvm_vcpu *vcpu,
		      struct sys_reg_params *params,
		      const struct sys_reg_desc *table,
		      size_t num)
{
	const struct sys_reg_desc *r;

	if (!table)
		return -1;	/* Not handled */

	r = find_reg(params, table, num);

	if (r) {
		/*
		 * Not having an accessor means that we have
		 * configured a trap that we don't know how to
		 * handle. This certainly qualifies as a gross bug
		 * that should be fixed right away.
		 */
		BUG_ON(!r->access);

		if (likely(r->access(vcpu, params, r))) {
			/* Skip instruction, since it was emulated */
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			/* Handled */
			return 0;
		}
	}

	/* Not handled */
	return -1;
}

static void unhandled_cp_access(struct kvm_vcpu *vcpu,
				struct sys_reg_params *params)
{
	u8 hsr_ec = kvm_vcpu_trap_get_class(vcpu);
	int cp = -1;

	switch(hsr_ec) {
	case ESR_ELx_EC_CP15_32:
	case ESR_ELx_EC_CP15_64:
		cp = 15;
		break;
	case ESR_ELx_EC_CP14_MR:
	case ESR_ELx_EC_CP14_64:
		cp = 14;
		break;
	default:
		WARN_ON(1);
	}

	kvm_err("Unsupported guest CP%d access at: %08lx\n",
		cp, *vcpu_pc(vcpu));
	print_sys_reg_instr(params);
	kvm_inject_undefined(vcpu);
}

/**
 * kvm_handle_cp_64 -- handles a mrrc/mcrr trap on a guest CP14/CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
static int kvm_handle_cp_64(struct kvm_vcpu *vcpu,
			    const struct sys_reg_desc *global,
			    size_t nr_global,
			    const struct sys_reg_desc *target_specific,
			    size_t nr_specific)
{
	struct sys_reg_params params;
	u32 hsr = kvm_vcpu_get_hsr(vcpu);
	int Rt = (hsr >> 5) & 0xf;
	int Rt2 = (hsr >> 10) & 0xf;

	params.is_aarch32 = true;
	params.is_32bit = false;
	params.CRm = (hsr >> 1) & 0xf;
	params.is_write = ((hsr & 1) == 0);

	params.Op0 = 0;
	params.Op1 = (hsr >> 16) & 0xf;
	params.Op2 = 0;
	params.CRn = 0;

	/*
	 * Make a 64-bit value out of Rt and Rt2. As we use the same trap
	 * backends between AArch32 and AArch64, we get away with it.
	 */
	if (params.is_write) {
		params.regval = vcpu_get_reg(vcpu, Rt) & 0xffffffff;
		params.regval |= vcpu_get_reg(vcpu, Rt2) << 32;
	}

	if (!emulate_cp(vcpu, &params, target_specific, nr_specific))
		goto out;
	if (!emulate_cp(vcpu, &params, global, nr_global))
		goto out;

	unhandled_cp_access(vcpu, &params);

out:
	/* Split up the value between registers for the read side */
	if (!params.is_write) {
		vcpu_set_reg(vcpu, Rt, lower_32_bits(params.regval));
		vcpu_set_reg(vcpu, Rt2, upper_32_bits(params.regval));
	}

	return 1;
}

/**
 * kvm_handle_cp_32 -- handles a mrc/mcr trap on a guest CP14/CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
static int kvm_handle_cp_32(struct kvm_vcpu *vcpu,
			    const struct sys_reg_desc *global,
			    size_t nr_global,
			    const struct sys_reg_desc *target_specific,
			    size_t nr_specific)
{
	struct sys_reg_params params;
	u32 hsr = kvm_vcpu_get_hsr(vcpu);
	int Rt  = (hsr >> 5) & 0xf;

	params.is_aarch32 = true;
	params.is_32bit = true;
	params.CRm = (hsr >> 1) & 0xf;
	params.regval = vcpu_get_reg(vcpu, Rt);
	params.is_write = ((hsr & 1) == 0);
	params.CRn = (hsr >> 10) & 0xf;
	params.Op0 = 0;
	params.Op1 = (hsr >> 14) & 0x7;
	params.Op2 = (hsr >> 17) & 0x7;

	if (!emulate_cp(vcpu, &params, target_specific, nr_specific) ||
	    !emulate_cp(vcpu, &params, global, nr_global)) {
		if (!params.is_write)
			vcpu_set_reg(vcpu, Rt, params.regval);
		return 1;
	}

	unhandled_cp_access(vcpu, &params);
	return 1;
}

int kvm_handle_cp15_64(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	const struct sys_reg_desc *target_specific;
	size_t num;

	target_specific = get_target_table(vcpu->arch.target, false, &num);
	return kvm_handle_cp_64(vcpu,
				cp15_64_regs, ARRAY_SIZE(cp15_64_regs),
				target_specific, num);
}

int kvm_handle_cp15_32(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	const struct sys_reg_desc *target_specific;
	size_t num;

	target_specific = get_target_table(vcpu->arch.target, false, &num);
	return kvm_handle_cp_32(vcpu,
				cp15_regs, ARRAY_SIZE(cp15_regs),
				target_specific, num);
}

int kvm_handle_cp14_64(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	return kvm_handle_cp_64(vcpu,
				cp14_64_regs, ARRAY_SIZE(cp14_64_regs),
				NULL, 0);
}

int kvm_handle_cp14_32(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	return kvm_handle_cp_32(vcpu,
				cp14_regs, ARRAY_SIZE(cp14_regs),
				NULL, 0);
}

static int emulate_sys_reg(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *params)
{
	size_t num;
	const struct sys_reg_desc *table, *r;

	table = get_target_table(vcpu->arch.target, true, &num);

	/* Search target-specific then generic table. */
	r = find_reg(params, table, num);
	if (!r)
		r = find_reg(params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	if (likely(r)) {
		/*
		 * Not having an accessor means that we have
		 * configured a trap that we don't know how to
		 * handle. This certainly qualifies as a gross bug
		 * that should be fixed right away.
		 */
		BUG_ON(!r->access);

		if (likely(r->access(vcpu, params, r))) {
			/* Skip instruction, since it was emulated */
			kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
			return 1;
		}
		/* If access function fails, it should complain. */
	} else {
		kvm_err("Unsupported guest sys_reg access at: %lx\n",
			*vcpu_pc(vcpu));
		print_sys_reg_instr(params);
	}
	kvm_inject_undefined(vcpu);
	return 1;
}

static void reset_sys_reg_descs(struct kvm_vcpu *vcpu,
			      const struct sys_reg_desc *table, size_t num)
{
	unsigned long i;

	for (i = 0; i < num; i++)
		if (table[i].reset)
			table[i].reset(vcpu, &table[i]);
}

/**
 * kvm_handle_sys_reg -- handles a mrs/msr trap on a guest sys_reg access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu, struct kvm_run *run)
{
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_hsr(vcpu);
	int Rt = (esr >> 5) & 0x1f;
	int ret;

	trace_kvm_handle_sys_reg(esr);

	params.is_aarch32 = false;
	params.is_32bit = false;
	params.Op0 = (esr >> 20) & 3;
	params.Op1 = (esr >> 14) & 0x7;
	params.CRn = (esr >> 10) & 0xf;
	params.CRm = (esr >> 1) & 0xf;
	params.Op2 = (esr >> 17) & 0x7;
	params.regval = vcpu_get_reg(vcpu, Rt);
	params.is_write = !(esr & 1);

	ret = emulate_sys_reg(vcpu, &params);

	if (!params.is_write)
		vcpu_set_reg(vcpu, Rt, params.regval);
	return ret;
}

/******************************************************************************
 * Userspace API
 *****************************************************************************/

static bool index_to_params(u64 id, struct sys_reg_params *params)
{
	switch (id & KVM_REG_SIZE_MASK) {
	case KVM_REG_SIZE_U64:
		/* Any unused index bits means it's not valid. */
		if (id & ~(KVM_REG_ARCH_MASK | KVM_REG_SIZE_MASK
			      | KVM_REG_ARM_COPROC_MASK
			      | KVM_REG_ARM64_SYSREG_OP0_MASK
			      | KVM_REG_ARM64_SYSREG_OP1_MASK
			      | KVM_REG_ARM64_SYSREG_CRN_MASK
			      | KVM_REG_ARM64_SYSREG_CRM_MASK
			      | KVM_REG_ARM64_SYSREG_OP2_MASK))
			return false;
		params->Op0 = ((id & KVM_REG_ARM64_SYSREG_OP0_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP0_SHIFT);
		params->Op1 = ((id & KVM_REG_ARM64_SYSREG_OP1_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP1_SHIFT);
		params->CRn = ((id & KVM_REG_ARM64_SYSREG_CRN_MASK)
			       >> KVM_REG_ARM64_SYSREG_CRN_SHIFT);
		params->CRm = ((id & KVM_REG_ARM64_SYSREG_CRM_MASK)
			       >> KVM_REG_ARM64_SYSREG_CRM_SHIFT);
		params->Op2 = ((id & KVM_REG_ARM64_SYSREG_OP2_MASK)
			       >> KVM_REG_ARM64_SYSREG_OP2_SHIFT);
		return true;
	default:
		return false;
	}
}

/* Decode an index value, and find the sys_reg_desc entry. */
static const struct sys_reg_desc *index_to_sys_reg_desc(struct kvm_vcpu *vcpu,
						    u64 id)
{
	size_t num;
	const struct sys_reg_desc *table, *r;
	struct sys_reg_params params;

	/* We only do sys_reg for now. */
	if ((id & KVM_REG_ARM_COPROC_MASK) != KVM_REG_ARM64_SYSREG)
		return NULL;

	if (!index_to_params(id, &params))
		return NULL;

	table = get_target_table(vcpu->arch.target, true, &num);
	r = find_reg(&params, table, num);
	if (!r)
		r = find_reg(&params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	/* Not saved in the sys_reg array? */
	if (r && !r->reg)
		r = NULL;

	return r;
}

/*
 * These are the invariant sys_reg registers: we let the guest see the
 * host versions of these, so they're part of the guest state.
 *
 * A future CPU may provide a mechanism to present different values to
 * the guest, or a future kvm may trap them.
 */

#define FUNCTION_INVARIANT(reg)						\
	static void get_##reg(struct kvm_vcpu *v,			\
			      const struct sys_reg_desc *r)		\
	{								\
		((struct sys_reg_desc *)r)->val = read_sysreg(reg);	\
	}

FUNCTION_INVARIANT(midr_el1)
FUNCTION_INVARIANT(ctr_el0)
FUNCTION_INVARIANT(revidr_el1)
FUNCTION_INVARIANT(id_pfr0_el1)
FUNCTION_INVARIANT(id_pfr1_el1)
FUNCTION_INVARIANT(id_dfr0_el1)
FUNCTION_INVARIANT(id_afr0_el1)
FUNCTION_INVARIANT(id_mmfr0_el1)
FUNCTION_INVARIANT(id_mmfr1_el1)
FUNCTION_INVARIANT(id_mmfr2_el1)
FUNCTION_INVARIANT(id_mmfr3_el1)
FUNCTION_INVARIANT(id_isar0_el1)
FUNCTION_INVARIANT(id_isar1_el1)
FUNCTION_INVARIANT(id_isar2_el1)
FUNCTION_INVARIANT(id_isar3_el1)
FUNCTION_INVARIANT(id_isar4_el1)
FUNCTION_INVARIANT(id_isar5_el1)
FUNCTION_INVARIANT(clidr_el1)
FUNCTION_INVARIANT(aidr_el1)

/* ->val is filled in by kvm_sys_reg_table_init() */
static struct sys_reg_desc invariant_sys_regs[] = {
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b000),
	  NULL, get_midr_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0000), Op2(0b110),
	  NULL, get_revidr_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b000),
	  NULL, get_id_pfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b001),
	  NULL, get_id_pfr1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b010),
	  NULL, get_id_dfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b011),
	  NULL, get_id_afr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b100),
	  NULL, get_id_mmfr0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b101),
	  NULL, get_id_mmfr1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b110),
	  NULL, get_id_mmfr2_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0001), Op2(0b111),
	  NULL, get_id_mmfr3_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b000),
	  NULL, get_id_isar0_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b001),
	  NULL, get_id_isar1_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b010),
	  NULL, get_id_isar2_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b011),
	  NULL, get_id_isar3_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b100),
	  NULL, get_id_isar4_el1 },
	{ Op0(0b11), Op1(0b000), CRn(0b0000), CRm(0b0010), Op2(0b101),
	  NULL, get_id_isar5_el1 },
	{ Op0(0b11), Op1(0b001), CRn(0b0000), CRm(0b0000), Op2(0b001),
	  NULL, get_clidr_el1 },
	{ Op0(0b11), Op1(0b001), CRn(0b0000), CRm(0b0000), Op2(0b111),
	  NULL, get_aidr_el1 },
	{ Op0(0b11), Op1(0b011), CRn(0b0000), CRm(0b0000), Op2(0b001),
	  NULL, get_ctr_el0 },
};

static int reg_from_user(u64 *val, const void __user *uaddr, u64 id)
{
	if (copy_from_user(val, uaddr, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

static int reg_to_user(void __user *uaddr, const u64 *val, u64 id)
{
	if (copy_to_user(uaddr, val, KVM_REG_SIZE(id)) != 0)
		return -EFAULT;
	return 0;
}

static int get_invariant_sys_reg(u64 id, void __user *uaddr)
{
	struct sys_reg_params params;
	const struct sys_reg_desc *r;

	if (!index_to_params(id, &params))
		return -ENOENT;

	r = find_reg(&params, invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	return reg_to_user(uaddr, &r->val, id);
}

static int set_invariant_sys_reg(u64 id, void __user *uaddr)
{
	struct sys_reg_params params;
	const struct sys_reg_desc *r;
	int err;
	u64 val = 0; /* Make sure high bits are 0 for 32-bit regs */

	if (!index_to_params(id, &params))
		return -ENOENT;
	r = find_reg(&params, invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	err = reg_from_user(&val, uaddr, id);
	if (err)
		return err;

	/* This is what we mean by invariant: you can't change it. */
	if (r->val != val)
		return -EINVAL;

	return 0;
}

static bool is_valid_cache(u32 val)
{
	u32 level, ctype;

	if (val >= CSSELR_MAX)
		return false;

	/* Bottom bit is Instruction or Data bit.  Next 3 bits are level. */
	level = (val >> 1);
	ctype = (cache_levels >> (level * 3)) & 7;

	switch (ctype) {
	case 0: /* No cache */
		return false;
	case 1: /* Instruction cache only */
		return (val & 1);
	case 2: /* Data cache only */
	case 4: /* Unified cache */
		return !(val & 1);
	case 3: /* Separate instruction and data caches */
		return true;
	default: /* Reserved: we can't know instruction or data. */
		return false;
	}
}

static int demux_c15_get(u64 id, void __user *uaddr)
{
	u32 val;
	u32 __user *uval = uaddr;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	switch (id & KVM_REG_ARM_DEMUX_ID_MASK) {
	case KVM_REG_ARM_DEMUX_ID_CCSIDR:
		if (KVM_REG_SIZE(id) != 4)
			return -ENOENT;
		val = (id & KVM_REG_ARM_DEMUX_VAL_MASK)
			>> KVM_REG_ARM_DEMUX_VAL_SHIFT;
		if (!is_valid_cache(val))
			return -ENOENT;

		return put_user(get_ccsidr(val), uval);
	default:
		return -ENOENT;
	}
}

static int demux_c15_set(u64 id, void __user *uaddr)
{
	u32 val, newval;
	u32 __user *uval = uaddr;

	/* Fail if we have unknown bits set. */
	if (id & ~(KVM_REG_ARCH_MASK|KVM_REG_SIZE_MASK|KVM_REG_ARM_COPROC_MASK
		   | ((1 << KVM_REG_ARM_COPROC_SHIFT)-1)))
		return -ENOENT;

	switch (id & KVM_REG_ARM_DEMUX_ID_MASK) {
	case KVM_REG_ARM_DEMUX_ID_CCSIDR:
		if (KVM_REG_SIZE(id) != 4)
			return -ENOENT;
		val = (id & KVM_REG_ARM_DEMUX_VAL_MASK)
			>> KVM_REG_ARM_DEMUX_VAL_SHIFT;
		if (!is_valid_cache(val))
			return -ENOENT;

		if (get_user(newval, uval))
			return -EFAULT;

		/* This is also invariant: you can't change it. */
		if (newval != get_ccsidr(val))
			return -EINVAL;
		return 0;
	default:
		return -ENOENT;
	}
}

int kvm_arm_sys_reg_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct sys_reg_desc *r;
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_get(reg->id, uaddr);

	if (KVM_REG_SIZE(reg->id) != sizeof(__u64))
		return -ENOENT;

	r = index_to_sys_reg_desc(vcpu, reg->id);
	if (!r)
		return get_invariant_sys_reg(reg->id, uaddr);

	if (r->get_user)
		return (r->get_user)(vcpu, r, reg, uaddr);

	return reg_to_user(uaddr, &vcpu_sys_reg(vcpu, r->reg), reg->id);
}

int kvm_arm_sys_reg_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	const struct sys_reg_desc *r;
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_set(reg->id, uaddr);

	if (KVM_REG_SIZE(reg->id) != sizeof(__u64))
		return -ENOENT;

	r = index_to_sys_reg_desc(vcpu, reg->id);
	if (!r)
		return set_invariant_sys_reg(reg->id, uaddr);

	if (r->set_user)
		return (r->set_user)(vcpu, r, reg, uaddr);

	return reg_from_user(&vcpu_sys_reg(vcpu, r->reg), uaddr, reg->id);
}

static unsigned int num_demux_regs(void)
{
	unsigned int i, count = 0;

	for (i = 0; i < CSSELR_MAX; i++)
		if (is_valid_cache(i))
			count++;

	return count;
}

static int write_demux_regids(u64 __user *uindices)
{
	u64 val = KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_DEMUX;
	unsigned int i;

	val |= KVM_REG_ARM_DEMUX_ID_CCSIDR;
	for (i = 0; i < CSSELR_MAX; i++) {
		if (!is_valid_cache(i))
			continue;
		if (put_user(val | i, uindices))
			return -EFAULT;
		uindices++;
	}
	return 0;
}

static u64 sys_reg_to_index(const struct sys_reg_desc *reg)
{
	return (KVM_REG_ARM64 | KVM_REG_SIZE_U64 |
		KVM_REG_ARM64_SYSREG |
		(reg->Op0 << KVM_REG_ARM64_SYSREG_OP0_SHIFT) |
		(reg->Op1 << KVM_REG_ARM64_SYSREG_OP1_SHIFT) |
		(reg->CRn << KVM_REG_ARM64_SYSREG_CRN_SHIFT) |
		(reg->CRm << KVM_REG_ARM64_SYSREG_CRM_SHIFT) |
		(reg->Op2 << KVM_REG_ARM64_SYSREG_OP2_SHIFT));
}

static bool copy_reg_to_user(const struct sys_reg_desc *reg, u64 __user **uind)
{
	if (!*uind)
		return true;

	if (put_user(sys_reg_to_index(reg), *uind))
		return false;

	(*uind)++;
	return true;
}

/* Assumed ordered tables, see kvm_sys_reg_table_init. */
static int walk_sys_regs(struct kvm_vcpu *vcpu, u64 __user *uind)
{
	const struct sys_reg_desc *i1, *i2, *end1, *end2;
	unsigned int total = 0;
	size_t num;

	/* We check for duplicates here, to allow arch-specific overrides. */
	i1 = get_target_table(vcpu->arch.target, true, &num);
	end1 = i1 + num;
	i2 = sys_reg_descs;
	end2 = sys_reg_descs + ARRAY_SIZE(sys_reg_descs);

	BUG_ON(i1 == end1 || i2 == end2);

	/* Walk carefully, as both tables may refer to the same register. */
	while (i1 || i2) {
		int cmp = cmp_sys_reg(i1, i2);
		/* target-specific overrides generic entry. */
		if (cmp <= 0) {
			/* Ignore registers we trap but don't save. */
			if (i1->reg) {
				if (!copy_reg_to_user(i1, &uind))
					return -EFAULT;
				total++;
			}
		} else {
			/* Ignore registers we trap but don't save. */
			if (i2->reg) {
				if (!copy_reg_to_user(i2, &uind))
					return -EFAULT;
				total++;
			}
		}

		if (cmp <= 0 && ++i1 == end1)
			i1 = NULL;
		if (cmp >= 0 && ++i2 == end2)
			i2 = NULL;
	}
	return total;
}

unsigned long kvm_arm_num_sys_reg_descs(struct kvm_vcpu *vcpu)
{
	return ARRAY_SIZE(invariant_sys_regs)
		+ num_demux_regs()
		+ walk_sys_regs(vcpu, (u64 __user *)NULL);
}

int kvm_arm_copy_sys_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	unsigned int i;
	int err;

	/* Then give them all the invariant registers' indices. */
	for (i = 0; i < ARRAY_SIZE(invariant_sys_regs); i++) {
		if (put_user(sys_reg_to_index(&invariant_sys_regs[i]), uindices))
			return -EFAULT;
		uindices++;
	}

	err = walk_sys_regs(vcpu, uindices);
	if (err < 0)
		return err;
	uindices += err;

	return write_demux_regids(uindices);
}

static int check_sysreg_table(const struct sys_reg_desc *table, unsigned int n)
{
	unsigned int i;

	for (i = 1; i < n; i++) {
		if (cmp_sys_reg(&table[i-1], &table[i]) >= 0) {
			kvm_err("sys_reg table %p out of order (%d)\n", table, i - 1);
			return 1;
		}
	}

	return 0;
}

void kvm_sys_reg_table_init(void)
{
	unsigned int i;
	struct sys_reg_desc clidr;

	/* Make sure tables are unique and in order. */
	BUG_ON(check_sysreg_table(sys_reg_descs, ARRAY_SIZE(sys_reg_descs)));
	BUG_ON(check_sysreg_table(cp14_regs, ARRAY_SIZE(cp14_regs)));
	BUG_ON(check_sysreg_table(cp14_64_regs, ARRAY_SIZE(cp14_64_regs)));
	BUG_ON(check_sysreg_table(cp15_regs, ARRAY_SIZE(cp15_regs)));
	BUG_ON(check_sysreg_table(cp15_64_regs, ARRAY_SIZE(cp15_64_regs)));
	BUG_ON(check_sysreg_table(invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs)));

	/* We abuse the reset function to overwrite the table itself. */
	for (i = 0; i < ARRAY_SIZE(invariant_sys_regs); i++)
		invariant_sys_regs[i].reset(NULL, &invariant_sys_regs[i]);

	/*
	 * CLIDR format is awkward, so clean it up.  See ARM B4.1.20:
	 *
	 *   If software reads the Cache Type fields from Ctype1
	 *   upwards, once it has seen a value of 0b000, no caches
	 *   exist at further-out levels of the hierarchy. So, for
	 *   example, if Ctype3 is the first Cache Type field with a
	 *   value of 0b000, the values of Ctype4 to Ctype7 must be
	 *   ignored.
	 */
	get_clidr_el1(NULL, &clidr); /* Ugly... */
	cache_levels = clidr.val;
	for (i = 0; i < 7; i++)
		if (((cache_levels >> (i*3)) & 7) == 0)
			break;
	/* Clear all higher bits. */
	cache_levels &= (1 << (i*3))-1;
}

/**
 * kvm_reset_sys_regs - sets system registers to reset value
 * @vcpu: The VCPU pointer
 *
 * This function finds the right table above and sets the registers on the
 * virtual CPU struct to their architecturally defined reset values.
 */
void kvm_reset_sys_regs(struct kvm_vcpu *vcpu)
{
	size_t num;
	const struct sys_reg_desc *table;

	/* Catch someone adding a register without putting in reset entry. */
	memset(&vcpu->arch.ctxt.sys_regs, 0x42, sizeof(vcpu->arch.ctxt.sys_regs));

	/* Generic chip reset first (so target could override). */
	reset_sys_reg_descs(vcpu, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	table = get_target_table(vcpu->arch.target, true, &num);
	reset_sys_reg_descs(vcpu, table, num);

	for (num = 1; num < NR_SYS_REGS; num++)
		if (vcpu_sys_reg(vcpu, num) == 0x4242424242424242)
			panic("Didn't reset vcpu_sys_reg(%zi)", num);
}
