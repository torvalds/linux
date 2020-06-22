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
#include <asm/cputype.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>
#include <asm/fpstate.h>
#include <kvm/arm_arch_timer.h>

#define __KVM_HAVE_ARCH_INTC_INITIALIZED

#define KVM_USER_MEM_SLOTS 32
#define KVM_HAVE_ONE_REG
#define KVM_HALT_POLL_NS_DEFAULT 500000

#define KVM_VCPU_MAX_FEATURES 2

#include <kvm/arm_vgic.h>


#ifdef CONFIG_ARM_GIC_V3
#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS
#else
#define KVM_MAX_VCPUS VGIC_V2_MAX_CPUS
#endif

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_IRQ_PENDING	KVM_ARCH_REQ(1)
#define KVM_REQ_VCPU_RESET	KVM_ARCH_REQ(2)

DECLARE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

u32 *kvm_vcpu_reg(struct kvm_vcpu *vcpu, u8 reg_num, u32 mode);
int __attribute_const__ kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
void kvm_reset_coprocs(struct kvm_vcpu *vcpu);

struct kvm_arch {
	/* VTTBR value associated with below pgd and vmid */
	u64    vttbr;

	/* The last vcpu id that ran on each physical CPU */
	int __percpu *last_vcpu_ran;

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
	int max_vcpus;

	/* Mandated version of PSCI */
	u32 psci_version;
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
};

/*
 * 0 is reserved as an invalid value.
 * Order should be kept in sync with the save/restore code.
 */
enum vcpu_sysreg {
	__INVALID_SYSREG__,
	c0_MPIDR,		/* MultiProcessor ID Register */
	c0_CSSELR,		/* Cache Size Selection Register */
	c1_SCTLR,		/* System Control Register */
	c1_ACTLR,		/* Auxiliary Control Register */
	c1_CPACR,		/* Coprocessor Access Control */
	c2_TTBR0,		/* Translation Table Base Register 0 */
	c2_TTBR0_high,		/* TTBR0 top 32 bits */
	c2_TTBR1,		/* Translation Table Base Register 1 */
	c2_TTBR1_high,		/* TTBR1 top 32 bits */
	c2_TTBCR,		/* Translation Table Base Control R. */
	c3_DACR,		/* Domain Access Control Register */
	c5_DFSR,		/* Data Fault Status Register */
	c5_IFSR,		/* Instruction Fault Status Register */
	c5_ADFSR,		/* Auxilary Data Fault Status R */
	c5_AIFSR,		/* Auxilary Instrunction Fault Status R */
	c6_DFAR,		/* Data Fault Address Register */
	c6_IFAR,		/* Instruction Fault Address Register */
	c7_PAR,			/* Physical Address Register */
	c7_PAR_high,		/* PAR top 32 bits */
	c9_L2CTLR,		/* Cortex A15/A7 L2 Control Register */
	c10_PRRR,		/* Primary Region Remap Register */
	c10_NMRR,		/* Normal Memory Remap Register */
	c12_VBAR,		/* Vector Base Address Register */
	c13_CID,		/* Context ID Register */
	c13_TID_URW,		/* Thread ID, User R/W */
	c13_TID_URO,		/* Thread ID, User R/O */
	c13_TID_PRIV,		/* Thread ID, Privileged */
	c14_CNTKCTL,		/* Timer Control Register (PL1) */
	c10_AMAIR0,		/* Auxilary Memory Attribute Indirection Reg0 */
	c10_AMAIR1,		/* Auxilary Memory Attribute Indirection Reg1 */
	NR_CP15_REGS		/* Number of regs (incl. invalid) */
};

struct kvm_cpu_context {
	struct kvm_regs	gp_regs;
	struct vfp_hard_struct vfp;
	u32 cp15[NR_CP15_REGS];
};

typedef struct kvm_cpu_context kvm_cpu_context_t;

struct vcpu_reset_state {
	unsigned long	pc;
	unsigned long	r0;
	bool		be;
	bool		reset;
};

struct kvm_vcpu_arch {
	struct kvm_cpu_context ctxt;

	int target; /* Processor target */
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* The CPU type we expose to the VM */
	u32 midr;

	/* HYP trapping configuration */
	u32 hcr;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Host FP context */
	kvm_cpu_context_t *host_cpu_context;

	/* VGIC state */
	struct vgic_cpu vgic_cpu;
	struct arch_timer_cpu timer_cpu;

	/*
	 * Anything that is not used directly from assembly code goes
	 * here.
	 */

	/* vcpu power-off state */
	bool power_off;

	 /* Don't run the guest (internal implementation need) */
	bool pause;

	/* IO related fields */
	struct kvm_decode mmio_decode;

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	struct vcpu_reset_state reset_state;

	/* Detect first run of a vcpu */
	bool has_run_once;
};

