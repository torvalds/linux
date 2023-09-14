// SPDX-License-Identifier: GPL-2.0-only

/*
 * Privileged (non-hypervisor) host registers to save.
 */
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
}

static inline u64 __kvmppc_get_msr_hv(struct kvm_vcpu *vcpu)
{
	return vcpu->arch.shregs.msr;
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_SET(reg, size)			\
static inline void kvmppc_set_##reg ##_hv(struct kvm_vcpu *vcpu, u##size val)	\
{									\
	vcpu->arch.reg = val;						\
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_GET(reg, size)			\
static inline u##size kvmppc_get_##reg ##_hv(struct kvm_vcpu *vcpu)	\
{									\
	return vcpu->arch.reg;						\
}

#define KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(reg, size)			\
	KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_SET(reg, size)			\
	KVMPPC_BOOK3S_HV_VCPU_ACCESSOR_GET(reg, size)			\

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_SET(reg, size)		\
static inline void kvmppc_set_##reg ##_hv(struct kvm_vcpu *vcpu, int i, u##size val)	\
{									\
	vcpu->arch.reg[i] = val;					\
}

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_GET(reg, size)		\
static inline u##size kvmppc_get_##reg ##_hv(struct kvm_vcpu *vcpu, int i)	\
{									\
	return vcpu->arch.reg[i];					\
}

#define KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(reg, size)			\
	KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_SET(reg, size)		\
	KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR_GET(reg, size)		\

KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(mmcra, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(hfscr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(fscr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dscr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(purr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(spurr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(amr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(uamor, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(siar, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(sdar, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(iamr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawr0, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawr1, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawrx0, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(dawrx1, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ciabr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(wort, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ppr, 64)
KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(ctrl, 64)

KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(mmcr, 64)
KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(sier, 64)
KVMPPC_BOOK3S_HV_VCPU_ARRAY_ACCESSOR(pmc, 32)

KVMPPC_BOOK3S_HV_VCPU_ACCESSOR(pspb, 32)
