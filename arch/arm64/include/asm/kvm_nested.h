/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ARM64_KVM_NESTED_H
#define __ARM64_KVM_NESTED_H

#include <linux/bitfield.h>
#include <linux/kvm_host.h>
#include <asm/kvm_emulate.h>
#include <asm/kvm_pgtable.h>

static inline bool vcpu_has_nv(const struct kvm_vcpu *vcpu)
{
	return (!__is_defined(__KVM_NVHE_HYPERVISOR__) &&
		cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) &&
		vcpu_has_feature(vcpu, KVM_ARM_VCPU_HAS_EL2));
}

/* Translation helpers from non-VHE EL2 to EL1 */
static inline u64 tcr_el2_ps_to_tcr_el1_ips(u64 tcr_el2)
{
	return (u64)FIELD_GET(TCR_EL2_PS_MASK, tcr_el2) << TCR_IPS_SHIFT;
}

static inline u64 translate_tcr_el2_to_tcr_el1(u64 tcr)
{
	return TCR_EPD1_MASK |				/* disable TTBR1_EL1 */
	       ((tcr & TCR_EL2_TBI) ? TCR_TBI0 : 0) |
	       tcr_el2_ps_to_tcr_el1_ips(tcr) |
	       (tcr & TCR_EL2_TG0_MASK) |
	       (tcr & TCR_EL2_ORGN0_MASK) |
	       (tcr & TCR_EL2_IRGN0_MASK) |
	       (tcr & TCR_EL2_T0SZ_MASK);
}

static inline u64 translate_cptr_el2_to_cpacr_el1(u64 cptr_el2)
{
	u64 cpacr_el1 = CPACR_EL1_RES1;

	if (cptr_el2 & CPTR_EL2_TTA)
		cpacr_el1 |= CPACR_EL1_TTA;
	if (!(cptr_el2 & CPTR_EL2_TFP))
		cpacr_el1 |= CPACR_EL1_FPEN;
	if (!(cptr_el2 & CPTR_EL2_TZ))
		cpacr_el1 |= CPACR_EL1_ZEN;

	cpacr_el1 |= cptr_el2 & (CPTR_EL2_TCPAC | CPTR_EL2_TAM);

	return cpacr_el1;
}

static inline u64 translate_sctlr_el2_to_sctlr_el1(u64 val)
{
	/* Only preserve the minimal set of bits we support */
	val &= (SCTLR_ELx_M | SCTLR_ELx_A | SCTLR_ELx_C | SCTLR_ELx_SA |
		SCTLR_ELx_I | SCTLR_ELx_IESB | SCTLR_ELx_WXN | SCTLR_ELx_EE);
	val |= SCTLR_EL1_RES1;

	return val;
}

static inline u64 translate_ttbr0_el2_to_ttbr0_el1(u64 ttbr0)
{
	/* Clear the ASID field */
	return ttbr0 & ~GENMASK_ULL(63, 48);
}

extern bool forward_smc_trap(struct kvm_vcpu *vcpu);
extern bool forward_debug_exception(struct kvm_vcpu *vcpu);
extern void kvm_init_nested(struct kvm *kvm);
extern int kvm_vcpu_init_nested(struct kvm_vcpu *vcpu);
extern void kvm_init_nested_s2_mmu(struct kvm_s2_mmu *mmu);
extern struct kvm_s2_mmu *lookup_s2_mmu(struct kvm_vcpu *vcpu);

union tlbi_info;

extern void kvm_s2_mmu_iterate_by_vmid(struct kvm *kvm, u16 vmid,
				       const union tlbi_info *info,
				       void (*)(struct kvm_s2_mmu *,
						const union tlbi_info *));
extern void kvm_vcpu_load_hw_mmu(struct kvm_vcpu *vcpu);
extern void kvm_vcpu_put_hw_mmu(struct kvm_vcpu *vcpu);

extern void check_nested_vcpu_requests(struct kvm_vcpu *vcpu);
extern void kvm_nested_flush_hwstate(struct kvm_vcpu *vcpu);
extern void kvm_nested_sync_hwstate(struct kvm_vcpu *vcpu);

