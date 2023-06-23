// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/kvm/coproc.c:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Authors: Rusty Russell <rusty@rustcorp.com.au>
 *          Christoffer Dall <c.dall@virtualopensystems.com>
 */

#include <linux/bitfield.h>
#include <linux/bsearch.h>
#include <linux/cacheinfo.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_nested.h>
#include <asm/perf_event.h>
#include <asm/sysreg.h>

#include <trace/events/kvm.h>

#include "sys_regs.h"

#include "trace.h"

/*
 * For AArch32, we only take care of what is being trapped. Anything
 * that has to do with init and userspace access has to go via the
 * 64bit interface.
 */

static u64 sys_reg_to_index(const struct sys_reg_desc *reg);

static bool read_from_write_only(struct kvm_vcpu *vcpu,
				 struct sys_reg_params *params,
				 const struct sys_reg_desc *r)
{
	WARN_ONCE(1, "Unexpected sys_reg read to write-only register\n");
	print_sys_reg_instr(params);
	kvm_inject_undefined(vcpu);
	return false;
}

static bool write_to_read_only(struct kvm_vcpu *vcpu,
			       struct sys_reg_params *params,
			       const struct sys_reg_desc *r)
{
	WARN_ONCE(1, "Unexpected sys_reg write to read-only register\n");
	print_sys_reg_instr(params);
	kvm_inject_undefined(vcpu);
	return false;
}

u64 vcpu_read_sys_reg(const struct kvm_vcpu *vcpu, int reg)
{
	u64 val = 0x8badf00d8badf00d;

	if (vcpu_get_flag(vcpu, SYSREGS_ON_CPU) &&
	    __vcpu_read_sys_reg_from_cpu(reg, &val))
		return val;

	return __vcpu_sys_reg(vcpu, reg);
}

void vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg)
{
	if (vcpu_get_flag(vcpu, SYSREGS_ON_CPU) &&
	    __vcpu_write_sys_reg_to_cpu(val, reg))
		return;

	__vcpu_sys_reg(vcpu, reg) = val;
}

/* CSSELR values; used to index KVM_REG_ARM_DEMUX_ID_CCSIDR */
#define CSSELR_MAX 14

/*
 * Returns the minimum line size for the selected cache, expressed as
 * Log2(bytes).
 */
static u8 get_min_cache_line_size(bool icache)
{
	u64 ctr = read_sanitised_ftr_reg(SYS_CTR_EL0);
	u8 field;

	if (icache)
		field = SYS_FIELD_GET(CTR_EL0, IminLine, ctr);
	else
		field = SYS_FIELD_GET(CTR_EL0, DminLine, ctr);

	/*
	 * Cache line size is represented as Log2(words) in CTR_EL0.
	 * Log2(bytes) can be derived with the following:
	 *
	 * Log2(words) + 2 = Log2(bytes / 4) + 2
	 * 		   = Log2(bytes) - 2 + 2
	 * 		   = Log2(bytes)
	 */
	return field + 2;
}

/* Which cache CCSIDR represents depends on CSSELR value. */
static u32 get_ccsidr(struct kvm_vcpu *vcpu, u32 csselr)
{
	u8 line_size;

	if (vcpu->arch.ccsidr)
		return vcpu->arch.ccsidr[csselr];

	line_size = get_min_cache_line_size(csselr & CSSELR_EL1_InD);

	/*
	 * Fabricate a CCSIDR value as the overriding value does not exist.
	 * The real CCSIDR value will not be used as it can vary by the
	 * physical CPU which the vcpu currently resides in.
	 *
	 * The line size is determined with get_min_cache_line_size(), which
	 * should be valid for all CPUs even if they have different cache
	 * configuration.
	 *
	 * The associativity bits are cleared, meaning the geometry of all data
	 * and unified caches (which are guaranteed to be PIPT and thus
	 * non-aliasing) are 1 set and 1 way.
	 * Guests should not be doing cache operations by set/way at all, and
	 * for this reason, we trap them and attempt to infer the intent, so
	 * that we can flush the entire guest's address space at the appropriate
	 * time. The exposed geometry minimizes the number of the traps.
	 * [If guests should attempt to infer aliasing properties from the
	 * geometry (which is not permitted by the architecture), they would
	 * only do so for virtually indexed caches.]
	 *
	 * We don't check if the cache level exists as it is allowed to return
	 * an UNKNOWN value if not.
	 */
	return SYS_FIELD_PREP(CCSIDR_EL1, LineSize, line_size - 4);
}

static int set_ccsidr(struct kvm_vcpu *vcpu, u32 csselr, u32 val)
{
	u8 line_size = FIELD_GET(CCSIDR_EL1_LineSize, val) + 4;
	u32 *ccsidr = vcpu->arch.ccsidr;
	u32 i;

	if ((val & CCSIDR_EL1_RES0) ||
	    line_size < get_min_cache_line_size(csselr & CSSELR_EL1_InD))
		return -EINVAL;

	if (!ccsidr) {
		if (val == get_ccsidr(vcpu, csselr))
			return 0;

		ccsidr = kmalloc_array(CSSELR_MAX, sizeof(u32), GFP_KERNEL_ACCOUNT);
		if (!ccsidr)
			return -ENOMEM;

		for (i = 0; i < CSSELR_MAX; i++)
			ccsidr[i] = get_ccsidr(vcpu, i);

		vcpu->arch.ccsidr = ccsidr;
	}

	ccsidr[csselr] = val;

	return 0;
}

static bool access_rw(struct kvm_vcpu *vcpu,
		      struct sys_reg_params *p,
		      const struct sys_reg_desc *r)
{
	if (p->is_write)
		vcpu_write_sys_reg(vcpu, p->regval, r->reg);
	else
		p->regval = vcpu_read_sys_reg(vcpu, r->reg);

	return true;
}

/*
 * See note at ARMv7 ARM B1.14.4 (TL;DR: S/W ops are not easily virtualized).
 */
static bool access_dcsw(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (!p->is_write)
		return read_from_write_only(vcpu, p, r);

	/*
	 * Only track S/W ops if we don't have FWB. It still indicates
	 * that the guest is a bit broken (S/W operations should only
	 * be done by firmware, knowing that there is only a single
	 * CPU left in the system, and certainly not from non-secure
	 * software).
	 */
	if (!cpus_have_const_cap(ARM64_HAS_STAGE2_FWB))
		kvm_set_way_flush(vcpu);

	return true;
}

static void get_access_mask(const struct sys_reg_desc *r, u64 *mask, u64 *shift)
{
	switch (r->aarch32_map) {
	case AA32_LO:
		*mask = GENMASK_ULL(31, 0);
		*shift = 0;
		break;
	case AA32_HI:
		*mask = GENMASK_ULL(63, 32);
		*shift = 32;
		break;
	default:
		*mask = GENMASK_ULL(63, 0);
		*shift = 0;
		break;
	}
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
	u64 val, mask, shift;

	BUG_ON(!p->is_write);

	get_access_mask(r, &mask, &shift);

	if (~mask) {
		val = vcpu_read_sys_reg(vcpu, r->reg);
		val &= ~mask;
	} else {
		val = 0;
	}

	val |= (p->regval & (mask >> shift)) << shift;
	vcpu_write_sys_reg(vcpu, val, r->reg);

	kvm_toggle_cache(vcpu, was_enabled);
	return true;
}

static bool access_actlr(struct kvm_vcpu *vcpu,
			 struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	u64 mask, shift;

	if (p->is_write)
		return ignore_write(vcpu, p);

	get_access_mask(r, &mask, &shift);
	p->regval = (vcpu_read_sys_reg(vcpu, r->reg) & mask) >> shift;

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
	bool g1;

	if (!p->is_write)
		return read_from_write_only(vcpu, p, r);

	/*
	 * In a system where GICD_CTLR.DS=1, a ICC_SGI0R_EL1 access generates
	 * Group0 SGIs only, while ICC_SGI1R_EL1 can generate either group,
	 * depending on the SGI configuration. ICC_ASGI1R_EL1 is effectively
	 * equivalent to ICC_SGI0R_EL1, as there is no "alternative" secure
	 * group.
	 */
	if (p->Op0 == 0) {		/* AArch32 */
		switch (p->Op1) {
		default:		/* Keep GCC quiet */
		case 0:			/* ICC_SGI1R */
			g1 = true;
			break;
		case 1:			/* ICC_ASGI1R */
		case 2:			/* ICC_SGI0R */
			g1 = false;
			break;
		}
	} else {			/* AArch64 */
		switch (p->Op2) {
		default:		/* Keep GCC quiet */
		case 5:			/* ICC_SGI1R_EL1 */
			g1 = true;
			break;
		case 6:			/* ICC_ASGI1R_EL1 */
		case 7:			/* ICC_SGI0R_EL1 */
			g1 = false;
			break;
		}
	}

	vgic_v3_dispatch_sgi(vcpu, p->regval, g1);

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

static bool trap_undef(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       const struct sys_reg_desc *r)
{
	kvm_inject_undefined(vcpu);
	return false;
}

/*
 * ARMv8.1 mandates at least a trivial LORegion implementation, where all the
 * RW registers are RES0 (which we can implement as RAZ/WI). On an ARMv8.0
 * system, these registers should UNDEF. LORID_EL1 being a RO register, we
 * treat it separately.
 */
static bool trap_loregion(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u64 val = read_sanitised_ftr_reg(SYS_ID_AA64MMFR1_EL1);
	u32 sr = reg_to_encoding(r);

	if (!(val & (0xfUL << ID_AA64MMFR1_EL1_LO_SHIFT))) {
		kvm_inject_undefined(vcpu);
		return false;
	}

	if (p->is_write && sr == SYS_LORID_EL1)
		return write_to_read_only(vcpu, p, r);

	return trap_raz_wi(vcpu, p, r);
}

static bool trap_oslar_el1(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 oslsr;

	if (!p->is_write)
		return read_from_write_only(vcpu, p, r);

	/* Forward the OSLK bit to OSLSR */
	oslsr = __vcpu_sys_reg(vcpu, OSLSR_EL1) & ~OSLSR_EL1_OSLK;
	if (p->regval & OSLAR_EL1_OSLK)
		oslsr |= OSLSR_EL1_OSLK;

	__vcpu_sys_reg(vcpu, OSLSR_EL1) = oslsr;
	return true;
}

static bool trap_oslsr_el1(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = __vcpu_sys_reg(vcpu, r->reg);
	return true;
}

static int set_oslsr_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			 u64 val)
{
	/*
	 * The only modifiable bit is the OSLK bit. Refuse the write if
	 * userspace attempts to change any other bit in the register.
	 */
	if ((val ^ rd->val) & ~OSLSR_EL1_OSLK)
		return -EINVAL;

	__vcpu_sys_reg(vcpu, rd->reg) = val;
	return 0;
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
	access_rw(vcpu, p, r);
	if (p->is_write)
		vcpu_set_flag(vcpu, DEBUG_DIRTY);

	trace_trap_reg(__func__, r->reg, p->is_write, p->regval);

	return true;
}

/*
 * reg_to_dbg/dbg_to_reg
 *
 * A 32 bit write to a debug register leave top bits alone
 * A 32 bit read from a debug register only returns the bottom bits
 *
 * All writes will set the DEBUG_DIRTY flag to ensure the hyp code
 * switches between host and guest values in future.
 */
static void reg_to_dbg(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       const struct sys_reg_desc *rd,
		       u64 *dbg_reg)
{
	u64 mask, shift, val;

	get_access_mask(rd, &mask, &shift);

	val = *dbg_reg;
	val &= ~mask;
	val |= (p->regval & (mask >> shift)) << shift;
	*dbg_reg = val;

	vcpu_set_flag(vcpu, DEBUG_DIRTY);
}

static void dbg_to_reg(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       const struct sys_reg_desc *rd,
		       u64 *dbg_reg)
{
	u64 mask, shift;

	get_access_mask(rd, &mask, &shift);
	p->regval = (*dbg_reg & mask) >> shift;
}

static bool trap_bvr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_bvr[rd->CRm];

	if (p->is_write)
		reg_to_dbg(vcpu, p, rd, dbg_reg);
	else
		dbg_to_reg(vcpu, p, rd, dbg_reg);

	trace_trap_reg(__func__, rd->CRm, p->is_write, *dbg_reg);

	return true;
}

static int set_bvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 val)
{
	vcpu->arch.vcpu_debug_state.dbg_bvr[rd->CRm] = val;
	return 0;
}

static int get_bvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 *val)
{
	*val = vcpu->arch.vcpu_debug_state.dbg_bvr[rd->CRm];
	return 0;
}

