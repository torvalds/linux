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

#include <linux/bsearch.h>
#include <linux/kvm_host.h>
#include <linux/mm.h>
#include <linux/printk.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/cputype.h>
#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_coproc.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_host.h>
#include <asm/kvm_hyp.h>
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
	if (!vcpu->arch.sysregs_loaded_on_cpu)
		goto immediate_read;

	/*
	 * System registers listed in the switch are not saved on every
	 * exit from the guest but are only saved on vcpu_put.
	 *
	 * Note that MPIDR_EL1 for the guest is set by KVM via VMPIDR_EL2 but
	 * should never be listed below, because the guest cannot modify its
	 * own MPIDR_EL1 and MPIDR_EL1 is accessed for VCPU A from VCPU B's
	 * thread when emulating cross-VCPU communication.
	 */
	switch (reg) {
	case CSSELR_EL1:	return read_sysreg_s(SYS_CSSELR_EL1);
	case SCTLR_EL1:		return read_sysreg_s(SYS_SCTLR_EL12);
	case ACTLR_EL1:		return read_sysreg_s(SYS_ACTLR_EL1);
	case CPACR_EL1:		return read_sysreg_s(SYS_CPACR_EL12);
	case TTBR0_EL1:		return read_sysreg_s(SYS_TTBR0_EL12);
	case TTBR1_EL1:		return read_sysreg_s(SYS_TTBR1_EL12);
	case TCR_EL1:		return read_sysreg_s(SYS_TCR_EL12);
	case ESR_EL1:		return read_sysreg_s(SYS_ESR_EL12);
	case AFSR0_EL1:		return read_sysreg_s(SYS_AFSR0_EL12);
	case AFSR1_EL1:		return read_sysreg_s(SYS_AFSR1_EL12);
	case FAR_EL1:		return read_sysreg_s(SYS_FAR_EL12);
	case MAIR_EL1:		return read_sysreg_s(SYS_MAIR_EL12);
	case VBAR_EL1:		return read_sysreg_s(SYS_VBAR_EL12);
	case CONTEXTIDR_EL1:	return read_sysreg_s(SYS_CONTEXTIDR_EL12);
	case TPIDR_EL0:		return read_sysreg_s(SYS_TPIDR_EL0);
	case TPIDRRO_EL0:	return read_sysreg_s(SYS_TPIDRRO_EL0);
	case TPIDR_EL1:		return read_sysreg_s(SYS_TPIDR_EL1);
	case AMAIR_EL1:		return read_sysreg_s(SYS_AMAIR_EL12);
	case CNTKCTL_EL1:	return read_sysreg_s(SYS_CNTKCTL_EL12);
	case PAR_EL1:		return read_sysreg_s(SYS_PAR_EL1);
	case DACR32_EL2:	return read_sysreg_s(SYS_DACR32_EL2);
	case IFSR32_EL2:	return read_sysreg_s(SYS_IFSR32_EL2);
	case DBGVCR32_EL2:	return read_sysreg_s(SYS_DBGVCR32_EL2);
	}

immediate_read:
	return __vcpu_sys_reg(vcpu, reg);
}

void vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg)
{
	if (!vcpu->arch.sysregs_loaded_on_cpu)
		goto immediate_write;

	/*
	 * System registers listed in the switch are not restored on every
	 * entry to the guest but are only restored on vcpu_load.
	 *
	 * Note that MPIDR_EL1 for the guest is set by KVM via VMPIDR_EL2 but
	 * should never be listed below, because the the MPIDR should only be
	 * set once, before running the VCPU, and never changed later.
	 */
	switch (reg) {
	case CSSELR_EL1:	write_sysreg_s(val, SYS_CSSELR_EL1);	return;
	case SCTLR_EL1:		write_sysreg_s(val, SYS_SCTLR_EL12);	return;
	case ACTLR_EL1:		write_sysreg_s(val, SYS_ACTLR_EL1);	return;
	case CPACR_EL1:		write_sysreg_s(val, SYS_CPACR_EL12);	return;
	case TTBR0_EL1:		write_sysreg_s(val, SYS_TTBR0_EL12);	return;
	case TTBR1_EL1:		write_sysreg_s(val, SYS_TTBR1_EL12);	return;
	case TCR_EL1:		write_sysreg_s(val, SYS_TCR_EL12);	return;
	case ESR_EL1:		write_sysreg_s(val, SYS_ESR_EL12);	return;
	case AFSR0_EL1:		write_sysreg_s(val, SYS_AFSR0_EL12);	return;
	case AFSR1_EL1:		write_sysreg_s(val, SYS_AFSR1_EL12);	return;
	case FAR_EL1:		write_sysreg_s(val, SYS_FAR_EL12);	return;
	case MAIR_EL1:		write_sysreg_s(val, SYS_MAIR_EL12);	return;
	case VBAR_EL1:		write_sysreg_s(val, SYS_VBAR_EL12);	return;
	case CONTEXTIDR_EL1:	write_sysreg_s(val, SYS_CONTEXTIDR_EL12); return;
	case TPIDR_EL0:		write_sysreg_s(val, SYS_TPIDR_EL0);	return;
	case TPIDRRO_EL0:	write_sysreg_s(val, SYS_TPIDRRO_EL0);	return;
	case TPIDR_EL1:		write_sysreg_s(val, SYS_TPIDR_EL1);	return;
	case AMAIR_EL1:		write_sysreg_s(val, SYS_AMAIR_EL12);	return;
	case CNTKCTL_EL1:	write_sysreg_s(val, SYS_CNTKCTL_EL12);	return;
	case PAR_EL1:		write_sysreg_s(val, SYS_PAR_EL1);	return;
	case DACR32_EL2:	write_sysreg_s(val, SYS_DACR32_EL2);	return;
	case IFSR32_EL2:	write_sysreg_s(val, SYS_IFSR32_EL2);	return;
	case DBGVCR32_EL2:	write_sysreg_s(val, SYS_DBGVCR32_EL2);	return;
	}

immediate_write:
	 __vcpu_sys_reg(vcpu, reg) = val;
}

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
	u64 val;
	int reg = r->reg;

	BUG_ON(!p->is_write);

	/* See the 32bit mapping in kvm_host.h */
	if (p->is_aarch32)
		reg = r->reg / 2;

	if (!p->is_aarch32 || !p->is_32bit) {
		val = p->regval;
	} else {
		val = vcpu_read_sys_reg(vcpu, reg);
		if (r->reg % 2)
			val = (p->regval << 32) | (u64)lower_32_bits(val);
		else
			val = ((u64)upper_32_bits(val) << 32) |
				lower_32_bits(p->regval);
	}
	vcpu_write_sys_reg(vcpu, val, reg);

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
	if (p->is_aarch32) {
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
	} else {
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
	u32 sr = sys_reg((u32)r->Op0, (u32)r->Op1,
			 (u32)r->CRn, (u32)r->CRm, (u32)r->Op2);

	if (!(val & (0xfUL << ID_AA64MMFR1_LOR_SHIFT))) {
		kvm_inject_undefined(vcpu);
		return false;
	}

	if (p->is_write && sr == SYS_LORID_EL1)
		return write_to_read_only(vcpu, p, r);

	return trap_raz_wi(vcpu, p, r);
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
		vcpu_write_sys_reg(vcpu, p->regval, r->reg);
		vcpu->arch.flags |= KVM_ARM64_DEBUG_DIRTY;
	} else {
		p->regval = vcpu_read_sys_reg(vcpu, r->reg);
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
	vcpu->arch.flags |= KVM_ARM64_DEBUG_DIRTY;
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
	u64 amair = read_sysreg(amair_el1);
	vcpu_write_sys_reg(vcpu, amair, AMAIR_EL1);
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
	if (!system_supports_32bit_el0())
		val |= ARMV8_PMU_PMCR_LC;
	__vcpu_sys_reg(vcpu, r->reg) = val;
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

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (pmu_access_el0_disabled(vcpu))
		return false;

	if (p->is_write) {
		/* Only update writeable bits of PMCR */
		val = __vcpu_sys_reg(vcpu, PMCR_EL0);
		val &= ~ARMV8_PMU_PMCR_MASK;
		val |= p->regval & ARMV8_PMU_PMCR_MASK;
		if (!system_supports_32bit_el0())
			val |= ARMV8_PMU_PMCR_LC;
		__vcpu_sys_reg(vcpu, PMCR_EL0) = val;
		kvm_pmu_handle_pmcr(vcpu, val);
		kvm_vcpu_pmu_restore_guest(vcpu);
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
	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

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

	pmcr = __vcpu_sys_reg(vcpu, PMCR_EL0);
	val = (pmcr >> ARMV8_PMU_PMCR_N_SHIFT) & ARMV8_PMU_PMCR_N_MASK;
	if (idx >= val && idx != ARMV8_PMU_CYCLE_IDX) {
		kvm_inject_undefined(vcpu);
		return false;
	}

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

			idx = __vcpu_sys_reg(vcpu, PMSELR_EL0)
			      & ARMV8_PMU_COUNTER_MASK;
		} else if (r->Op2 == 0) {
			/* PMCCNTR_EL0 */
			if (pmu_access_cycle_counter_el0_disabled(vcpu))
				return false;

			idx = ARMV8_PMU_CYCLE_IDX;
		} else {
			return false;
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
	} else {
		return false;
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

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

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
		p->regval = __vcpu_sys_reg(vcpu, PMCNTENSET_EL0) & mask;
	}

	return true;
}

