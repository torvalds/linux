/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/include/asm/kvm_host.h:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
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

#ifndef __ARM64_KVM_HOST_H__
#define __ARM64_KVM_HOST_H__

#include <linux/types.h>
#include <linux/kvm_types.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>

#if defined(CONFIG_KVM_ARM_MAX_VCPUS)
#define KVM_MAX_VCPUS CONFIG_KVM_ARM_MAX_VCPUS
#else
#define KVM_MAX_VCPUS 0
#endif

#define KVM_USER_MEM_SLOTS 32
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_COALESCED_MMIO_PAGE_OFFSET 1

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#define KVM_VCPU_MAX_FEATURES 3

int kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
int kvm_arch_dev_ioctl_check_extension(long ext);

struct kvm_arch {
	/* The VMID generation used for the virt. memory system */
	u64    vmid_gen;
	u32    vmid;

	/* 1-level 2nd stage table and lock */
	spinlock_t pgd_lock;
	pgd_t *pgd;

	/* VTTBR value associated with above pgd and vmid */
	u64    vttbr;

	/* Interrupt controller */
	struct vgic_dist	vgic;

	/* Timer */
	struct arch_timer_kvm	timer;
};

#define KVM_NR_MEM_OBJS     40

/*
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

struct kvm_vcpu_fault_info {
	u32 esr_el2;		/* Hyp Syndrom Register */
	u64 far_el2;		/* Hyp Fault Address Register */
	u64 hpfar_el2;		/* Hyp IPA Fault Address Register */
};

struct kvm_cpu_context {
	struct kvm_regs	gp_regs;
	union {
		u64 sys_regs[NR_SYS_REGS];
		u32 copro[NR_COPRO_REGS];
	};
};

typedef struct kvm_cpu_context kvm_cpu_context_t;

struct kvm_vcpu_arch {
	struct kvm_cpu_context ctxt;

	/* HYP configuration */
	u64 hcr_el2;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Debug state */
	u64 debug_flags;

	/* Pointer to host CPU context */
	kvm_cpu_context_t *host_cpu_context;

	/* VGIC state */
	struct vgic_cpu vgic_cpu;
	struct arch_timer_cpu timer_cpu;

	/*
	 * Anything that is not used directly from assembly code goes
	 * here.
	 */
	/* dcache set/way operation pending */
	int last_pcpu;
	cpumask_t require_dcache_flush;

	/* Don't run the guest */
	bool pause;

	/* IO related fields */
	struct kvm_decode mmio_decode;

	/* Interrupt related fields */
	u64 irq_lines;		/* IRQ and FIQ levels */

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* Target CPU and feature flags */
	int target;
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* Detect first run of a vcpu */
	bool has_run_once;
};

#define vcpu_gp_regs(v)		(&(v)->arch.ctxt.gp_regs)
#define vcpu_sys_reg(v,r)	((v)->arch.ctxt.sys_regs[(r)])
/*
 * CP14 and CP15 live in the same array, as they are backed by the
 * same system registers.
 */
#define vcpu_cp14(v,r)		((v)->arch.ctxt.copro[(r)])
#define vcpu_cp15(v,r)		((v)->arch.ctxt.copro[(r)])

#ifdef CONFIG_CPU_BIG_ENDIAN
#define vcpu_cp15_64_high(v,r)	vcpu_cp15((v),(r))
#define vcpu_cp15_64_low(v,r)	vcpu_cp15((v),(r) + 1)
#else
#define vcpu_cp15_64_high(v,r)	vcpu_cp15((v),(r) + 1)
#define vcpu_cp15_64_low(v,r)	vcpu_cp15((v),(r))
#endif

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u32 halt_wakeup;
};

int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			const struct kvm_vcpu_init *init);
int kvm_vcpu_preferred_target(struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);

/* We do not have shadow page tables, hence the empty hooks */
static inline int kvm_age_hva(struct kvm *kvm, unsigned long start,
			      unsigned long end)
{
	return 0;
}

static inline int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return 0;
}

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu __percpu **kvm_get_running_vcpus(void);

u64 kvm_call_hyp(void *hypfn, ...);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);

int kvm_perf_init(void);
int kvm_perf_teardown(void);

static inline void __cpu_init_hyp_mode(phys_addr_t boot_pgd_ptr,
				       phys_addr_t pgd_ptr,
				       unsigned long hyp_stack_ptr,
				       unsigned long vector_ptr)
{
	/*
	 * Call initialization code, and switch to the full blown
	 * HYP code.
	 */
	kvm_call_hyp((void *)boot_pgd_ptr, pgd_ptr,
		     hyp_stack_ptr, vector_ptr);
}

struct vgic_sr_vectors {
	void	*save_vgic;
	void	*restore_vgic;
};

static inline void vgic_arch_setup(const struct vgic_params *vgic)
{
	extern struct vgic_sr_vectors __vgic_sr_vectors;

	switch(vgic->type)
	{
	case VGIC_V2:
		__vgic_sr_vectors.save_vgic	= __save_vgic_v2_state;
		__vgic_sr_vectors.restore_vgic	= __restore_vgic_v2_state;
		break;

#ifdef CONFIG_ARM_GIC_V3
	case VGIC_V3:
		__vgic_sr_vectors.save_vgic	= __save_vgic_v3_state;
		__vgic_sr_vectors.restore_vgic	= __restore_vgic_v3_state;
		break;
#endif

	default:
		BUG();
	}
}

static inline void kvm_arch_hardware_disable(void) {}
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}

#endif /* __ARM64_KVM_HOST_H__ */