static void reset_bvr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_bvr[rd->CRm] = rd->val;
}

static bool trap_bcr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_bcr[rd->CRm];

	if (p->is_write)
		reg_to_dbg(vcpu, p, rd, dbg_reg);
	else
		dbg_to_reg(vcpu, p, rd, dbg_reg);

	trace_trap_reg(__func__, rd->CRm, p->is_write, *dbg_reg);

	return true;
}

static int set_bcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 val)
{
	vcpu->arch.vcpu_debug_state.dbg_bcr[rd->CRm] = val;
	return 0;
}

static int get_bcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 *val)
{
	*val = vcpu->arch.vcpu_debug_state.dbg_bcr[rd->CRm];
	return 0;
}

static void reset_bcr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_bcr[rd->CRm] = rd->val;
}

static bool trap_wvr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_wvr[rd->CRm];

	if (p->is_write)
		reg_to_dbg(vcpu, p, rd, dbg_reg);
	else
		dbg_to_reg(vcpu, p, rd, dbg_reg);

	trace_trap_reg(__func__, rd->CRm, p->is_write,
		vcpu->arch.vcpu_debug_state.dbg_wvr[rd->CRm]);

	return true;
}

static int set_wvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 val)
{
	vcpu->arch.vcpu_debug_state.dbg_wvr[rd->CRm] = val;
	return 0;
}

static int get_wvr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 *val)
{
	*val = vcpu->arch.vcpu_debug_state.dbg_wvr[rd->CRm];
	return 0;
}

static void reset_wvr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_wvr[rd->CRm] = rd->val;
}

static bool trap_wcr(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *p,
		     const struct sys_reg_desc *rd)
{
	u64 *dbg_reg = &vcpu->arch.vcpu_debug_state.dbg_wcr[rd->CRm];

	if (p->is_write)
		reg_to_dbg(vcpu, p, rd, dbg_reg);
	else
		dbg_to_reg(vcpu, p, rd, dbg_reg);

	trace_trap_reg(__func__, rd->CRm, p->is_write, *dbg_reg);

	return true;
}

static int set_wcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 val)
{
	vcpu->arch.vcpu_debug_state.dbg_wcr[rd->CRm] = val;
	return 0;
}

static int get_wcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		   u64 *val)
{
	*val = vcpu->arch.vcpu_debug_state.dbg_wcr[rd->CRm];
	return 0;
}

static void reset_wcr(struct kvm_vcpu *vcpu,
		      const struct sys_reg_desc *rd)
{
	vcpu->arch.vcpu_debug_state.dbg_wcr[rd->CRm] = rd->val;
}

static void reset_amair_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 amair = read_sysreg(amair_el1);
	vcpu_write_sys_reg(vcpu, amair, AMAIR_EL1);
}

static void reset_actlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 actlr = read_sysreg(actlr_el1);
	vcpu_write_sys_reg(vcpu, actlr, ACTLR_EL1);
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
	vcpu_write_sys_reg(vcpu, (1ULL << 31) | mpidr, MPIDR_EL1);
}

static unsigned int pmu_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *r)
{
	if (kvm_vcpu_has_pmu(vcpu))
		return 0;

	return REG_HIDDEN;
}

static void reset_pmu_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 n, mask = BIT(ARMV8_PMU_CYCLE_IDX);

	/* No PMU available, any PMU reg may UNDEF... */
	if (!kvm_arm_support_pmu_v3())
		return;

	n = read_sysreg(pmcr_el0) >> ARMV8_PMU_PMCR_N_SHIFT;
	n &= ARMV8_PMU_PMCR_N_MASK;
	if (n)
		mask |= GENMASK(n - 1, 0);

	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= mask;
}

static void reset_pmevcntr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= GENMASK(31, 0);
}

static void reset_pmevtyper(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= ARMV8_PMU_EVTYPE_MASK;
}

static void reset_pmselr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= ARMV8_PMU_COUNTER_MASK;
}

static void reset_pmcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 pmcr;

	/* No PMU available, PMCR_EL0 may UNDEF... */
	if (!kvm_arm_support_pmu_v3())
		return;

	/* Only preserve PMCR_EL0.N, and reset the rest to 0 */
	pmcr = read_sysreg(pmcr_el0) & (ARMV8_PMU_PMCR_N_MASK << ARMV8_PMU_PMCR_N_SHIFT);
	if (!kvm_supports_32bit_el0())
		pmcr |= ARMV8_PMU_PMCR_LC;

	__vcpu_sys_reg(vcpu, r->reg) = pmcr;
}

static bool check_pmu_access_disabled(struct kvm_vcpu *vcpu, u64 flags)
{
	u64 reg = __vcpu_sys_reg(vcpu, PMUSERENR_EL0);
	bool enabled = (reg & flags) || vcpu_mode_priv(vcpu);

	if (!enabled)
		kvm_inject_undefined(vcpu);

	return !enabled;
}

static bool pmu_access_el0_disabled(struct kvm_vcpu *vcpu)
{
	return check_pmu_access_disabled(vcpu, ARMV8_PMU_USERENR_EN);
}

static bool pmu_write_swinc_el0_disabled(struct kvm_vcpu *vcpu)
{
	return check_pmu_access_disabled(vcpu, ARMV8_PMU_USERENR_SW | ARMV8_PMU_USERENR_EN);
}

static bool pmu_access_cycle_counter_el0_disabled(struct kvm_vcpu *vcpu)
{
	return check_pmu_access_disabled(vcpu, ARMV8_PMU_USERENR_CR | ARMV8_PMU_USERENR_EN);
}

static bool pmu_access_event_counter_el0_disabled(struct kvm_vcpu *vcpu)
{
	return check_pmu_access_disabled(vcpu, ARMV8_PMU_USERENR_ER | ARMV8_PMU_USERENR_EN);
}

static bool access_pmcr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	u64 val;

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		/*
		 * Only update writeable bits of PMCR (continuing into
		 * kvm_pmu_handle_pmcr() as well)
		 */
		val = __vcpu_sys_reg(vcpu, PMCR_EL0);
		val &= ~ARMV8_PMU_PMCR_MASK;
		val |= p->regval & ARMV8_PMU_PMCR_MASK;
		if (!kvm_supports_32bit_el0())
			val |= ARMV8_PMU_PMCR_LC;
		kvm_pmu_handle_pmcr(vcpu, val);
	} else {
		/* PMCR.P & PMCR.C are RAZ */
		val = __vcpu_sys_reg(vcpu, PMCR_EL0)
		      & ~(ARMV8_PMU_PMCR_P | ARMV8_PMU_PMCR_C);
		p->regval = val;
	}

	return true;
}

static bool access_pmselr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	if (pmu_access_event_counter_el0_disabled(vcpu))
		return false;

	if (p->is_write)
		__vcpu_sys_reg(vcpu, PMSELR_EL0) = p->regval;
	else
		/* return PMSELR.SEL field */
		p->regval = __vcpu_sys_reg(vcpu, PMSELR_EL0)
			    & ARMV8_PMU_COUNTER_MASK;

	return true;
}

static bool access_pmceid(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u64 pmceid, mask, shift;

	BUG_ON(p->is_write);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	get_access_mask(r, &mask, &shift);

	pmceid = kvm_pmu_get_pmceid(vcpu, (p->Op2 & 1));
	pmceid &= mask;
	pmceid >>= shift;

	p->regval = pmceid;

	return true;
}

static bool pmu_counter_idx_valid(struct kvm_vcpu *vcpu, u64 idx)
{
	u64 pmcr, val;

	pmcr = __vcpu_sys_reg(vcpu, PMCR_EL0);
	val = (pmcr >> ARMV8_PMU_PMCR_N_SHIFT) & ARMV8_PMU_PMCR_N_MASK;
	if (idx >= val && idx != ARMV8_PMU_CYCLE_IDX) {
		kvm_inject_undefined(vcpu);
		return false;
	}

	return true;
}

static int get_pmu_evcntr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 *val)
{
	u64 idx;

	if (r->CRn == 9 && r->CRm == 13 && r->Op2 == 0)
		/* PMCCNTR_EL0 */
		idx = ARMV8_PMU_CYCLE_IDX;
	else
		/* PMEVCNTRn_EL0 */
		idx = ((r->CRm & 3) << 3) | (r->Op2 & 7);

	*val = kvm_pmu_get_counter_value(vcpu, idx);
	return 0;
}

static bool access_pmu_evcntr(struct kvm_vcpu *vcpu,
			      struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	u64 idx = ~0UL;

	if (r->CRn == 9 && r->CRm == 13) {
		if (r->Op2 == 2) {
			/* PMXEVCNTR_EL0 */
			if (pmu_access_event_counter_el0_disabled(vcpu))
				return false;

			idx = __vcpu_sys_reg(vcpu, PMSELR_EL0)
			      & ARMV8_PMU_COUNTER_MASK;
		} else if (r->Op2 == 0) {
			/* PMCCNTR_EL0 */
			if (pmu_access_cycle_counter_el0_disabled(vcpu))
				return false;

			idx = ARMV8_PMU_CYCLE_IDX;
		}
	} else if (r->CRn == 0 && r->CRm == 9) {
		/* PMCCNTR */
		if (pmu_access_event_counter_el0_disabled(vcpu))
			return false;

		idx = ARMV8_PMU_CYCLE_IDX;
	} else if (r->CRn == 14 && (r->CRm & 12) == 8) {
		/* PMEVCNTRn_EL0 */
		if (pmu_access_event_counter_el0_disabled(vcpu))
			return false;

		idx = ((r->CRm & 3) << 3) | (r->Op2 & 7);
	}

	/* Catch any decoding mistake */
	WARN_ON(idx == ~0UL);

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

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (r->CRn == 9 && r->CRm == 13 && r->Op2 == 1) {
		/* PMXEVTYPER_EL0 */
		idx = __vcpu_sys_reg(vcpu, PMSELR_EL0) & ARMV8_PMU_COUNTER_MASK;
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
		__vcpu_sys_reg(vcpu, reg) = p->regval & ARMV8_PMU_EVTYPE_MASK;
		kvm_vcpu_pmu_restore_guest(vcpu);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, reg) & ARMV8_PMU_EVTYPE_MASK;
	}

	return true;
}

static bool access_pmcnten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 val, mask;

	if (pmu_access_el0_disabled(vcpu))
		return false;

	mask = kvm_pmu_valid_counter_mask(vcpu);
	if (p->is_write) {
		val = p->regval & mask;
		if (r->Op2 & 0x1) {
			/* accessing PMCNTENSET_EL0 */
			__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) |= val;
			kvm_pmu_enable_counter_mask(vcpu, val);
			kvm_vcpu_pmu_restore_guest(vcpu);
		} else {
			/* accessing PMCNTENCLR_EL0 */
			__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) &= ~val;
			kvm_pmu_disable_counter_mask(vcpu, val);
		}
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);
	}

	return true;
}

static bool access_pminten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	if (check_pmu_access_disabled(vcpu, 0))
		return false;

	if (p->is_write) {
		u64 val = p->regval & mask;

		if (r->Op2 & 0x1)
			/* accessing PMINTENSET_EL1 */
			__vcpu_sys_reg(vcpu, PMINTENSET_EL1) |= val;
		else
			/* accessing PMINTENCLR_EL1 */
			__vcpu_sys_reg(vcpu, PMINTENSET_EL1) &= ~val;
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMINTENSET_EL1);
	}

	return true;
}

static bool access_pmovs(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		if (r->CRm & 0x2)
			/* accessing PMOVSSET_EL0 */
			__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= (p->regval & mask);
		else
			/* accessing PMOVSCLR_EL0 */
			__vcpu_sys_reg(vcpu, PMOVSSET_EL0) &= ~(p->regval & mask);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMOVSSET_EL0);
	}

	return true;
}

static bool access_pmswinc(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask;

	if (!p->is_write)
		return read_from_write_only(vcpu, p, r);

	if (pmu_write_swinc_el0_disabled(vcpu))
		return false;

	mask = kvm_pmu_valid_counter_mask(vcpu);
	kvm_pmu_software_increment(vcpu, p->regval & mask);
	return true;
}

static bool access_pmuserenr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	if (p->is_write) {
		if (!vcpu_mode_priv(vcpu)) {
			kvm_inject_undefined(vcpu);
			return false;
		}

		__vcpu_sys_reg(vcpu, PMUSERENR_EL0) =
			       p->regval & ARMV8_PMU_USERENR_MASK;
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMUSERENR_EL0)
			    & ARMV8_PMU_USERENR_MASK;
	}

	return true;
}

