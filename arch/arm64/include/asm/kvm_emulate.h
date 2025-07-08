/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/include/kvm_emulate.h
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#ifndef __ARM64_KVM_EMULATE_H__
#define __ARM64_KVM_EMULATE_H__

#include <linux/bitfield.h>
#include <linux/kvm_host.h>

#include <asm/debug-monitors.h>
#include <asm/esr.h>
#include <asm/kvm_arm.h>
#include <asm/kvm_hyp.h>
#include <asm/kvm_nested.h>
#include <asm/ptrace.h>
#include <asm/cputype.h>
#include <asm/virt.h>

#define CURRENT_EL_SP_EL0_VECTOR	0x0
#define CURRENT_EL_SP_ELx_VECTOR	0x200
#define LOWER_EL_AArch64_VECTOR		0x400
#define LOWER_EL_AArch32_VECTOR		0x600

enum exception_type {
	except_type_sync	= 0,
	except_type_irq		= 0x80,
	except_type_fiq		= 0x100,
	except_type_serror	= 0x180,
};

#define kvm_exception_type_names		\
	{ except_type_sync,	"SYNC"   },	\
	{ except_type_irq,	"IRQ"    },	\
	{ except_type_fiq,	"FIQ"    },	\
	{ except_type_serror,	"SERROR" }

bool kvm_condition_valid32(const struct kvm_vcpu *vcpu);
void kvm_skip_instr32(struct kvm_vcpu *vcpu);

void kvm_inject_undefined(struct kvm_vcpu *vcpu);
void kvm_inject_vabt(struct kvm_vcpu *vcpu);
int kvm_inject_sea(struct kvm_vcpu *vcpu, bool iabt, u64 addr);
void kvm_inject_size_fault(struct kvm_vcpu *vcpu);

static inline int kvm_inject_sea_dabt(struct kvm_vcpu *vcpu, u64 addr)
{
	return kvm_inject_sea(vcpu, false, addr);
}

static inline int kvm_inject_sea_iabt(struct kvm_vcpu *vcpu, u64 addr)
{
	return kvm_inject_sea(vcpu, true, addr);
}

void kvm_vcpu_wfi(struct kvm_vcpu *vcpu);

void kvm_emulate_nested_eret(struct kvm_vcpu *vcpu);
int kvm_inject_nested_sync(struct kvm_vcpu *vcpu, u64 esr_el2);
int kvm_inject_nested_irq(struct kvm_vcpu *vcpu);
int kvm_inject_nested_sea(struct kvm_vcpu *vcpu, bool iabt, u64 addr);

static inline void kvm_inject_nested_sve_trap(struct kvm_vcpu *vcpu)
{
	u64 esr = FIELD_PREP(ESR_ELx_EC_MASK, ESR_ELx_EC_SVE) |
		  ESR_ELx_IL;

	kvm_inject_nested_sync(vcpu, esr);
}

#if defined(__KVM_VHE_HYPERVISOR__) || defined(__KVM_NVHE_HYPERVISOR__)
static __always_inline bool vcpu_el1_is_32bit(struct kvm_vcpu *vcpu)
{
	return !(vcpu->arch.hcr_el2 & HCR_RW);
}
#else
static __always_inline bool vcpu_el1_is_32bit(struct kvm_vcpu *vcpu)
{
	return vcpu_has_feature(vcpu, KVM_ARM_VCPU_EL1_32BIT);
}
#endif

static inline void vcpu_reset_hcr(struct kvm_vcpu *vcpu)
{
	if (!vcpu_has_run_once(vcpu))
		vcpu->arch.hcr_el2 = HCR_GUEST_FLAGS;

	/*
	 * For non-FWB CPUs, we trap VM ops (HCR_EL2.TVM) until M+C
	 * get set in SCTLR_EL1 such that we can detect when the guest
	 * MMU gets turned on and do the necessary cache maintenance
	 * then.
	 */
	if (!cpus_have_final_cap(ARM64_HAS_STAGE2_FWB))
		vcpu->arch.hcr_el2 |= HCR_TVM;
}

static inline unsigned long *vcpu_hcr(struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu->arch.hcr_el2;
}

static inline void vcpu_clear_wfx_traps(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 &= ~HCR_TWE;
	if (atomic_read(&vcpu->arch.vgic_cpu.vgic_v3.its_vpe.vlpi_count) ||
	    vcpu->kvm->arch.vgic.nassgireq)
		vcpu->arch.hcr_el2 &= ~HCR_TWI;
	else
		vcpu->arch.hcr_el2 |= HCR_TWI;
}

