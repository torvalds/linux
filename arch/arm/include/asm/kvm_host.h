/*
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef __ARM_KVM_HOST_H__
#define __ARM_KVM_HOST_H__

#include <linux/types.h>
#include <linux/kvm_types.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>
#include <asm/fpstate.h>
#include <kvm/arm_arch_timer.h>

#if defined(CONFIG_KVM_ARM_MAX_VCPUS)
#define KVM_MAX_VCPUS CONFIG_KVM_ARM_MAX_VCPUS
#else
#define KVM_MAX_VCPUS 0
#endif

#define KVM_USER_MEM_SLOTS 32
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_COALESCED_MMIO_PAGE_OFFSET 1
#define KVM_HAVE_ONE_REG

#define KVM_VCPU_MAX_FEATURES 2

#include <kvm/arm_vgic.h>

u32 *kvm_vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num, u32 mode);
int __attribute_const__ kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
void kvm_reset_coprocs(struct kvm_vcpu *vcpu);

struct kvm_arch {
	/* VTTBR value associated with below pgd and vmid */
	u64    vttbr;

	/* Timer */
	struct arch_timer_kvm	timer;

	/*
	 * Anything that is not used directly from assembly code goes
	 * here.
	 */

	/* The VMID generation used for the virt. memory system */
	u64    vmid_gen;
	u32    vmid;

	/* Stage-2 page table */
	pgd_t *pgd;

	/* Interrupt controller */
	struct vgic_dist	vgic;
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
	u32 hsr;		/* Hyp Syndrome Register */
	u32 hxfar;		/* Hyp Data/Inst. Fault Address Register */
	u32 hpfar;		/* Hyp IPA Fault Address Register */
	u32 hyp_pc;		/* PC when exception was taken from Hyp mode */
};

typedef struct vfp_hard_struct kvm_cpu_context_t;

struct kvm_vcpu_arch {
	struct kvm_regs regs;

	int target; /* Processor target */
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* System control coprocessor (cp15) */
	u32 cp15[NR_CP15_REGS];

	/* The CPU type we expose to the VM */
	u32 midr;

	/* HYP trapping configuration */
	u32 hcr;

	/* Interrupt related fields */
	u32 irq_lines;		/* IRQ and FIQ levels */

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Floating point registers (VFP and Advanced SIMD/NEON) */
	struct vfp_hard_struct vfp_guest;

	/* Host FP context */
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

	/* Don't run the guest on this vcpu */
	bool pause;

	/* IO related fields */
	struct kvm_decode mmio_decode;

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* Detect first run of a vcpu */
	bool has_run_once;
};

struct kvm_vm_stat {
	u32 remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u32 halt_wakeup;
};

int kvm_vcpu_preferred_target(struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
u64 kvm_call_hyp(void *hypfn, ...);
void force_vm_exit(const cpumask_t *mask);

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);

unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);

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

static inline void kvm_arch_mmu_notifier_invalidate_page(struct kvm *kvm,
							 unsigned long address)
{
}

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu __percpu **kvm_get_running_vcpus(void);

int kvm_arm_copy_coproc_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
unsigned long kvm_arm_num_coproc_regs(struct kvm_vcpu *vcpu);
int kvm_arm_coproc_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
int kvm_arm_coproc_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);

static inline void __cpu_init_hyp_mode(phys_addr_t boot_pgd_ptr,
				       phys_addr_t pgd_ptr,
				       unsigned long hyp_stack_ptr,
				       unsigned long vector_ptr)
{
	/*
	 * Call initialization code, and switch to the full blown HYP
	 * code. The init code doesn't need to preserve these
	 * registers as r0-r3 are already callee saved according to
	 * the AAPCS.
	 * Note that we slightly misuse the prototype by casing the
	 * stack pointer to a void *.
	 *
	 * We don't have enough registers to perform the full init in
	 * one go.  Install the boot PGD first, and then install the
	 * runtime PGD, stack pointer and vectors. The PGDs are always
	 * passed as the third argument, in order to be passed into
	 * r2-r3 to the init code (yes, this is compliant with the
	 * PCS!).
	 */

	kvm_call_hyp(NULL, 0, boot_pgd_ptr);

	kvm_call_hyp((void*)hyp_stack_ptr, vector_ptr, pgd_ptr);
}

static inline int kvm_arch_dev_ioctl_check_extension(long ext)
{
	return 0;
}

static inline void vgic_arch_setup(const struct vgic_params *vgic)
{
	BUG_ON(vgic->type != VGIC_V2);
}

int kvm_perf_init(void);
int kvm_perf_teardown(void);

static inline void kvm_arch_hardware_disable(void) {}
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}

#endif /* __ARM_KVM_HOST_H__ */