static bool access_pminten(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask = kvm_pmu_valid_counter_mask(vcpu);

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

	if (!vcpu_mode_priv(vcpu)) {
		kvm_inject_undefined(vcpu);
		return false;
	}

	if (p->is_write) {
		u64 val = p->regval & mask;

		if (r->Op2 & 0x1)
			/* accessing PMINTENSET_EL1 */
			__vcpu_sys_reg(vcpu, PMINTENSET_EL1) |= val;
		else
			/* accessing PMINTENCLR_EL1 */
			__vcpu_sys_reg(vcpu, PMINTENSET_EL1) &= ~val;
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMINTENSET_EL1) & mask;
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
			__vcpu_sys_reg(vcpu, PMOVSSET_EL0) |= (p->regval & mask);
		else
			/* accessing PMOVSCLR_EL0 */
			__vcpu_sys_reg(vcpu, PMOVSSET_EL0) &= ~(p->regval & mask);
	} else {
		p->regval = __vcpu_sys_reg(vcpu, PMOVSSET_EL0) & mask;
	}

	return true;
}

static bool access_pmswinc(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			   const struct sys_reg_desc *r)
{
	u64 mask;

	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

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
	if (!kvm_arm_pmu_v3_ready(vcpu))
		return trap_raz_wi(vcpu, p, r);

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

#define reg_to_encoding(x)						\
	sys_reg((u32)(x)->Op0, (u32)(x)->Op1,				\
		(u32)(x)->CRn, (u32)(x)->CRm, (u32)(x)->Op2);

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

/* Macro to expand the PMEVCNTRn_EL0 register */
#define PMU_PMEVCNTR_EL0(n)						\
	{ SYS_DESC(SYS_PMEVCNTRn_EL0(n)),					\
	  access_pmu_evcntr, reset_unknown, (PMEVCNTR0_EL0 + n), }

/* Macro to expand the PMEVTYPERn_EL0 register */
#define PMU_PMEVTYPER_EL0(n)						\
	{ SYS_DESC(SYS_PMEVTYPERn_EL0(n)),					\
	  access_pmu_evtyper, reset_unknown, (PMEVTYPER0_EL0 + n), }

static bool trap_ptrauth(struct kvm_vcpu *vcpu,
			 struct sys_reg_params *p,
			 const struct sys_reg_desc *rd)
{
	kvm_arm_vcpu_ptrauth_trap(vcpu);

	/*
	 * Return false for both cases as we never skip the trapped
	 * instruction:
	 *
	 * - Either we re-execute the same key register access instruction
	 *   after enabling ptrauth.
	 * - Or an UNDEF is injected as ptrauth is not supported/enabled.
	 */
	return false;
}

static unsigned int ptrauth_visibility(const struct kvm_vcpu *vcpu,
			const struct sys_reg_desc *rd)
{
	return vcpu_has_ptrauth(vcpu) ? 0 : REG_HIDDEN_USER | REG_HIDDEN_GUEST;
}

#define __PTRAUTH_KEY(k)						\
	{ SYS_DESC(SYS_## k), trap_ptrauth, reset_unknown, k,		\
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
	default:
		BUG();
	}

	if (p->is_write)
		kvm_arm_timer_write_sysreg(vcpu, tmr, treg, p->regval);
	else
		p->regval = kvm_arm_timer_read_sysreg(vcpu, tmr, treg);

	return true;
}

/* Read a sanitised cpufeature ID register by sys_reg_desc */
static u64 read_id_reg(const struct kvm_vcpu *vcpu,
		struct sys_reg_desc const *r, bool raz)
{
	u32 id = sys_reg((u32)r->Op0, (u32)r->Op1,
			 (u32)r->CRn, (u32)r->CRm, (u32)r->Op2);
	u64 val = raz ? 0 : read_sanitised_ftr_reg(id);

	if (id == SYS_ID_AA64PFR0_EL1 && !vcpu_has_sve(vcpu)) {
		val &= ~(0xfUL << ID_AA64PFR0_SVE_SHIFT);
	} else if (id == SYS_ID_AA64ISAR1_EL1 && !vcpu_has_ptrauth(vcpu)) {
		val &= ~((0xfUL << ID_AA64ISAR1_APA_SHIFT) |
			 (0xfUL << ID_AA64ISAR1_API_SHIFT) |
			 (0xfUL << ID_AA64ISAR1_GPA_SHIFT) |
			 (0xfUL << ID_AA64ISAR1_GPI_SHIFT));
	}

	return val;
}

/* cpufeature ID register access trap handlers */

static bool __access_id_reg(struct kvm_vcpu *vcpu,
			    struct sys_reg_params *p,
			    const struct sys_reg_desc *r,
			    bool raz)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	p->regval = read_id_reg(vcpu, r, raz);
	return true;
}