static inline void vcpu_set_wfx_traps(struct kvm_vcpu *vcpu)
{
	vcpu->arch.hcr_el2 |= HCR_TWE;
	vcpu->arch.hcr_el2 |= HCR_TWI;
}

static inline unsigned long vcpu_get_vsesr(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.vsesr_el2;
}

static inline void vcpu_set_vsesr(struct kvm_vcpu *vcpu, u64 vsesr)
{
	vcpu->arch.vsesr_el2 = vsesr;
}

static __always_inline unsigned long *vcpu_pc(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu_gp_regs(vcpu)->pc;
}

static __always_inline unsigned long *vcpu_cpsr(const struct kvm_vcpu *vcpu)
{
	return (unsigned long *)&vcpu_gp_regs(vcpu)->pstate;
}

static __always_inline bool vcpu_mode_is_32bit(const struct kvm_vcpu *vcpu)
{
	return !!(*vcpu_cpsr(vcpu) & PSR_MODE32_BIT);
}

static __always_inline bool kvm_condition_valid(const struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		return kvm_condition_valid32(vcpu);

	return true;
}

static inline void vcpu_set_thumb(struct kvm_vcpu *vcpu)
{
	*vcpu_cpsr(vcpu) |= PSR_AA32_T_BIT;
}

/*
 * vcpu_get_reg and vcpu_set_reg should always be passed a register number
 * coming from a read of ESR_EL2. Otherwise, it may give the wrong result on
 * AArch32 with banked registers.
 */
static __always_inline unsigned long vcpu_get_reg(const struct kvm_vcpu *vcpu,
					 u8 reg_num)
{
	return (reg_num == 31) ? 0 : vcpu_gp_regs(vcpu)->regs[reg_num];
}

static __always_inline void vcpu_set_reg(struct kvm_vcpu *vcpu, u8 reg_num,
				unsigned long val)
{
	if (reg_num != 31)
		vcpu_gp_regs(vcpu)->regs[reg_num] = val;
}

static inline bool vcpu_is_el2_ctxt(const struct kvm_cpu_context *ctxt)
{
	switch (ctxt->regs.pstate & (PSR_MODE32_BIT | PSR_MODE_MASK)) {
	case PSR_MODE_EL2h:
	case PSR_MODE_EL2t:
		return true;
	default:
		return false;
	}
}

static inline bool vcpu_is_el2(const struct kvm_vcpu *vcpu)
{
	return vcpu_is_el2_ctxt(&vcpu->arch.ctxt);
}

static inline bool vcpu_el2_e2h_is_set(const struct kvm_vcpu *vcpu)
{
	return (!cpus_have_final_cap(ARM64_HAS_HCR_NV1) ||
		(__vcpu_sys_reg(vcpu, HCR_EL2) & HCR_E2H));
}

static inline bool vcpu_el2_tge_is_set(const struct kvm_vcpu *vcpu)
{
	return ctxt_sys_reg(&vcpu->arch.ctxt, HCR_EL2) & HCR_TGE;
}

static inline bool is_hyp_ctxt(const struct kvm_vcpu *vcpu)
{
	bool e2h, tge;
	u64 hcr;

	if (!vcpu_has_nv(vcpu))
		return false;

	hcr = __vcpu_sys_reg(vcpu, HCR_EL2);

	e2h = (hcr & HCR_E2H);
	tge = (hcr & HCR_TGE);

	/*
	 * We are in a hypervisor context if the vcpu mode is EL2 or
	 * E2H and TGE bits are set. The latter means we are in the user space
	 * of the VHE kernel. ARMv8.1 ARM describes this as 'InHost'
	 *
	 * Note that the HCR_EL2.{E2H,TGE}={0,1} isn't really handled in the
	 * rest of the KVM code, and will result in a misbehaving guest.
	 */
	return vcpu_is_el2(vcpu) || (e2h && tge) || tge;
}

static inline bool vcpu_is_host_el0(const struct kvm_vcpu *vcpu)
{
	return is_hyp_ctxt(vcpu) && !vcpu_is_el2(vcpu);
}

static inline bool is_nested_ctxt(struct kvm_vcpu *vcpu)
{
	return vcpu_has_nv(vcpu) && !is_hyp_ctxt(vcpu);
}

