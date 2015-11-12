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

#define __KVM_HAVE_ARCH_INTC_INITIALIZED

#define KVM_USER_MEM_SLOTS 32
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_COALESCED_MMIO_PAGE_OFFSET 1
#define KVM_HALT_POLL_NS_DEFAULT 500000

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>

#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS

#define KVM_VCPU_MAX_FEATURES 3

int __attribute_const__ kvm_target_cpu(void);
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

	/* The maximum number of vCPUs depends on the used GIC model */
	int max_vcpus;

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
	u32 mdcr_el2;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Guest debug state */
	u64 debug_flags;

	/*
	 * We maintain more than a single set of debug registers to support
	 * debugging the guest from the host and to maintain separate host and
	 * guest state during world switches. vcpu_debug_state are the debug
	 * registers of the vcpu as the guest sees them.  host_debug_state are
	 * the host registers which are saved and restored during
	 * world switches. external_debug_state contains the debug
	 * values we want to debug the guest. This is set via the
	 * KVM_SET_GUEST_DEBUG ioctl.
	 *
	 * debug_ptr points to the set of debug registers that should be loaded
	 * onto the hardware when running the guest.
	 */
	struct kvm_guest_debug_arch *debug_ptr;
	struct kvm_guest_debug_arch vcpu_debug_state;
	struct kvm_guest_debug_arch external_debug_state;

	/* Pointer to host CPU context */
	kvm_cpu_context_t *host_cpu_context;
	struct kvm_guest_debug_arch host_debug_state;

	/* VGIC state */
	struct vgic_cpu vgic_cpu;
	struct arch_timer_cpu timer_cpu;

	/*
	 * Anything that is not used directly from assembly code goes
	 * here.
	 */

	/*
	 * Guest registers we preserve during guest debugging.
	 *
	 * These shadow registers are updated by the kvm_handle_sys_reg
	 * trap handler if the guest accesses or updates them while we
	 * are using guest debug.
	 */
	struct {
		u32	mdscr_el1;
	} guest_debug_preserved;

	/* vcpu power-off state */
	bool power_off;

	/* Don't run the guest (internal implementation need) */
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
	u32 halt_successful_poll;
	u32 halt_attempted_poll;
	u32 halt_wakeup;
};

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
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);

/* We do not have shadow page tables, hence the empty hooks */
static inline void kvm_arch_mmu_notifier_invalidate_page(struct kvm *kvm,
							 unsigned long address)
{
}

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu * __percpu *kvm_get_running_vcpus(void);

u64 kvm_call_hyp(void *hypfn, ...);
void force_vm_exit(const cpumask_t *mask);
void kvm_mmu_wp_memory_region(struct kvm *kvm, int slot);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);

int kvm_perf_init(void);
int kvm_perf_teardown(void);

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr);

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

static inline void kvm_arch_hardware_disable(void) {}
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}

void kvm_arm_init_debug(void);
void kvm_arm_setup_debug(struct kvm_vcpu *vcpu);
void kvm_arm_clear_debug(struct kvm_vcpu *vcpu);
void kvm_arm_reset_debug_ptr(struct kvm_vcpu *vcpu);

#endif /* __ARM64_KVM_HOST_H__ */