/* Silly macro to expand the DBG{BCR,BVR,WVR,WCR}n_EL1 registers in one go */
#define DBG_BCR_BVR_WCR_WVR_EL1(n)					\
	{ SYS_DESC(SYS_DBGBVRn_EL1(n)),					\
	  trap_bvr, reset_bvr, 0, 0, get_bvr, set_bvr },		\
	{ SYS_DESC(SYS_DBGBCRn_EL1(n)),					\
	  trap_bcr, reset_bcr, 0, 0, get_bcr, set_bcr },		\
	{ SYS_DESC(SYS_DBGWVRn_EL1(n)),					\
	  trap_wvr, reset_wvr, 0, 0,  get_wvr, set_wvr },		\
	{ SYS_DESC(SYS_DBGWCRn_EL1(n)),					\
	  trap_wcr, reset_wcr, 0, 0,  get_wcr, set_wcr }

#define PMU_SYS_REG(r)						\
	SYS_DESC(r), .reset = reset_pmu_reg, .visibility = pmu_visibility

/* Macro to expand the PMEVCNTRn_EL0 register */
#define PMU_PMEVCNTR_EL0(n)						\
	{ PMU_SYS_REG(SYS_PMEVCNTRn_EL0(n)),				\
	  .reset = reset_pmevcntr, .get_user = get_pmu_evcntr,		\
	  .access = access_pmu_evcntr, .reg = (PMEVCNTR0_EL0 + n), }

/* Macro to expand the PMEVTYPERn_EL0 register */
#define PMU_PMEVTYPER_EL0(n)						\
	{ PMU_SYS_REG(SYS_PMEVTYPERn_EL0(n)),				\
	  .reset = reset_pmevtyper,					\
	  .access = access_pmu_evtyper, .reg = (PMEVTYPER0_EL0 + n), }

static bool undef_access(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	kvm_inject_undefined(vcpu);

	return false;
}

/* Macro to expand the AMU counter and type registers*/
#define AMU_AMEVCNTR0_EL0(n) { SYS_DESC(SYS_AMEVCNTR0_EL0(n)), undef_access }
#define AMU_AMEVTYPER0_EL0(n) { SYS_DESC(SYS_AMEVTYPER0_EL0(n)), undef_access }
#define AMU_AMEVCNTR1_EL0(n) { SYS_DESC(SYS_AMEVCNTR1_EL0(n)), undef_access }
#define AMU_AMEVTYPER1_EL0(n) { SYS_DESC(SYS_AMEVTYPER1_EL0(n)), undef_access }

static unsigned int ptrauth_visibility(const struct kvm_vcpu *vcpu,
			const struct sys_reg_desc *rd)
{
	return vcpu_has_ptrauth(vcpu) ? 0 : REG_HIDDEN;
}

/*
 * If we land here on a PtrAuth access, that is because we didn't
 * fixup the access on exit by allowing the PtrAuth sysregs. The only
 * way this happens is when the guest does not have PtrAuth support
 * enabled.
 */
#define __PTRAUTH_KEY(k)						\
	{ SYS_DESC(SYS_## k), undef_access, reset_unknown, k,		\
	.visibility = ptrauth_visibility}

#define PTRAUTH_KEY(k)							\
	__PTRAUTH_KEY(k ## KEYLO_EL1),					\
	__PTRAUTH_KEY(k ## KEYHI_EL1)

static bool access_arch_timer(struct kvm_vcpu *vcpu,
			      struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	enum kvm_arch_timers tmr;
	enum kvm_arch_timer_regs treg;
	u64 reg = reg_to_encoding(r);

	switch (reg) {
	case SYS_CNTP_TVAL_EL0:
	case SYS_AARCH32_CNTP_TVAL:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_TVAL;
		break;
	case SYS_CNTP_CTL_EL0:
	case SYS_AARCH32_CNTP_CTL:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CTL;
		break;
	case SYS_CNTP_CVAL_EL0:
	case SYS_AARCH32_CNTP_CVAL:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CVAL;
		break;
	case SYS_CNTPCT_EL0:
	case SYS_CNTPCTSS_EL0:
	case SYS_AARCH32_CNTPCT:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CNT;
		break;
	default:
		print_sys_reg_msg(p, "%s", "Unhandled trapped timer register");
		kvm_inject_undefined(vcpu);
		return false;
	}

	if (p->is_write)
		kvm_arm_timer_write_sysreg(vcpu, tmr, treg, p->regval);
	else
		p->regval = kvm_arm_timer_read_sysreg(vcpu, tmr, treg);

	return true;
}

static u8 vcpu_pmuver(const struct kvm_vcpu *vcpu)
{
	if (kvm_vcpu_has_pmu(vcpu))
		return vcpu->kvm->arch.dfr0_pmuver.imp;

	return vcpu->kvm->arch.dfr0_pmuver.unimp;
}

static u8 perfmon_to_pmuver(u8 perfmon)
{
	switch (perfmon) {
	case ID_DFR0_EL1_PerfMon_PMUv3:
		return ID_AA64DFR0_EL1_PMUVer_IMP;
	case ID_DFR0_EL1_PerfMon_IMPDEF:
		return ID_AA64DFR0_EL1_PMUVer_IMP_DEF;
	default:
		/* Anything ARMv8.1+ and NI have the same value. For now. */
		return perfmon;
	}
}

static u8 pmuver_to_perfmon(u8 pmuver)
{
	switch (pmuver) {
	case ID_AA64DFR0_EL1_PMUVer_IMP:
		return ID_DFR0_EL1_PerfMon_PMUv3;
	case ID_AA64DFR0_EL1_PMUVer_IMP_DEF:
		return ID_DFR0_EL1_PerfMon_IMPDEF;
	default:
		/* Anything ARMv8.1+ and NI have the same value. For now. */
		return pmuver;
	}
}

/* Read a sanitised cpufeature ID register by sys_reg_desc */
static u64 read_id_reg(const struct kvm_vcpu *vcpu, struct sys_reg_desc const *r)
{
	u32 id = reg_to_encoding(r);
	u64 val;

	if (sysreg_visible_as_raz(vcpu, r))
		return 0;

	val = read_sanitised_ftr_reg(id);

	switch (id) {
	case SYS_ID_AA64PFR0_EL1:
		if (!vcpu_has_sve(vcpu))
			val &= ~ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_SVE);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_AMU);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2);
		val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2), (u64)vcpu->kvm->arch.pfr0_csv2);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3);
		val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3), (u64)vcpu->kvm->arch.pfr0_csv3);
		if (kvm_vgic_global_state.type == VGIC_V3) {
			val &= ~ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_GIC);
			val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_GIC), 1);
		}
		break;
	case SYS_ID_AA64PFR1_EL1:
		if (!kvm_has_mte(vcpu->kvm))
			val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MTE);

		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_SME);
		break;
	case SYS_ID_AA64ISAR1_EL1:
		if (!vcpu_has_ptrauth(vcpu))
			val &= ~(ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_APA) |
				 ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_API) |
				 ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPA) |
				 ARM64_FEATURE_MASK(ID_AA64ISAR1_EL1_GPI));
		break;
	case SYS_ID_AA64ISAR2_EL1:
		if (!vcpu_has_ptrauth(vcpu))
			val &= ~(ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_APA3) |
				 ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_GPA3));
		if (!cpus_have_final_cap(ARM64_HAS_WFXT))
			val &= ~ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_WFxT);
		val &= ~ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_MOPS);
		break;
	case SYS_ID_AA64DFR0_EL1:
		/* Limit debug to ARMv8.0 */
		val &= ~ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_DebugVer);
		val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_DebugVer), 6);
		/* Set PMUver to the required version */
		val &= ~ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer);
		val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer),
				  vcpu_pmuver(vcpu));
		/* Hide SPE from guests */
		val &= ~ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMSVer);
		break;
	case SYS_ID_DFR0_EL1:
		val &= ~ARM64_FEATURE_MASK(ID_DFR0_EL1_PerfMon);
		val |= FIELD_PREP(ARM64_FEATURE_MASK(ID_DFR0_EL1_PerfMon),
				  pmuver_to_perfmon(vcpu_pmuver(vcpu)));
		break;
	case SYS_ID_AA64MMFR2_EL1:
		val &= ~ID_AA64MMFR2_EL1_CCIDX_MASK;
		break;
	case SYS_ID_MMFR4_EL1:
		val &= ~ARM64_FEATURE_MASK(ID_MMFR4_EL1_CCIDX);
		break;
	}

	return val;
}

static unsigned int id_visibility(const struct kvm_vcpu *vcpu,
				  const struct sys_reg_desc *r)
{
	u32 id = reg_to_encoding(r);

	switch (id) {
	case SYS_ID_AA64ZFR0_EL1:
		if (!vcpu_has_sve(vcpu))
			return REG_RAZ;
		break;
	}

	return 0;
}

static unsigned int aa32_id_visibility(const struct kvm_vcpu *vcpu,
				       const struct sys_reg_desc *r)
{
	/*
	 * AArch32 ID registers are UNKNOWN if AArch32 isn't implemented at any
	 * EL. Promote to RAZ/WI in order to guarantee consistency between
	 * systems.
	 */
	if (!kvm_supports_32bit_el0())
		return REG_RAZ | REG_USER_WI;

	return id_visibility(vcpu, r);
}

static unsigned int raz_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *r)
{
	return REG_RAZ;
}

/* cpufeature ID register access trap handlers */

static bool access_id_reg(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = read_id_reg(vcpu, r);
	if (vcpu_has_nv(vcpu))
		access_nested_id_reg(vcpu, p, r);

	return true;
}

/* Visibility overrides for SVE-specific control registers */
static unsigned int sve_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (vcpu_has_sve(vcpu))
		return 0;

	return REG_HIDDEN;
}

static int set_id_aa64pfr0_el1(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *rd,
			       u64 val)
{
	u8 csv2, csv3;

	/*
	 * Allow AA64PFR0_EL1.CSV2 to be set from userspace as long as
	 * it doesn't promise more than what is actually provided (the
	 * guest could otherwise be covered in ectoplasmic residue).
	 */
	csv2 = cpuid_feature_extract_unsigned_field(val, ID_AA64PFR0_EL1_CSV2_SHIFT);
	if (csv2 > 1 ||
	    (csv2 && arm64_get_spectre_v2_state() != SPECTRE_UNAFFECTED))
		return -EINVAL;

	/* Same thing for CSV3 */
	csv3 = cpuid_feature_extract_unsigned_field(val, ID_AA64PFR0_EL1_CSV3_SHIFT);
	if (csv3 > 1 ||
	    (csv3 && arm64_get_meltdown_state() != SPECTRE_UNAFFECTED))
		return -EINVAL;

	/* We can only differ with CSV[23], and anything else is an error */
	val ^= read_id_reg(vcpu, rd);
	val &= ~(ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV2) |
		 ARM64_FEATURE_MASK(ID_AA64PFR0_EL1_CSV3));
	if (val)
		return -EINVAL;

	vcpu->kvm->arch.pfr0_csv2 = csv2;
	vcpu->kvm->arch.pfr0_csv3 = csv3;

	return 0;
}

static int set_id_aa64dfr0_el1(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *rd,
			       u64 val)
{
	u8 pmuver, host_pmuver;
	bool valid_pmu;

	host_pmuver = kvm_arm_pmu_get_pmuver_limit();

	/*
	 * Allow AA64DFR0_EL1.PMUver to be set from userspace as long
	 * as it doesn't promise more than what the HW gives us. We
	 * allow an IMPDEF PMU though, only if no PMU is supported
	 * (KVM backward compatibility handling).
	 */
	pmuver = FIELD_GET(ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer), val);
	if ((pmuver != ID_AA64DFR0_EL1_PMUVer_IMP_DEF && pmuver > host_pmuver))
		return -EINVAL;

	valid_pmu = (pmuver != 0 && pmuver != ID_AA64DFR0_EL1_PMUVer_IMP_DEF);

	/* Make sure view register and PMU support do match */
	if (kvm_vcpu_has_pmu(vcpu) != valid_pmu)
		return -EINVAL;

	/* We can only differ with PMUver, and anything else is an error */
	val ^= read_id_reg(vcpu, rd);
	val &= ~ARM64_FEATURE_MASK(ID_AA64DFR0_EL1_PMUVer);
	if (val)
		return -EINVAL;

	if (valid_pmu)
		vcpu->kvm->arch.dfr0_pmuver.imp = pmuver;
	else
		vcpu->kvm->arch.dfr0_pmuver.unimp = pmuver;

	return 0;
}

