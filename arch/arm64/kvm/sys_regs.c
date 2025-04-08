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
#include <linux/debugfs.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/uaccess.h>
#include <linux/irqchip/arm-gic-v3.h>

#include <asm/arm_pmuv3.h>
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
#include "vgic/vgic.h"

#include "trace.h"

/*
 * For AArch32, we only take care of what is being trapped. Anything
 * that has to do with init and userspace access has to go via the
 * 64bit interface.
 */

static u64 sys_reg_to_index(const struct sys_reg_desc *reg);
static int set_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 val);

static bool undef_access(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	kvm_inject_undefined(vcpu);
	return false;
}

static bool bad_trap(struct kvm_vcpu *vcpu,
		     struct sys_reg_params *params,
		     const struct sys_reg_desc *r,
		     const char *msg)
{
	WARN_ONCE(1, "Unexpected %s\n", msg);
	print_sys_reg_instr(params);
	return undef_access(vcpu, params, r);
}

static bool read_from_write_only(struct kvm_vcpu *vcpu,
				 struct sys_reg_params *params,
				 const struct sys_reg_desc *r)
{
	return bad_trap(vcpu, params, r,
			"sys_reg read to write-only register");
}

static bool write_to_read_only(struct kvm_vcpu *vcpu,
			       struct sys_reg_params *params,
			       const struct sys_reg_desc *r)
{
	return bad_trap(vcpu, params, r,
			"sys_reg write to read-only register");
}

#define PURE_EL2_SYSREG(el2)						\
	case el2: {							\
		*el1r = el2;						\
		return true;						\
	}

#define MAPPED_EL2_SYSREG(el2, el1, fn)					\
	case el2: {							\
		*xlate = fn;						\
		*el1r = el1;						\
		return true;						\
	}

static bool get_el2_to_el1_mapping(unsigned int reg,
				   unsigned int *el1r, u64 (**xlate)(u64))
{
	switch (reg) {
		PURE_EL2_SYSREG(  VPIDR_EL2	);
		PURE_EL2_SYSREG(  VMPIDR_EL2	);
		PURE_EL2_SYSREG(  ACTLR_EL2	);
		PURE_EL2_SYSREG(  HCR_EL2	);
		PURE_EL2_SYSREG(  MDCR_EL2	);
		PURE_EL2_SYSREG(  HSTR_EL2	);
		PURE_EL2_SYSREG(  HACR_EL2	);
		PURE_EL2_SYSREG(  VTTBR_EL2	);
		PURE_EL2_SYSREG(  VTCR_EL2	);
		PURE_EL2_SYSREG(  RVBAR_EL2	);
		PURE_EL2_SYSREG(  TPIDR_EL2	);
		PURE_EL2_SYSREG(  HPFAR_EL2	);
		PURE_EL2_SYSREG(  HCRX_EL2	);
		PURE_EL2_SYSREG(  HFGRTR_EL2	);
		PURE_EL2_SYSREG(  HFGWTR_EL2	);
		PURE_EL2_SYSREG(  HFGITR_EL2	);
		PURE_EL2_SYSREG(  HDFGRTR_EL2	);
		PURE_EL2_SYSREG(  HDFGWTR_EL2	);
		PURE_EL2_SYSREG(  HAFGRTR_EL2	);
		PURE_EL2_SYSREG(  CNTVOFF_EL2	);
		PURE_EL2_SYSREG(  CNTHCTL_EL2	);
		MAPPED_EL2_SYSREG(SCTLR_EL2,   SCTLR_EL1,
				  translate_sctlr_el2_to_sctlr_el1	     );
		MAPPED_EL2_SYSREG(CPTR_EL2,    CPACR_EL1,
				  translate_cptr_el2_to_cpacr_el1	     );
		MAPPED_EL2_SYSREG(TTBR0_EL2,   TTBR0_EL1,
				  translate_ttbr0_el2_to_ttbr0_el1	     );
		MAPPED_EL2_SYSREG(TTBR1_EL2,   TTBR1_EL1,   NULL	     );
		MAPPED_EL2_SYSREG(TCR_EL2,     TCR_EL1,
				  translate_tcr_el2_to_tcr_el1		     );
		MAPPED_EL2_SYSREG(VBAR_EL2,    VBAR_EL1,    NULL	     );
		MAPPED_EL2_SYSREG(AFSR0_EL2,   AFSR0_EL1,   NULL	     );
		MAPPED_EL2_SYSREG(AFSR1_EL2,   AFSR1_EL1,   NULL	     );
		MAPPED_EL2_SYSREG(ESR_EL2,     ESR_EL1,     NULL	     );
		MAPPED_EL2_SYSREG(FAR_EL2,     FAR_EL1,     NULL	     );
		MAPPED_EL2_SYSREG(MAIR_EL2,    MAIR_EL1,    NULL	     );
		MAPPED_EL2_SYSREG(TCR2_EL2,    TCR2_EL1,    NULL	     );
		MAPPED_EL2_SYSREG(PIR_EL2,     PIR_EL1,     NULL	     );
		MAPPED_EL2_SYSREG(PIRE0_EL2,   PIRE0_EL1,   NULL	     );
		MAPPED_EL2_SYSREG(POR_EL2,     POR_EL1,     NULL	     );
		MAPPED_EL2_SYSREG(AMAIR_EL2,   AMAIR_EL1,   NULL	     );
		MAPPED_EL2_SYSREG(ELR_EL2,     ELR_EL1,	    NULL	     );
		MAPPED_EL2_SYSREG(SPSR_EL2,    SPSR_EL1,    NULL	     );
		MAPPED_EL2_SYSREG(ZCR_EL2,     ZCR_EL1,     NULL	     );
		MAPPED_EL2_SYSREG(CONTEXTIDR_EL2, CONTEXTIDR_EL1, NULL	     );
	default:
		return false;
	}
}

u64 vcpu_read_sys_reg(const struct kvm_vcpu *vcpu, int reg)
{
	u64 val = 0x8badf00d8badf00d;
	u64 (*xlate)(u64) = NULL;
	unsigned int el1r;

	if (!vcpu_get_flag(vcpu, SYSREGS_ON_CPU))
		goto memory_read;

	if (unlikely(get_el2_to_el1_mapping(reg, &el1r, &xlate))) {
		if (!is_hyp_ctxt(vcpu))
			goto memory_read;

		/*
		 * CNTHCTL_EL2 requires some special treatment to
		 * account for the bits that can be set via CNTKCTL_EL1.
		 */
		switch (reg) {
		case CNTHCTL_EL2:
			if (vcpu_el2_e2h_is_set(vcpu)) {
				val = read_sysreg_el1(SYS_CNTKCTL);
				val &= CNTKCTL_VALID_BITS;
				val |= __vcpu_sys_reg(vcpu, reg) & ~CNTKCTL_VALID_BITS;
				return val;
			}
			break;
		}

		/*
		 * If this register does not have an EL1 counterpart,
		 * then read the stored EL2 version.
		 */
		if (reg == el1r)
			goto memory_read;

		/*
		 * If we have a non-VHE guest and that the sysreg
		 * requires translation to be used at EL1, use the
		 * in-memory copy instead.
		 */
		if (!vcpu_el2_e2h_is_set(vcpu) && xlate)
			goto memory_read;

		/* Get the current version of the EL1 counterpart. */
		WARN_ON(!__vcpu_read_sys_reg_from_cpu(el1r, &val));
		if (reg >= __SANITISED_REG_START__)
			val = kvm_vcpu_apply_reg_masks(vcpu, reg, val);

		return val;
	}

	/* EL1 register can't be on the CPU if the guest is in vEL2. */
	if (unlikely(is_hyp_ctxt(vcpu)))
		goto memory_read;

	if (__vcpu_read_sys_reg_from_cpu(reg, &val))
		return val;

memory_read:
	return __vcpu_sys_reg(vcpu, reg);
}

void vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg)
{
	u64 (*xlate)(u64) = NULL;
	unsigned int el1r;

	if (!vcpu_get_flag(vcpu, SYSREGS_ON_CPU))
		goto memory_write;

	if (unlikely(get_el2_to_el1_mapping(reg, &el1r, &xlate))) {
		if (!is_hyp_ctxt(vcpu))
			goto memory_write;

		/*
		 * Always store a copy of the write to memory to avoid having
		 * to reverse-translate virtual EL2 system registers for a
		 * non-VHE guest hypervisor.
		 */
		__vcpu_sys_reg(vcpu, reg) = val;

		switch (reg) {
		case CNTHCTL_EL2:
			/*
			 * If E2H=0, CNHTCTL_EL2 is a pure shadow register.
			 * Otherwise, some of the bits are backed by
			 * CNTKCTL_EL1, while the rest is kept in memory.
			 * Yes, this is fun stuff.
			 */
			if (vcpu_el2_e2h_is_set(vcpu))
				write_sysreg_el1(val, SYS_CNTKCTL);
			return;
		}

		/* No EL1 counterpart? We're done here.? */
		if (reg == el1r)
			return;

		if (!vcpu_el2_e2h_is_set(vcpu) && xlate)
			val = xlate(val);

		/* Redirect this to the EL1 version of the register. */
		WARN_ON(!__vcpu_write_sys_reg_to_cpu(val, el1r));
		return;
	}

	/* EL1 register can't be on the CPU if the guest is in vEL2. */
	if (unlikely(is_hyp_ctxt(vcpu)))
		goto memory_write;

	if (__vcpu_write_sys_reg_to_cpu(val, reg))
		return;

memory_write:
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
	if (!cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
		kvm_set_way_flush(vcpu);

	return true;
}

static bool access_dcgsw(struct kvm_vcpu *vcpu,
			 struct sys_reg_params *p,
			 const struct sys_reg_desc *r)
{
	if (!kvm_has_mte(vcpu->kvm))
		return undef_access(vcpu, p, r);

	/* Treat MTE S/W ops as we treat the classic ones: with contempt */
	return access_dcsw(vcpu, p, r);
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

	if (!kvm_has_gicv3(vcpu->kvm))
		return undef_access(vcpu, p, r);

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
	if (!kvm_has_gicv3(vcpu->kvm))
		return undef_access(vcpu, p, r);

	if (p->is_write)
		return ignore_write(vcpu, p);

	if (p->Op1 == 4) {	/* ICC_SRE_EL2 */
		p->regval = (ICC_SRE_EL2_ENABLE | ICC_SRE_EL2_SRE |
			     ICC_SRE_EL1_DIB | ICC_SRE_EL1_DFB);
	} else {		/* ICC_SRE_EL1 */
		p->regval = vcpu->arch.vgic_cpu.vgic_v3.vgic_sre;
	}

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
	u32 sr = reg_to_encoding(r);

	if (!kvm_has_feat(vcpu->kvm, ID_AA64MMFR1_EL1, LO, IMP))
		return undef_access(vcpu, p, r);

	if (p->is_write && sr == SYS_LORID_EL1)
		return write_to_read_only(vcpu, p, r);

	return trap_raz_wi(vcpu, p, r);
}

static bool trap_oslar_el1(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (!p->is_write)
		return read_from_write_only(vcpu, p, r);

	kvm_debug_handle_oslar(vcpu, p->regval);
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

static bool trap_debug_regs(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	access_rw(vcpu, p, r);

	kvm_debug_set_guest_ownership(vcpu);
	return true;
}

/*
 * reg_to_dbg/dbg_to_reg
 *
 * A 32 bit write to a debug register leave top bits alone
 * A 32 bit read from a debug register only returns the bottom bits
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

static u64 *demux_wb_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd)
{
	struct kvm_guest_debug_arch *dbg = &vcpu->arch.vcpu_debug_state;

	switch (rd->Op2) {
	case 0b100:
		return &dbg->dbg_bvr[rd->CRm];
	case 0b101:
		return &dbg->dbg_bcr[rd->CRm];
	case 0b110:
		return &dbg->dbg_wvr[rd->CRm];
	case 0b111:
		return &dbg->dbg_wcr[rd->CRm];
	default:
		KVM_BUG_ON(1, vcpu->kvm);
		return NULL;
	}
}

static bool trap_dbg_wb_reg(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *rd)
{
	u64 *reg = demux_wb_reg(vcpu, rd);

	if (!reg)
		return false;

	if (p->is_write)
		reg_to_dbg(vcpu, p, rd, reg);
	else
		dbg_to_reg(vcpu, p, rd, reg);

	kvm_debug_set_guest_ownership(vcpu);
	return true;
}

static int set_dbg_wb_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			  u64 val)
{
	u64 *reg = demux_wb_reg(vcpu, rd);

	if (!reg)
		return -EINVAL;

	*reg = val;
	return 0;
}

static int get_dbg_wb_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			  u64 *val)
{
	u64 *reg = demux_wb_reg(vcpu, rd);

	if (!reg)
		return -EINVAL;

	*val = *reg;
	return 0;
}

static u64 reset_dbg_wb_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd)
{
	u64 *reg = demux_wb_reg(vcpu, rd);

	/*
	 * Bail early if we couldn't find storage for the register, the
	 * KVM_BUG_ON() in demux_wb_reg() will prevent this VM from ever
	 * being run.
	 */
	if (!reg)
		return 0;

	*reg = rd->val;
	return rd->val;
}

static u64 reset_amair_el1(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 amair = read_sysreg(amair_el1);
	vcpu_write_sys_reg(vcpu, amair, AMAIR_EL1);
	return amair;
}

static u64 reset_actlr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 actlr = read_sysreg(actlr_el1);
	vcpu_write_sys_reg(vcpu, actlr, ACTLR_EL1);
	return actlr;
}

static u64 reset_mpidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
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
	mpidr |= (1ULL << 31);
	vcpu_write_sys_reg(vcpu, mpidr, MPIDR_EL1);

	return mpidr;
}

static unsigned int pmu_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *r)
{
	if (kvm_vcpu_has_pmu(vcpu))
		return 0;

	return REG_HIDDEN;
}

static u64 reset_pmu_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 mask = BIT(ARMV8_PMU_CYCLE_IDX);
	u8 n = vcpu->kvm->arch.pmcr_n;

	if (n)
		mask |= GENMASK(n - 1, 0);

	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= mask;

	return __vcpu_sys_reg(vcpu, r->reg);
}

static u64 reset_pmevcntr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= GENMASK(31, 0);

	return __vcpu_sys_reg(vcpu, r->reg);
}

static u64 reset_pmevtyper(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	/* This thing will UNDEF, who cares about the reset value? */
	if (!kvm_vcpu_has_pmu(vcpu))
		return 0;

	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= kvm_pmu_evtyper_mask(vcpu->kvm);

	return __vcpu_sys_reg(vcpu, r->reg);
}

