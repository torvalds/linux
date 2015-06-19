#ifndef __KVM_X86_PMU_H
#define __KVM_X86_PMU_H

#define vcpu_to_pmu(vcpu) (&(vcpu)->arch.pmu)
#define pmu_to_vcpu(pmu)  (container_of((pmu), struct kvm_vcpu, arch.pmu))
#define pmc_to_pmu(pmc)   (&(pmc)->vcpu->arch.pmu)

struct kvm_event_hw_type_mapping {
	u8 eventsel;
	u8 unit_mask;
	unsigned event_type;
};

void kvm_pmu_deliver_pmi(struct kvm_vcpu *vcpu);
void kvm_pmu_handle_event(struct kvm_vcpu *vcpu);
int kvm_pmu_rdpmc(struct kvm_vcpu *vcpu, unsigned pmc, u64 *data);
int kvm_pmu_is_valid_msr_idx(struct kvm_vcpu *vcpu, unsigned idx);
bool kvm_pmu_is_valid_msr(struct kvm_vcpu *vcpu, u32 msr);
int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *data);
int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr_info);
void kvm_pmu_refresh(struct kvm_vcpu *vcpu);
void kvm_pmu_reset(struct kvm_vcpu *vcpu);
void kvm_pmu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_destroy(struct kvm_vcpu *vcpu);

#endif /* __KVM_X86_PMU_H */