static int set_id_dfr0_el1(struct kvm_vcpu *vcpu,
			   const struct sys_reg_desc *rd,
			   u64 val)
{
	u8 perfmon, host_perfmon;
	bool valid_pmu;

	host_perfmon = pmuver_to_perfmon(kvm_arm_pmu_get_pmuver_limit());

	/*
	 * Allow DFR0_EL1.PerfMon to be set from userspace as long as
	 * it doesn't promise more than what the HW gives us on the
	 * AArch64 side (as everything is emulated with that), and
	 * that this is a PMUv3.
	 */
	perfmon = FIELD_GET(ARM64_FEATURE_MASK(ID_DFR0_EL1_PerfMon), val);
	if ((perfmon != ID_DFR0_EL1_PerfMon_IMPDEF && perfmon > host_perfmon) ||
	    (perfmon != 0 && perfmon < ID_DFR0_EL1_PerfMon_PMUv3))
		return -EINVAL;

	valid_pmu = (perfmon != 0 && perfmon != ID_DFR0_EL1_PerfMon_IMPDEF);

	/* Make sure view register and PMU support do match */
	if (kvm_vcpu_has_pmu(vcpu) != valid_pmu)
		return -EINVAL;

	/* We can only differ with PerfMon, and anything else is an error */
	val ^= read_id_reg(vcpu, rd);
	val &= ~ARM64_FEATURE_MASK(ID_DFR0_EL1_PerfMon);
	if (val)
		return -EINVAL;

	if (valid_pmu)
		vcpu->kvm->arch.dfr0_pmuver.imp = perfmon_to_pmuver(perfmon);
	else
		vcpu->kvm->arch.dfr0_pmuver.unimp = perfmon_to_pmuver(perfmon);

	return 0;
}

/*
 * cpufeature ID register user accessors
 *
 * For now, these registers are immutable for userspace, so no values
 * are stored, and for set_id_reg() we don't allow the effective value
 * to be changed.
 */
static int get_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 *val)
{
	*val = read_id_reg(vcpu, rd);
	return 0;
}

static int set_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 val)
{
	/* This is what we mean by invariant: you can't change it. */
	if (val != read_id_reg(vcpu, rd))
		return -EINVAL;

	return 0;
}

static int get_raz_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		       u64 *val)
{
	*val = 0;
	return 0;
}

static int set_wi_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 val)
{
	return 0;
}

static bool access_ctr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
		       const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = read_sanitised_ftr_reg(SYS_CTR_EL0);
	return true;
}

static bool access_clidr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = __vcpu_sys_reg(vcpu, r->reg);
	return true;
}

/*
 * Fabricate a CLIDR_EL1 value instead of using the real value, which can vary
 * by the physical CPU which the vcpu currently resides in.
 */
static void reset_clidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 ctr_el0 = read_sanitised_ftr_reg(SYS_CTR_EL0);
	u64 clidr;
	u8 loc;

	if ((ctr_el0 & CTR_EL0_IDC)) {
		/*
		 * Data cache clean to the PoU is not required so LoUU and LoUIS
		 * will not be set and a unified cache, which will be marked as
		 * LoC, will be added.
		 *
		 * If not DIC, let the unified cache L2 so that an instruction
		 * cache can be added as L1 later.
		 */
		loc = (ctr_el0 & CTR_EL0_DIC) ? 1 : 2;
		clidr = CACHE_TYPE_UNIFIED << CLIDR_CTYPE_SHIFT(loc);
	} else {
		/*
		 * Data cache clean to the PoU is required so let L1 have a data
		 * cache and mark it as LoUU and LoUIS. As L1 has a data cache,
		 * it can be marked as LoC too.
		 */
		loc = 1;
		clidr = 1 << CLIDR_LOUU_SHIFT;
		clidr |= 1 << CLIDR_LOUIS_SHIFT;
		clidr |= CACHE_TYPE_DATA << CLIDR_CTYPE_SHIFT(1);
	}

	/*
	 * Instruction cache invalidation to the PoU is required so let L1 have
	 * an instruction cache. If L1 already has a data cache, it will be
	 * CACHE_TYPE_SEPARATE.
	 */
	if (!(ctr_el0 & CTR_EL0_DIC))
		clidr |= CACHE_TYPE_INST << CLIDR_CTYPE_SHIFT(1);

	clidr |= loc << CLIDR_LOC_SHIFT;

	/*
	 * Add tag cache unified to data cache. Allocation tags and data are
	 * unified in a cache line so that it looks valid even if there is only
	 * one cache line.
	 */
	if (kvm_has_mte(vcpu->kvm))
		clidr |= 2 << CLIDR_TTYPE_SHIFT(loc);

	__vcpu_sys_reg(vcpu, r->reg) = clidr;
}

static int set_clidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 val)
{
	u64 ctr_el0 = read_sanitised_ftr_reg(SYS_CTR_EL0);
	u64 idc = !CLIDR_LOC(val) || (!CLIDR_LOUIS(val) && !CLIDR_LOUU(val));

	if ((val & CLIDR_EL1_RES0) || (!(ctr_el0 & CTR_EL0_IDC) && idc))
		return -EINVAL;

	__vcpu_sys_reg(vcpu, rd->reg) = val;

	return 0;
}

static bool access_csselr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	int reg = r->reg;

	if (p->is_write)
		vcpu_write_sys_reg(vcpu, p->regval, reg);
	else
		p->regval = vcpu_read_sys_reg(vcpu, reg);
	return true;
}

static bool access_ccsidr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 csselr;

	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	csselr = vcpu_read_sys_reg(vcpu, CSSELR_EL1);
	csselr &= CSSELR_EL1_Level | CSSELR_EL1_InD;
	if (csselr < CSSELR_MAX)
		p->regval = get_ccsidr(vcpu, csselr);

	return true;
}

static unsigned int mte_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (kvm_has_mte(vcpu->kvm))
		return 0;

	return REG_HIDDEN;
}

#define MTE_REG(name) {				\
	SYS_DESC(SYS_##name),			\
	.access = undef_access,			\
	.reset = reset_unknown,			\
	.reg = name,				\
	.visibility = mte_visibility,		\
}

static unsigned int el2_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (vcpu_has_nv(vcpu))
		return 0;

	return REG_HIDDEN;
}

#define EL2_REG(name, acc, rst, v) {		\
	SYS_DESC(SYS_##name),			\
	.access = acc,				\
	.reset = rst,				\
	.reg = name,				\
	.visibility = el2_visibility,		\
	.val = v,				\
}

/*
 * EL{0,1}2 registers are the EL2 view on an EL0 or EL1 register when
 * HCR_EL2.E2H==1, and only in the sysreg table for convenience of
 * handling traps. Given that, they are always hidden from userspace.
 */
static unsigned int elx2_visibility(const struct kvm_vcpu *vcpu,
				    const struct sys_reg_desc *rd)
{
	return REG_HIDDEN_USER;
}

#define EL12_REG(name, acc, rst, v) {		\
	SYS_DESC(SYS_##name##_EL12),		\
	.access = acc,				\
	.reset = rst,				\
	.reg = name##_EL1,			\
	.val = v,				\
	.visibility = elx2_visibility,		\
}

/* sys_reg_desc initialiser for known cpufeature ID registers */
#define ID_SANITISED(name) {			\
	SYS_DESC(SYS_##name),			\
	.access	= access_id_reg,		\
	.get_user = get_id_reg,			\
	.set_user = set_id_reg,			\
	.visibility = id_visibility,		\
}

/* sys_reg_desc initialiser for known cpufeature ID registers */
#define AA32_ID_SANITISED(name) {		\
	SYS_DESC(SYS_##name),			\
	.access	= access_id_reg,		\
	.get_user = get_id_reg,			\
	.set_user = set_id_reg,			\
	.visibility = aa32_id_visibility,	\
}

/*
 * sys_reg_desc initialiser for architecturally unallocated cpufeature ID
 * register with encoding Op0=3, Op1=0, CRn=0, CRm=crm, Op2=op2
 * (1 <= crm < 8, 0 <= Op2 < 8).
 */
#define ID_UNALLOCATED(crm, op2) {			\
	Op0(3), Op1(0), CRn(0), CRm(crm), Op2(op2),	\
	.access = access_id_reg,			\
	.get_user = get_id_reg,				\
	.set_user = set_id_reg,				\
	.visibility = raz_visibility			\
}

/*
 * sys_reg_desc initialiser for known ID registers that we hide from guests.
 * For now, these are exposed just like unallocated ID regs: they appear
 * RAZ for the guest.
 */
#define ID_HIDDEN(name) {			\
	SYS_DESC(SYS_##name),			\
	.access = access_id_reg,		\
	.get_user = get_id_reg,			\
	.set_user = set_id_reg,			\
	.visibility = raz_visibility,		\
}

static bool access_sp_el1(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	if (p->is_write)
		__vcpu_sys_reg(vcpu, SP_EL1) = p->regval;
	else
		p->regval = __vcpu_sys_reg(vcpu, SP_EL1);

	return true;
}

static bool access_elr(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *p,
		       const struct sys_reg_desc *r)
{
	if (p->is_write)
		vcpu_write_sys_reg(vcpu, p->regval, ELR_EL1);
	else
		p->regval = vcpu_read_sys_reg(vcpu, ELR_EL1);

	return true;
}

static bool access_spsr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write)
		__vcpu_sys_reg(vcpu, SPSR_EL1) = p->regval;
	else
		p->regval = __vcpu_sys_reg(vcpu, SPSR_EL1);

	return true;
}

/*
 * Architected system registers.
 * Important: Must be sorted ascending by Op0, Op1, CRn, CRm, Op2
 *
 * Debug handling: We do trap most, if not all debug related system
 * registers. The implementation is good enough to ensure that a guest
 * can use these with minimal performance degradation. The drawback is
 * that we don't implement any of the external debug architecture.
 * This should be revisited if we ever encounter a more demanding
 * guest...
 */