static u64 reset_pmselr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	reset_unknown(vcpu, r);
	__vcpu_sys_reg(vcpu, r->reg) &= PMSELR_EL0_SEL_MASK;

	return __vcpu_sys_reg(vcpu, r->reg);
}

static u64 reset_pmcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 pmcr = 0;

	if (!kvm_supports_32bit_el0())
		pmcr |= ARMV8_PMU_PMCR_LC;

	/*
	 * The value of PMCR.N field is included when the
	 * vCPU register is read via kvm_vcpu_read_pmcr().
	 */
	__vcpu_sys_reg(vcpu, r->reg) = pmcr;

	return __vcpu_sys_reg(vcpu, r->reg);
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
		val = kvm_vcpu_read_pmcr(vcpu);
		val &= ~ARMV8_PMU_PMCR_MASK;
		val |= p->regval & ARMV8_PMU_PMCR_MASK;
		if (!kvm_supports_32bit_el0())
			val |= ARMV8_PMU_PMCR_LC;
		kvm_pmu_handle_pmcr(vcpu, val);
	} else {
		/* PMCR.P & PMCR.C are RAZ */
		val = kvm_vcpu_read_pmcr(vcpu)
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
			    & PMSELR_EL0_SEL_MASK;

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

	pmcr = kvm_vcpu_read_pmcr(vcpu);
	val = FIELD_GET(ARMV8_PMU_PMCR_N, pmcr);
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

static int set_pmu_evcntr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 val)
{
	u64 idx;

	if (r->CRn == 9 && r->CRm == 13 && r->Op2 == 0)
		/* PMCCNTR_EL0 */
		idx = ARMV8_PMU_CYCLE_IDX;
	else
		/* PMEVCNTRn_EL0 */
		idx = ((r->CRm & 3) << 3) | (r->Op2 & 7);

	kvm_pmu_set_counter_value_user(vcpu, idx, val);
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

			idx = SYS_FIELD_GET(PMSELR_EL0, SEL,
					    __vcpu_sys_reg(vcpu, PMSELR_EL0));
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
		idx = SYS_FIELD_GET(PMSELR_EL0, SEL, __vcpu_sys_reg(vcpu, PMSELR_EL0));
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
		kvm_vcpu_pmu_restore_guest(vcpu);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, reg);
	}

	return true;
}

static int set_pmreg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r, u64 val)
{
	u64 mask = kvm_pmu_accessible_counter_mask(vcpu);

	__vcpu_sys_reg(vcpu, r->reg) = val & mask;
	kvm_make_request(KVM_REQ_RELOAD_PMU, vcpu);

	return 0;
}

static int get_pmreg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r, u64 *val)
{
	u64 mask = kvm_pmu_accessible_counter_mask(vcpu);

	*val = __vcpu_sys_reg(vcpu, r->reg) & mask;
	return 0;
}

static bool access_pmcnten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 val, mask;

	if (pmu_access_el0_disabled(vcpu))
		return false;

	mask = kvm_pmu_accessible_counter_mask(vcpu);
	if (p->is_write) {
		val = p->regval & mask;
		if (r->Op2 & 0x1)
			/* accessing PMCNTENSET_EL0 */
			__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) |= val;
		else
			/* accessing PMCNTENCLR_EL0 */
			__vcpu_sys_reg(vcpu, PMCNTENSET_EL0) &= ~val;

		kvm_pmu_reprogram_counter_mask(vcpu, val);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMCNTENSET_EL0);
	}

	return true;
}

static bool access_pminten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_accessible_counter_mask(vcpu);

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
	u64 mask = kvm_pmu_accessible_counter_mask(vcpu);

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

	mask = kvm_pmu_accessible_counter_mask(vcpu);
	kvm_pmu_software_increment(vcpu, p->regval & mask);
	return true;
}

static bool access_pmuserenr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	if (p->is_write) {
		if (!vcpu_mode_priv(vcpu))
			return undef_access(vcpu, p, r);

		__vcpu_sys_reg(vcpu, PMUSERENR_EL0) =
			       p->regval & ARMV8_PMU_USERENR_MASK;
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMUSERENR_EL0)
			    & ARMV8_PMU_USERENR_MASK;
	}

	return true;
}

static int get_pmcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		    u64 *val)
{
	*val = kvm_vcpu_read_pmcr(vcpu);
	return 0;
}

static int set_pmcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
		    u64 val)
{
	u8 new_n = FIELD_GET(ARMV8_PMU_PMCR_N, val);
	struct kvm *kvm = vcpu->kvm;

	mutex_lock(&kvm->arch.config_lock);

	/*
	 * The vCPU can't have more counters than the PMU hardware
	 * implements. Ignore this error to maintain compatibility
	 * with the existing KVM behavior.
	 */
	if (!kvm_vm_has_ran_once(kvm) &&
	    new_n <= kvm_arm_pmu_get_max_counters(kvm))
		kvm->arch.pmcr_n = new_n;

	mutex_unlock(&kvm->arch.config_lock);

	/*
	 * Ignore writes to RES0 bits, read only bits that are cleared on
	 * vCPU reset, and writable bits that KVM doesn't support yet.
	 * (i.e. only PMCR.N and bits [7:0] are mutable from userspace)
	 * The LP bit is RES0 when FEAT_PMUv3p5 is not supported on the vCPU.
	 * But, we leave the bit as it is here, as the vCPU's PMUver might
	 * be changed later (NOTE: the bit will be cleared on first vCPU run
	 * if necessary).
	 */
	val &= ARMV8_PMU_PMCR_MASK;

	/* The LC bit is RES1 when AArch32 is not supported */
	if (!kvm_supports_32bit_el0())
		val |= ARMV8_PMU_PMCR_LC;

	__vcpu_sys_reg(vcpu, r->reg) = val;
	kvm_make_request(KVM_REQ_RELOAD_PMU, vcpu);

	return 0;
}

/* Silly macro to expand the DBG{BCR,BVR,WVR,WCR}n_EL1 registers in one go */
#define DBG_BCR_BVR_WCR_WVR_EL1(n)					\
	{ SYS_DESC(SYS_DBGBVRn_EL1(n)),					\
	  trap_dbg_wb_reg, reset_dbg_wb_reg, 0, 0,			\
	  get_dbg_wb_reg, set_dbg_wb_reg },				\
	{ SYS_DESC(SYS_DBGBCRn_EL1(n)),					\
	  trap_dbg_wb_reg, reset_dbg_wb_reg, 0, 0,			\
	  get_dbg_wb_reg, set_dbg_wb_reg },				\
	{ SYS_DESC(SYS_DBGWVRn_EL1(n)),					\
	  trap_dbg_wb_reg, reset_dbg_wb_reg, 0, 0,			\
	  get_dbg_wb_reg, set_dbg_wb_reg },				\
	{ SYS_DESC(SYS_DBGWCRn_EL1(n)),					\
	  trap_dbg_wb_reg, reset_dbg_wb_reg, 0, 0,			\
	  get_dbg_wb_reg, set_dbg_wb_reg }

#define PMU_SYS_REG(name)						\
	SYS_DESC(SYS_##name), .reset = reset_pmu_reg,			\
	.visibility = pmu_visibility

/* Macro to expand the PMEVCNTRn_EL0 register */
#define PMU_PMEVCNTR_EL0(n)						\
	{ PMU_SYS_REG(PMEVCNTRn_EL0(n)),				\
	  .reset = reset_pmevcntr, .get_user = get_pmu_evcntr,		\
	  .set_user = set_pmu_evcntr,					\
	  .access = access_pmu_evcntr, .reg = (PMEVCNTR0_EL0 + n), }

/* Macro to expand the PMEVTYPERn_EL0 register */
#define PMU_PMEVTYPER_EL0(n)						\
	{ PMU_SYS_REG(PMEVTYPERn_EL0(n)),				\
	  .reset = reset_pmevtyper,					\
	  .access = access_pmu_evtyper, .reg = (PMEVTYPER0_EL0 + n), }

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
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HPTIMER;
		else
			tmr = TIMER_PTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_CNTV_TVAL_EL0:
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HVTIMER;
		else
			tmr = TIMER_VTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_AARCH32_CNTP_TVAL:
	case SYS_CNTP_TVAL_EL02:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_CNTV_TVAL_EL02:
		tmr = TIMER_VTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_CNTHP_TVAL_EL2:
		tmr = TIMER_HPTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_CNTHV_TVAL_EL2:
		tmr = TIMER_HVTIMER;
		treg = TIMER_REG_TVAL;
		break;

	case SYS_CNTP_CTL_EL0:
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HPTIMER;
		else
			tmr = TIMER_PTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_CNTV_CTL_EL0:
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HVTIMER;
		else
			tmr = TIMER_VTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_AARCH32_CNTP_CTL:
	case SYS_CNTP_CTL_EL02:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_CNTV_CTL_EL02:
		tmr = TIMER_VTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_CNTHP_CTL_EL2:
		tmr = TIMER_HPTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_CNTHV_CTL_EL2:
		tmr = TIMER_HVTIMER;
		treg = TIMER_REG_CTL;
		break;

	case SYS_CNTP_CVAL_EL0:
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HPTIMER;
		else
			tmr = TIMER_PTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_CNTV_CVAL_EL0:
		if (is_hyp_ctxt(vcpu) && vcpu_el2_e2h_is_set(vcpu))
			tmr = TIMER_HVTIMER;
		else
			tmr = TIMER_VTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_AARCH32_CNTP_CVAL:
	case SYS_CNTP_CVAL_EL02:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_CNTV_CVAL_EL02:
		tmr = TIMER_VTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_CNTHP_CVAL_EL2:
		tmr = TIMER_HPTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_CNTHV_CVAL_EL2:
		tmr = TIMER_HVTIMER;
		treg = TIMER_REG_CVAL;
		break;

	case SYS_CNTPCT_EL0:
	case SYS_CNTPCTSS_EL0:
		if (is_hyp_ctxt(vcpu))
			tmr = TIMER_HPTIMER;
		else
			tmr = TIMER_PTIMER;
		treg = TIMER_REG_CNT;
		break;

	case SYS_AARCH32_CNTPCT:
	case SYS_AARCH32_CNTPCTSS:
		tmr = TIMER_PTIMER;
		treg = TIMER_REG_CNT;
		break;

	case SYS_CNTVCT_EL0:
	case SYS_CNTVCTSS_EL0:
		if (is_hyp_ctxt(vcpu))
			tmr = TIMER_HVTIMER;
		else
			tmr = TIMER_VTIMER;
		treg = TIMER_REG_CNT;
		break;

	case SYS_AARCH32_CNTVCT:
	case SYS_AARCH32_CNTVCTSS:
		tmr = TIMER_VTIMER;
		treg = TIMER_REG_CNT;
		break;

	default:
		print_sys_reg_msg(p, "%s", "Unhandled trapped timer register");
		return undef_access(vcpu, p, r);
	}

	if (p->is_write)
		kvm_arm_timer_write_sysreg(vcpu, tmr, treg, p->regval);
	else
		p->regval = kvm_arm_timer_read_sysreg(vcpu, tmr, treg);

	return true;
}

static bool access_hv_timer(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	if (!vcpu_el2_e2h_is_set(vcpu))
		return undef_access(vcpu, p, r);

	return access_arch_timer(vcpu, p, r);
}

static s64 kvm_arm64_ftr_safe_value(u32 id, const struct arm64_ftr_bits *ftrp,
				    s64 new, s64 cur)
{
	struct arm64_ftr_bits kvm_ftr = *ftrp;

	/* Some features have different safe value type in KVM than host features */
	switch (id) {
	case SYS_ID_AA64DFR0_EL1:
		switch (kvm_ftr.shift) {
		case ID_AA64DFR0_EL1_PMUVer_SHIFT:
			kvm_ftr.type = FTR_LOWER_SAFE;
			break;
		case ID_AA64DFR0_EL1_DebugVer_SHIFT:
			kvm_ftr.type = FTR_LOWER_SAFE;
			break;
		}
		break;
	case SYS_ID_DFR0_EL1:
		if (kvm_ftr.shift == ID_DFR0_EL1_PerfMon_SHIFT)
			kvm_ftr.type = FTR_LOWER_SAFE;
		break;
	}

	return arm64_ftr_safe_value(&kvm_ftr, new, cur);
}

/*
 * arm64_check_features() - Check if a feature register value constitutes
 * a subset of features indicated by the idreg's KVM sanitised limit.
 *
 * This function will check if each feature field of @val is the "safe" value
 * against idreg's KVM sanitised limit return from reset() callback.
 * If a field value in @val is the same as the one in limit, it is always
 * considered the safe value regardless For register fields that are not in
 * writable, only the value in limit is considered the safe value.
 *
 * Return: 0 if all the fields are safe. Otherwise, return negative errno.
 */