struct kvm_s2_trans {
	phys_addr_t output;
	unsigned long block_size;
	bool writable;
	bool readable;
	int level;
	u32 esr;
	u64 desc;
};

static inline phys_addr_t kvm_s2_trans_output(struct kvm_s2_trans *trans)
{
	return trans->output;
}

static inline unsigned long kvm_s2_trans_size(struct kvm_s2_trans *trans)
{
	return trans->block_size;
}

static inline u32 kvm_s2_trans_esr(struct kvm_s2_trans *trans)
{
	return trans->esr;
}

static inline bool kvm_s2_trans_readable(struct kvm_s2_trans *trans)
{
	return trans->readable;
}

static inline bool kvm_s2_trans_writable(struct kvm_s2_trans *trans)
{
	return trans->writable;
}

static inline bool kvm_s2_trans_executable(struct kvm_s2_trans *trans)
{
	return !(trans->desc & BIT(54));
}

extern int kvm_walk_nested_s2(struct kvm_vcpu *vcpu, phys_addr_t gipa,
			      struct kvm_s2_trans *result);
extern int kvm_s2_handle_perm_fault(struct kvm_vcpu *vcpu,
				    struct kvm_s2_trans *trans);
extern int kvm_inject_s2_fault(struct kvm_vcpu *vcpu, u64 esr_el2);
extern void kvm_nested_s2_wp(struct kvm *kvm);
extern void kvm_nested_s2_unmap(struct kvm *kvm, bool may_block);
extern void kvm_nested_s2_flush(struct kvm *kvm);

unsigned long compute_tlb_inval_range(struct kvm_s2_mmu *mmu, u64 val);