static const struct sys_reg_desc sys_reg_descs[] = {
	{ SYS_DESC(SYS_DC_ISW), access_dcsw },
	{ SYS_DESC(SYS_DC_CSW), access_dcsw },
	{ SYS_DESC(SYS_DC_CISW), access_dcsw },

	DBG_BCR_BVR_WCR_WVR_EL1(0),
	DBG_BCR_BVR_WCR_WVR_EL1(1),
	{ SYS_DESC(SYS_MDCCINT_EL1), trap_debug_regs, reset_val, MDCCINT_EL1, 0 },
	{ SYS_DESC(SYS_MDSCR_EL1), trap_debug_regs, reset_val, MDSCR_EL1, 0 },
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

	{ SYS_DESC(SYS_MDRAR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_OSLAR_EL1), trap_oslar_el1 },
	{ SYS_DESC(SYS_OSLSR_EL1), trap_oslsr_el1, reset_val, OSLSR_EL1,
		OSLSR_EL1_OSLM_IMPLEMENTED, .set_user = set_oslsr_el1, },
	{ SYS_DESC(SYS_OSDLR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_DBGPRCR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_DBGCLAIMSET_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_DBGCLAIMCLR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_DBGAUTHSTATUS_EL1), trap_dbgauthstatus_el1 },

	{ SYS_DESC(SYS_MDCCSR_EL0), trap_raz_wi },
	{ SYS_DESC(SYS_DBGDTR_EL0), trap_raz_wi },
	// DBGDTR[TR]X_EL0 share the same encoding
	{ SYS_DESC(SYS_DBGDTRTX_EL0), trap_raz_wi },

	{ SYS_DESC(SYS_DBGVCR32_EL2), NULL, reset_val, DBGVCR32_EL2, 0 },

	{ SYS_DESC(SYS_MPIDR_EL1), NULL, reset_mpidr, MPIDR_EL1 },

	/*
	 * ID regs: all ID_SANITISED() entries here must have corresponding
	 * entries in arm64_ftr_regs[].
	 */

	/* AArch64 mappings of the AArch32 ID registers */
	/* CRm=1 */
	AA32_ID_SANITISED(ID_PFR0_EL1),
	AA32_ID_SANITISED(ID_PFR1_EL1),
	{ SYS_DESC(SYS_ID_DFR0_EL1), .access = access_id_reg,
	  .get_user = get_id_reg, .set_user = set_id_dfr0_el1,
	  .visibility = aa32_id_visibility, },
	ID_HIDDEN(ID_AFR0_EL1),
	AA32_ID_SANITISED(ID_MMFR0_EL1),
	AA32_ID_SANITISED(ID_MMFR1_EL1),
	AA32_ID_SANITISED(ID_MMFR2_EL1),
	AA32_ID_SANITISED(ID_MMFR3_EL1),

	/* CRm=2 */
	AA32_ID_SANITISED(ID_ISAR0_EL1),
	AA32_ID_SANITISED(ID_ISAR1_EL1),
	AA32_ID_SANITISED(ID_ISAR2_EL1),
	AA32_ID_SANITISED(ID_ISAR3_EL1),
	AA32_ID_SANITISED(ID_ISAR4_EL1),
	AA32_ID_SANITISED(ID_ISAR5_EL1),
	AA32_ID_SANITISED(ID_MMFR4_EL1),
	AA32_ID_SANITISED(ID_ISAR6_EL1),

	/* CRm=3 */
	AA32_ID_SANITISED(MVFR0_EL1),
	AA32_ID_SANITISED(MVFR1_EL1),
	AA32_ID_SANITISED(MVFR2_EL1),
	ID_UNALLOCATED(3,3),
	AA32_ID_SANITISED(ID_PFR2_EL1),
	ID_HIDDEN(ID_DFR1_EL1),
	AA32_ID_SANITISED(ID_MMFR5_EL1),
	ID_UNALLOCATED(3,7),

	/* AArch64 ID registers */
	/* CRm=4 */
	{ SYS_DESC(SYS_ID_AA64PFR0_EL1), .access = access_id_reg,
	  .get_user = get_id_reg, .set_user = set_id_aa64pfr0_el1, },
	ID_SANITISED(ID_AA64PFR1_EL1),
	ID_UNALLOCATED(4,2),
	ID_UNALLOCATED(4,3),
	ID_SANITISED(ID_AA64ZFR0_EL1),
	ID_HIDDEN(ID_AA64SMFR0_EL1),
	ID_UNALLOCATED(4,6),
	ID_UNALLOCATED(4,7),

	/* CRm=5 */
	{ SYS_DESC(SYS_ID_AA64DFR0_EL1), .access = access_id_reg,
	  .get_user = get_id_reg, .set_user = set_id_aa64dfr0_el1, },
	ID_SANITISED(ID_AA64DFR1_EL1),
	ID_UNALLOCATED(5,2),
	ID_UNALLOCATED(5,3),
	ID_HIDDEN(ID_AA64AFR0_EL1),
	ID_HIDDEN(ID_AA64AFR1_EL1),
	ID_UNALLOCATED(5,6),
	ID_UNALLOCATED(5,7),

	/* CRm=6 */
	ID_SANITISED(ID_AA64ISAR0_EL1),
	ID_SANITISED(ID_AA64ISAR1_EL1),
	ID_SANITISED(ID_AA64ISAR2_EL1),
	ID_UNALLOCATED(6,3),
	ID_UNALLOCATED(6,4),
	ID_UNALLOCATED(6,5),
	ID_UNALLOCATED(6,6),
	ID_UNALLOCATED(6,7),

	/* CRm=7 */
	ID_SANITISED(ID_AA64MMFR0_EL1),
	ID_SANITISED(ID_AA64MMFR1_EL1),
	ID_SANITISED(ID_AA64MMFR2_EL1),
	ID_UNALLOCATED(7,3),
	ID_UNALLOCATED(7,4),
	ID_UNALLOCATED(7,5),
	ID_UNALLOCATED(7,6),
	ID_UNALLOCATED(7,7),

	{ SYS_DESC(SYS_SCTLR_EL1), access_vm_reg, reset_val, SCTLR_EL1, 0x00C50078 },
	{ SYS_DESC(SYS_ACTLR_EL1), access_actlr, reset_actlr, ACTLR_EL1 },
	{ SYS_DESC(SYS_CPACR_EL1), NULL, reset_val, CPACR_EL1, 0 },

	MTE_REG(RGSR_EL1),
	MTE_REG(GCR_EL1),

	{ SYS_DESC(SYS_ZCR_EL1), NULL, reset_val, ZCR_EL1, 0, .visibility = sve_visibility },
	{ SYS_DESC(SYS_TRFCR_EL1), undef_access },
	{ SYS_DESC(SYS_SMPRI_EL1), undef_access },
	{ SYS_DESC(SYS_SMCR_EL1), undef_access },
	{ SYS_DESC(SYS_TTBR0_EL1), access_vm_reg, reset_unknown, TTBR0_EL1 },
	{ SYS_DESC(SYS_TTBR1_EL1), access_vm_reg, reset_unknown, TTBR1_EL1 },
	{ SYS_DESC(SYS_TCR_EL1), access_vm_reg, reset_val, TCR_EL1, 0 },

	PTRAUTH_KEY(APIA),
	PTRAUTH_KEY(APIB),
	PTRAUTH_KEY(APDA),
	PTRAUTH_KEY(APDB),
	PTRAUTH_KEY(APGA),

	{ SYS_DESC(SYS_SPSR_EL1), access_spsr},
	{ SYS_DESC(SYS_ELR_EL1), access_elr},

	{ SYS_DESC(SYS_AFSR0_EL1), access_vm_reg, reset_unknown, AFSR0_EL1 },
	{ SYS_DESC(SYS_AFSR1_EL1), access_vm_reg, reset_unknown, AFSR1_EL1 },
	{ SYS_DESC(SYS_ESR_EL1), access_vm_reg, reset_unknown, ESR_EL1 },

	{ SYS_DESC(SYS_ERRIDR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERRSELR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXFR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXCTLR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXSTATUS_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXADDR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXMISC0_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_ERXMISC1_EL1), trap_raz_wi },

	MTE_REG(TFSR_EL1),
	MTE_REG(TFSRE0_EL1),

	{ SYS_DESC(SYS_FAR_EL1), access_vm_reg, reset_unknown, FAR_EL1 },
	{ SYS_DESC(SYS_PAR_EL1), NULL, reset_unknown, PAR_EL1 },

	{ SYS_DESC(SYS_PMSCR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSNEVFR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSICR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSIRR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSFCR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSEVFR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSLATFR_EL1), undef_access },
	{ SYS_DESC(SYS_PMSIDR_EL1), undef_access },
	{ SYS_DESC(SYS_PMBLIMITR_EL1), undef_access },
	{ SYS_DESC(SYS_PMBPTR_EL1), undef_access },
	{ SYS_DESC(SYS_PMBSR_EL1), undef_access },
	/* PMBIDR_EL1 is not trapped */

	{ PMU_SYS_REG(SYS_PMINTENSET_EL1),
	  .access = access_pminten, .reg = PMINTENSET_EL1 },
	{ PMU_SYS_REG(SYS_PMINTENCLR_EL1),
	  .access = access_pminten, .reg = PMINTENSET_EL1 },
	{ SYS_DESC(SYS_PMMIR_EL1), trap_raz_wi },

	{ SYS_DESC(SYS_MAIR_EL1), access_vm_reg, reset_unknown, MAIR_EL1 },
	{ SYS_DESC(SYS_AMAIR_EL1), access_vm_reg, reset_amair_el1, AMAIR_EL1 },

	{ SYS_DESC(SYS_LORSA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LOREA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORN_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORC_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORID_EL1), trap_loregion },

	{ SYS_DESC(SYS_VBAR_EL1), access_rw, reset_val, VBAR_EL1, 0 },
	{ SYS_DESC(SYS_DISR_EL1), NULL, reset_val, DISR_EL1, 0 },

	{ SYS_DESC(SYS_ICC_IAR0_EL1), write_to_read_only },
	{ SYS_DESC(SYS_ICC_EOIR0_EL1), read_from_write_only },
	{ SYS_DESC(SYS_ICC_HPPIR0_EL1), write_to_read_only },
	{ SYS_DESC(SYS_ICC_DIR_EL1), read_from_write_only },
	{ SYS_DESC(SYS_ICC_RPR_EL1), write_to_read_only },
	{ SYS_DESC(SYS_ICC_SGI1R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_ASGI1R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_SGI0R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_IAR1_EL1), write_to_read_only },
	{ SYS_DESC(SYS_ICC_EOIR1_EL1), read_from_write_only },
	{ SYS_DESC(SYS_ICC_HPPIR1_EL1), write_to_read_only },
	{ SYS_DESC(SYS_ICC_SRE_EL1), access_gic_sre },

	{ SYS_DESC(SYS_CONTEXTIDR_EL1), access_vm_reg, reset_val, CONTEXTIDR_EL1, 0 },
	{ SYS_DESC(SYS_TPIDR_EL1), NULL, reset_unknown, TPIDR_EL1 },

	{ SYS_DESC(SYS_SCXTNUM_EL1), undef_access },

	{ SYS_DESC(SYS_CNTKCTL_EL1), NULL, reset_val, CNTKCTL_EL1, 0},

	{ SYS_DESC(SYS_CCSIDR_EL1), access_ccsidr },
	{ SYS_DESC(SYS_CLIDR_EL1), access_clidr, reset_clidr, CLIDR_EL1,
	  .set_user = set_clidr },
	{ SYS_DESC(SYS_CCSIDR2_EL1), undef_access },
	{ SYS_DESC(SYS_SMIDR_EL1), undef_access },
	{ SYS_DESC(SYS_CSSELR_EL1), access_csselr, reset_unknown, CSSELR_EL1 },
	{ SYS_DESC(SYS_CTR_EL0), access_ctr },
	{ SYS_DESC(SYS_SVCR), undef_access },

	{ PMU_SYS_REG(SYS_PMCR_EL0), .access = access_pmcr,
	  .reset = reset_pmcr, .reg = PMCR_EL0 },
	{ PMU_SYS_REG(SYS_PMCNTENSET_EL0),
	  .access = access_pmcnten, .reg = PMCNTENSET_EL0 },
	{ PMU_SYS_REG(SYS_PMCNTENCLR_EL0),
	  .access = access_pmcnten, .reg = PMCNTENSET_EL0 },
	{ PMU_SYS_REG(SYS_PMOVSCLR_EL0),
	  .access = access_pmovs, .reg = PMOVSSET_EL0 },
	/*
	 * PM_SWINC_EL0 is exposed to userspace as RAZ/WI, as it was
	 * previously (and pointlessly) advertised in the past...
	 */
	{ PMU_SYS_REG(SYS_PMSWINC_EL0),
	  .get_user = get_raz_reg, .set_user = set_wi_reg,
	  .access = access_pmswinc, .reset = NULL },
	{ PMU_SYS_REG(SYS_PMSELR_EL0),
	  .access = access_pmselr, .reset = reset_pmselr, .reg = PMSELR_EL0 },
	{ PMU_SYS_REG(SYS_PMCEID0_EL0),
	  .access = access_pmceid, .reset = NULL },
	{ PMU_SYS_REG(SYS_PMCEID1_EL0),
	  .access = access_pmceid, .reset = NULL },
	{ PMU_SYS_REG(SYS_PMCCNTR_EL0),
	  .access = access_pmu_evcntr, .reset = reset_unknown,
	  .reg = PMCCNTR_EL0, .get_user = get_pmu_evcntr},
	{ PMU_SYS_REG(SYS_PMXEVTYPER_EL0),
	  .access = access_pmu_evtyper, .reset = NULL },
	{ PMU_SYS_REG(SYS_PMXEVCNTR_EL0),
	  .access = access_pmu_evcntr, .reset = NULL },
	/*
	 * PMUSERENR_EL0 resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ PMU_SYS_REG(SYS_PMUSERENR_EL0), .access = access_pmuserenr,
	  .reset = reset_val, .reg = PMUSERENR_EL0, .val = 0 },
	{ PMU_SYS_REG(SYS_PMOVSSET_EL0),
	  .access = access_pmovs, .reg = PMOVSSET_EL0 },

	{ SYS_DESC(SYS_TPIDR_EL0), NULL, reset_unknown, TPIDR_EL0 },
	{ SYS_DESC(SYS_TPIDRRO_EL0), NULL, reset_unknown, TPIDRRO_EL0 },
	{ SYS_DESC(SYS_TPIDR2_EL0), undef_access },

	{ SYS_DESC(SYS_SCXTNUM_EL0), undef_access },

	{ SYS_DESC(SYS_AMCR_EL0), undef_access },
	{ SYS_DESC(SYS_AMCFGR_EL0), undef_access },
	{ SYS_DESC(SYS_AMCGCR_EL0), undef_access },
	{ SYS_DESC(SYS_AMUSERENR_EL0), undef_access },
	{ SYS_DESC(SYS_AMCNTENCLR0_EL0), undef_access },
	{ SYS_DESC(SYS_AMCNTENSET0_EL0), undef_access },
	{ SYS_DESC(SYS_AMCNTENCLR1_EL0), undef_access },
	{ SYS_DESC(SYS_AMCNTENSET1_EL0), undef_access },
	AMU_AMEVCNTR0_EL0(0),
	AMU_AMEVCNTR0_EL0(1),
	AMU_AMEVCNTR0_EL0(2),
	AMU_AMEVCNTR0_EL0(3),
	AMU_AMEVCNTR0_EL0(4),
	AMU_AMEVCNTR0_EL0(5),
	AMU_AMEVCNTR0_EL0(6),
	AMU_AMEVCNTR0_EL0(7),
	AMU_AMEVCNTR0_EL0(8),
	AMU_AMEVCNTR0_EL0(9),
	AMU_AMEVCNTR0_EL0(10),
	AMU_AMEVCNTR0_EL0(11),
	AMU_AMEVCNTR0_EL0(12),
	AMU_AMEVCNTR0_EL0(13),
	AMU_AMEVCNTR0_EL0(14),
	AMU_AMEVCNTR0_EL0(15),
	AMU_AMEVTYPER0_EL0(0),
	AMU_AMEVTYPER0_EL0(1),
	AMU_AMEVTYPER0_EL0(2),
	AMU_AMEVTYPER0_EL0(3),
	AMU_AMEVTYPER0_EL0(4),
	AMU_AMEVTYPER0_EL0(5),
	AMU_AMEVTYPER0_EL0(6),
	AMU_AMEVTYPER0_EL0(7),
	AMU_AMEVTYPER0_EL0(8),
	AMU_AMEVTYPER0_EL0(9),
	AMU_AMEVTYPER0_EL0(10),
	AMU_AMEVTYPER0_EL0(11),
	AMU_AMEVTYPER0_EL0(12),
	AMU_AMEVTYPER0_EL0(13),
	AMU_AMEVTYPER0_EL0(14),
	AMU_AMEVTYPER0_EL0(15),
	AMU_AMEVCNTR1_EL0(0),
	AMU_AMEVCNTR1_EL0(1),
	AMU_AMEVCNTR1_EL0(2),
	AMU_AMEVCNTR1_EL0(3),
	AMU_AMEVCNTR1_EL0(4),
	AMU_AMEVCNTR1_EL0(5),
	AMU_AMEVCNTR1_EL0(6),
	AMU_AMEVCNTR1_EL0(7),
	AMU_AMEVCNTR1_EL0(8),
	AMU_AMEVCNTR1_EL0(9),
	AMU_AMEVCNTR1_EL0(10),
	AMU_AMEVCNTR1_EL0(11),
	AMU_AMEVCNTR1_EL0(12),
	AMU_AMEVCNTR1_EL0(13),
	AMU_AMEVCNTR1_EL0(14),
	AMU_AMEVCNTR1_EL0(15),
	AMU_AMEVTYPER1_EL0(0),
	AMU_AMEVTYPER1_EL0(1),
	AMU_AMEVTYPER1_EL0(2),
	AMU_AMEVTYPER1_EL0(3),
	AMU_AMEVTYPER1_EL0(4),
	AMU_AMEVTYPER1_EL0(5),
	AMU_AMEVTYPER1_EL0(6),
	AMU_AMEVTYPER1_EL0(7),
	AMU_AMEVTYPER1_EL0(8),
	AMU_AMEVTYPER1_EL0(9),
	AMU_AMEVTYPER1_EL0(10),
	AMU_AMEVTYPER1_EL0(11),
	AMU_AMEVTYPER1_EL0(12),
	AMU_AMEVTYPER1_EL0(13),
	AMU_AMEVTYPER1_EL0(14),
	AMU_AMEVTYPER1_EL0(15),

	{ SYS_DESC(SYS_CNTPCT_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTPCTSS_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_TVAL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CTL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CVAL_EL0), access_arch_timer },

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
	/*
	 * PMCCFILTR_EL0 resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ PMU_SYS_REG(SYS_PMCCFILTR_EL0), .access = access_pmu_evtyper,
	  .reset = reset_val, .reg = PMCCFILTR_EL0, .val = 0 },

	EL2_REG(VPIDR_EL2, access_rw, reset_unknown, 0),
	EL2_REG(VMPIDR_EL2, access_rw, reset_unknown, 0),
	EL2_REG(SCTLR_EL2, access_rw, reset_val, SCTLR_EL2_RES1),
	EL2_REG(ACTLR_EL2, access_rw, reset_val, 0),
	EL2_REG(HCR_EL2, access_rw, reset_val, 0),
	EL2_REG(MDCR_EL2, access_rw, reset_val, 0),
	EL2_REG(CPTR_EL2, access_rw, reset_val, CPTR_EL2_DEFAULT ),
	EL2_REG(HSTR_EL2, access_rw, reset_val, 0),
	EL2_REG(HACR_EL2, access_rw, reset_val, 0),

	EL2_REG(TTBR0_EL2, access_rw, reset_val, 0),
	EL2_REG(TTBR1_EL2, access_rw, reset_val, 0),
	EL2_REG(TCR_EL2, access_rw, reset_val, TCR_EL2_RES1),
	EL2_REG(VTTBR_EL2, access_rw, reset_val, 0),
	EL2_REG(VTCR_EL2, access_rw, reset_val, 0),

	{ SYS_DESC(SYS_DACR32_EL2), NULL, reset_unknown, DACR32_EL2 },
	EL2_REG(SPSR_EL2, access_rw, reset_val, 0),
	EL2_REG(ELR_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_SP_EL1), access_sp_el1},

	{ SYS_DESC(SYS_IFSR32_EL2), NULL, reset_unknown, IFSR32_EL2 },
	EL2_REG(AFSR0_EL2, access_rw, reset_val, 0),
	EL2_REG(AFSR1_EL2, access_rw, reset_val, 0),
	EL2_REG(ESR_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_FPEXC32_EL2), NULL, reset_val, FPEXC32_EL2, 0x700 },

	EL2_REG(FAR_EL2, access_rw, reset_val, 0),
	EL2_REG(HPFAR_EL2, access_rw, reset_val, 0),

	EL2_REG(MAIR_EL2, access_rw, reset_val, 0),
	EL2_REG(AMAIR_EL2, access_rw, reset_val, 0),

	EL2_REG(VBAR_EL2, access_rw, reset_val, 0),
	EL2_REG(RVBAR_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_RMR_EL2), trap_undef },

	EL2_REG(CONTEXTIDR_EL2, access_rw, reset_val, 0),
	EL2_REG(TPIDR_EL2, access_rw, reset_val, 0),

	EL2_REG(CNTVOFF_EL2, access_rw, reset_val, 0),
	EL2_REG(CNTHCTL_EL2, access_rw, reset_val, 0),

	EL12_REG(SCTLR, access_vm_reg, reset_val, 0x00C50078),
	EL12_REG(CPACR, access_rw, reset_val, 0),
	EL12_REG(TTBR0, access_vm_reg, reset_unknown, 0),
	EL12_REG(TTBR1, access_vm_reg, reset_unknown, 0),
	EL12_REG(TCR, access_vm_reg, reset_val, 0),
	{ SYS_DESC(SYS_SPSR_EL12), access_spsr},
	{ SYS_DESC(SYS_ELR_EL12), access_elr},
	EL12_REG(AFSR0, access_vm_reg, reset_unknown, 0),
	EL12_REG(AFSR1, access_vm_reg, reset_unknown, 0),
	EL12_REG(ESR, access_vm_reg, reset_unknown, 0),
	EL12_REG(FAR, access_vm_reg, reset_unknown, 0),
	EL12_REG(MAIR, access_vm_reg, reset_unknown, 0),
	EL12_REG(AMAIR, access_vm_reg, reset_amair_el1, 0),
	EL12_REG(VBAR, access_rw, reset_val, 0),
	EL12_REG(CONTEXTIDR, access_vm_reg, reset_val, 0),
	EL12_REG(CNTKCTL, access_rw, reset_val, 0),

	EL2_REG(SP_EL2, NULL, reset_unknown, 0),
};

static bool trap_dbgdidr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		u64 dfr = read_sanitised_ftr_reg(SYS_ID_AA64DFR0_EL1);
		u64 pfr = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);
		u32 el3 = !!cpuid_feature_extract_unsigned_field(pfr, ID_AA64PFR0_EL1_EL3_SHIFT);

		p->regval = ((((dfr >> ID_AA64DFR0_EL1_WRPs_SHIFT) & 0xf) << 28) |
			     (((dfr >> ID_AA64DFR0_EL1_BRPs_SHIFT) & 0xf) << 24) |
			     (((dfr >> ID_AA64DFR0_EL1_CTX_CMPs_SHIFT) & 0xf) << 20)
			     | (6 << 16) | (1 << 15) | (el3 << 14) | (el3 << 12));
		return true;
	}
}

/*
 * AArch32 debug register mappings
 *
 * AArch32 DBGBVRn is mapped to DBGBVRn_EL1[31:0]
 * AArch32 DBGBXVRn is mapped to DBGBVRn_EL1[63:32]
 *
 * None of the other registers share their location, so treat them as
 * if they were 64bit.
 */
#define DBG_BCR_BVR_WCR_WVR(n)						      \
	/* DBGBVRn */							      \
	{ AA32(LO), Op1( 0), CRn( 0), CRm((n)), Op2( 4), trap_bvr, NULL, n }, \
	/* DBGBCRn */							      \
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 5), trap_bcr, NULL, n },	      \
	/* DBGWVRn */							      \
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 6), trap_wvr, NULL, n },	      \
	/* DBGWCRn */							      \
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 7), trap_wcr, NULL, n }