static bool access_id_reg(struct kvm_vcpu *vcpu,
			  struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	return __access_id_reg(vcpu, p, r, false);
}

static bool access_raz_id_reg(struct kvm_vcpu *vcpu,
			      struct sys_reg_params *p,
			      const struct sys_reg_desc *r)
{
	return __access_id_reg(vcpu, p, r, true);
}

static int reg_from_user(u64 *val, const void __user *uaddr, u64 id);
static int reg_to_user(void __user *uaddr, const u64 *val, u64 id);
static u64 sys_reg_to_index(const struct sys_reg_desc *reg);

/* Visibility overrides for SVE-specific control registers */
static unsigned int sve_visibility(const struct kvm_vcpu *vcpu,
				   const struct sys_reg_desc *rd)
{
	if (vcpu_has_sve(vcpu))
		return 0;

	return REG_HIDDEN_USER | REG_HIDDEN_GUEST;
}

/* Visibility overrides for SVE-specific ID registers */
static unsigned int sve_id_visibility(const struct kvm_vcpu *vcpu,
				      const struct sys_reg_desc *rd)
{
	if (vcpu_has_sve(vcpu))
		return 0;

	return REG_HIDDEN_USER;
}

/* Generate the emulated ID_AA64ZFR0_EL1 value exposed to the guest */
static u64 guest_id_aa64zfr0_el1(const struct kvm_vcpu *vcpu)
{
	if (!vcpu_has_sve(vcpu))
		return 0;

	return read_sanitised_ftr_reg(SYS_ID_AA64ZFR0_EL1);
}

static bool access_id_aa64zfr0_el1(struct kvm_vcpu *vcpu,
				   struct sys_reg_params *p,
				   const struct sys_reg_desc *rd)
{
	if (p->is_write)
		return write_to_read_only(vcpu, p, rd);

	p->regval = guest_id_aa64zfr0_el1(vcpu);
	return true;
}

static int get_id_aa64zfr0_el1(struct kvm_vcpu *vcpu,
		const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	u64 val;

	if (WARN_ON(!vcpu_has_sve(vcpu)))
		return -ENOENT;

	val = guest_id_aa64zfr0_el1(vcpu);
	return reg_to_user(uaddr, &val, reg->id);
}

static int set_id_aa64zfr0_el1(struct kvm_vcpu *vcpu,
		const struct sys_reg_desc *rd,
		const struct kvm_one_reg *reg, void __user *uaddr)
{
	const u64 id = sys_reg_to_index(rd);
	int err;
	u64 val;

	if (WARN_ON(!vcpu_has_sve(vcpu)))
		return -ENOENT;

	err = reg_from_user(&val, uaddr, id);
	if (err)
		return err;

	/* This is what we mean by invariant: you can't change it. */
	if (val != guest_id_aa64zfr0_el1(vcpu))
		return -EINVAL;

	return 0;
}

/*
 * cpufeature ID register user accessors
 *
 * For now, these registers are immutable for userspace, so no values
 * are stored, and for set_id_reg() we don't allow the effective value
 * to be changed.
 */
static int __get_id_reg(const struct kvm_vcpu *vcpu,
			const struct sys_reg_desc *rd, void __user *uaddr,
			bool raz)
{
	const u64 id = sys_reg_to_index(rd);
	const u64 val = read_id_reg(vcpu, rd, raz);

	return reg_to_user(uaddr, &val, id);
}

static int __set_id_reg(const struct kvm_vcpu *vcpu,
			const struct sys_reg_desc *rd, void __user *uaddr,
			bool raz)
{
	const u64 id = sys_reg_to_index(rd);
	int err;
	u64 val;

	err = reg_from_user(&val, uaddr, id);
	if (err)
		return err;

	/* This is what we mean by invariant: you can't change it. */
	if (val != read_id_reg(vcpu, rd, raz))
		return -EINVAL;

	return 0;
}

static int get_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      const struct kvm_one_reg *reg, void __user *uaddr)
{
	return __get_id_reg(vcpu, rd, uaddr, false);
}

static int set_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
		      const struct kvm_one_reg *reg, void __user *uaddr)
{
	return __set_id_reg(vcpu, rd, uaddr, false);
}