static int arm64_check_features(struct kvm_vcpu *vcpu,
				const struct sys_reg_desc *rd,
				u64 val)
{
	const struct arm64_ftr_reg *ftr_reg;
	const struct arm64_ftr_bits *ftrp = NULL;
	u32 id = reg_to_encoding(rd);
	u64 writable_mask = rd->val;
	u64 limit = rd->reset(vcpu, rd);
	u64 mask = 0;

	/*
	 * Hidden and unallocated ID registers may not have a corresponding
	 * struct arm64_ftr_reg. Of course, if the register is RAZ we know the
	 * only safe value is 0.
	 */
	if (sysreg_visible_as_raz(vcpu, rd))
		return val ? -E2BIG : 0;

	ftr_reg = get_arm64_ftr_reg(id);
	if (!ftr_reg)
		return -EINVAL;

	ftrp = ftr_reg->ftr_bits;

	for (; ftrp && ftrp->width; ftrp++) {
		s64 f_val, f_lim, safe_val;
		u64 ftr_mask;

		ftr_mask = arm64_ftr_mask(ftrp);
		if ((ftr_mask & writable_mask) != ftr_mask)
			continue;

		f_val = arm64_ftr_value(ftrp, val);
		f_lim = arm64_ftr_value(ftrp, limit);
		mask |= ftr_mask;

		if (f_val == f_lim)
			safe_val = f_val;
		else
			safe_val = kvm_arm64_ftr_safe_value(id, ftrp, f_val, f_lim);

		if (safe_val != f_val)
			return -E2BIG;
	}

	/* For fields that are not writable, values in limit are the safe values. */
	if ((val & ~mask) != (limit & ~mask))
		return -E2BIG;

	return 0;
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

static u64 sanitise_id_aa64pfr0_el1(const struct kvm_vcpu *vcpu, u64 val);
static u64 sanitise_id_aa64dfr0_el1(const struct kvm_vcpu *vcpu, u64 val);

/* Read a sanitised cpufeature ID register by sys_reg_desc */
static u64 __kvm_read_sanitised_id_reg(const struct kvm_vcpu *vcpu,
				       const struct sys_reg_desc *r)
{
	u32 id = reg_to_encoding(r);
	u64 val;

	if (sysreg_visible_as_raz(vcpu, r))
		return 0;

	val = read_sanitised_ftr_reg(id);

	switch (id) {
	case SYS_ID_AA64DFR0_EL1:
		val = sanitise_id_aa64dfr0_el1(vcpu, val);
		break;
	case SYS_ID_AA64PFR0_EL1:
		val = sanitise_id_aa64pfr0_el1(vcpu, val);
		break;
	case SYS_ID_AA64PFR1_EL1:
		if (!kvm_has_mte(vcpu->kvm))
			val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MTE);

		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_SME);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_RNDR_trap);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_NMI);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MTE_frac);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_GCS);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_THE);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MTEX);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_DF2);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_PFAR);
		val &= ~ARM64_FEATURE_MASK(ID_AA64PFR1_EL1_MPAM_frac);
		break;
	case SYS_ID_AA64PFR2_EL1:
		/* We only expose FPMR */
		val &= ID_AA64PFR2_EL1_FPMR;
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
		if (!cpus_have_final_cap(ARM64_HAS_WFXT) ||
		    has_broken_cntvoff())
			val &= ~ARM64_FEATURE_MASK(ID_AA64ISAR2_EL1_WFxT);
		break;
	case SYS_ID_AA64ISAR3_EL1:
		val &= ID_AA64ISAR3_EL1_FPRCVT | ID_AA64ISAR3_EL1_FAMINMAX;
		break;
	case SYS_ID_AA64MMFR2_EL1:
		val &= ~ID_AA64MMFR2_EL1_CCIDX_MASK;
		val &= ~ID_AA64MMFR2_EL1_NV;
		break;
	case SYS_ID_AA64MMFR3_EL1:
		val &= ID_AA64MMFR3_EL1_TCRX | ID_AA64MMFR3_EL1_S1POE |
			ID_AA64MMFR3_EL1_S1PIE;
		break;
	case SYS_ID_MMFR4_EL1:
		val &= ~ARM64_FEATURE_MASK(ID_MMFR4_EL1_CCIDX);
		break;
	}

	if (vcpu_has_nv(vcpu))
		val = limit_nv_id_reg(vcpu->kvm, id, val);

	return val;
}

static u64 kvm_read_sanitised_id_reg(struct kvm_vcpu *vcpu,
				     const struct sys_reg_desc *r)
{
	return __kvm_read_sanitised_id_reg(vcpu, r);
}

static u64 read_id_reg(const struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	return kvm_read_vm_id_reg(vcpu->kvm, reg_to_encoding(r));
}

static bool is_feature_id_reg(u32 encoding)
{
	return (sys_reg_Op0(encoding) == 3 &&
		(sys_reg_Op1(encoding) < 2 || sys_reg_Op1(encoding) == 3) &&
		sys_reg_CRn(encoding) == 0 &&
		sys_reg_CRm(encoding) <= 7);
}

/*
 * Return true if the register's (Op0, Op1, CRn, CRm, Op2) is
 * (3, 0, 0, crm, op2), where 1<=crm<8, 0<=op2<8, which is the range of ID
 * registers KVM maintains on a per-VM basis.
 *
 * Additionally, the implementation ID registers and CTR_EL0 are handled as
 * per-VM registers.
 */
static inline bool is_vm_ftr_id_reg(u32 id)
{
	switch (id) {
	case SYS_CTR_EL0:
	case SYS_MIDR_EL1:
	case SYS_REVIDR_EL1:
	case SYS_AIDR_EL1:
		return true;
	default:
		return (sys_reg_Op0(id) == 3 && sys_reg_Op1(id) == 0 &&
			sys_reg_CRn(id) == 0 && sys_reg_CRm(id) >= 1 &&
			sys_reg_CRm(id) < 8);

	}
}

static inline bool is_vcpu_ftr_id_reg(u32 id)
{
	return is_feature_id_reg(id) && !is_vm_ftr_id_reg(id);
}

static inline bool is_aa32_id_reg(u32 id)
{
	return (sys_reg_Op0(id) == 3 && sys_reg_Op1(id) == 0 &&
		sys_reg_CRn(id) == 0 && sys_reg_CRm(id) >= 1 &&
		sys_reg_CRm(id) <= 3);
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

static unsigned int sme_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (kvm_has_feat(vcpu->kvm, ID_AA64PFR1_EL1, SME, IMP))
		return 0;

	return REG_HIDDEN;
}

static unsigned int fp8_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (kvm_has_fpmr(vcpu->kvm))
		return 0;

	return REG_HIDDEN;
}

static u64 sanitise_id_aa64pfr0_el1(const struct kvm_vcpu *vcpu, u64 val)
{
	if (!vcpu_has_sve(vcpu))
		val &= ~ID_AA64PFR0_EL1_SVE_MASK;

	/*
	 * The default is to expose CSV2 == 1 if the HW isn't affected.
	 * Although this is a per-CPU feature, we make it global because
	 * asymmetric systems are just a nuisance.
	 *
	 * Userspace can override this as long as it doesn't promise
	 * the impossible.
	 */
	if (arm64_get_spectre_v2_state() == SPECTRE_UNAFFECTED) {
		val &= ~ID_AA64PFR0_EL1_CSV2_MASK;
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, CSV2, IMP);
	}
	if (arm64_get_meltdown_state() == SPECTRE_UNAFFECTED) {
		val &= ~ID_AA64PFR0_EL1_CSV3_MASK;
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, CSV3, IMP);
	}

	if (kvm_vgic_global_state.type == VGIC_V3) {
		val &= ~ID_AA64PFR0_EL1_GIC_MASK;
		val |= SYS_FIELD_PREP_ENUM(ID_AA64PFR0_EL1, GIC, IMP);
	}

	val &= ~ID_AA64PFR0_EL1_AMU_MASK;

	/*
	 * MPAM is disabled by default as KVM also needs a set of PARTID to
	 * program the MPAMVPMx_EL2 PARTID remapping registers with. But some
	 * older kernels let the guest see the ID bit.
	 */
	val &= ~ID_AA64PFR0_EL1_MPAM_MASK;

	return val;
}

static u64 sanitise_id_aa64dfr0_el1(const struct kvm_vcpu *vcpu, u64 val)
{
	val = ID_REG_LIMIT_FIELD_ENUM(val, ID_AA64DFR0_EL1, DebugVer, V8P8);

	/*
	 * Only initialize the PMU version if the vCPU was configured with one.
	 */
	val &= ~ID_AA64DFR0_EL1_PMUVer_MASK;
	if (kvm_vcpu_has_pmu(vcpu))
		val |= SYS_FIELD_PREP(ID_AA64DFR0_EL1, PMUVer,
				      kvm_arm_pmu_get_pmuver_limit());

	/* Hide SPE from guests */
	val &= ~ID_AA64DFR0_EL1_PMSVer_MASK;

	/* Hide BRBE from guests */
	val &= ~ID_AA64DFR0_EL1_BRBE_MASK;

	return val;
}

static int set_id_aa64dfr0_el1(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *rd,
			       u64 val)
{
	u8 debugver = SYS_FIELD_GET(ID_AA64DFR0_EL1, DebugVer, val);
	u8 pmuver = SYS_FIELD_GET(ID_AA64DFR0_EL1, PMUVer, val);

	/*
	 * Prior to commit 3d0dba5764b9 ("KVM: arm64: PMU: Move the
	 * ID_AA64DFR0_EL1.PMUver limit to VM creation"), KVM erroneously
	 * exposed an IMP_DEF PMU to userspace and the guest on systems w/
	 * non-architectural PMUs. Of course, PMUv3 is the only game in town for
	 * PMU virtualization, so the IMP_DEF value was rather user-hostile.
	 *
	 * At minimum, we're on the hook to allow values that were given to
	 * userspace by KVM. Cover our tracks here and replace the IMP_DEF value
	 * with a more sensible NI. The value of an ID register changing under
	 * the nose of the guest is unfortunate, but is certainly no more
	 * surprising than an ill-guided PMU driver poking at impdef system
	 * registers that end in an UNDEF...
	 */
	if (pmuver == ID_AA64DFR0_EL1_PMUVer_IMP_DEF)
		val &= ~ID_AA64DFR0_EL1_PMUVer_MASK;

	/*
	 * ID_AA64DFR0_EL1.DebugVer is one of those awkward fields with a
	 * nonzero minimum safe value.
	 */
	if (debugver < ID_AA64DFR0_EL1_DebugVer_IMP)
		return -EINVAL;

	return set_id_reg(vcpu, rd, val);
}

static u64 read_sanitised_id_dfr0_el1(struct kvm_vcpu *vcpu,
				      const struct sys_reg_desc *rd)
{
	u8 perfmon;
	u64 val = read_sanitised_ftr_reg(SYS_ID_DFR0_EL1);

	val &= ~ID_DFR0_EL1_PerfMon_MASK;
	if (kvm_vcpu_has_pmu(vcpu)) {
		perfmon = pmuver_to_perfmon(kvm_arm_pmu_get_pmuver_limit());
		val |= SYS_FIELD_PREP(ID_DFR0_EL1, PerfMon, perfmon);
	}

	val = ID_REG_LIMIT_FIELD_ENUM(val, ID_DFR0_EL1, CopDbg, Debugv8p8);

	return val;
}

static int set_id_dfr0_el1(struct kvm_vcpu *vcpu,
			   const struct sys_reg_desc *rd,
			   u64 val)
{
	u8 perfmon = SYS_FIELD_GET(ID_DFR0_EL1, PerfMon, val);
	u8 copdbg = SYS_FIELD_GET(ID_DFR0_EL1, CopDbg, val);

	if (perfmon == ID_DFR0_EL1_PerfMon_IMPDEF) {
		val &= ~ID_DFR0_EL1_PerfMon_MASK;
		perfmon = 0;
	}

	/*
	 * Allow DFR0_EL1.PerfMon to be set from userspace as long as
	 * it doesn't promise more than what the HW gives us on the
	 * AArch64 side (as everything is emulated with that), and
	 * that this is a PMUv3.
	 */
	if (perfmon != 0 && perfmon < ID_DFR0_EL1_PerfMon_PMUv3)
		return -EINVAL;

	if (copdbg < ID_DFR0_EL1_CopDbg_Armv8)
		return -EINVAL;

	return set_id_reg(vcpu, rd, val);
}

static int set_id_aa64pfr0_el1(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *rd, u64 user_val)
{
	u64 hw_val = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);
	u64 mpam_mask = ID_AA64PFR0_EL1_MPAM_MASK;

	/*
	 * Commit 011e5f5bf529f ("arm64/cpufeature: Add remaining feature bits
	 * in ID_AA64PFR0 register") exposed the MPAM field of AA64PFR0_EL1 to
	 * guests, but didn't add trap handling. KVM doesn't support MPAM and
	 * always returns an UNDEF for these registers. The guest must see 0
	 * for this field.
	 *
	 * But KVM must also accept values from user-space that were provided
	 * by KVM. On CPUs that support MPAM, permit user-space to write
	 * the sanitizied value to ID_AA64PFR0_EL1.MPAM, but ignore this field.
	 */
	if ((hw_val & mpam_mask) == (user_val & mpam_mask))
		user_val &= ~ID_AA64PFR0_EL1_MPAM_MASK;

	return set_id_reg(vcpu, rd, user_val);
}

static int set_id_aa64pfr1_el1(struct kvm_vcpu *vcpu,
			       const struct sys_reg_desc *rd, u64 user_val)
{
	u64 hw_val = read_sanitised_ftr_reg(SYS_ID_AA64PFR1_EL1);
	u64 mpam_mask = ID_AA64PFR1_EL1_MPAM_frac_MASK;

	/* See set_id_aa64pfr0_el1 for comment about MPAM */
	if ((hw_val & mpam_mask) == (user_val & mpam_mask))
		user_val &= ~ID_AA64PFR1_EL1_MPAM_frac_MASK;

	return set_id_reg(vcpu, rd, user_val);
}

static int set_id_aa64mmfr0_el1(struct kvm_vcpu *vcpu,
				const struct sys_reg_desc *rd, u64 user_val)
{
	u64 sanitized_val = kvm_read_sanitised_id_reg(vcpu, rd);
	u64 tgran2_mask = ID_AA64MMFR0_EL1_TGRAN4_2_MASK |
			  ID_AA64MMFR0_EL1_TGRAN16_2_MASK |
			  ID_AA64MMFR0_EL1_TGRAN64_2_MASK;

	if (vcpu_has_nv(vcpu) &&
	    ((sanitized_val & tgran2_mask) != (user_val & tgran2_mask)))
		return -EINVAL;

	return set_id_reg(vcpu, rd, user_val);
}

static int set_id_aa64mmfr2_el1(struct kvm_vcpu *vcpu,
				const struct sys_reg_desc *rd, u64 user_val)
{
	u64 hw_val = read_sanitised_ftr_reg(SYS_ID_AA64MMFR2_EL1);
	u64 nv_mask = ID_AA64MMFR2_EL1_NV_MASK;

	/*
	 * We made the mistake to expose the now deprecated NV field,
	 * so allow userspace to write it, but silently ignore it.
	 */
	if ((hw_val & nv_mask) == (user_val & nv_mask))
		user_val &= ~nv_mask;

	return set_id_reg(vcpu, rd, user_val);
}