#define DBGBXVR(n)							      \
	{ AA32(HI), Op1( 0), CRn( 1), CRm((n)), Op2( 1), trap_bvr, NULL, n }

/*
 * Trapped cp14 registers. We generally ignore most of the external
 * debug, on the principle that they don't really make sense to a
 * guest. Revisit this one day, would this principle change.
 */
static const struct sys_reg_desc cp14_regs[] = {
	/* DBGDIDR */
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 0), trap_dbgdidr },
	/* DBGDTRRXext */
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 2), trap_raz_wi },

	DBG_BCR_BVR_WCR_WVR(0),
	/* DBGDSCRint */
	{ Op1( 0), CRn( 0), CRm( 1), Op2( 0), trap_raz_wi },
	DBG_BCR_BVR_WCR_WVR(1),
	/* DBGDCCINT */
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 0), trap_debug_regs, NULL, MDCCINT_EL1 },
	/* DBGDSCRext */
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 2), trap_debug_regs, NULL, MDSCR_EL1 },
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
	{ Op1( 0), CRn( 0), CRm( 7), Op2( 0), trap_debug_regs, NULL, DBGVCR32_EL2 },
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
	{ Op1( 0), CRn( 1), CRm( 0), Op2( 4), trap_oslar_el1 },
	DBGBXVR(1),
	/* DBGOSLSR */
	{ Op1( 0), CRn( 1), CRm( 1), Op2( 4), trap_oslsr_el1, NULL, OSLSR_EL1 },
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

#define CP15_PMU_SYS_REG(_map, _Op1, _CRn, _CRm, _Op2)			\
	AA32(_map),							\
	Op1(_Op1), CRn(_CRn), CRm(_CRm), Op2(_Op2),			\
	.visibility = pmu_visibility

/* Macro to expand the PMEVCNTRn register */
#define PMU_PMEVCNTR(n)							\
	{ CP15_PMU_SYS_REG(DIRECT, 0, 0b1110,				\
	  (0b1000 | (((n) >> 3) & 0x3)), ((n) & 0x7)),			\
	  .access = access_pmu_evcntr }

/* Macro to expand the PMEVTYPERn register */
#define PMU_PMEVTYPER(n)						\
	{ CP15_PMU_SYS_REG(DIRECT, 0, 0b1110,				\
	  (0b1100 | (((n) >> 3) & 0x3)), ((n) & 0x7)),			\
	  .access = access_pmu_evtyper }
/*
 * Trapped cp15 registers. TTBR0/TTBR1 get a double encoding,
 * depending on the way they are accessed (as a 32bit or a 64bit
 * register).
 */