static int get_raz_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			  const struct kvm_one_reg *reg, void __user *uaddr)
{
	return __get_id_reg(vcpu, rd, uaddr, true);
}

static int set_raz_id_reg(struct kvm_vcpu *vcpu, const struct sys_reg_desc *rd,
			  const struct kvm_one_reg *reg, void __user *uaddr)
{
	return __set_id_reg(vcpu, rd, uaddr, true);
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

	p->regval = read_sysreg(clidr_el1);
	return true;
}

static bool access_csselr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	if (p->is_write)
		vcpu_write_sys_reg(vcpu, p->regval, r->reg);
	else
		p->regval = vcpu_read_sys_reg(vcpu, r->reg);
	return true;
}

static bool access_ccsidr(struct kvm_vcpu *vcpu, struct sys_reg_params *p,
			  const struct sys_reg_desc *r)
{
	u32 csselr;

	if (p->is_write)
		return write_to_read_only(vcpu, p, r);

	csselr = vcpu_read_sys_reg(vcpu, CSSELR_EL1);
	p->regval = get_ccsidr(csselr);

	/*
	 * Guests should not be doing cache operations by set/way at all, and
	 * for this reason, we trap them and attempt to infer the intent, so
	 * that we can flush the entire guest's address space at the appropriate
	 * time.
	 * To prevent this trapping from causing performance problems, let's
	 * expose the geometry of all data and unified caches (which are
	 * guaranteed to be PIPT and thus non-aliasing) as 1 set and 1 way.
	 * [If guests should attempt to infer aliasing properties from the
	 * geometry (which is not permitted by the architecture), they would
	 * only do so for virtually indexed caches.]
	 */
	if (!(csselr & 1)) // data or unified cache
		p->regval &= ~GENMASK(27, 3);
	return true;
}

/* sys_reg_desc initialiser for known cpufeature ID registers */
#define ID_SANITISED(name) {			\
	SYS_DESC(SYS_##name),			\
	.access	= access_id_reg,		\
	.get_user = get_id_reg,			\
	.set_user = set_id_reg,			\
}

/*
 * sys_reg_desc initialiser for architecturally unallocated cpufeature ID
 * register with encoding Op0=3, Op1=0, CRn=0, CRm=crm, Op2=op2
 * (1 <= crm < 8, 0 <= Op2 < 8).
 */
#define ID_UNALLOCATED(crm, op2) {			\
	Op0(3), Op1(0), CRn(0), CRm(crm), Op2(op2),	\
	.access = access_raz_id_reg,			\
	.get_user = get_raz_id_reg,			\
	.set_user = set_raz_id_reg,			\
}

/*
 * sys_reg_desc initialiser for known ID registers that we hide from guests.
 * For now, these are exposed just like unallocated ID regs: they appear
 * RAZ for the guest.
 */