static int set_ctr_el0(struct kvm_vcpu *vcpu,
		       const struct sys_reg_desc *rd, u64 user_val)
{
	u8 user_L1Ip = SYS_FIELD_GET(CTR_EL0, L1Ip, user_val);

	/*
	 * Both AIVIVT (0b01) and VPIPT (0b00) are documented as reserved.
	 * Hence only allow to set VIPT(0b10) or PIPT(0b11) for L1Ip based
	 * on what hardware reports.
	 *
	 * Using a VIPT software model on PIPT will lead to over invalidation,
	 * but still correct. Hence, we can allow downgrading PIPT to VIPT,
	 * but not the other way around. This is handled via arm64_ftr_safe_value()
	 * as CTR_EL0 ftr_bits has L1Ip field with type FTR_EXACT and safe value
	 * set as VIPT.
	 */
	switch (user_L1Ip) {
	case CTR_EL0_L1Ip_RESERVED_VPIPT:
	case CTR_EL0_L1Ip_RESERVED_AIVIVT:
		return -EINVAL;
	case CTR_EL0_L1Ip_VIPT:
	case CTR_EL0_L1Ip_PIPT:
		return set_id_reg(vcpu, rd, user_val);
	default:
		return -ENOENT;
	}
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
	/*
	 * Avoid locking if the VM has already started, as the ID registers are
	 * guaranteed to be invariant at that point.
	 */
	if (kvm_vm_has_ran_once(vcpu->kvm)) {
		*val = read_id_reg(vcpu, rd);
		return 0;
	}

	mutex_lock(&vcpu->kvm->arch.config_lock);
	*val = read_id_reg(vcpu, rd);
	mutex_unlock(&vcpu->kvm->arch.config_lock);

	return 0;
}

static int set_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      u64 val)
{
	u32 id = reg_to_encoding(rd);
	int ret;

	mutex_lock(&vcpu->kvm->arch.config_lock);

	/*
	 * Once the VM has started the ID registers are immutable. Reject any
	 * write that does not match the final register value.
	 */
	if (kvm_vm_has_ran_once(vcpu->kvm)) {
		if (val != read_id_reg(vcpu, rd))
			ret = -EBUSY;
		else
			ret = 0;

		mutex_unlock(&vcpu->kvm->arch.config_lock);
		return ret;
	}

	ret = arm64_check_features(vcpu, rd, val);
	if (!ret)
		kvm_set_vm_id_reg(vcpu->kvm, id, val);

	mutex_unlock(&vcpu->kvm->arch.config_lock);

	/*
	 * arm64_check_features() returns -E2BIG to indicate the register's
	 * feature set is a superset of the maximally-allowed register value.
	 * While it would be nice to precisely describe this to userspace, the
	 * existing UAPI for KVM_SET_ONE_REG has it that invalid register
	 * writes return -EINVAL.
	 */
	if (ret == -E2BIG)
		ret = -EINVAL;
	return ret;
}

void kvm_set_vm_id_reg(struct kvm *kvm, u32 reg, u64 val)
{
	u64 *p = __vm_id_reg(&kvm->arch, reg);

	lockdep_assert_held(&kvm->arch.config_lock);

	if (KVM_BUG_ON(kvm_vm_has_ran_once(kvm) || !p, kvm))
		return;

	*p = val;
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

	p->regval = kvm_read_vm_id_reg(vcpu->kvm, SYS_CTR_EL0);
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
static u64 reset_clidr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
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
		clidr |= 2ULL << CLIDR_TTYPE_SHIFT(loc);

	__vcpu_sys_reg(vcpu, r->reg) = clidr;

	return __vcpu_sys_reg(vcpu, r->reg);
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

static bool bad_vncr_trap(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	/*
	 * We really shouldn't be here, and this is likely the result
	 * of a misconfigured trap, as this register should target the
	 * VNCR page, and nothing else.
	 */
	return bad_trap(vcpu, p, r,
			"trap of VNCR-backed register");
}

static bool bad_redir_trap(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	/*
	 * We really shouldn't be here, and this is likely the result
	 * of a misconfigured trap, as this register should target the
	 * corresponding EL1, and nothing else.
	 */
	return bad_trap(vcpu, p, r,
			"trap of EL2 register redirected to EL1");
}

#define EL2_REG(name, acc, rst, v) {		\
	SYS_DESC(SYS_##name),			\
	.access = acc,				\
	.reset = rst,				\
	.reg = name,				\
	.visibility = el2_visibility,		\
	.val = v,				\
}

#define EL2_REG_FILTERED(name, acc, rst, v, filter) {	\
	SYS_DESC(SYS_##name),			\
	.access = acc,				\
	.reset = rst,				\
	.reg = name,				\
	.visibility = filter,			\
	.val = v,				\
}

#define EL2_REG_VNCR(name, rst, v)	EL2_REG(name, bad_vncr_trap, rst, v)
#define EL2_REG_REDIR(name, rst, v)	EL2_REG(name, bad_redir_trap, rst, v)

/*
 * Since reset() callback and field val are not used for idregs, they will be
 * used for specific purposes for idregs.
 * The reset() would return KVM sanitised register value. The value would be the
 * same as the host kernel sanitised value if there is no KVM sanitisation.
 * The val would be used as a mask indicating writable fields for the idreg.
 * Only bits with 1 are writable from userspace. This mask might not be
 * necessary in the future whenever all ID registers are enabled as writable
 * from userspace.
 */

#define ID_DESC_DEFAULT_CALLBACKS		\
	.access	= access_id_reg,		\
	.get_user = get_id_reg,			\
	.set_user = set_id_reg,			\
	.visibility = id_visibility,		\
	.reset = kvm_read_sanitised_id_reg

#define ID_DESC(name)				\
	SYS_DESC(SYS_##name),			\
	ID_DESC_DEFAULT_CALLBACKS

/* sys_reg_desc initialiser for known cpufeature ID registers */
#define ID_SANITISED(name) {			\
	ID_DESC(name),				\
	.val = 0,				\
}

/* sys_reg_desc initialiser for known cpufeature ID registers */
#define AA32_ID_SANITISED(name) {		\
	ID_DESC(name),				\
	.visibility = aa32_id_visibility,	\
	.val = 0,				\
}

/* sys_reg_desc initialiser for writable ID registers */
#define ID_WRITABLE(name, mask) {		\
	ID_DESC(name),				\
	.val = mask,				\
}

/* sys_reg_desc initialiser for cpufeature ID registers that need filtering */
#define ID_FILTERED(sysreg, name, mask) {	\
	ID_DESC(sysreg),				\
	.set_user = set_##name,				\
	.val = (mask),					\
}

/*
 * sys_reg_desc initialiser for architecturally unallocated cpufeature ID
 * register with encoding Op0=3, Op1=0, CRn=0, CRm=crm, Op2=op2
 * (1 <= crm < 8, 0 <= Op2 < 8).
 */
#define ID_UNALLOCATED(crm, op2) {			\
	.name = "S3_0_0_" #crm "_" #op2,		\
	Op0(3), Op1(0), CRn(0), CRm(crm), Op2(op2),	\
	ID_DESC_DEFAULT_CALLBACKS,			\
	.visibility = raz_visibility,			\
	.val = 0,					\
}

/*
 * sys_reg_desc initialiser for known ID registers that we hide from guests.
 * For now, these are exposed just like unallocated ID regs: they appear
 * RAZ for the guest.
 */
#define ID_HIDDEN(name) {			\
	ID_DESC(name),				\
	.visibility = raz_visibility,		\
	.val = 0,				\
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

static bool access_cntkctl_el12(struct kvm_vcpu *vcpu,
				struct sys_reg_params *p,
				const struct sys_reg_desc *r)
{
	if (p->is_write)
		__vcpu_sys_reg(vcpu, CNTKCTL_EL1) = p->regval;
	else
		p->regval = __vcpu_sys_reg(vcpu, CNTKCTL_EL1);

	return true;
}

static u64 reset_hcr(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	u64 val = r->val;

	if (!cpus_have_final_cap(ARM64_HAS_HCR_NV1))
		val |= HCR_E2H;

	return __vcpu_sys_reg(vcpu, r->reg) = val;
}

static unsigned int __el2_visibility(const struct kvm_vcpu *vcpu,
				     const struct sys_reg_desc *rd,
				     unsigned int (*fn)(const struct kvm_vcpu *,
							const struct sys_reg_desc *))
{
	return el2_visibility(vcpu, rd) ?: fn(vcpu, rd);
}

static unsigned int sve_el2_visibility(const struct kvm_vcpu *vcpu,
				       const struct sys_reg_desc *rd)
{
	return __el2_visibility(vcpu, rd, sve_visibility);
}

static bool access_zcr_el2(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	unsigned int vq;

	if (guest_hyp_sve_traps_enabled(vcpu)) {
		kvm_inject_nested_sve_trap(vcpu);
		return true;
	}

	if (!p->is_write) {
		p->regval = vcpu_read_sys_reg(vcpu, ZCR_EL2);
		return true;
	}

	vq = SYS_FIELD_GET(ZCR_ELx, LEN, p->regval) + 1;
	vq = min(vq, vcpu_sve_max_vq(vcpu));
	vcpu_write_sys_reg(vcpu, vq - 1, ZCR_EL2);

	return true;
}

static bool access_gic_vtr(struct kvm_vcpu *vcpu,
			   struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = kvm_vgic_global_state.ich_vtr_el2;
	p->regval &= ~(ICH_VTR_EL2_DVIM 	|
		       ICH_VTR_EL2_A3V		|
		       ICH_VTR_EL2_IDbits);
	p->regval |= ICH_VTR_EL2_nV4;

	return true;
}

static bool access_gic_misr(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = vgic_v3_get_misr(vcpu);

	return true;
}

static bool access_gic_eisr(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = vgic_v3_get_eisr(vcpu);

	return true;
}

static bool access_gic_elrsr(struct kvm_vcpu *vcpu,
			     struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = vgic_v3_get_elrsr(vcpu);

	return true;
}

static unsigned int s1poe_visibility(const struct kvm_vcpu *vcpu,
				     const struct sys_reg_desc *rd)
{
	if (kvm_has_s1poe(vcpu->kvm))
		return 0;

	return REG_HIDDEN;
}

static unsigned int s1poe_el2_visibility(const struct kvm_vcpu *vcpu,
					 const struct sys_reg_desc *rd)
{
	return __el2_visibility(vcpu, rd, s1poe_visibility);
}

static unsigned int tcr2_visibility(const struct kvm_vcpu *vcpu,
				    const struct sys_reg_desc *rd)
{
	if (kvm_has_tcr2(vcpu->kvm))
		return 0;

	return REG_HIDDEN;
}

static unsigned int tcr2_el2_visibility(const struct kvm_vcpu *vcpu,
				    const struct sys_reg_desc *rd)
{
	return __el2_visibility(vcpu, rd, tcr2_visibility);
}

static unsigned int s1pie_visibility(const struct kvm_vcpu *vcpu,
				     const struct sys_reg_desc *rd)
{
	if (kvm_has_s1pie(vcpu->kvm))
		return 0;

	return REG_HIDDEN;
}

static unsigned int s1pie_el2_visibility(const struct kvm_vcpu *vcpu,
					 const struct sys_reg_desc *rd)
{
	return __el2_visibility(vcpu, rd, s1pie_visibility);
}

static bool access_mdcr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	u64 old = __vcpu_sys_reg(vcpu, MDCR_EL2);

	if (!access_rw(vcpu, p, r))
		return false;

	/*
	 * Request a reload of the PMU to enable/disable the counters affected
	 * by HPME.
	 */
	if ((old ^ __vcpu_sys_reg(vcpu, MDCR_EL2)) & MDCR_EL2_HPME)
		kvm_make_request(KVM_REQ_RELOAD_PMU, vcpu);

	return true;
}

/*
 * For historical (ahem ABI) reasons, KVM treated MIDR_EL1, REVIDR_EL1, and
 * AIDR_EL1 as "invariant" registers, meaning userspace cannot change them.
 * The values made visible to userspace were the register values of the boot
 * CPU.
 *
 * At the same time, reads from these registers at EL1 previously were not
 * trapped, allowing the guest to read the actual hardware value. On big-little
 * machines, this means the VM can see different values depending on where a
 * given vCPU got scheduled.
 *
 * These registers are now trapped as collateral damage from SME, and what
 * follows attempts to give a user / guest view consistent with the existing
 * ABI.
 */
static bool access_imp_id_reg(struct kvm_vcpu *vcpu,
			      struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	/*
	 * Return the VM-scoped implementation ID register values if userspace
	 * has made them writable.
	 */
	if (test_bit(KVM_ARCH_FLAG_WRITABLE_IMP_ID_REGS, &vcpu->kvm->arch.flags))
		return access_id_reg(vcpu, p, r);

	/*
	 * Otherwise, fall back to the old behavior of returning the value of
	 * the current CPU.
	 */
	switch (reg_to_encoding(r)) {
	case SYS_REVIDR_EL1:
		p->regval = read_sysreg(revidr_el1);
		break;
	case SYS_AIDR_EL1:
		p->regval = read_sysreg(aidr_el1);
		break;
	default:
		WARN_ON_ONCE(1);
	}

	return true;
}

static u64 __ro_after_init boot_cpu_midr_val;
static u64 __ro_after_init boot_cpu_revidr_val;
static u64 __ro_after_init boot_cpu_aidr_val;

static void init_imp_id_regs(void)
{
	boot_cpu_midr_val = read_sysreg(midr_el1);
	boot_cpu_revidr_val = read_sysreg(revidr_el1);
	boot_cpu_aidr_val = read_sysreg(aidr_el1);
}

static u64 reset_imp_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r)
{
	switch (reg_to_encoding(r)) {
	case SYS_MIDR_EL1:
		return boot_cpu_midr_val;
	case SYS_REVIDR_EL1:
		return boot_cpu_revidr_val;
	case SYS_AIDR_EL1:
		return boot_cpu_aidr_val;
	default:
		KVM_BUG_ON(1, vcpu->kvm);
		return 0;
	}
}

static int set_imp_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *r,
			  u64 val)
{
	struct kvm *kvm = vcpu->kvm;
	u64 expected;

	guard(mutex)(&kvm->arch.config_lock);

	expected = read_id_reg(vcpu, r);
	if (expected == val)
		return 0;

	if (!test_bit(KVM_ARCH_FLAG_WRITABLE_IMP_ID_REGS, &kvm->arch.flags))
		return -EINVAL;

	/*
	 * Once the VM has started the ID registers are immutable. Reject the
	 * write if userspace tries to change it.
	 */
	if (kvm_vm_has_ran_once(kvm))
		return -EBUSY;

	/*
	 * Any value is allowed for the implementation ID registers so long as
	 * it is within the writable mask.
	 */
	if ((val & r->val) != val)
		return -EINVAL;

	kvm_set_vm_id_reg(kvm, reg_to_encoding(r), val);
	return 0;
}