static const struct sys_reg_desc cp15_regs[] = {
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 1), access_ctr },
	{ Op1( 0), CRn( 1), CRm( 0), Op2( 0), access_vm_reg, NULL, SCTLR_EL1 },
	/* ACTLR */
	{ AA32(LO), Op1( 0), CRn( 1), CRm( 0), Op2( 1), access_actlr, NULL, ACTLR_EL1 },
	/* ACTLR2 */
	{ AA32(HI), Op1( 0), CRn( 1), CRm( 0), Op2( 3), access_actlr, NULL, ACTLR_EL1 },
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 0), access_vm_reg, NULL, TTBR0_EL1 },
	{ Op1( 0), CRn( 2), CRm( 0), Op2( 1), access_vm_reg, NULL, TTBR1_EL1 },
	/* TTBCR */
	{ AA32(LO), Op1( 0), CRn( 2), CRm( 0), Op2( 2), access_vm_reg, NULL, TCR_EL1 },
	/* TTBCR2 */
	{ AA32(HI), Op1( 0), CRn( 2), CRm( 0), Op2( 3), access_vm_reg, NULL, TCR_EL1 },
	{ Op1( 0), CRn( 3), CRm( 0), Op2( 0), access_vm_reg, NULL, DACR32_EL2 },
	/* DFSR */
	{ Op1( 0), CRn( 5), CRm( 0), Op2( 0), access_vm_reg, NULL, ESR_EL1 },
	{ Op1( 0), CRn( 5), CRm( 0), Op2( 1), access_vm_reg, NULL, IFSR32_EL2 },
	/* ADFSR */
	{ Op1( 0), CRn( 5), CRm( 1), Op2( 0), access_vm_reg, NULL, AFSR0_EL1 },
	/* AIFSR */
	{ Op1( 0), CRn( 5), CRm( 1), Op2( 1), access_vm_reg, NULL, AFSR1_EL1 },
	/* DFAR */
	{ AA32(LO), Op1( 0), CRn( 6), CRm( 0), Op2( 0), access_vm_reg, NULL, FAR_EL1 },
	/* IFAR */
	{ AA32(HI), Op1( 0), CRn( 6), CRm( 0), Op2( 2), access_vm_reg, NULL, FAR_EL1 },

	/*
	 * DC{C,I,CI}SW operations:
	 */
	{ Op1( 0), CRn( 7), CRm( 6), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(10), Op2( 2), access_dcsw },
	{ Op1( 0), CRn( 7), CRm(14), Op2( 2), access_dcsw },

	/* PMU */
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 0), .access = access_pmcr },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 1), .access = access_pmcnten },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 2), .access = access_pmcnten },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 3), .access = access_pmovs },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 4), .access = access_pmswinc },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 12, 5), .access = access_pmselr },
	{ CP15_PMU_SYS_REG(LO,     0, 9, 12, 6), .access = access_pmceid },
	{ CP15_PMU_SYS_REG(LO,     0, 9, 12, 7), .access = access_pmceid },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 13, 0), .access = access_pmu_evcntr },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 13, 1), .access = access_pmu_evtyper },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 13, 2), .access = access_pmu_evcntr },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 14, 0), .access = access_pmuserenr },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 14, 1), .access = access_pminten },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 14, 2), .access = access_pminten },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 14, 3), .access = access_pmovs },
	{ CP15_PMU_SYS_REG(HI,     0, 9, 14, 4), .access = access_pmceid },
	{ CP15_PMU_SYS_REG(HI,     0, 9, 14, 5), .access = access_pmceid },
	/* PMMIR */
	{ CP15_PMU_SYS_REG(DIRECT, 0, 9, 14, 6), .access = trap_raz_wi },

	/* PRRR/MAIR0 */
	{ AA32(LO), Op1( 0), CRn(10), CRm( 2), Op2( 0), access_vm_reg, NULL, MAIR_EL1 },
	/* NMRR/MAIR1 */
	{ AA32(HI), Op1( 0), CRn(10), CRm( 2), Op2( 1), access_vm_reg, NULL, MAIR_EL1 },
	/* AMAIR0 */
	{ AA32(LO), Op1( 0), CRn(10), CRm( 3), Op2( 0), access_vm_reg, NULL, AMAIR_EL1 },
	/* AMAIR1 */
	{ AA32(HI), Op1( 0), CRn(10), CRm( 3), Op2( 1), access_vm_reg, NULL, AMAIR_EL1 },

	/* ICC_SRE */
	{ Op1( 0), CRn(12), CRm(12), Op2( 5), access_gic_sre },

	{ Op1( 0), CRn(13), CRm( 0), Op2( 1), access_vm_reg, NULL, CONTEXTIDR_EL1 },

	/* Arch Tmers */
	{ SYS_DESC(SYS_AARCH32_CNTP_TVAL), access_arch_timer },
	{ SYS_DESC(SYS_AARCH32_CNTP_CTL), access_arch_timer },

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
	{ CP15_PMU_SYS_REG(DIRECT, 0, 14, 15, 7), .access = access_pmu_evtyper },

	{ Op1(1), CRn( 0), CRm( 0), Op2(0), access_ccsidr },
	{ Op1(1), CRn( 0), CRm( 0), Op2(1), access_clidr },

	/* CCSIDR2 */
	{ Op1(1), CRn( 0), CRm( 0),  Op2(2), undef_access },

	{ Op1(2), CRn( 0), CRm( 0), Op2(0), access_csselr, NULL, CSSELR_EL1 },
};

static const struct sys_reg_desc cp15_64_regs[] = {
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, TTBR0_EL1 },
	{ CP15_PMU_SYS_REG(DIRECT, 0, 0, 9, 0), .access = access_pmu_evcntr },
	{ Op1( 0), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_SGI1R */
	{ SYS_DESC(SYS_AARCH32_CNTPCT),	      access_arch_timer },
	{ Op1( 1), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, TTBR1_EL1 },
	{ Op1( 1), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_ASGI1R */
	{ Op1( 2), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_SGI0R */
	{ SYS_DESC(SYS_AARCH32_CNTP_CVAL),    access_arch_timer },
	{ SYS_DESC(SYS_AARCH32_CNTPCTSS),     access_arch_timer },
};

static bool check_sysreg_table(const struct sys_reg_desc *table, unsigned int n,
			       bool is_32)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (!is_32 && table[i].reg && !table[i].reset) {
			kvm_err("sys_reg table %pS entry %d lacks reset\n", &table[i], i);
			return false;
		}

		if (i && cmp_sys_reg(&table[i-1], &table[i]) >= 0) {
			kvm_err("sys_reg table %pS entry %d out of order\n", &table[i - 1], i - 1);
			return false;
		}
	}

	return true;
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu)
{
	kvm_inject_undefined(vcpu);
	return 1;
}

static void perform_access(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *params,
			   const struct sys_reg_desc *r)
{
	trace_kvm_sys_access(*vcpu_pc(vcpu), params, r);

	/* Check for regs disabled by runtime config */
	if (sysreg_hidden(vcpu, r)) {
		kvm_inject_undefined(vcpu);
		return;
	}

	/*
	 * Not having an accessor means that we have configured a trap
	 * that we don't know how to handle. This certainly qualifies
	 * as a gross bug that should be fixed right away.
	 */
	BUG_ON(!r->access);

	/* Skip instruction if instructed so */
	if (likely(r->access(vcpu, params, r)))
		kvm_incr_pc(vcpu);
}

/*
 * emulate_cp --  tries to match a sys_reg access in a handling table, and
 *                call the corresponding trap handler.
 *
 * @params: pointer to the descriptor of the access
 * @table: array of trap descriptors
 * @num: size of the trap descriptor array
 *
 * Return true if the access has been handled, false if not.
 */
static bool emulate_cp(struct kvm_vcpu *vcpu,
		       struct sys_reg_params *params,
		       const struct sys_reg_desc *table,
		       size_t num)
{
	const struct sys_reg_desc *r;

	if (!table)
		return false;	/* Not handled */

	r = find_reg(params, table, num);

	if (r) {
		perform_access(vcpu, params, r);
		return true;
	}

	/* Not handled */
	return false;
}

static void unhandled_cp_access(struct kvm_vcpu *vcpu,
				struct sys_reg_params *params)
{
	u8 esr_ec = kvm_vcpu_trap_get_class(vcpu);
	int cp = -1;

	switch (esr_ec) {
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

	print_sys_reg_msg(params,
			  "Unsupported guest CP%d access at: %08lx [%08lx]\n",
			  cp, *vcpu_pc(vcpu), *vcpu_cpsr(vcpu));
	kvm_inject_undefined(vcpu);
}

/**
 * kvm_handle_cp_64 -- handles a mrrc/mcrr trap on a guest CP14/CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
static int kvm_handle_cp_64(struct kvm_vcpu *vcpu,
			    const struct sys_reg_desc *global,
			    size_t nr_global)
{
	struct sys_reg_params params;
	u64 esr = kvm_vcpu_get_esr(vcpu);
	int Rt = kvm_vcpu_sys_get_rt(vcpu);
	int Rt2 = (esr >> 10) & 0x1f;

	params.CRm = (esr >> 1) & 0xf;
	params.is_write = ((esr & 1) == 0);

	params.Op0 = 0;
	params.Op1 = (esr >> 16) & 0xf;
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

	/*
	 * If the table contains a handler, handle the
	 * potential register operation in the case of a read and return
	 * with success.
	 */
	if (emulate_cp(vcpu, &params, global, nr_global)) {
		/* Split up the value between registers for the read side */
		if (!params.is_write) {
			vcpu_set_reg(vcpu, Rt, lower_32_bits(params.regval));
			vcpu_set_reg(vcpu, Rt2, upper_32_bits(params.regval));
		}

		return 1;
	}

	unhandled_cp_access(vcpu, &params);
	return 1;
}

static bool emulate_sys_reg(struct kvm_vcpu *vcpu, struct sys_reg_params *params);

/*
 * The CP10 ID registers are architecturally mapped to AArch64 feature
 * registers. Abuse that fact so we can rely on the AArch64 handler for accesses
 * from AArch32.
 */
static bool kvm_esr_cp10_id_to_sys64(u64 esr, struct sys_reg_params *params)
{
	u8 reg_id = (esr >> 10) & 0xf;
	bool valid;

	params->is_write = ((esr & 1) == 0);
	params->Op0 = 3;
	params->Op1 = 0;
	params->CRn = 0;
	params->CRm = 3;

	/* CP10 ID registers are read-only */
	valid = !params->is_write;

	switch (reg_id) {
	/* MVFR0 */
	case 0b0111:
		params->Op2 = 0;
		break;
	/* MVFR1 */
	case 0b0110:
		params->Op2 = 1;
		break;
	/* MVFR2 */
	case 0b0101:
		params->Op2 = 2;
		break;
	default:
		valid = false;
	}

	if (valid)
		return true;

	kvm_pr_unimpl("Unhandled cp10 register %s: %u\n",
		      params->is_write ? "write" : "read", reg_id);
	return false;
}

/**
 * kvm_handle_cp10_id() - Handles a VMRS trap on guest access to a 'Media and
 *			  VFP Register' from AArch32.
 * @vcpu: The vCPU pointer
 *
 * MVFR{0-2} are architecturally mapped to the AArch64 MVFR{0-2}_EL1 registers.
 * Work out the correct AArch64 system register encoding and reroute to the
 * AArch64 system register emulation.
 */
int kvm_handle_cp10_id(struct kvm_vcpu *vcpu)
{
	int Rt = kvm_vcpu_sys_get_rt(vcpu);
	u64 esr = kvm_vcpu_get_esr(vcpu);
	struct sys_reg_params params;

	/* UNDEF on any unhandled register access */
	if (!kvm_esr_cp10_id_to_sys64(esr, &params)) {
		kvm_inject_undefined(vcpu);
		return 1;
	}

	if (emulate_sys_reg(vcpu, &params))
		vcpu_set_reg(vcpu, Rt, params.regval);

	return 1;
}

/**
 * kvm_emulate_cp15_id_reg() - Handles an MRC trap on a guest CP15 access where
 *			       CRn=0, which corresponds to the AArch32 feature
 *			       registers.
 * @vcpu: the vCPU pointer
 * @params: the system register access parameters.
 *
 * Our cp15 system register tables do not enumerate the AArch32 feature
 * registers. Conveniently, our AArch64 table does, and the AArch32 system
 * register encoding can be trivially remapped into the AArch64 for the feature
 * registers: Append op0=3, leaving op1, CRn, CRm, and op2 the same.
 *
 * According to DDI0487G.b G7.3.1, paragraph "Behavior of VMSAv8-32 32-bit
 * System registers with (coproc=0b1111, CRn==c0)", read accesses from this
 * range are either UNKNOWN or RES0. Rerouting remains architectural as we
 * treat undefined registers in this range as RAZ.
 */
static int kvm_emulate_cp15_id_reg(struct kvm_vcpu *vcpu,
				   struct sys_reg_params *params)
{
	int Rt = kvm_vcpu_sys_get_rt(vcpu);