/*
 * The layout of SPSR for an AArch32 state is different when observed from an
 * AArch64 SPSR_ELx or an AArch32 SPSR_*. This function generates the AArch32
 * view given an AArch64 view.
 *
 * In ARM DDI 0487E.a see:
 *
 * - The AArch64 view (SPSR_EL2) in section C5.2.18, page C5-426
 * - The AArch32 view (SPSR_abt) in section G8.2.126, page G8-6256
 * - The AArch32 view (SPSR_und) in section G8.2.132, page G8-6280
 *
 * Which show the following differences:
 *
 * | Bit | AA64 | AA32 | Notes                       |
 * +-----+------+------+-----------------------------|
 * | 24  | DIT  | J    | J is RES0 in ARMv8          |
 * | 21  | SS   | DIT  | SS doesn't exist in AArch32 |
 *
 * ... and all other bits are (currently) common.
 */
static inline unsigned long host_spsr_to_spsr32(unsigned long spsr)
{
	const unsigned long overlap = BIT(24) | BIT(21);
	unsigned long dit = !!(spsr & PSR_AA32_DIT_BIT);

	spsr &= ~overlap;

	spsr |= dit << 21;

	return spsr;
}

static inline bool vcpu_mode_priv(const struct kvm_vcpu *vcpu)
{
	u32 mode;

	if (vcpu_mode_is_32bit(vcpu)) {
		mode = *vcpu_cpsr(vcpu) & PSR_AA32_MODE_MASK;
		return mode > PSR_AA32_MODE_USR;
	}

	mode = *vcpu_cpsr(vcpu) & PSR_MODE_MASK;

	return mode != PSR_MODE_EL0t;
}

static __always_inline u64 kvm_vcpu_get_esr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.esr_el2;
}

static inline bool guest_hyp_wfx_traps_enabled(const struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
	bool is_wfe = !!(esr & ESR_ELx_WFx_ISS_WFE);
	u64 hcr_el2 = __vcpu_sys_reg(vcpu, HCR_EL2);

	if (!vcpu_has_nv(vcpu) || vcpu_is_el2(vcpu))
		return false;

	return ((is_wfe && (hcr_el2 & HCR_TWE)) ||
		(!is_wfe && (hcr_el2 & HCR_TWI)));
}

static __always_inline int kvm_vcpu_get_condition(const struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);

	if (esr & ESR_ELx_CV)
		return (esr & ESR_ELx_COND_MASK) >> ESR_ELx_COND_SHIFT;

	return -1;
}

static __always_inline unsigned long kvm_vcpu_get_hfar(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.far_el2;
}

static __always_inline phys_addr_t kvm_vcpu_get_fault_ipa(const struct kvm_vcpu *vcpu)
{
	u64 hpfar = vcpu->arch.fault.hpfar_el2;

	if (unlikely(!(hpfar & HPFAR_EL2_NS)))
		return INVALID_GPA;

	return FIELD_GET(HPFAR_EL2_FIPA, hpfar) << 12;
}

static inline u64 kvm_vcpu_get_disr(const struct kvm_vcpu *vcpu)
{
	return vcpu->arch.fault.disr_el1;
}

static inline u32 kvm_vcpu_hvc_get_imm(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_esr(vcpu) & ESR_ELx_xVC_IMM_MASK;
}

static __always_inline bool kvm_vcpu_dabt_isvalid(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_ISV);
}

static inline unsigned long kvm_vcpu_dabt_iss_nisv_sanitized(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_esr(vcpu) & (ESR_ELx_CM | ESR_ELx_WNR | ESR_ELx_FSC);
}

static inline bool kvm_vcpu_dabt_issext(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_SSE);
}

static inline bool kvm_vcpu_dabt_issf(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_SF);
}

static __always_inline int kvm_vcpu_dabt_get_rd(const struct kvm_vcpu *vcpu)
{
	return (kvm_vcpu_get_esr(vcpu) & ESR_ELx_SRT_MASK) >> ESR_ELx_SRT_SHIFT;
}

static __always_inline bool kvm_vcpu_abt_iss1tw(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_S1PTW);
}

/* Always check for S1PTW *before* using this. */
static __always_inline bool kvm_vcpu_dabt_iswrite(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_esr(vcpu) & ESR_ELx_WNR;
}

static inline bool kvm_vcpu_dabt_is_cm(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_CM);
}

static __always_inline unsigned int kvm_vcpu_dabt_get_as(const struct kvm_vcpu *vcpu)
{
	return 1 << ((kvm_vcpu_get_esr(vcpu) & ESR_ELx_SAS) >> ESR_ELx_SAS_SHIFT);
}

/* This one is not specific to Data Abort */
static __always_inline bool kvm_vcpu_trap_il_is32bit(const struct kvm_vcpu *vcpu)
{
	return !!(kvm_vcpu_get_esr(vcpu) & ESR_ELx_IL);
}