#define IMPLEMENTATION_ID(reg, mask) {			\
	SYS_DESC(SYS_##reg),				\
	.access = access_imp_id_reg,			\
	.get_user = get_id_reg,				\
	.set_user = set_imp_id_reg,			\
	.reset = reset_imp_id_reg,			\
	.val = mask,					\
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

	{ SYS_DESC(SYS_DBGVCR32_EL2), undef_access, reset_val, DBGVCR32_EL2, 0 },

	IMPLEMENTATION_ID(MIDR_EL1, GENMASK_ULL(31, 0)),
	{ SYS_DESC(SYS_MPIDR_EL1), NULL, reset_mpidr, MPIDR_EL1 },
	IMPLEMENTATION_ID(REVIDR_EL1, GENMASK_ULL(63, 0)),

	/*
	 * ID regs: all ID_SANITISED() entries here must have corresponding
	 * entries in arm64_ftr_regs[].
	 */

	/* AArch64 mappings of the AArch32 ID registers */
	/* CRm=1 */
	AA32_ID_SANITISED(ID_PFR0_EL1),
	AA32_ID_SANITISED(ID_PFR1_EL1),
	{ SYS_DESC(SYS_ID_DFR0_EL1),
	  .access = access_id_reg,
	  .get_user = get_id_reg,
	  .set_user = set_id_dfr0_el1,
	  .visibility = aa32_id_visibility,
	  .reset = read_sanitised_id_dfr0_el1,
	  .val = ID_DFR0_EL1_PerfMon_MASK |
		 ID_DFR0_EL1_CopDbg_MASK, },
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
	ID_FILTERED(ID_AA64PFR0_EL1, id_aa64pfr0_el1,
		    ~(ID_AA64PFR0_EL1_AMU |
		      ID_AA64PFR0_EL1_MPAM |
		      ID_AA64PFR0_EL1_SVE |
		      ID_AA64PFR0_EL1_RAS |
		      ID_AA64PFR0_EL1_AdvSIMD |
		      ID_AA64PFR0_EL1_FP)),
	ID_FILTERED(ID_AA64PFR1_EL1, id_aa64pfr1_el1,
				     ~(ID_AA64PFR1_EL1_PFAR |
				       ID_AA64PFR1_EL1_DF2 |
				       ID_AA64PFR1_EL1_MTEX |
				       ID_AA64PFR1_EL1_THE |
				       ID_AA64PFR1_EL1_GCS |
				       ID_AA64PFR1_EL1_MTE_frac |
				       ID_AA64PFR1_EL1_NMI |
				       ID_AA64PFR1_EL1_RNDR_trap |
				       ID_AA64PFR1_EL1_SME |
				       ID_AA64PFR1_EL1_RES0 |
				       ID_AA64PFR1_EL1_MPAM_frac |
				       ID_AA64PFR1_EL1_RAS_frac |
				       ID_AA64PFR1_EL1_MTE)),
	ID_WRITABLE(ID_AA64PFR2_EL1, ID_AA64PFR2_EL1_FPMR),
	ID_UNALLOCATED(4,3),
	ID_WRITABLE(ID_AA64ZFR0_EL1, ~ID_AA64ZFR0_EL1_RES0),
	ID_HIDDEN(ID_AA64SMFR0_EL1),
	ID_UNALLOCATED(4,6),
	ID_WRITABLE(ID_AA64FPFR0_EL1, ~ID_AA64FPFR0_EL1_RES0),

	/* CRm=5 */
	/*
	 * Prior to FEAT_Debugv8.9, the architecture defines context-aware
	 * breakpoints (CTX_CMPs) as the highest numbered breakpoints (BRPs).
	 * KVM does not trap + emulate the breakpoint registers, and as such
	 * cannot support a layout that misaligns with the underlying hardware.
	 * While it may be possible to describe a subset that aligns with
	 * hardware, just prevent changes to BRPs and CTX_CMPs altogether for
	 * simplicity.
	 *
	 * See DDI0487K.a, section D2.8.3 Breakpoint types and linking
	 * of breakpoints for more details.
	 */
	ID_FILTERED(ID_AA64DFR0_EL1, id_aa64dfr0_el1,
		    ID_AA64DFR0_EL1_DoubleLock_MASK |
		    ID_AA64DFR0_EL1_WRPs_MASK |
		    ID_AA64DFR0_EL1_PMUVer_MASK |
		    ID_AA64DFR0_EL1_DebugVer_MASK),
	ID_SANITISED(ID_AA64DFR1_EL1),
	ID_UNALLOCATED(5,2),
	ID_UNALLOCATED(5,3),
	ID_HIDDEN(ID_AA64AFR0_EL1),
	ID_HIDDEN(ID_AA64AFR1_EL1),
	ID_UNALLOCATED(5,6),
	ID_UNALLOCATED(5,7),

	/* CRm=6 */
	ID_WRITABLE(ID_AA64ISAR0_EL1, ~ID_AA64ISAR0_EL1_RES0),
	ID_WRITABLE(ID_AA64ISAR1_EL1, ~(ID_AA64ISAR1_EL1_GPI |
					ID_AA64ISAR1_EL1_GPA |
					ID_AA64ISAR1_EL1_API |
					ID_AA64ISAR1_EL1_APA)),
	ID_WRITABLE(ID_AA64ISAR2_EL1, ~(ID_AA64ISAR2_EL1_RES0 |
					ID_AA64ISAR2_EL1_APA3 |
					ID_AA64ISAR2_EL1_GPA3)),
	ID_WRITABLE(ID_AA64ISAR3_EL1, (ID_AA64ISAR3_EL1_FPRCVT |
				       ID_AA64ISAR3_EL1_FAMINMAX)),
	ID_UNALLOCATED(6,4),
	ID_UNALLOCATED(6,5),
	ID_UNALLOCATED(6,6),
	ID_UNALLOCATED(6,7),

	/* CRm=7 */
	ID_FILTERED(ID_AA64MMFR0_EL1, id_aa64mmfr0_el1,
				      ~(ID_AA64MMFR0_EL1_RES0 |
					ID_AA64MMFR0_EL1_ASIDBITS)),
	ID_WRITABLE(ID_AA64MMFR1_EL1, ~(ID_AA64MMFR1_EL1_RES0 |
					ID_AA64MMFR1_EL1_HCX |
					ID_AA64MMFR1_EL1_TWED |
					ID_AA64MMFR1_EL1_XNX |
					ID_AA64MMFR1_EL1_VH |
					ID_AA64MMFR1_EL1_VMIDBits)),
	ID_FILTERED(ID_AA64MMFR2_EL1,
		    id_aa64mmfr2_el1, ~(ID_AA64MMFR2_EL1_RES0 |
					ID_AA64MMFR2_EL1_EVT |
					ID_AA64MMFR2_EL1_FWB |
					ID_AA64MMFR2_EL1_IDS |
					ID_AA64MMFR2_EL1_NV |
					ID_AA64MMFR2_EL1_CCIDX)),
	ID_WRITABLE(ID_AA64MMFR3_EL1, (ID_AA64MMFR3_EL1_TCRX	|
				       ID_AA64MMFR3_EL1_S1PIE   |
				       ID_AA64MMFR3_EL1_S1POE)),
	ID_WRITABLE(ID_AA64MMFR4_EL1, ID_AA64MMFR4_EL1_NV_frac),
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
	{ SYS_DESC(SYS_TCR2_EL1), access_vm_reg, reset_val, TCR2_EL1, 0,
	  .visibility = tcr2_visibility },

	PTRAUTH_KEY(APIA),
	PTRAUTH_KEY(APIB),
	PTRAUTH_KEY(APDA),
	PTRAUTH_KEY(APDB),
	PTRAUTH_KEY(APGA),

	{ SYS_DESC(SYS_SPSR_EL1), access_spsr},
	{ SYS_DESC(SYS_ELR_EL1), access_elr},

	{ SYS_DESC(SYS_ICC_PMR_EL1), undef_access },

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

	{ PMU_SYS_REG(PMINTENSET_EL1),
	  .access = access_pminten, .reg = PMINTENSET_EL1,
	  .get_user = get_pmreg, .set_user = set_pmreg },
	{ PMU_SYS_REG(PMINTENCLR_EL1),
	  .access = access_pminten, .reg = PMINTENSET_EL1,
	  .get_user = get_pmreg, .set_user = set_pmreg },
	{ SYS_DESC(SYS_PMMIR_EL1), trap_raz_wi },

	{ SYS_DESC(SYS_MAIR_EL1), access_vm_reg, reset_unknown, MAIR_EL1 },
	{ SYS_DESC(SYS_PIRE0_EL1), NULL, reset_unknown, PIRE0_EL1,
	  .visibility = s1pie_visibility },
	{ SYS_DESC(SYS_PIR_EL1), NULL, reset_unknown, PIR_EL1,
	  .visibility = s1pie_visibility },
	{ SYS_DESC(SYS_POR_EL1), NULL, reset_unknown, POR_EL1,
	  .visibility = s1poe_visibility },
	{ SYS_DESC(SYS_AMAIR_EL1), access_vm_reg, reset_amair_el1, AMAIR_EL1 },

	{ SYS_DESC(SYS_LORSA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LOREA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORN_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORC_EL1), trap_loregion },
	{ SYS_DESC(SYS_MPAMIDR_EL1), undef_access },
	{ SYS_DESC(SYS_LORID_EL1), trap_loregion },

	{ SYS_DESC(SYS_MPAM1_EL1), undef_access },
	{ SYS_DESC(SYS_MPAM0_EL1), undef_access },
	{ SYS_DESC(SYS_VBAR_EL1), access_rw, reset_val, VBAR_EL1, 0 },
	{ SYS_DESC(SYS_DISR_EL1), NULL, reset_val, DISR_EL1, 0 },

	{ SYS_DESC(SYS_ICC_IAR0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_EOIR0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_HPPIR0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_BPR0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP0R0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP0R1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP0R2_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP0R3_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP1R0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP1R1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP1R2_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_AP1R3_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_DIR_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_RPR_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_SGI1R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_ASGI1R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_SGI0R_EL1), access_gic_sgi },
	{ SYS_DESC(SYS_ICC_IAR1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_EOIR1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_HPPIR1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_BPR1_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_CTLR_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_SRE_EL1), access_gic_sre },
	{ SYS_DESC(SYS_ICC_IGRPEN0_EL1), undef_access },
	{ SYS_DESC(SYS_ICC_IGRPEN1_EL1), undef_access },

	{ SYS_DESC(SYS_CONTEXTIDR_EL1), access_vm_reg, reset_val, CONTEXTIDR_EL1, 0 },
	{ SYS_DESC(SYS_TPIDR_EL1), NULL, reset_unknown, TPIDR_EL1 },

	{ SYS_DESC(SYS_ACCDATA_EL1), undef_access },

	{ SYS_DESC(SYS_SCXTNUM_EL1), undef_access },

	{ SYS_DESC(SYS_CNTKCTL_EL1), NULL, reset_val, CNTKCTL_EL1, 0},

	{ SYS_DESC(SYS_CCSIDR_EL1), access_ccsidr },
	{ SYS_DESC(SYS_CLIDR_EL1), access_clidr, reset_clidr, CLIDR_EL1,
	  .set_user = set_clidr, .val = ~CLIDR_EL1_RES0 },
	{ SYS_DESC(SYS_CCSIDR2_EL1), undef_access },
	{ SYS_DESC(SYS_SMIDR_EL1), undef_access },
	IMPLEMENTATION_ID(AIDR_EL1, GENMASK_ULL(63, 0)),
	{ SYS_DESC(SYS_CSSELR_EL1), access_csselr, reset_unknown, CSSELR_EL1 },
	ID_FILTERED(CTR_EL0, ctr_el0,
		    CTR_EL0_DIC_MASK |
		    CTR_EL0_IDC_MASK |
		    CTR_EL0_DminLine_MASK |
		    CTR_EL0_L1Ip_MASK |
		    CTR_EL0_IminLine_MASK),
	{ SYS_DESC(SYS_SVCR), undef_access, reset_val, SVCR, 0, .visibility = sme_visibility  },
	{ SYS_DESC(SYS_FPMR), undef_access, reset_val, FPMR, 0, .visibility = fp8_visibility },

	{ PMU_SYS_REG(PMCR_EL0), .access = access_pmcr, .reset = reset_pmcr,
	  .reg = PMCR_EL0, .get_user = get_pmcr, .set_user = set_pmcr },
	{ PMU_SYS_REG(PMCNTENSET_EL0),
	  .access = access_pmcnten, .reg = PMCNTENSET_EL0,
	  .get_user = get_pmreg, .set_user = set_pmreg },
	{ PMU_SYS_REG(PMCNTENCLR_EL0),
	  .access = access_pmcnten, .reg = PMCNTENSET_EL0,
	  .get_user = get_pmreg, .set_user = set_pmreg },
	{ PMU_SYS_REG(PMOVSCLR_EL0),
	  .access = access_pmovs, .reg = PMOVSSET_EL0,
	  .get_user = get_pmreg, .set_user = set_pmreg },
	/*
	 * PM_SWINC_EL0 is exposed to userspace as RAZ/WI, as it was
	 * previously (and pointlessly) advertised in the past...
	 */
	{ PMU_SYS_REG(PMSWINC_EL0),
	  .get_user = get_raz_reg, .set_user = set_wi_reg,
	  .access = access_pmswinc, .reset = NULL },
	{ PMU_SYS_REG(PMSELR_EL0),
	  .access = access_pmselr, .reset = reset_pmselr, .reg = PMSELR_EL0 },
	{ PMU_SYS_REG(PMCEID0_EL0),
	  .access = access_pmceid, .reset = NULL },
	{ PMU_SYS_REG(PMCEID1_EL0),
	  .access = access_pmceid, .reset = NULL },
	{ PMU_SYS_REG(PMCCNTR_EL0),
	  .access = access_pmu_evcntr, .reset = reset_unknown,
	  .reg = PMCCNTR_EL0, .get_user = get_pmu_evcntr,
	  .set_user = set_pmu_evcntr },
	{ PMU_SYS_REG(PMXEVTYPER_EL0),
	  .access = access_pmu_evtyper, .reset = NULL },
	{ PMU_SYS_REG(PMXEVCNTR_EL0),
	  .access = access_pmu_evcntr, .reset = NULL },
	/*
	 * PMUSERENR_EL0 resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ PMU_SYS_REG(PMUSERENR_EL0), .access = access_pmuserenr,
	  .reset = reset_val, .reg = PMUSERENR_EL0, .val = 0 },
	{ PMU_SYS_REG(PMOVSSET_EL0),
	  .access = access_pmovs, .reg = PMOVSSET_EL0,
	  .get_user = get_pmreg, .set_user = set_pmreg },

	{ SYS_DESC(SYS_POR_EL0), NULL, reset_unknown, POR_EL0,
	  .visibility = s1poe_visibility },
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
	{ SYS_DESC(SYS_CNTVCT_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTPCTSS_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTVCTSS_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_TVAL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CTL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CVAL_EL0), access_arch_timer },

	{ SYS_DESC(SYS_CNTV_TVAL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTV_CTL_EL0), access_arch_timer },
	{ SYS_DESC(SYS_CNTV_CVAL_EL0), access_arch_timer },

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
	{ PMU_SYS_REG(PMCCFILTR_EL0), .access = access_pmu_evtyper,
	  .reset = reset_val, .reg = PMCCFILTR_EL0, .val = 0 },

	EL2_REG_VNCR(VPIDR_EL2, reset_unknown, 0),
	EL2_REG_VNCR(VMPIDR_EL2, reset_unknown, 0),
	EL2_REG(SCTLR_EL2, access_rw, reset_val, SCTLR_EL2_RES1),
	EL2_REG(ACTLR_EL2, access_rw, reset_val, 0),
	EL2_REG_VNCR(HCR_EL2, reset_hcr, 0),
	EL2_REG(MDCR_EL2, access_mdcr, reset_val, 0),
	EL2_REG(CPTR_EL2, access_rw, reset_val, CPTR_NVHE_EL2_RES1),
	EL2_REG_VNCR(HSTR_EL2, reset_val, 0),
	EL2_REG_VNCR(HFGRTR_EL2, reset_val, 0),
	EL2_REG_VNCR(HFGWTR_EL2, reset_val, 0),
	EL2_REG_VNCR(HFGITR_EL2, reset_val, 0),
	EL2_REG_VNCR(HACR_EL2, reset_val, 0),

	EL2_REG_FILTERED(ZCR_EL2, access_zcr_el2, reset_val, 0,
			 sve_el2_visibility),

	EL2_REG_VNCR(HCRX_EL2, reset_val, 0),

	EL2_REG(TTBR0_EL2, access_rw, reset_val, 0),
	EL2_REG(TTBR1_EL2, access_rw, reset_val, 0),
	EL2_REG(TCR_EL2, access_rw, reset_val, TCR_EL2_RES1),
	EL2_REG_FILTERED(TCR2_EL2, access_rw, reset_val, TCR2_EL2_RES1,
			 tcr2_el2_visibility),
	EL2_REG_VNCR(VTTBR_EL2, reset_val, 0),
	EL2_REG_VNCR(VTCR_EL2, reset_val, 0),

	{ SYS_DESC(SYS_DACR32_EL2), undef_access, reset_unknown, DACR32_EL2 },
	EL2_REG_VNCR(HDFGRTR_EL2, reset_val, 0),
	EL2_REG_VNCR(HDFGWTR_EL2, reset_val, 0),
	EL2_REG_VNCR(HAFGRTR_EL2, reset_val, 0),
	EL2_REG_REDIR(SPSR_EL2, reset_val, 0),
	EL2_REG_REDIR(ELR_EL2, reset_val, 0),
	{ SYS_DESC(SYS_SP_EL1), access_sp_el1},

	/* AArch32 SPSR_* are RES0 if trapped from a NV guest */
	{ SYS_DESC(SYS_SPSR_irq), .access = trap_raz_wi },
	{ SYS_DESC(SYS_SPSR_abt), .access = trap_raz_wi },
	{ SYS_DESC(SYS_SPSR_und), .access = trap_raz_wi },
	{ SYS_DESC(SYS_SPSR_fiq), .access = trap_raz_wi },

	{ SYS_DESC(SYS_IFSR32_EL2), undef_access, reset_unknown, IFSR32_EL2 },
	EL2_REG(AFSR0_EL2, access_rw, reset_val, 0),
	EL2_REG(AFSR1_EL2, access_rw, reset_val, 0),
	EL2_REG_REDIR(ESR_EL2, reset_val, 0),
	{ SYS_DESC(SYS_FPEXC32_EL2), undef_access, reset_val, FPEXC32_EL2, 0x700 },

	EL2_REG_REDIR(FAR_EL2, reset_val, 0),
	EL2_REG(HPFAR_EL2, access_rw, reset_val, 0),

	EL2_REG(MAIR_EL2, access_rw, reset_val, 0),
	EL2_REG_FILTERED(PIRE0_EL2, access_rw, reset_val, 0,
			 s1pie_el2_visibility),
	EL2_REG_FILTERED(PIR_EL2, access_rw, reset_val, 0,
			 s1pie_el2_visibility),
	EL2_REG_FILTERED(POR_EL2, access_rw, reset_val, 0,
			 s1poe_el2_visibility),
	EL2_REG(AMAIR_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_MPAMHCR_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPMV_EL2), undef_access },
	{ SYS_DESC(SYS_MPAM2_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM0_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM1_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM2_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM3_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM4_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM5_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM6_EL2), undef_access },
	{ SYS_DESC(SYS_MPAMVPM7_EL2), undef_access },

	EL2_REG(VBAR_EL2, access_rw, reset_val, 0),
	EL2_REG(RVBAR_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_RMR_EL2), undef_access },

	EL2_REG_VNCR(ICH_AP0R0_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP0R1_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP0R2_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP0R3_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP1R0_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP1R1_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP1R2_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_AP1R3_EL2, reset_val, 0),

	{ SYS_DESC(SYS_ICC_SRE_EL2), access_gic_sre },

	EL2_REG_VNCR(ICH_HCR_EL2, reset_val, 0),
	{ SYS_DESC(SYS_ICH_VTR_EL2), access_gic_vtr },
	{ SYS_DESC(SYS_ICH_MISR_EL2), access_gic_misr },
	{ SYS_DESC(SYS_ICH_EISR_EL2), access_gic_eisr },
	{ SYS_DESC(SYS_ICH_ELRSR_EL2), access_gic_elrsr },
	EL2_REG_VNCR(ICH_VMCR_EL2, reset_val, 0),

	EL2_REG_VNCR(ICH_LR0_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR1_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR2_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR3_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR4_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR5_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR6_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR7_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR8_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR9_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR10_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR11_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR12_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR13_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR14_EL2, reset_val, 0),
	EL2_REG_VNCR(ICH_LR15_EL2, reset_val, 0),

	EL2_REG(CONTEXTIDR_EL2, access_rw, reset_val, 0),
	EL2_REG(TPIDR_EL2, access_rw, reset_val, 0),

	EL2_REG_VNCR(CNTVOFF_EL2, reset_val, 0),
	EL2_REG(CNTHCTL_EL2, access_rw, reset_val, 0),
	{ SYS_DESC(SYS_CNTHP_TVAL_EL2), access_arch_timer },
	EL2_REG(CNTHP_CTL_EL2, access_arch_timer, reset_val, 0),
	EL2_REG(CNTHP_CVAL_EL2, access_arch_timer, reset_val, 0),

	{ SYS_DESC(SYS_CNTHV_TVAL_EL2), access_hv_timer },
	EL2_REG(CNTHV_CTL_EL2, access_hv_timer, reset_val, 0),
	EL2_REG(CNTHV_CVAL_EL2, access_hv_timer, reset_val, 0),

	{ SYS_DESC(SYS_CNTKCTL_EL12), access_cntkctl_el12 },

	{ SYS_DESC(SYS_CNTP_TVAL_EL02), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CTL_EL02), access_arch_timer },
	{ SYS_DESC(SYS_CNTP_CVAL_EL02), access_arch_timer },

	{ SYS_DESC(SYS_CNTV_TVAL_EL02), access_arch_timer },
	{ SYS_DESC(SYS_CNTV_CTL_EL02), access_arch_timer },
	{ SYS_DESC(SYS_CNTV_CVAL_EL02), access_arch_timer },

	EL2_REG(SP_EL2, NULL, reset_unknown, 0),
};