	/* Treat impossible writes to RO registers as UNDEFINED */
	if (params->is_write) {
		unhandled_cp_access(vcpu, params);
		return 1;
	}

	params->Op0 = 3;

	/*
	 * All registers where CRm > 3 are known to be UNKNOWN/RAZ from AArch32.
	 * Avoid conflicting with future expansion of AArch64 feature registers
	 * and simply treat them as RAZ here.
	 */
	if (params->CRm > 3)
		params->regval = 0;
	else if (!emulate_sys_reg(vcpu, params))
		return 1;

	vcpu_set_reg(vcpu, Rt, params->regval);
	return 1;
}

/**
 * kvm_handle_cp_32 -- handles a mrc/mcr trap on a guest CP14/CP15 access
 * @vcpu: The VCPU pointer
 * @run:  The kvm_run struct
 */
static int kvm_handle_cp_32(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *params,
			    const struct sys_reg_desc *global,
			    size_t nr_global)
{
	int Rt  = kvm_vcpu_sys_get_rt(vcpu);

	params->regval = vcpu_get_reg(vcpu, Rt);

	if (emulate_cp(vcpu, params, global, nr_global)) {
		if (!params->is_write)
			vcpu_set_reg(vcpu, Rt, params->regval);
		return 1;
	}

	unhandled_cp_access(vcpu, params);
	return 1;
}

int kvm_handle_cp15_64(struct kvm_vcpu *vcpu)
{
	return kvm_handle_cp_64(vcpu, cp15_64_regs, ARRAY_SIZE(cp15_64_regs));
}

int kvm_handle_cp15_32(struct kvm_vcpu *vcpu)
{
	struct sys_reg_params params;

	params = esr_cp1x_32_to_params(kvm_vcpu_get_esr(vcpu));

	/*
	 * Certain AArch32 ID registers are handled by rerouting to the AArch64
	 * system register table. Registers in the ID range where CRm=0 are
	 * excluded from this scheme as they do not trivially map into AArch64
	 * system register encodings.
	 */
	if (params.Op1 == 0 && params.CRn == 0 && params.CRm)
		return kvm_emulate_cp15_id_reg(vcpu, &params);

	return kvm_handle_cp_32(vcpu, &params, cp15_regs, ARRAY_SIZE(cp15_regs));
}

int kvm_handle_cp14_64(struct kvm_vcpu *vcpu)
{
	return kvm_handle_cp_64(vcpu, cp14_64_regs, ARRAY_SIZE(cp14_64_regs));
}

int kvm_handle_cp14_32(struct kvm_vcpu *vcpu)
{
	struct sys_reg_params params;

	params = esr_cp1x_32_to_params(kvm_vcpu_get_esr(vcpu));

	return kvm_handle_cp_32(vcpu, &params, cp14_regs, ARRAY_SIZE(cp14_regs));
}

static bool is_imp_def_sys_reg(struct sys_reg_params *params)
{
	// See ARM DDI 0487E.a, section D12.3.2
	return params->Op0 == 3 && (params->CRn & 0b1011) == 0b1011;
}

/**
 * emulate_sys_reg - Emulate a guest access to an AArch64 system register
 * @vcpu: The VCPU pointer
 * @params: Decoded system register parameters
 *
 * Return: true if the system register access was successful, false otherwise.
 */
static bool emulate_sys_reg(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *params)
{
	const struct sys_reg_desc *r;

	r = find_reg(params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	if (likely(r)) {
		perform_access(vcpu, params, r);
		return true;
	}

	if (is_imp_def_sys_reg(params)) {
		kvm_inject_undefined(vcpu);
	} else {
		print_sys_reg_msg(params,
				  "Unsupported guest sys_reg access at: %lx [%08lx]\n",
				  *vcpu_pc(vcpu), *vcpu_cpsr(vcpu));
		kvm_inject_undefined(vcpu);
	}
	return false;
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
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(sys_reg_descs); i++)
		if (sys_reg_descs[i].reset)
			sys_reg_descs[i].reset(vcpu, &sys_reg_descs[i]);
}

/**
 * kvm_handle_sys_reg -- handles a mrs/msr trap on a guest sys_reg access
 * @vcpu: The VCPU pointer
 */
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu)
{
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_esr(vcpu);
	int Rt = kvm_vcpu_sys_get_rt(vcpu);

	trace_kvm_handle_sys_reg(esr);

	params = esr_sys64_to_params(esr);
	params.regval = vcpu_get_reg(vcpu, Rt);

	if (!emulate_sys_reg(vcpu, &params))
		return 1;

	if (!params.is_write)
		vcpu_set_reg(vcpu, Rt, params.regval);
	return 1;
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

const struct sys_reg_desc *get_reg_by_id(u64 id,
					 const struct sys_reg_desc table[],
					 unsigned int num)
{
	struct sys_reg_params params;

	if (!index_to_params(id, &params))
		return NULL;

	return find_reg(&params, table, num);
}

/* Decode an index value, and find the sys_reg_desc entry. */
static const struct sys_reg_desc *
id_to_sys_reg_desc(struct kvm_vcpu *vcpu, u64 id,
		   const struct sys_reg_desc table[], unsigned int num)

{
	const struct sys_reg_desc *r;

	/* We only do sys_reg for now. */
	if ((id & KVM_REG_ARM_COPROC_MASK) != KVM_REG_ARM64_SYSREG)
		return NULL;

	r = get_reg_by_id(id, table, num);

	/* Not saved in the sys_reg array and not otherwise accessible? */
	if (r && (!(r->reg || r->get_user) || sysreg_hidden(vcpu, r)))
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
FUNCTION_INVARIANT(revidr_el1)
FUNCTION_INVARIANT(aidr_el1)

static void get_ctr_el0(struct kvm_vcpu *v, const struct sys_reg_desc *r)
{
	((struct sys_reg_desc *)r)->val = read_sanitised_ftr_reg(SYS_CTR_EL0);
}

/* ->val is filled in by kvm_sys_reg_table_init() */
static struct sys_reg_desc invariant_sys_regs[] __ro_after_init = {
	{ SYS_DESC(SYS_MIDR_EL1), NULL, get_midr_el1 },
	{ SYS_DESC(SYS_REVIDR_EL1), NULL, get_revidr_el1 },
	{ SYS_DESC(SYS_AIDR_EL1), NULL, get_aidr_el1 },
	{ SYS_DESC(SYS_CTR_EL0), NULL, get_ctr_el0 },
};

static int get_invariant_sys_reg(u64 id, u64 __user *uaddr)
{
	const struct sys_reg_desc *r;

	r = get_reg_by_id(id, invariant_sys_regs,
			  ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	return put_user(r->val, uaddr);
}

static int set_invariant_sys_reg(u64 id, u64 __user *uaddr)
{
	const struct sys_reg_desc *r;
	u64 val;

	r = get_reg_by_id(id, invariant_sys_regs,
			  ARRAY_SIZE(invariant_sys_regs));
	if (!r)
		return -ENOENT;

	if (get_user(val, uaddr))
		return -EFAULT;

	/* This is what we mean by invariant: you can't change it. */
	if (r->val != val)
		return -EINVAL;

	return 0;
}

static int demux_c15_get(struct kvm_vcpu *vcpu, u64 id, void __user *uaddr)
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
		if (val >= CSSELR_MAX)
			return -ENOENT;

		return put_user(get_ccsidr(vcpu, val), uval);
	default:
		return -ENOENT;
	}
}

static int demux_c15_set(struct kvm_vcpu *vcpu, u64 id, void __user *uaddr)
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
		if (val >= CSSELR_MAX)
			return -ENOENT;

		if (get_user(newval, uval))
			return -EFAULT;

		return set_ccsidr(vcpu, val, newval);
	default:
		return -ENOENT;
	}
}

int kvm_sys_reg_get_user(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg,
			 const struct sys_reg_desc table[], unsigned int num)
{
	u64 __user *uaddr = (u64 __user *)(unsigned long)reg->addr;
	const struct sys_reg_desc *r;
	u64 val;
	int ret;

	r = id_to_sys_reg_desc(vcpu, reg->id, table, num);
	if (!r || sysreg_hidden_user(vcpu, r))
		return -ENOENT;

	if (r->get_user) {
		ret = (r->get_user)(vcpu, r, &val);
	} else {
		val = __vcpu_sys_reg(vcpu, r->reg);
		ret = 0;
	}

	if (!ret)
		ret = put_user(val, uaddr);

	return ret;
}

int kvm_arm_sys_reg_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;
	int err;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_get(vcpu, reg->id, uaddr);

	err = get_invariant_sys_reg(reg->id, uaddr);
	if (err != -ENOENT)
		return err;

	return kvm_sys_reg_get_user(vcpu, reg,
				    sys_reg_descs, ARRAY_SIZE(sys_reg_descs));
}

int kvm_sys_reg_set_user(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg,
			 const struct sys_reg_desc table[], unsigned int num)
{
	u64 __user *uaddr = (u64 __user *)(unsigned long)reg->addr;
	const struct sys_reg_desc *r;
	u64 val;
	int ret;

	if (get_user(val, uaddr))
		return -EFAULT;

	r = id_to_sys_reg_desc(vcpu, reg->id, table, num);
	if (!r || sysreg_hidden_user(vcpu, r))
		return -ENOENT;

	if (sysreg_user_write_ignore(vcpu, r))
		return 0;

	if (r->set_user) {
		ret = (r->set_user)(vcpu, r, val);
	} else {
		__vcpu_sys_reg(vcpu, r->reg) = val;
		ret = 0;
	}

	return ret;
}

int kvm_arm_sys_reg_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg)
{
	void __user *uaddr = (void __user *)(unsigned long)reg->addr;
	int err;

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_set(vcpu, reg->id, uaddr);

	err = set_invariant_sys_reg(reg->id, uaddr);
	if (err != -ENOENT)
		return err;

	return kvm_sys_reg_set_user(vcpu, reg,
				    sys_reg_descs, ARRAY_SIZE(sys_reg_descs));
}

static unsigned int num_demux_regs(void)
{
	return CSSELR_MAX;
}

static int write_demux_regids(u64 __user *uindices)
{
	u64 val = KVM_REG_ARM64 | KVM_REG_SIZE_U32 | KVM_REG_ARM_DEMUX;
	unsigned int i;

	val |= KVM_REG_ARM_DEMUX_ID_CCSIDR;
	for (i = 0; i < CSSELR_MAX; i++) {
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

static int walk_one_sys_reg(const struct kvm_vcpu *vcpu,
			    const struct sys_reg_desc *rd,
			    u64 __user **uind,
			    unsigned int *total)
{
	/*
	 * Ignore registers we trap but don't save,
	 * and for which no custom user accessor is provided.
	 */
	if (!(rd->reg || rd->get_user))
		return 0;

	if (sysreg_hidden_user(vcpu, rd))
		return 0;

	if (!copy_reg_to_user(rd, uind))
		return -EFAULT;

	(*total)++;
	return 0;
}

/* Assumed ordered tables, see kvm_sys_reg_table_init. */
static int walk_sys_regs(struct kvm_vcpu *vcpu, u64 __user *uind)
{
	const struct sys_reg_desc *i2, *end2;
	unsigned int total = 0;
	int err;

	i2 = sys_reg_descs;
	end2 = sys_reg_descs + ARRAY_SIZE(sys_reg_descs);

	while (i2 != end2) {
		err = walk_one_sys_reg(vcpu, i2++, &uind, &total);
		if (err)
			return err;
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

int __init kvm_sys_reg_table_init(void)
{
	bool valid = true;
	unsigned int i;

	/* Make sure tables are unique and in order. */
	valid &= check_sysreg_table(sys_reg_descs, ARRAY_SIZE(sys_reg_descs), false);
	valid &= check_sysreg_table(cp14_regs, ARRAY_SIZE(cp14_regs), true);
	valid &= check_sysreg_table(cp14_64_regs, ARRAY_SIZE(cp14_64_regs), true);
	valid &= check_sysreg_table(cp15_regs, ARRAY_SIZE(cp15_regs), true);
	valid &= check_sysreg_table(cp15_64_regs, ARRAY_SIZE(cp15_64_regs), true);
	valid &= check_sysreg_table(invariant_sys_regs, ARRAY_SIZE(invariant_sys_regs), false);

	if (!valid)
		return -EINVAL;

	/* We abuse the reset function to overwrite the table itself. */
	for (i = 0; i < ARRAY_SIZE(invariant_sys_regs); i++)
		invariant_sys_regs[i].reset(NULL, &invariant_sys_regs[i]);

	return 0;
}