struct kvm_vm_stat {
	ulong remote_tlb_flush;
};

struct kvm_vcpu_stat {
	u64 halt_successful_poll;
	u64 halt_attempted_poll;
	u64 halt_poll_invalid;
	u64 halt_wakeup;
	u64 hvc_exit_stat;
	u64 wfe_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 exits;
};

#define vcpu_cp15(v,r)	(v)->arch.ctxt.cp15[r]

int kvm_vcpu_preferred_target(struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
unsigned long kvm_call_hyp(void *hypfn, ...);
void force_vm_exit(const cpumask_t *mask);
int __kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

int __kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);

unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu __percpu **kvm_get_running_vcpus(void);
void kvm_arm_halt_guest(struct kvm *kvm);
void kvm_arm_resume_guest(struct kvm *kvm);

int kvm_arm_copy_coproc_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);
unsigned long kvm_arm_num_coproc_regs(struct kvm_vcpu *vcpu);
int kvm_arm_coproc_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);
int kvm_arm_coproc_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);

static inline void handle_exit_early(struct kvm_vcpu *vcpu, struct kvm_run *run,
				     int exception_index) {}

static inline void __cpu_init_hyp_mode(phys_addr_t pgd_ptr,
				       unsigned long hyp_stack_ptr,
				       unsigned long vector_ptr)
{
	/*
	 * Call initialization code, and switch to the full blown HYP
	 * code. The init code doesn't need to preserve these
	 * registers as r0-r3 are already callee saved according to
	 * the AAPCS.
	 * Note that we slightly misuse the prototype by casting the
	 * stack pointer to a void *.

	 * The PGDs are always passed as the third argument, in order
	 * to be passed into r2-r3 to the init code (yes, this is
	 * compliant with the PCS!).
	 */

	kvm_call_hyp((void*)hyp_stack_ptr, vector_ptr, pgd_ptr);
}

static inline void __cpu_init_stage2(void)
{
	kvm_call_hyp(__init_stage2_translation);
}

static inline int kvm_arch_dev_ioctl_check_extension(struct kvm *kvm, long ext)
{
	return 0;
}

int kvm_perf_init(void);
int kvm_perf_teardown(void);

void kvm_mmu_wp_memory_region(struct kvm *kvm, int slot);

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr);

static inline bool kvm_arch_check_sve_has_vhe(void) { return true; }
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

static inline void kvm_arm_init_debug(void) {}
static inline void kvm_arm_setup_debug(struct kvm_vcpu *vcpu) {}
static inline void kvm_arm_clear_debug(struct kvm_vcpu *vcpu) {}
static inline void kvm_arm_reset_debug_ptr(struct kvm_vcpu *vcpu) {}
static inline bool kvm_arm_handle_step_debug(struct kvm_vcpu *vcpu,
					     struct kvm_run *run)
{
	return false;
}

int kvm_arm_vcpu_arch_set_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_get_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_has_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);

/*
 * VFP/NEON switching is all done by the hyp switch code, so no need to
 * coordinate with host context handling for this state:
 */
static inline void kvm_arch_vcpu_load_fp(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_ctxsync_fp(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_put_fp(struct kvm_vcpu *vcpu) {}

static inline void kvm_arm_vhe_guest_enter(void) {}
static inline void kvm_arm_vhe_guest_exit(void) {}

static inline bool kvm_arm_harden_branch_predictor(void)
{
	switch(read_cpuid_part()) {
#ifdef CONFIG_HARDEN_BRANCH_PREDICTOR
	case ARM_CPU_PART_BRAHMA_B15:
	case ARM_CPU_PART_CORTEX_A12:
	case ARM_CPU_PART_CORTEX_A15:
	case ARM_CPU_PART_CORTEX_A17:
		return true;
#endif
	default:
		return false;
	}
}

#define KVM_SSBD_UNKNOWN		-1
#define KVM_SSBD_FORCE_DISABLE		0
#define KVM_SSBD_KERNEL		1
#define KVM_SSBD_FORCE_ENABLE		2
#define KVM_SSBD_MITIGATED		3

static inline int kvm_arm_have_ssbd(void)
{
	/* No way to detect it yet, pretend it is not there. */
	return KVM_SSBD_UNKNOWN;
}

static inline void kvm_vcpu_load_sysregs(struct kvm_vcpu *vcpu) {}
static inline void kvm_vcpu_put_sysregs(struct kvm_vcpu *vcpu) {}

#define __KVM_HAVE_ARCH_VM_ALLOC
struct kvm *kvm_arch_alloc_vm(void);
void kvm_arch_free_vm(struct kvm *kvm);

#define kvm_arm_vcpu_loaded(vcpu)	(false)

#endif /* __ARM_KVM_HOST_H__ */