static bool handle_at_s1e01(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	u32 op = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);

	__kvm_at_s1e01(vcpu, op, p->regval);

	return true;
}

static bool handle_at_s1e2(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u32 op = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);

	/* There is no FGT associated with AT S1E2A :-( */
	if (op == OP_AT_S1E2A &&
	    !kvm_has_feat(vcpu->kvm, ID_AA64ISAR2_EL1, ATS1A, IMP)) {
		kvm_inject_undefined(vcpu);
		return false;
	}

	__kvm_at_s1e2(vcpu, op, p->regval);

	return true;
}

static bool handle_at_s12(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 op = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);

	__kvm_at_s12(vcpu, op, p->regval);

	return true;
}

static bool kvm_supported_tlbi_s12_op(struct kvm_vcpu *vpcu, u32 instr)
{
	struct kvm *kvm = vpcu->kvm;
	u8 CRm = sys_reg_CRm(instr);

	if (sys_reg_CRn(instr) == TLBI_CRn_nXS &&
	    !kvm_has_feat(kvm, ID_AA64ISAR1_EL1, XS, IMP))
		return false;

	if (CRm == TLBI_CRm_nROS &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		return false;

	return true;
}

static bool handle_alle1is(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u32 sys_encoding = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);

	if (!kvm_supported_tlbi_s12_op(vcpu, sys_encoding))
		return undef_access(vcpu, p, r);

	write_lock(&vcpu->kvm->mmu_lock);

	/*
	 * Drop all shadow S2s, resulting in S1/S2 TLBIs for each of the
	 * corresponding VMIDs.
	 */
	kvm_nested_s2_unmap(vcpu->kvm, true);

	write_unlock(&vcpu->kvm->mmu_lock);

	return true;
}

static bool kvm_supported_tlbi_ipas2_op(struct kvm_vcpu *vpcu, u32 instr)
{
	struct kvm *kvm = vpcu->kvm;
	u8 CRm = sys_reg_CRm(instr);
	u8 Op2 = sys_reg_Op2(instr);

	if (sys_reg_CRn(instr) == TLBI_CRn_nXS &&
	    !kvm_has_feat(kvm, ID_AA64ISAR1_EL1, XS, IMP))
		return false;

	if (CRm == TLBI_CRm_IPAIS && (Op2 == 2 || Op2 == 6) &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		return false;

	if (CRm == TLBI_CRm_IPAONS && (Op2 == 0 || Op2 == 4) &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		return false;

	if (CRm == TLBI_CRm_IPAONS && (Op2 == 3 || Op2 == 7) &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		return false;

	return true;
}

/* Only defined here as this is an internal "abstraction" */
union tlbi_info {
	struct {
		u64	start;
		u64	size;
	} range;

	struct {
		u64	addr;
	} ipa;

	struct {
		u64	addr;
		u32	encoding;
	} va;
};

static void s2_mmu_unmap_range(struct kvm_s2_mmu *mmu,
			       const union tlbi_info *info)
{
	/*
	 * The unmap operation is allowed to drop the MMU lock and block, which
	 * means that @mmu could be used for a different context than the one
	 * currently being invalidated.
	 *
	 * This behavior is still safe, as:
	 *
	 *  1) The vCPU(s) that recycled the MMU are responsible for invalidating
	 *     the entire MMU before reusing it, which still honors the intent
	 *     of a TLBI.
	 *
	 *  2) Until the guest TLBI instruction is 'retired' (i.e. increment PC
	 *     and ERET to the guest), other vCPUs are allowed to use stale
	 *     translations.
	 *
	 *  3) Accidentally unmapping an unrelated MMU context is nonfatal, and
	 *     at worst may cause more aborts for shadow stage-2 fills.
	 *
	 * Dropping the MMU lock also implies that shadow stage-2 fills could
	 * happen behind the back of the TLBI. This is still safe, though, as
	 * the L1 needs to put its stage-2 in a consistent state before doing
	 * the TLBI.
	 */
	kvm_stage2_unmap_range(mmu, info->range.start, info->range.size, true);
}

static bool handle_vmalls12e1is(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
				const struct sys_reg_desc *r)
{
	u32 sys_encoding = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);
	u64 limit, vttbr;

	if (!kvm_supported_tlbi_s12_op(vcpu, sys_encoding))
		return undef_access(vcpu, p, r);

	vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);
	limit = BIT_ULL(kvm_get_pa_bits(vcpu->kvm));

	kvm_s2_mmu_iterate_by_vmid(vcpu->kvm, get_vmid(vttbr),
				   &(union tlbi_info) {
					   .range = {
						   .start = 0,
						   .size = limit,
					   },
				   },
				   s2_mmu_unmap_range);

	return true;
}

static bool handle_ripas2e1is(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	u32 sys_encoding = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);
	u64 vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);
	u64 base, range, tg, num, scale;
	int shift;

	if (!kvm_supported_tlbi_ipas2_op(vcpu, sys_encoding))
		return undef_access(vcpu, p, r);

	/*
	 * Because the shadow S2 structure doesn't necessarily reflect that
	 * of the guest's S2 (different base granule size, for example), we
	 * decide to ignore TTL and only use the described range.
	 */
	tg	= FIELD_GET(GENMASK(47, 46), p->regval);
	scale	= FIELD_GET(GENMASK(45, 44), p->regval);
	num	= FIELD_GET(GENMASK(43, 39), p->regval);
	base	= p->regval & GENMASK(36, 0);

	switch(tg) {
	case 1:
		shift = 12;
		break;
	case 2:
		shift = 14;
		break;
	case 3:
	default:		/* IMPDEF: handle tg==0 as 64k */
		shift = 16;
		break;
	}

	base <<= shift;
	range = __TLBI_RANGE_PAGES(num, scale) << shift;

	kvm_s2_mmu_iterate_by_vmid(vcpu->kvm, get_vmid(vttbr),
				   &(union tlbi_info) {
					   .range = {
						   .start = base,
						   .size = range,
					   },
				   },
				   s2_mmu_unmap_range);

	return true;
}

static void s2_mmu_unmap_ipa(struct kvm_s2_mmu *mmu,
			     const union tlbi_info *info)
{
	unsigned long max_size;
	u64 base_addr;

	/*
	 * We drop a number of things from the supplied value:
	 *
	 * - NS bit: we're non-secure only.
	 *
	 * - IPA[51:48]: We don't support 52bit IPA just yet...
	 *
	 * And of course, adjust the IPA to be on an actual address.
	 */
	base_addr = (info->ipa.addr & GENMASK_ULL(35, 0)) << 12;
	max_size = compute_tlb_inval_range(mmu, info->ipa.addr);
	base_addr &= ~(max_size - 1);

	/*
	 * See comment in s2_mmu_unmap_range() for why this is allowed to
	 * reschedule.
	 */
	kvm_stage2_unmap_range(mmu, base_addr, max_size, true);
}