#define ID_HIDDEN(name) {			\
	SYS_DESC(SYS_##name),			\
	.access = access_raz_id_reg,		\
	.get_user = get_raz_id_reg,		\
	.set_user = set_raz_id_reg,		\
}

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
	{ SYS_DESC(SYS_OSLAR_EL1), trap_raz_wi },
	{ SYS_DESC(SYS_OSLSR_EL1), trap_oslsr_el1 },
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
	ID_SANITISED(ID_PFR0_EL1),
	ID_SANITISED(ID_PFR1_EL1),
	ID_SANITISED(ID_DFR0_EL1),
	ID_HIDDEN(ID_AFR0_EL1),
	ID_SANITISED(ID_MMFR0_EL1),
	ID_SANITISED(ID_MMFR1_EL1),
	ID_SANITISED(ID_MMFR2_EL1),
	ID_SANITISED(ID_MMFR3_EL1),

	/* CRm=2 */
	ID_SANITISED(ID_ISAR0_EL1),
	ID_SANITISED(ID_ISAR1_EL1),
	ID_SANITISED(ID_ISAR2_EL1),
	ID_SANITISED(ID_ISAR3_EL1),
	ID_SANITISED(ID_ISAR4_EL1),
	ID_SANITISED(ID_ISAR5_EL1),
	ID_SANITISED(ID_MMFR4_EL1),
	ID_UNALLOCATED(2,7),

	/* CRm=3 */
	ID_SANITISED(MVFR0_EL1),
	ID_SANITISED(MVFR1_EL1),
	ID_SANITISED(MVFR2_EL1),
	ID_UNALLOCATED(3,3),
	ID_UNALLOCATED(3,4),
	ID_UNALLOCATED(3,5),
	ID_UNALLOCATED(3,6),
	ID_UNALLOCATED(3,7),

	/* AArch64 ID registers */
	/* CRm=4 */
	ID_SANITISED(ID_AA64PFR0_EL1),
	ID_SANITISED(ID_AA64PFR1_EL1),
	ID_UNALLOCATED(4,2),
	ID_UNALLOCATED(4,3),
	{ SYS_DESC(SYS_ID_AA64ZFR0_EL1), access_id_aa64zfr0_el1, .get_user = get_id_aa64zfr0_el1, .set_user = set_id_aa64zfr0_el1, .visibility = sve_id_visibility },
	ID_UNALLOCATED(4,5),
	ID_UNALLOCATED(4,6),
	ID_UNALLOCATED(4,7),

	/* CRm=5 */
	ID_SANITISED(ID_AA64DFR0_EL1),
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
	ID_UNALLOCATED(6,2),
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
	{ SYS_DESC(SYS_CPACR_EL1), NULL, reset_val, CPACR_EL1, 0 },
	{ SYS_DESC(SYS_ZCR_EL1), NULL, reset_val, ZCR_EL1, 0, .visibility = sve_visibility },
	{ SYS_DESC(SYS_TTBR0_EL1), access_vm_reg, reset_unknown, TTBR0_EL1 },
	{ SYS_DESC(SYS_TTBR1_EL1), access_vm_reg, reset_unknown, TTBR1_EL1 },
	{ SYS_DESC(SYS_TCR_EL1), access_vm_reg, reset_val, TCR_EL1, 0 },

	PTRAUTH_KEY(APIA),
	PTRAUTH_KEY(APIB),
	PTRAUTH_KEY(APDA),
	PTRAUTH_KEY(APDB),
	PTRAUTH_KEY(APGA),

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

	{ SYS_DESC(SYS_FAR_EL1), access_vm_reg, reset_unknown, FAR_EL1 },
	{ SYS_DESC(SYS_PAR_EL1), NULL, reset_unknown, PAR_EL1 },

	{ SYS_DESC(SYS_PMINTENSET_EL1), access_pminten, reset_unknown, PMINTENSET_EL1 },
	{ SYS_DESC(SYS_PMINTENCLR_EL1), access_pminten, NULL, PMINTENSET_EL1 },

	{ SYS_DESC(SYS_MAIR_EL1), access_vm_reg, reset_unknown, MAIR_EL1 },
	{ SYS_DESC(SYS_AMAIR_EL1), access_vm_reg, reset_amair_el1, AMAIR_EL1 },

	{ SYS_DESC(SYS_LORSA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LOREA_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORN_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORC_EL1), trap_loregion },
	{ SYS_DESC(SYS_LORID_EL1), trap_loregion },

	{ SYS_DESC(SYS_VBAR_EL1), NULL, reset_val, VBAR_EL1, 0 },
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

	{ SYS_DESC(SYS_CNTKCTL_EL1), NULL, reset_val, CNTKCTL_EL1, 0},

	{ SYS_DESC(SYS_CCSIDR_EL1), access_ccsidr },
	{ SYS_DESC(SYS_CLIDR_EL1), access_clidr },
	{ SYS_DESC(SYS_CSSELR_EL1), access_csselr, reset_unknown, CSSELR_EL1 },
	{ SYS_DESC(SYS_CTR_EL0), access_ctr },

	{ SYS_DESC(SYS_PMCR_EL0), access_pmcr, reset_pmcr, PMCR_EL0 },
	{ SYS_DESC(SYS_PMCNTENSET_EL0), access_pmcnten, reset_unknown, PMCNTENSET_EL0 },
	{ SYS_DESC(SYS_PMCNTENCLR_EL0), access_pmcnten, NULL, PMCNTENSET_EL0 },
	{ SYS_DESC(SYS_PMOVSCLR_EL0), access_pmovs, NULL, PMOVSSET_EL0 },
	{ SYS_DESC(SYS_PMSWINC_EL0), access_pmswinc, reset_unknown, PMSWINC_EL0 },
	{ SYS_DESC(SYS_PMSELR_EL0), access_pmselr, reset_unknown, PMSELR_EL0 },
	{ SYS_DESC(SYS_PMCEID0_EL0), access_pmceid },
	{ SYS_DESC(SYS_PMCEID1_EL0), access_pmceid },
	{ SYS_DESC(SYS_PMCCNTR_EL0), access_pmu_evcntr, reset_unknown, PMCCNTR_EL0 },
	{ SYS_DESC(SYS_PMXEVTYPER_EL0), access_pmu_evtyper },
	{ SYS_DESC(SYS_PMXEVCNTR_EL0), access_pmu_evcntr },
	/*
	 * PMUSERENR_EL0 resets as unknown in 64bit mode while it resets as zero
	 * in 32bit mode. Here we choose to reset it as zero for consistency.
	 */
	{ SYS_DESC(SYS_PMUSERENR_EL0), access_pmuserenr, reset_val, PMUSERENR_EL0, 0 },
	{ SYS_DESC(SYS_PMOVSSET_EL0), access_pmovs, reset_unknown, PMOVSSET_EL0 },

	{ SYS_DESC(SYS_TPIDR_EL0), NULL, reset_unknown, TPIDR_EL0 },
	{ SYS_DESC(SYS_TPIDRRO_EL0), NULL, reset_unknown, TPIDRRO_EL0 },

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
	{ SYS_DESC(SYS_PMCCFILTR_EL0), access_pmu_evtyper, reset_val, PMCCFILTR_EL0, 0 },

	{ SYS_DESC(SYS_DACR32_EL2), NULL, reset_unknown, DACR32_EL2 },
	{ SYS_DESC(SYS_IFSR32_EL2), NULL, reset_unknown, IFSR32_EL2 },
	{ SYS_DESC(SYS_FPEXC32_EL2), NULL, reset_val, FPEXC32_EL2, 0x700 },
};