static inline bool kvm_supported_tlbi_s1e1_op(struct kvm_vcpu *vpcu, u32 instr)
{
	struct kvm *kvm = vpcu->kvm;
	u8 CRm = sys_reg_CRm(instr);

	if (!(sys_reg_Op0(instr) == TLBI_Op0 &&
	      sys_reg_Op1(instr) == TLBI_Op1_EL1))
		return false;

	if (!(sys_reg_CRn(instr) == TLBI_CRn_XS ||
	      (sys_reg_CRn(instr) == TLBI_CRn_nXS &&
	       kvm_has_feat(kvm, ID_AA64ISAR1_EL1, XS, IMP))))
		return false;

	if (CRm == TLBI_CRm_nROS &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		return false;

	if ((CRm == TLBI_CRm_RIS || CRm == TLBI_CRm_ROS ||
	     CRm == TLBI_CRm_RNS) &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		return false;

	return true;
}

static inline bool kvm_supported_tlbi_s1e2_op(struct kvm_vcpu *vpcu, u32 instr)
{
	struct kvm *kvm = vpcu->kvm;
	u8 CRm = sys_reg_CRm(instr);

	if (!(sys_reg_Op0(instr) == TLBI_Op0 &&
	      sys_reg_Op1(instr) == TLBI_Op1_EL2))
		return false;

	if (!(sys_reg_CRn(instr) == TLBI_CRn_XS ||
	      (sys_reg_CRn(instr) == TLBI_CRn_nXS &&
	       kvm_has_feat(kvm, ID_AA64ISAR1_EL1, XS, IMP))))
		return false;

	if (CRm == TLBI_CRm_IPAIS || CRm == TLBI_CRm_IPAONS)
		return false;

	if (CRm == TLBI_CRm_nROS &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, OS))
		return false;

	if ((CRm == TLBI_CRm_RIS || CRm == TLBI_CRm_ROS ||
	     CRm == TLBI_CRm_RNS) &&
	    !kvm_has_feat(kvm, ID_AA64ISAR0_EL1, TLB, RANGE))
		return false;

	return true;
}

int kvm_init_nv_sysregs(struct kvm_vcpu *vcpu);
u64 limit_nv_id_reg(struct kvm *kvm, u32 reg, u64 val);

#ifdef CONFIG_ARM64_PTR_AUTH
bool kvm_auth_eretax(struct kvm_vcpu *vcpu, u64 *elr);
#else
static inline bool kvm_auth_eretax(struct kvm_vcpu *vcpu, u64 *elr)
{
	/* We really should never execute this... */
	WARN_ON_ONCE(1);
	*elr = 0xbad9acc0debadbad;
	return false;
}
#endif

#define KVM_NV_GUEST_MAP_SZ	(KVM_PGTABLE_PROT_SW1 | KVM_PGTABLE_PROT_SW0)

static inline u64 kvm_encode_nested_level(struct kvm_s2_trans *trans)
{
	return FIELD_PREP(KVM_NV_GUEST_MAP_SZ, trans->level);
}

/* Adjust alignment for the contiguous bit as per StageOA() */
#define contiguous_bit_shift(d, wi, l)					\
	({								\
		u8 shift = 0;						\
									\
		if ((d) & PTE_CONT) {					\
			switch (BIT((wi)->pgshift)) {			\
			case SZ_4K:					\
				shift = 4;				\
				break;					\
			case SZ_16K:					\
				shift = (l) == 2 ? 5 : 7;		\
				break;					\
			case SZ_64K:					\
				shift = 5;				\
				break;					\
			}						\
		}							\
									\
		shift;							\
	})

static inline u64 decode_range_tlbi(u64 val, u64 *range, u16 *asid)
{
	u64 base, tg, num, scale;
	int shift;

	tg	= FIELD_GET(GENMASK(47, 46), val);

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

	base	= (val & GENMASK(36, 0)) << shift;

	if (asid)
		*asid = FIELD_GET(TLBIR_ASID_MASK, val);

	scale	= FIELD_GET(GENMASK(45, 44), val);
	num	= FIELD_GET(GENMASK(43, 39), val);
	*range	= __TLBI_RANGE_PAGES(num, scale) << shift;

	return base;
}

static inline unsigned int ps_to_output_size(unsigned int ps)
{
	switch (ps) {
	case 0: return 32;
	case 1: return 36;
	case 2: return 40;
	case 3: return 42;
	case 4: return 44;
	case 5:
	default:
		return 48;
	}
}

enum trans_regime {
	TR_EL10,
	TR_EL20,
	TR_EL2,
};

struct s1_walk_info {
	u64	     		baddr;
	enum trans_regime	regime;
	unsigned int		max_oa_bits;
	unsigned int		pgshift;
	unsigned int		txsz;
	int 	     		sl;
	bool			as_el0;
	bool	     		hpd;
	bool			e0poe;
	bool			poe;
	bool			pan;
	bool	     		be;
	bool	     		s2;
};

struct s1_walk_result {
	union {
		struct {
			u64	desc;
			u64	pa;
			s8	level;
			u8	APTable;
			bool	nG;
			u16	asid;
			bool	UXNTable;
			bool	PXNTable;
			bool	uwxn;
			bool	uov;
			bool	ur;
			bool	uw;
			bool	ux;
			bool	pwxn;
			bool	pov;
			bool	pr;
			bool	pw;
			bool	px;
		};
		struct {
			u8	fst;
			bool	ptw;
			bool	s2;
		};
	};
	bool	failed;
};

int __kvm_translate_va(struct kvm_vcpu *vcpu, struct s1_walk_info *wi,
		       struct s1_walk_result *wr, u64 va);

/* VNCR management */
int kvm_vcpu_allocate_vncr_tlb(struct kvm_vcpu *vcpu);
int kvm_handle_vncr_abort(struct kvm_vcpu *vcpu);
void kvm_handle_s1e2_tlbi(struct kvm_vcpu *vcpu, u32 inst, u64 val);

#define vncr_fixmap(c)						\
	({							\
		u32 __c = (c);					\
		BUG_ON(__c >= NR_CPUS);				\
		(FIX_VNCR - __c);				\
	})

#endif /* __ARM64_KVM_NESTED_H */
