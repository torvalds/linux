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

#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>
#include <asm/fpstate.h>
#include <asm/kvm_arch_timer.h>

#define KVM_MAX_VCPUS CONFIG_KVM_ARM_MAX_VCPUS
#define KVM_USER_MEM_SLOTS 32
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_COALESCED_MMIO_PAGE_OFFSET 1
#define KVM_HAVE_ONE_REG

#define KVM_VCPU_MAX_FEATURES 1

/* We don't currently support large pages. */
#define KVM_HPAGE_GFN_SHIFT(x)	0
#define KVM_NR_PAGE_SIZES	1
#define KVM_PAGES_PER_HPAGE(x)	(1UL<<31)

#include <asm/kvm_vgic.h>

struct kvm_vcpu;
u32 *kvm_vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num, u32 mode);
int kvm_target_cpu(void);
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

struct kvm_vcpu_arch {
	struct kvm_regs regs;

	int target; /* Processor target */
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* System control coprocessor (cp15) */
	u32 cp15[NR_CP15_REGS];

	/* The CPU type we expose to the VM */
	u32 midr;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Floating point registers (VFP and Advanced SIMD/NEON) */
	struct vfp_hard_struct vfp_guest;
	struct vfp_hard_struct *vfp_host;

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

	/* Interrupt related fields */
	u32 irq_lines;		/* IRQ and FIQ levels */

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

struct kvm_vcpu_init;
int kvm_vcpu_set_target(struct kvm_vcpu *vcpu,
			const struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
struct kvm_one_reg;
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
u64 kvm_call_hyp(void *hypfn, ...);
void force_vm_exit(const cpumask_t *mask);

#define KVM_ARCH_WANT_MMU_NOTIFIER
struct kvm;
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);

unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);

/* We do not have shadow page tables, hence the empty hooks */
static inline int kvm_age_hva(struct kvm *kvm, unsigned long hva)
{
	return 0;
}

static inline int kvm_test_age_hva(struct kvm *kvm, unsigned long hva)
{
	return 0;
}

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu __percpu **kvm_get_running_vcpus(void);

int kvm_arm_copy_coproc_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
unsigned long kvm_arm_num_coproc_regs(struct kvm_vcpu *vcpu);
struct kvm_one_reg;
int kvm_arm_coproc_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
int kvm_arm_coproc_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);

#endif /* __ARM_KVM_HOST_H__ */