static bool trap_dbgidr(struct kvm_vcpu *vcpu,
			struct sys_reg_params *p,
			const struct sys_reg_desc *r)
{
	if (p->is_write) {
		return ignore_write(vcpu, p);
	} else {
		u64 dfr = read_sanitised_ftr_reg(SYS_ID_AA64DFR0_EL1);
		u64 pfr = read_sanitised_ftr_reg(SYS_ID_AA64PFR0_EL1);
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
		vcpu->arch.flags |= KVM_ARM64_DEBUG_DIRTY;
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

		vcpu->arch.flags |= KVM_ARM64_DEBUG_DIRTY;
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
	{ Op1( 0), CRn( 0), CRm( 0), Op2( 1), access_ctr },
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
	{ Op1(0), CRn(14), CRm(15), Op2(7), access_pmu_evtyper },

	{ Op1(1), CRn( 0), CRm( 0), Op2(0), access_ccsidr },
	{ Op1(1), CRn( 0), CRm( 0), Op2(1), access_clidr },
	{ Op1(2), CRn( 0), CRm( 0), Op2(0), access_csselr, NULL, c0_CSSELR },
};

static const struct sys_reg_desc cp15_64_regs[] = {
	{ Op1( 0), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, c2_TTBR0 },
	{ Op1( 0), CRn( 0), CRm( 9), Op2( 0), access_pmu_evcntr },
	{ Op1( 0), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_SGI1R */
	{ Op1( 1), CRn( 0), CRm( 2), Op2( 0), access_vm_reg, NULL, c2_TTBR1 },
	{ Op1( 1), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_ASGI1R */
	{ Op1( 2), CRn( 0), CRm(12), Op2( 0), access_gic_sgi }, /* ICC_SGI0R */
	{ SYS_DESC(SYS_AARCH32_CNTP_CVAL),    access_arch_timer },
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

static int match_sys_reg(const void *key, const void *elt)
{
	const unsigned long pval = (unsigned long)key;
	const struct sys_reg_desc *r = elt;

	return pval - reg_to_encoding(r);
}

static const struct sys_reg_desc *find_reg(const struct sys_reg_params *params,
					 const struct sys_reg_desc table[],
					 unsigned int num)
{
	unsigned long pval = reg_to_encoding(params);

	return bsearch((void *)pval, table, num, sizeof(table[0]), match_sys_reg);
}

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu, struct kvm_run *run)
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
	if (sysreg_hidden_from_guest(vcpu, r)) {
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
		kvm_skip_instr(vcpu, kvm_vcpu_trap_il_is32bit(vcpu));
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
		perform_access(vcpu, params, r);
		return 0;
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

	kvm_err("Unsupported guest CP%d access at: %08lx [%08lx]\n",
		cp, *vcpu_pc(vcpu), *vcpu_cpsr(vcpu));
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
	int Rt = kvm_vcpu_sys_get_rt(vcpu);
	int Rt2 = (hsr >> 10) & 0x1f;

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

	/*
	 * Try to emulate the coprocessor access using the target
	 * specific table first, and using the global table afterwards.
	 * If either of the tables contains a handler, handle the
	 * potential register operation in the case of a read and return
	 * with success.
	 */
	if (!emulate_cp(vcpu, &params, target_specific, nr_specific) ||
	    !emulate_cp(vcpu, &params, global, nr_global)) {
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
	int Rt  = kvm_vcpu_sys_get_rt(vcpu);

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
		perform_access(vcpu, params, r);
	} else {
		kvm_err("Unsupported guest sys_reg access at: %lx [%08lx]\n",
			*vcpu_pc(vcpu), *vcpu_cpsr(vcpu));
		print_sys_reg_instr(params);
		kvm_inject_undefined(vcpu);
	}
	return 1;
}

static void reset_sys_reg_descs(struct kvm_vcpu *vcpu,
				const struct sys_reg_desc *table, size_t num,
				unsigned long *bmap)
{
	unsigned long i;

	for (i = 0; i < num; i++)
		if (table[i].reset) {
			int reg = table[i].reg;

			table[i].reset(vcpu, &table[i]);
			if (reg > 0 && reg < NR_SYS_REGS)
				set_bit(reg, bmap);
		}
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
	int Rt = kvm_vcpu_sys_get_rt(vcpu);
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

const struct sys_reg_desc *find_reg_by_id(u64 id,
					  struct sys_reg_params *params,
					  const struct sys_reg_desc table[],
					  unsigned int num)
{
	if (!index_to_params(id, params))
		return NULL;

	return find_reg(params, table, num);
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

	table = get_target_table(vcpu->arch.target, true, &num);
	r = find_reg_by_id(id, &params, table, num);
	if (!r)
		r = find_reg(&params, sys_reg_descs, ARRAY_SIZE(sys_reg_descs));

	/* Not saved in the sys_reg array and not otherwise accessible? */
	if (r && !(r->reg || r->get_user))
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
FUNCTION_INVARIANT(clidr_el1)
FUNCTION_INVARIANT(aidr_el1)

static void get_ctr_el0(struct kvm_vcpu *v, const struct sys_reg_desc *r)
{
	((struct sys_reg_desc *)r)->val = read_sanitised_ftr_reg(SYS_CTR_EL0);
}

/* ->val is filled in by kvm_sys_reg_table_init() */
static struct sys_reg_desc invariant_sys_regs[] = {
	{ SYS_DESC(SYS_MIDR_EL1), NULL, get_midr_el1 },
	{ SYS_DESC(SYS_REVIDR_EL1), NULL, get_revidr_el1 },
	{ SYS_DESC(SYS_CLIDR_EL1), NULL, get_clidr_el1 },
	{ SYS_DESC(SYS_AIDR_EL1), NULL, get_aidr_el1 },
	{ SYS_DESC(SYS_CTR_EL0), NULL, get_ctr_el0 },
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

	r = find_reg_by_id(id, &params, invariant_sys_regs,
			   ARRAY_SIZE(invariant_sys_regs));
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

	r = find_reg_by_id(id, &params, invariant_sys_regs,
			   ARRAY_SIZE(invariant_sys_regs));
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

	/* Check for regs disabled by runtime config */
	if (sysreg_hidden_from_user(vcpu, r))
		return -ENOENT;

	if (r->get_user)
		return (r->get_user)(vcpu, r, reg, uaddr);

	return reg_to_user(uaddr, &__vcpu_sys_reg(vcpu, r->reg), reg->id);
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

	/* Check for regs disabled by runtime config */
	if (sysreg_hidden_from_user(vcpu, r))
		return -ENOENT;

	if (r->set_user)
		return (r->set_user)(vcpu, r, reg, uaddr);

	return reg_from_user(&__vcpu_sys_reg(vcpu, r->reg), uaddr, reg->id);
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

	if (sysreg_hidden_from_user(vcpu, rd))
		return 0;

	if (!copy_reg_to_user(rd, uind))
		return -EFAULT;

	(*total)++;
	return 0;
}

/* Assumed ordered tables, see kvm_sys_reg_table_init. */
static int walk_sys_regs(struct kvm_vcpu *vcpu, u64 __user *uind)
{
	const struct sys_reg_desc *i1, *i2, *end1, *end2;
	unsigned int total = 0;
	size_t num;
	int err;

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
		if (cmp <= 0)
			err = walk_one_sys_reg(vcpu, i1, &uind, &total);
		else
			err = walk_one_sys_reg(vcpu, i2, &uind, &total);

		if (err)
			return err;

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
	DECLARE_BITMAP(bmap, NR_SYS_REGS) = { 0, };

	/* Generic chip reset first (so target could override). */
	reset_sys_reg_descs(vcpu, sys_reg_descs, ARRAY_SIZE(sys_reg_descs), bmap);

	table = get_target_table(vcpu->arch.target, true, &num);
	reset_sys_reg_descs(vcpu, table, num, bmap);

	for (num = 1; num < NR_SYS_REGS; num++) {
		if (WARN(!test_bit(num, bmap),
			 "Didn't reset __vcpu_sys_reg(%zi)\n", num))
			break;
	}
}