static bool handle_ipas2e1is(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			     const struct sys_reg_desc *r)
{
	u32 sys_encoding = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);
	u64 vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);

	if (!kvm_supported_tlbi_ipas2_op(vcpu, sys_encoding))
		return undef_access(vcpu, p, r);

	kvm_s2_mmu_iterate_by_vmid(vcpu->kvm, get_vmid(vttbr),
				   &(union tlbi_info) {
					   .ipa = {
						   .addr = p->regval,
					   },
				   },
				   s2_mmu_unmap_ipa);

	return true;
}

static void s2_mmu_tlbi_s1e1(struct kvm_s2_mmu *mmu,
			     const union tlbi_info *info)
{
	WARN_ON(__kvm_tlbi_s1e2(mmu, info->va.addr, info->va.encoding));
}

static bool handle_tlbi_el1(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			    const struct sys_reg_desc *r)
{
	u32 sys_encoding = sys_insn(p->Op0, p->Op1, p->CRn, p->CRm, p->Op2);
	u64 vttbr = vcpu_read_sys_reg(vcpu, VTTBR_EL2);

	/*
	 * If we're here, this is because we've trapped on a EL1 TLBI
	 * instruction that affects the EL1 translation regime while
	 * we're running in a context that doesn't allow us to let the
	 * HW do its thing (aka vEL2):
	 *
	 * - HCR_EL2.E2H == 0 : a non-VHE guest
	 * - HCR_EL2.{E2H,TGE} == { 1, 0 } : a VHE guest in guest mode
	 *
	 * We don't expect these helpers to ever be called when running
	 * in a vEL1 context.
	 */

	WARN_ON(!vcpu_is_el2(vcpu));

	if (!kvm_supported_tlbi_s1e1_op(vcpu, sys_encoding))
		return undef_access(vcpu, p, r);

	kvm_s2_mmu_iterate_by_vmid(vcpu->kvm, get_vmid(vttbr),
				   &(union tlbi_info) {
					   .va = {
						   .addr = p->regval,
						   .encoding = sys_encoding,
					   },
				   },
				   s2_mmu_tlbi_s1e1);

	return true;
}

#define SYS_INSN(insn, access_fn)					\
	{								\
		SYS_DESC(OP_##insn),					\
		.access = (access_fn),					\
	}

static struct sys_reg_desc sys_insn_descs[] = {
	{ SYS_DESC(SYS_DC_ISW), access_dcsw },
	{ SYS_DESC(SYS_DC_IGSW), access_dcgsw },
	{ SYS_DESC(SYS_DC_IGDSW), access_dcgsw },

	SYS_INSN(AT_S1E1R, handle_at_s1e01),
	SYS_INSN(AT_S1E1W, handle_at_s1e01),
	SYS_INSN(AT_S1E0R, handle_at_s1e01),
	SYS_INSN(AT_S1E0W, handle_at_s1e01),
	SYS_INSN(AT_S1E1RP, handle_at_s1e01),
	SYS_INSN(AT_S1E1WP, handle_at_s1e01),

	{ SYS_DESC(SYS_DC_CSW), access_dcsw },
	{ SYS_DESC(SYS_DC_CGSW), access_dcgsw },
	{ SYS_DESC(SYS_DC_CGDSW), access_dcgsw },
	{ SYS_DESC(SYS_DC_CISW), access_dcsw },
	{ SYS_DESC(SYS_DC_CIGSW), access_dcgsw },
	{ SYS_DESC(SYS_DC_CIGDSW), access_dcgsw },

	SYS_INSN(TLBI_VMALLE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1OS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1IS, handle_tlbi_el1),

	SYS_INSN(TLBI_VMALLE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1IS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1IS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1OS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1OS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1, handle_tlbi_el1),

	SYS_INSN(TLBI_VMALLE1, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1, handle_tlbi_el1),

	SYS_INSN(TLBI_VMALLE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1OSNXS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1ISNXS, handle_tlbi_el1),

	SYS_INSN(TLBI_VMALLE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1ISNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1ISNXS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1OSNXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1OSNXS, handle_tlbi_el1),

	SYS_INSN(TLBI_RVAE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAAE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVALE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_RVAALE1NXS, handle_tlbi_el1),

	SYS_INSN(TLBI_VMALLE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_ASIDE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAAE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VALE1NXS, handle_tlbi_el1),
	SYS_INSN(TLBI_VAALE1NXS, handle_tlbi_el1),

	SYS_INSN(AT_S1E2R, handle_at_s1e2),
	SYS_INSN(AT_S1E2W, handle_at_s1e2),
	SYS_INSN(AT_S12E1R, handle_at_s12),
	SYS_INSN(AT_S12E1W, handle_at_s12),
	SYS_INSN(AT_S12E0R, handle_at_s12),
	SYS_INSN(AT_S12E0W, handle_at_s12),
	SYS_INSN(AT_S1E2A, handle_at_s1e2),

	SYS_INSN(TLBI_IPAS2E1IS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2E1IS, handle_ripas2e1is),
	SYS_INSN(TLBI_IPAS2LE1IS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1IS, handle_ripas2e1is),

	SYS_INSN(TLBI_ALLE2OS, undef_access),
	SYS_INSN(TLBI_VAE2OS, undef_access),
	SYS_INSN(TLBI_ALLE1OS, handle_alle1is),
	SYS_INSN(TLBI_VALE2OS, undef_access),
	SYS_INSN(TLBI_VMALLS12E1OS, handle_vmalls12e1is),

	SYS_INSN(TLBI_RVAE2IS, undef_access),
	SYS_INSN(TLBI_RVALE2IS, undef_access),

	SYS_INSN(TLBI_ALLE1IS, handle_alle1is),
	SYS_INSN(TLBI_VMALLS12E1IS, handle_vmalls12e1is),
	SYS_INSN(TLBI_IPAS2E1OS, handle_ipas2e1is),
	SYS_INSN(TLBI_IPAS2E1, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2E1, handle_ripas2e1is),
	SYS_INSN(TLBI_RIPAS2E1OS, handle_ripas2e1is),
	SYS_INSN(TLBI_IPAS2LE1OS, handle_ipas2e1is),
	SYS_INSN(TLBI_IPAS2LE1, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1, handle_ripas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1OS, handle_ripas2e1is),
	SYS_INSN(TLBI_RVAE2OS, undef_access),
	SYS_INSN(TLBI_RVALE2OS, undef_access),
	SYS_INSN(TLBI_RVAE2, undef_access),
	SYS_INSN(TLBI_RVALE2, undef_access),
	SYS_INSN(TLBI_ALLE1, handle_alle1is),
	SYS_INSN(TLBI_VMALLS12E1, handle_vmalls12e1is),

	SYS_INSN(TLBI_IPAS2E1ISNXS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2E1ISNXS, handle_ripas2e1is),
	SYS_INSN(TLBI_IPAS2LE1ISNXS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1ISNXS, handle_ripas2e1is),

	SYS_INSN(TLBI_ALLE2OSNXS, undef_access),
	SYS_INSN(TLBI_VAE2OSNXS, undef_access),
	SYS_INSN(TLBI_ALLE1OSNXS, handle_alle1is),
	SYS_INSN(TLBI_VALE2OSNXS, undef_access),
	SYS_INSN(TLBI_VMALLS12E1OSNXS, handle_vmalls12e1is),

	SYS_INSN(TLBI_RVAE2ISNXS, undef_access),
	SYS_INSN(TLBI_RVALE2ISNXS, undef_access),
	SYS_INSN(TLBI_ALLE2ISNXS, undef_access),
	SYS_INSN(TLBI_VAE2ISNXS, undef_access),

	SYS_INSN(TLBI_ALLE1ISNXS, handle_alle1is),
	SYS_INSN(TLBI_VALE2ISNXS, undef_access),
	SYS_INSN(TLBI_VMALLS12E1ISNXS, handle_vmalls12e1is),
	SYS_INSN(TLBI_IPAS2E1OSNXS, handle_ipas2e1is),
	SYS_INSN(TLBI_IPAS2E1NXS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2E1NXS, handle_ripas2e1is),
	SYS_INSN(TLBI_RIPAS2E1OSNXS, handle_ripas2e1is),
	SYS_INSN(TLBI_IPAS2LE1OSNXS, handle_ipas2e1is),
	SYS_INSN(TLBI_IPAS2LE1NXS, handle_ipas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1NXS, handle_ripas2e1is),
	SYS_INSN(TLBI_RIPAS2LE1OSNXS, handle_ripas2e1is),
	SYS_INSN(TLBI_RVAE2OSNXS, undef_access),
	SYS_INSN(TLBI_RVALE2OSNXS, undef_access),
	SYS_INSN(TLBI_RVAE2NXS, undef_access),
	SYS_INSN(TLBI_RVALE2NXS, undef_access),
	SYS_INSN(TLBI_ALLE2NXS, undef_access),
	SYS_INSN(TLBI_VAE2NXS, undef_access),
	SYS_INSN(TLBI_ALLE1NXS, handle_alle1is),
	SYS_INSN(TLBI_VALE2NXS, undef_access),
	SYS_INSN(TLBI_VMALLS12E1NXS, handle_vmalls12e1is),
};

static bool trap_dbgdidr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		u64 dfr = kvm_read_vm_id_reg(vcpu->kvm, SYS_ID_AA64DFR0_EL1);
		u32 el3 = kvm_has_feat(vcpu->kvm, ID_AA64PFR0_EL1, EL3, IMP);

		p->regval = ((SYS_FIELD_GET(ID_AA64DFR0_EL1, WRPs, dfr) << 28) |
			     (SYS_FIELD_GET(ID_AA64DFR0_EL1, BRPs, dfr) << 24) |
			     (SYS_FIELD_GET(ID_AA64DFR0_EL1, CTX_CMPs, dfr) << 20) |
			     (SYS_FIELD_GET(ID_AA64DFR0_EL1, DebugVer, dfr) << 16) |
			     (1 << 15) | (el3 << 14) | (el3 << 12));
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
#define DBG_BCR_BVR_WCR_WVR(n)							\
	/* DBGBVRn */								\
	{ AA32(LO), Op1( 0), CRn( 0), CRm((n)), Op2( 4),			\
	  trap_dbg_wb_reg, NULL, n },						\
	/* DBGBCRn */								\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 5), trap_dbg_wb_reg, NULL, n },	\
	/* DBGWVRn */								\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 6), trap_dbg_wb_reg, NULL, n },	\
	/* DBGWCRn */								\
	{ Op1( 0), CRn( 0), CRm((n)), Op2( 7), trap_dbg_wb_reg, NULL, n }

#define DBGBXVR(n)								\
	{ AA32(HI), Op1( 0), CRn( 1), CRm((n)), Op2( 1),			\
	  trap_dbg_wb_reg, NULL, n }

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
	{ CP15_SYS_DESC(SYS_ICC_PMR_EL1), undef_access },
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

	{ CP15_SYS_DESC(SYS_ICC_IAR0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_EOIR0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_HPPIR0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_BPR0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP0R0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP0R1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP0R2_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP0R3_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP1R0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP1R1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP1R2_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_AP1R3_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_DIR_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_RPR_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_IAR1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_EOIR1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_HPPIR1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_BPR1_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_CTLR_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_SRE_EL1), access_gic_sre },
	{ CP15_SYS_DESC(SYS_ICC_IGRPEN0_EL1), undef_access },
	{ CP15_SYS_DESC(SYS_ICC_IGRPEN1_EL1), undef_access },

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
	{ SYS_DESC(SYS_AARCH32_CNTVCT),	      access_arch_timer },
	{ Op1( 2), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_SGI0R */
	{ SYS_DESC(SYS_AARCH32_CNTP_CVAL),    access_arch_timer },
	{ SYS_DESC(SYS_AARCH32_CNTPCTSS),     access_arch_timer },
	{ SYS_DESC(SYS_AARCH32_CNTVCTSS),     access_arch_timer },
};

static bool check_sysreg_table(const struct sys_reg_desc *table, unsigned int n,
			       bool is_32)
{
	unsigned int i;

	for (i = 0; i < n; i++) {
		if (!is_32 && table[i].reg && !table[i].reset) {
			kvm_err("sys_reg table %pS entry %d (%s) lacks reset\n",
				&table[i], i, table[i].name);
			return false;
		}

		if (i && cmp_sys_reg(&table[i-1], &table[i]) >= 0) {
			kvm_err("sys_reg table %pS entry %d (%s -> %s) out of order\n",
				&table[i], i, table[i - 1].name, table[i].name);
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
 * @global: &struct sys_reg_desc
 * @nr_global: size of the @global array
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
 * @params: &struct sys_reg_params
 * @global: &struct sys_reg_desc
 * @nr_global: size of the @global array
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
	 * system register encodings, except for AIDR/REVIDR.
	 */
	if (params.Op1 == 0 && params.CRn == 0 &&
	    (params.CRm || params.Op2 == 6 /* REVIDR */))
		return kvm_emulate_cp15_id_reg(vcpu, &params);
	if (params.Op1 == 1 && params.CRn == 0 &&
	    params.CRm == 0 && params.Op2 == 7 /* AIDR */)
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

	print_sys_reg_msg(params,
			  "Unsupported guest sys_reg access at: %lx [%08lx]\n",
			  *vcpu_pc(vcpu), *vcpu_cpsr(vcpu));
	kvm_inject_undefined(vcpu);

	return false;
}

static const struct sys_reg_desc *idregs_debug_find(struct kvm *kvm, u8 pos)
{
	unsigned long i, idreg_idx = 0;

	for (i = 0; i < ARRAY_SIZE(sys_reg_descs); i++) {
		const struct sys_reg_desc *r = &sys_reg_descs[i];

		if (!is_vm_ftr_id_reg(reg_to_encoding(r)))
			continue;

		if (idreg_idx == pos)
			return r;

		idreg_idx++;
	}

	return NULL;
}

static void *idregs_debug_start(struct seq_file *s, loff_t *pos)
{
	struct kvm *kvm = s->private;
	u8 *iter;

	mutex_lock(&kvm->arch.config_lock);

	iter = &kvm->arch.idreg_debugfs_iter;
	if (test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags) &&
	    *iter == (u8)~0) {
		*iter = *pos;
		if (!idregs_debug_find(kvm, *iter))
			iter = NULL;
	} else {
		iter = ERR_PTR(-EBUSY);
	}

	mutex_unlock(&kvm->arch.config_lock);

	return iter;
}

static void *idregs_debug_next(struct seq_file *s, void *v, loff_t *pos)
{
	struct kvm *kvm = s->private;

	(*pos)++;

	if (idregs_debug_find(kvm, kvm->arch.idreg_debugfs_iter + 1)) {
		kvm->arch.idreg_debugfs_iter++;

		return &kvm->arch.idreg_debugfs_iter;
	}

	return NULL;
}

static void idregs_debug_stop(struct seq_file *s, void *v)
{
	struct kvm *kvm = s->private;

	if (IS_ERR(v))
		return;

	mutex_lock(&kvm->arch.config_lock);

	kvm->arch.idreg_debugfs_iter = ~0;

	mutex_unlock(&kvm->arch.config_lock);
}

static int idregs_debug_show(struct seq_file *s, void *v)
{
	const struct sys_reg_desc *desc;
	struct kvm *kvm = s->private;

	desc = idregs_debug_find(kvm, kvm->arch.idreg_debugfs_iter);

	if (!desc->name)
		return 0;

	seq_printf(s, "%20s:\t%016llx\n",
		   desc->name, kvm_read_vm_id_reg(kvm, reg_to_encoding(desc)));

	return 0;
}

static const struct seq_operations idregs_debug_sops = {
	.start	= idregs_debug_start,
	.next	= idregs_debug_next,
	.stop	= idregs_debug_stop,
	.show	= idregs_debug_show,
};

DEFINE_SEQ_ATTRIBUTE(idregs_debug);

void kvm_sys_regs_create_debugfs(struct kvm *kvm)
{
	kvm->arch.idreg_debugfs_iter = ~0;

	debugfs_create_file("idregs", 0444, kvm->debugfs_dentry, kvm,
			    &idregs_debug_fops);
}

static void reset_vm_ftr_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *reg)
{
	u32 id = reg_to_encoding(reg);
	struct kvm *kvm = vcpu->kvm;

	if (test_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags))
		return;

	kvm_set_vm_id_reg(kvm, id, reg->reset(vcpu, reg));
}