static __always_inline u8 kvm_vcpu_trap_get_class(const struct kvm_vcpu *vcpu)
{
	return ESR_ELx_EC(kvm_vcpu_get_esr(vcpu));
}

static inline bool kvm_vcpu_trap_is_iabt(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_trap_get_class(vcpu) == ESR_ELx_EC_IABT_LOW;
}

static inline bool kvm_vcpu_trap_is_exec_fault(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_trap_is_iabt(vcpu) && !kvm_vcpu_abt_iss1tw(vcpu);
}

static __always_inline u8 kvm_vcpu_trap_get_fault(const struct kvm_vcpu *vcpu)
{
	return kvm_vcpu_get_esr(vcpu) & ESR_ELx_FSC;
}

static inline
bool kvm_vcpu_trap_is_permission_fault(const struct kvm_vcpu *vcpu)
{
	return esr_fsc_is_permission_fault(kvm_vcpu_get_esr(vcpu));
}

static inline
bool kvm_vcpu_trap_is_translation_fault(const struct kvm_vcpu *vcpu)
{
	return esr_fsc_is_translation_fault(kvm_vcpu_get_esr(vcpu));
}

static inline
u64 kvm_vcpu_trap_get_perm_fault_granule(const struct kvm_vcpu *vcpu)
{
	unsigned long esr = kvm_vcpu_get_esr(vcpu);

	BUG_ON(!esr_fsc_is_permission_fault(esr));
	return BIT(ARM64_HW_PGTABLE_LEVEL_SHIFT(esr & ESR_ELx_FSC_LEVEL));
}

static __always_inline bool kvm_vcpu_abt_issea(const struct kvm_vcpu *vcpu)
{
	switch (kvm_vcpu_trap_get_fault(vcpu)) {
	case ESR_ELx_FSC_EXTABT:
	case ESR_ELx_FSC_SEA_TTW(-1) ... ESR_ELx_FSC_SEA_TTW(3):
	case ESR_ELx_FSC_SECC:
	case ESR_ELx_FSC_SECC_TTW(-1) ... ESR_ELx_FSC_SECC_TTW(3):
		return true;
	default:
		return false;
	}
}

static __always_inline int kvm_vcpu_sys_get_rt(struct kvm_vcpu *vcpu)
{
	u64 esr = kvm_vcpu_get_esr(vcpu);
	return ESR_ELx_SYS64_ISS_RT(esr);
}

static inline bool kvm_is_write_fault(struct kvm_vcpu *vcpu)
{
	if (kvm_vcpu_abt_iss1tw(vcpu)) {
		/*
		 * Only a permission fault on a S1PTW should be
		 * considered as a write. Otherwise, page tables baked
		 * in a read-only memslot will result in an exception
		 * being delivered in the guest.
		 *
		 * The drawback is that we end-up faulting twice if the
		 * guest is using any of HW AF/DB: a translation fault
		 * to map the page containing the PT (read only at
		 * first), then a permission fault to allow the flags
		 * to be set.
		 */
		return kvm_vcpu_trap_is_permission_fault(vcpu);
	}

	if (kvm_vcpu_trap_is_iabt(vcpu))
		return false;

	return kvm_vcpu_dabt_iswrite(vcpu);
}

static inline unsigned long kvm_vcpu_get_mpidr_aff(struct kvm_vcpu *vcpu)
{
	return __vcpu_sys_reg(vcpu, MPIDR_EL1) & MPIDR_HWID_BITMASK;
}

static inline void kvm_vcpu_set_be(struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu)) {
		*vcpu_cpsr(vcpu) |= PSR_AA32_E_BIT;
	} else {
		u64 sctlr = vcpu_read_sys_reg(vcpu, SCTLR_EL1);
		sctlr |= SCTLR_ELx_EE;
		vcpu_write_sys_reg(vcpu, sctlr, SCTLR_EL1);
	}
}

static inline bool kvm_vcpu_is_be(struct kvm_vcpu *vcpu)
{
	if (vcpu_mode_is_32bit(vcpu))
		return !!(*vcpu_cpsr(vcpu) & PSR_AA32_E_BIT);

	if (vcpu_mode_priv(vcpu))
		return !!(vcpu_read_sys_reg(vcpu, SCTLR_EL1) & SCTLR_ELx_EE);
	else
		return !!(vcpu_read_sys_reg(vcpu, SCTLR_EL1) & SCTLR_EL1_E0E);
}

