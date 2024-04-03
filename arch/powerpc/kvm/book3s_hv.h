// SPDX-License-Identifier: GPL-2.0-only

/*
 * Privileged (non-hypervisor) host registers to save.
 */
#include "asm/guest-state-buffer.h"

struct p9_host_os_sprs {
	unsigned long iamr;
	unsigned long amr;

	unsigned int pmc1;
	unsigned int pmc2;
	unsigned int pmc3;
	unsigned int pmc4;
	unsigned int pmc5;
	unsigned int pmc6;
	unsigned long mmcr0;
	unsigned long mmcr1;
	unsigned long mmcr2;
	unsigned long mmcr3;
	unsigned long mmcra;
	unsigned long siar;
	unsigned long sier1;
	unsigned long sier2;
	unsigned long sier3;
	unsigned long sdar;
};

static inline bool nesting_enabled(struct kvm *kvm)
{
	return kvm->arch.nested_enable && kvm_is_radix(kvm);
}

bool load_vcpu_state(struct kvm_vcpu *vcpu,
			   struct p9_host_os_sprs *host_os_sprs);
void store_vcpu_state(struct kvm_vcpu *vcpu);
void save_p9_host_os_sprs(struct p9_host_os_sprs *host_os_sprs);
void restore_p9_host_os_sprs(struct kvm_vcpu *vcpu,
				    struct p9_host_os_sprs *host_os_sprs);
void switch_pmu_to_guest(struct kvm_vcpu *vcpu,
			    struct p9_host_os_sprs *host_os_sprs);
void switch_pmu_to_host(struct kvm_vcpu *vcpu,
			    struct p9_host_os_sprs *host_os_sprs);

#ifdef CONFIG_KVM_BOOK3S_HV_P9_TIMING
void accumulate_time(struct kvm_vcpu *vcpu, struct kvmhv_tb_accumulator *next);
#define start_timing(vcpu, next) accumulate_time(vcpu, next)
#define end_timing(vcpu) accumulate_time(vcpu, NULL)
#else
#define accumulate_time(vcpu, next) do {} while (0)
#define start_timing(vcpu, next) do {} while (0)
#define end_timing(vcpu) do {} while (0)
#endif

static inline void __kvmppc_set_msr_hv(struct kvm_vcpu *vcpu, u64 val)
{
	vcpu->arch.shregs.msr = val;
	kvmhv_nestedv2_mark_dirty(vcpu, KVMPPC_GSID_MSR);
}

static inline u64 __kvmppc_get_msr_hv(struct kvm_vcpu *vcpu)
{
	WARN_ON(kvmhv_nestedv2_cached_reload(vcpu, KVMPPC_GSID_MSR) < 0);
	return vcpu->arch.shregs.msr;
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_SET(reg, size, iden)		\
static inline void kvmppc_set_##reg ##_hv(struct kvm_vcpu *vcpu, u##size val)	\
{									\
	vcpu->arch.reg = val;						\
	kvmhv_nestedv2_mark_dirty(vcpu, iden);				\
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_GET(reg, size, iden)		\
static inline u##size kvmppc_get_##reg ##_hv(struct kvm_vcpu *vcpu)	\
{									\
	kvmhv_nestedv2_cached_reload(vcpu, iden);			\
	return vcpu->arch.reg;						\
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(reg, size, iden)			\
	KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_SET(reg, size, iden)		\
	KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_GET(reg, size, iden)		\

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_SET(reg, size, iden)	\
static inline void kvmppc_set_##reg ##_hv(struct kvm_vcpu *vcpu, int i, u##size val)	\
{									\
	vcpu->arch.reg[i] = val;					\
	kvmhv_nestedv2_mark_dirty(vcpu, iden(i));			\
}

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_GET(reg, size, iden)	\
static inline u##size kvmppc_get_##reg ##_hv(struct kvm_vcpu *vcpu, int i)	\
{									\
	WARN_ON(kvmhv_nestedv2_cached_reload(vcpu, iden(i)) < 0);	\
	return vcpu->arch.reg[i];					\
}

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(reg, size, iden)		\
	KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_SET(reg, size, iden)	\
	KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_GET(reg, size, iden)	\

KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(mmcra, 64, KVMPPC_GSID_MMCRA)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(hfscr, 64, KVMPPC_GSID_HFSCR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(fscr, 64, KVMPPC_GSID_FSCR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dscr, 64, KVMPPC_GSID_DSCR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(purr, 64, KVMPPC_GSID_PURR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(spurr, 64, KVMPPC_GSID_SPURR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(amr, 64, KVMPPC_GSID_AMR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(uamor, 64, KVMPPC_GSID_UAMOR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(siar, 64, KVMPPC_GSID_SIAR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(sdar, 64, KVMPPC_GSID_SDAR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(iamr, 64, KVMPPC_GSID_IAMR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawr0, 64, KVMPPC_GSID_DAWR0)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawr1, 64, KVMPPC_GSID_DAWR1)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawrx0, 64, KVMPPC_GSID_DAWRX0)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawrx1, 64, KVMPPC_GSID_DAWRX1)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ciabr, 64, KVMPPC_GSID_CIABR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(wort, 64, KVMPPC_GSID_WORT)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ppr, 64, KVMPPC_GSID_PPR)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ctrl, 64, KVMPPC_GSID_CTRL);

KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(mmcr, 64, KVMPPC_GSID_MMCR)
KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(sier, 64, KVMPPC_GSID_SIER)
KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(pmc, 32, KVMPPC_GSID_PMC)

KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(pspb, 32, KVMPPC_GSID_PSPB)