static void reset_vcpu_ftr_id_reg(struct kvm_vcpu *vcpu,
				  const struct sys_reg_desc *reg)
{
	if (kvm_vcpu_initialized(vcpu))
		return;

	reg->reset(vcpu, reg);
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
	struct kvm *kvm = vcpu->kvm;
	unsigned long i;

	for (i = 0; i < ARRAY_SIZE(sys_reg_descs); i++) {
		const struct sys_reg_desc *r = &sys_reg_descs[i];

		if (!r->reset)
			continue;

		if (is_vm_ftr_id_reg(reg_to_encoding(r)))
			reset_vm_ftr_id_reg(vcpu, r);
		else if (is_vcpu_ftr_id_reg(reg_to_encoding(r)))
			reset_vcpu_ftr_id_reg(vcpu, r);
		else
			r->reset(vcpu, r);

		if (r->reg >= __SANITISED_REG_START__ && r->reg < NR_SYS_REGS)
			(void)__vcpu_sys_reg(vcpu, r->reg);
	}

	set_bit(KVM_ARCH_FLAG_ID_REGS_INITIALIZED, &kvm->arch.flags);

	if (kvm_vcpu_has_pmu(vcpu))
		kvm_make_request(KVM_REQ_RELOAD_PMU, vcpu);
}

/**
 * kvm_handle_sys_reg -- handles a system instruction or mrs/msr instruction
 *			 trap on a guest execution
 * @vcpu: The VCPU pointer
 */
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu)
{
	const struct sys_reg_desc *desc = NULL;
	struct sys_reg_params params;
	unsigned long esr = kvm_vcpu_get_esr(vcpu);
	int Rt = kvm_vcpu_sys_get_rt(vcpu);
	int sr_idx;

	trace_kvm_handle_sys_reg(esr);

	if (triage_sysreg_trap(vcpu, &sr_idx))
		return 1;

	params = esr_sys64_to_params(esr);
	params.regval = vcpu_get_reg(vcpu, Rt);

	/* System registers have Op0=={2,3}, as per DDI487 J.a C5.1.2 */
	if (params.Op0 == 2 || params.Op0 == 3)
		desc = &sys_reg_descs[sr_idx];
	else
		desc = &sys_insn_descs[sr_idx];

	perform_access(vcpu, &params, desc);

	/* Read from system register? */
	if (!params.is_write &&
	    (params.Op0 == 2 || params.Op0 == 3))
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
	if (!r || sysreg_hidden(vcpu, r))
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

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_get(vcpu, reg->id, uaddr);

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
	if (!r || sysreg_hidden(vcpu, r))
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

	if ((reg->id & KVM_REG_ARM_COPROC_MASK) == KVM_REG_ARM_DEMUX)
		return demux_c15_set(vcpu, reg->id, uaddr);

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

	if (sysreg_hidden(vcpu, rd))
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
	return num_demux_regs()
		+ walk_sys_regs(vcpu, (u64 __user *)NULL);
}

int kvm_arm_copy_sys_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices)
{
	int err;

	err = walk_sys_regs(vcpu, uindices);
	if (err < 0)
		return err;
	uindices += err;

	return write_demux_regids(uindices);
}

#define KVM_ARM_FEATURE_ID_RANGE_INDEX(r)			\
	KVM_ARM_FEATURE_ID_RANGE_IDX(sys_reg_Op0(r),		\
		sys_reg_Op1(r),					\
		sys_reg_CRn(r),					\
		sys_reg_CRm(r),					\
		sys_reg_Op2(r))

int kvm_vm_ioctl_get_reg_writable_masks(struct kvm *kvm, struct reg_mask_range *range)
{
	const void *zero_page = page_to_virt(ZERO_PAGE(0));
	u64 __user *masks = (u64 __user *)range->addr;

	/* Only feature id range is supported, reserved[13] must be zero. */
	if (range->range ||
	    memcmp(range->reserved, zero_page, sizeof(range->reserved)))
		return -EINVAL;

	/* Wipe the whole thing first */
	if (clear_user(masks, KVM_ARM_FEATURE_ID_RANGE_SIZE * sizeof(__u64)))
		return -EFAULT;

	for (int i = 0; i < ARRAY_SIZE(sys_reg_descs); i++) {
		const struct sys_reg_desc *reg = &sys_reg_descs[i];
		u32 encoding = reg_to_encoding(reg);
		u64 val;

		if (!is_feature_id_reg(encoding) || !reg->set_user)
			continue;

		if (!reg->val ||
		    (is_aa32_id_reg(encoding) && !kvm_supports_32bit_el0())) {
			continue;
		}
		val = reg->val;

		if (put_user(val, (masks + KVM_ARM_FEATURE_ID_RANGE_INDEX(encoding))))
			return -EFAULT;
	}

	return 0;
}

static void vcpu_set_hcr(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (has_vhe() || has_hvhe())
		vcpu->arch.hcr_el2 |= HCR_E2H;
	if (cpus_have_final_cap(ARM64_HAS_RAS_EXTN)) {
		/* route synchronous external abort exceptions to EL2 */
		vcpu->arch.hcr_el2 |= HCR_TEA;
		/* trap error record accesses */
		vcpu->arch.hcr_el2 |= HCR_TERR;
	}

	if (cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
		vcpu->arch.hcr_el2 |= HCR_FWB;

	if (cpus_have_final_cap(ARM64_HAS_EVT) &&
	    !cpus_have_final_cap(ARM64_MISMATCHED_CACHE_TYPE) &&
	    kvm_read_vm_id_reg(kvm, SYS_CTR_EL0) == read_sanitised_ftr_reg(SYS_CTR_EL0))
		vcpu->arch.hcr_el2 |= HCR_TID4;
	else
		vcpu->arch.hcr_el2 |= HCR_TID2;

	if (vcpu_el1_is_32bit(vcpu))
		vcpu->arch.hcr_el2 &= ~HCR_RW;

	if (kvm_has_mte(vcpu->kvm))
		vcpu->arch.hcr_el2 |= HCR_ATA;

	/*
	 * In the absence of FGT, we cannot independently trap TLBI
	 * Range instructions. This isn't great, but trapping all
	 * TLBIs would be far worse. Live with it...
	 */
	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		vcpu->arch.hcr_el2 |= HCR_TTLBOS;
}

void kvm_calculate_traps(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	mutex_lock(&kvm->arch.config_lock);
	vcpu_set_hcr(vcpu);
	vcpu_set_ich_hcr(vcpu);
	vcpu_set_hcrx(vcpu);

	if (test_bit(KVM_ARCH_FLAG_FGU_INITIALIZED, &kvm->arch.flags))
		goto out;

	kvm->arch.fgu[HFGxTR_GROUP] = (HFGxTR_EL2_nAMAIR2_EL1		|
				       HFGxTR_EL2_nMAIR2_EL1		|
				       HFGxTR_EL2_nS2POR_EL1		|
				       HFGxTR_EL2_nACCDATA_EL1		|
				       HFGxTR_EL2_nSMPRI_EL1_MASK	|
				       HFGxTR_EL2_nTPIDR2_EL0_MASK);

	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		kvm->arch.fgu[HFGITR_GROUP] |= (HFGITR_EL2_TLBIRVAALE1OS|
						HFGITR_EL2_TLBIRVALE1OS	|
						HFGITR_EL2_TLBIRVAAE1OS	|
						HFGITR_EL2_TLBIRVAE1OS	|
						HFGITR_EL2_TLBIVAALE1OS	|
						HFGITR_EL2_TLBIVALE1OS	|
						HFGITR_EL2_TLBIVAAE1OS	|
						HFGITR_EL2_TLBIASIDE1OS	|
						HFGITR_EL2_TLBIVAE1OS	|
						HFGITR_EL2_TLBIVMALLE1OS);

	if (!kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		kvm->arch.fgu[HFGITR_GROUP] |= (HFGITR_EL2_TLBIRVAALE1	|
						HFGITR_EL2_TLBIRVALE1	|
						HFGITR_EL2_TLBIRVAAE1	|
						HFGITR_EL2_TLBIRVAE1	|
						HFGITR_EL2_TLBIRVAALE1IS|
						HFGITR_EL2_TLBIRVALE1IS	|
						HFGITR_EL2_TLBIRVAAE1IS	|
						HFGITR_EL2_TLBIRVAE1IS	|
						HFGITR_EL2_TLBIRVAALE1OS|
						HFGITR_EL2_TLBIRVALE1OS	|
						HFGITR_EL2_TLBIRVAAE1OS	|
						HFGITR_EL2_TLBIRVAE1OS);

	if (!kvm_has_feat(kvm, ID_AA64ISAR2_EL1, ATS1A, IMP))
		kvm->arch.fgu[HFGITR_GROUP] |= HFGITR_EL2_ATS1E1A;

	if (!kvm_has_feat(kvm, ID_AA64MMFR1_EL1, PAN, PAN2))
		kvm->arch.fgu[HFGITR_GROUP] |= (HFGITR_EL2_ATS1E1RP |
						HFGITR_EL2_ATS1E1WP);

	if (!kvm_has_s1pie(kvm))
		kvm->arch.fgu[HFGxTR_GROUP] |= (HFGxTR_EL2_nPIRE0_EL1 |
						HFGxTR_EL2_nPIR_EL1);

	if (!kvm_has_s1poe(kvm))
		kvm->arch.fgu[HFGxTR_GROUP] |= (HFGxTR_EL2_nPOR_EL1 |
						HFGxTR_EL2_nPOR_EL0);

	if (!kvm_has_feat(kvm, ID_AA64PFR0_EL1, AMU, IMP))
		kvm->arch.fgu[HAFGRTR_GROUP] |= ~(HAFGRTR_EL2_RES0 |
						  HAFGRTR_EL2_RES1);

	if (!kvm_has_feat(kvm, ID_AA64DFR0_EL1, BRBE, IMP)) {
		kvm->arch.fgu[HDFGRTR_GROUP] |= (HDFGRTR_EL2_nBRBDATA  |
						 HDFGRTR_EL2_nBRBCTL   |
						 HDFGRTR_EL2_nBRBIDR);
		kvm->arch.fgu[HFGITR_GROUP] |= (HFGITR_EL2_nBRBINJ |
						HFGITR_EL2_nBRBIALL);
	}

	set_bit(KVM_ARCH_FLAG_FGU_INITIALIZED, &kvm->arch.flags);
out:
	mutex_unlock(&kvm->arch.config_lock);
}

/*
 * Perform last adjustments to the ID registers that are implied by the
 * configuration outside of the ID regs themselves, as well as any
 * initialisation that directly depend on these ID registers (such as
 * RES0/RES1 behaviours). This is not the place to configure traps though.
 *
 * Because this can be called once per CPU, changes must be idempotent.
 */
int kvm_finalize_sys_regs(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	guard(mutex)(&kvm->arch.config_lock);

	if (!(static_branch_unlikely(&kvm_vgic_global_state.gicv3_cpuif) &&
	      irqchip_in_kernel(kvm) &&
	      kvm->arch.vgic.vgic_model == KVM_DEV_TYPE_ARM_VGIC_V3)) {
		kvm->arch.id_regs[IDREG_IDX(SYS_ID_AA64PFR0_EL1)] &= ~ID_AA64PFR0_EL1_GIC_MASK;
		kvm->arch.id_regs[IDREG_IDX(SYS_ID_PFR1_EL1)] &= ~ID_PFR1_EL1_GIC_MASK;
	}

	if (vcpu_has_nv(vcpu)) {
		int ret = kvm_init_nv_sysregs(vcpu);
		if (ret)
			return ret;
	}

	return 0;
}

int __init kvm_sys_reg_table_init(void)
{
	bool valid = true;
	unsigned int i;
	int ret = 0;

	/* Make sure tables are unique and in order. */
	valid &= check_sysreg_table(sys_reg_descs, ARRAY_SIZE(sys_reg_descs), false);
	valid &= check_sysreg_table(cp14_regs, ARRAY_SIZE(cp14_regs), true);
	valid &= check_sysreg_table(cp14_64_regs, ARRAY_SIZE(cp14_64_regs), true);
	valid &= check_sysreg_table(cp15_regs, ARRAY_SIZE(cp15_regs), true);
	valid &= check_sysreg_table(cp15_64_regs, ARRAY_SIZE(cp15_64_regs), true);
	valid &= check_sysreg_table(sys_insn_descs, ARRAY_SIZE(sys_insn_descs), false);

	if (!valid)
		return -EINVAL;

	init_imp_id_regs();

	ret = populate_nv_trap_config();

	for (i = 0; !ret && i < ARRAY_SIZE(sys_reg_descs); i++)
		ret = populate_sysreg_config(sys_reg_descs + i, i);

	for (i = 0; !ret && i < ARRAY_SIZE(sys_insn_descs); i++)
		ret = populate_sysreg_config(sys_insn_descs + i, i);

	return ret;
}
