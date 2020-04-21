/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 */

#ifndef __POWERPC_KVM_EXITTIMING_H__
#define __POWERPC_KVM_EXITTIMING_H__

#include <linux/kvm_host.h>

#ifdef CONFIG_KVM_EXIT_TIMING
void kvmppc_init_timing_stats(struct kvm_vcpu *vcpu);
void kvmppc_update_timing_stats(struct kvm_vcpu *vcpu);
void kvmppc_create_vcpu_debugfs(struct kvm_vcpu *vcpu, unsigned int id);
void kvmppc_remove_vcpu_debugfs(struct kvm_vcpu *vcpu);

static inline void kvmppc_set_exit_type(struct kvm_vcpu *vcpu, int type)
{
	vcpu->arch.last_exit_type = type;
}

#else
/* if exit timing is not configured there is no need to build the c file */
static inline void kvmppc_init_timing_stats(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_update_timing_stats(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_create_vcpu_debugfs(struct kvm_vcpu *vcpu,
						unsigned int id) {}
static inline void kvmppc_remove_vcpu_debugfs(struct kvm_vcpu *vcpu) {}
static inline void kvmppc_set_exit_type(struct kvm_vcpu *vcpu, int type) {}
#endif /* CONFIG_KVM_EXIT_TIMING */

/* account the exit in kvm_stats */
static inline void kvmppc_account_exit_stat(struct kvm_vcpu *vcpu, int type)
{
	/* type has to be known at build time for optimization */

	/* The BUILD_BUG_ON below breaks in funny ways, commented out
	 * for now ... -BenH
	BUILD_BUG_ON(!__builtin_constant_p(type));
	*/
	switch (type) {
	case EXT_INTR_EXITS:
		vcpu->stat.ext_intr_exits++;
		break;
	case DEC_EXITS:
		vcpu->stat.dec_exits++;
		break;
	case EMULATED_INST_EXITS:
		vcpu->stat.emulated_inst_exits++;
		break;
	case DSI_EXITS:
		vcpu->stat.dsi_exits++;
		break;
	case ISI_EXITS:
		vcpu->stat.isi_exits++;
		break;
	case SYSCALL_EXITS:
		vcpu->stat.syscall_exits++;
		break;
	case DTLB_REAL_MISS_EXITS:
		vcpu->stat.dtlb_real_miss_exits++;
		break;
	case DTLB_VIRT_MISS_EXITS:
		vcpu->stat.dtlb_virt_miss_exits++;
		break;
	case MMIO_EXITS:
		vcpu->stat.mmio_exits++;
		break;
	case ITLB_REAL_MISS_EXITS:
		vcpu->stat.itlb_real_miss_exits++;
		break;
	case ITLB_VIRT_MISS_EXITS:
		vcpu->stat.itlb_virt_miss_exits++;
		break;
	case SIGNAL_EXITS:
		vcpu->stat.signal_exits++;
		break;
	case DBELL_EXITS:
		vcpu->stat.dbell_exits++;
		break;
	case GDBELL_EXITS:
		vcpu->stat.gdbell_exits++;
		break;
	}
}

/* wrapper to set exit time and account for it in kvm_stats */
static inline void kvmppc_account_exit(struct kvm_vcpu *vcpu, int type)
{
	kvmppc_set_exit_type(vcpu, type);
	kvmppc_account_exit_stat(vcpu, type);
}

#endif /* __POWERPC_KVM_EXITTIMING_H__ */