static inline unsigned long vcpu_data_guest_to_host(struct kvm_vcpu *vcpu,
						    unsigned long data,
						    unsigned int len)
{
	if (kvm_vcpu_is_be(vcpu)) {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return be16_to_cpu(data & 0xffff);
		case 4:
			return be32_to_cpu(data & 0xffffffff);
		default:
			return be64_to_cpu(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return le16_to_cpu(data & 0xffff);
		case 4:
			return le32_to_cpu(data & 0xffffffff);
		default:
			return le64_to_cpu(data);
		}
	}

	return data;		/* Leave LE untouched */
}

static inline unsigned long vcpu_data_host_to_guest(struct kvm_vcpu *vcpu,
						    unsigned long data,
						    unsigned int len)
{
	if (kvm_vcpu_is_be(vcpu)) {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return cpu_to_be16(data & 0xffff);
		case 4:
			return cpu_to_be32(data & 0xffffffff);
		default:
			return cpu_to_be64(data);
		}
	} else {
		switch (len) {
		case 1:
			return data & 0xff;
		case 2:
			return cpu_to_le16(data & 0xffff);
		case 4:
			return cpu_to_le32(data & 0xffffffff);
		default:
			return cpu_to_le64(data);
		}
	}

	return data;		/* Leave LE untouched */
}

static __always_inline void kvm_incr_pc(struct kvm_vcpu *vcpu)
{
	WARN_ON(vcpu_get_flag(vcpu, PENDING_EXCEPTION));
	vcpu_set_flag(vcpu, INCREMENT_PC);
}

#define kvm_pend_exception(v, e)					\
	do {								\
		WARN_ON(vcpu_get_flag((v), INCREMENT_PC));		\
		vcpu_set_flag((v), PENDING_EXCEPTION);			\
		vcpu_set_flag((v), e);					\
	} while (0)

/*
 * Returns a 'sanitised' view of CPTR_EL2, translating from nVHE to the VHE
 * format if E2H isn't set.
 */
static inline u64 vcpu_sanitised_cptr_el2(const struct kvm_vcpu *vcpu)
{
	u64 cptr = __vcpu_sys_reg(vcpu, CPTR_EL2);

	if (!vcpu_el2_e2h_is_set(vcpu))
		cptr = translate_cptr_el2_to_cpacr_el1(cptr);

	return cptr;
}

static inline bool ____cptr_xen_trap_enabled(const struct kvm_vcpu *vcpu,
					     unsigned int xen)
{
	switch (xen) {
	case 0b00:
	case 0b10:
		return true;
	case 0b01:
		return vcpu_el2_tge_is_set(vcpu) && !vcpu_is_el2(vcpu);
	case 0b11:
	default:
		return false;
	}
}

#define __guest_hyp_cptr_xen_trap_enabled(vcpu, xen)				\
	(!vcpu_has_nv(vcpu) ? false :						\
	 ____cptr_xen_trap_enabled(vcpu,					\
				   SYS_FIELD_GET(CPACR_EL1, xen,		\
						 vcpu_sanitised_cptr_el2(vcpu))))

static inline bool guest_hyp_fpsimd_traps_enabled(const struct kvm_vcpu *vcpu)
{
	return __guest_hyp_cptr_xen_trap_enabled(vcpu, FPEN);
}

static inline bool guest_hyp_sve_traps_enabled(const struct kvm_vcpu *vcpu)
{
	return __guest_hyp_cptr_xen_trap_enabled(vcpu, ZEN);
}

static inline void vcpu_set_hcrx(struct kvm_vcpu *vcpu)
{
	struct kvm *kvm = vcpu->kvm;

	if (cpus_have_final_cap(ARM64_HAS_HCX)) {
		/*
		 * In general, all HCRX_EL2 bits are gated by a feature.
		 * The only reason we can set SMPME without checking any
		 * feature is that its effects are not directly observable
		 * from the guest.
		 */
		vcpu->arch.hcrx_el2 = HCRX_EL2_SMPME;

		if (kvm_has_feat(kvm, ID_AA64ISAR2_EL1, MOPS, IMP))
			vcpu->arch.hcrx_el2 |= (HCRX_EL2_MSCEn | HCRX_EL2_MCE2);

		if (kvm_has_tcr2(kvm))
			vcpu->arch.hcrx_el2 |= HCRX_EL2_TCR2En;

		if (kvm_has_fpmr(kvm))
			vcpu->arch.hcrx_el2 |= HCRX_EL2_EnFPM;
	}
}
#endif /* __ARM64_KVM_EMULATE_H__ */
