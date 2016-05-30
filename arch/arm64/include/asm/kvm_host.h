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
#include <kvm/arm_pmu.h>

#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS

#define KVM_VCPU_MAX_FEATURES 4

#define KVM_REQ_VCPU_EXIT	8

int __attribute_const__ kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
int kvm_arch_dev_ioctl_check_extension(long ext);
unsigned long kvm_hyp_reset_entry(void);
void __extended_idmap_trampoline(phys_addr_t boot_pgd, phys_addr_t idmap_start);

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

/*
 * 0 is reserved as an invalid value.
 * Order should be kept in sync with the save/restore code.
 */
enum vcpu_sysreg {
	__INVALID_SYSREG__,
	MPIDR_EL1,	/* MultiProcessor Affinity Register */
	CSSELR_EL1,	/* Cache Size Selection Register */
	SCTLR_EL1,	/* System Control Register */
	ACTLR_EL1,	/* Auxiliary Control Register */
	CPACR_EL1,	/* Coprocessor Access Control */
	TTBR0_EL1,	/* Translation Table Base Register 0 */
	TTBR1_EL1,	/* Translation Table Base Register 1 */
	TCR_EL1,	/* Translation Control Register */
	ESR_EL1,	/* Exception Syndrome Register */
	AFSR0_EL1,	/* Auxiliary Fault Status Register 0 */
	AFSR1_EL1,	/* Auxiliary Fault Status Register 1 */
	FAR_EL1,	/* Fault Address Register */
	MAIR_EL1,	/* Memory Attribute Indirection Register */
	VBAR_EL1,	/* Vector Base Address Register */
	CONTEXTIDR_EL1,	/* Context ID Register */
	TPIDR_EL0,	/* Thread ID, User R/W */
	TPIDRRO_EL0,	/* Thread ID, User R/O */
	TPIDR_EL1,	/* Thread ID, Privileged */
	AMAIR_EL1,	/* Aux Memory Attribute Indirection Register */
	CNTKCTL_EL1,	/* Timer Control Register (EL1) */
	PAR_EL1,	/* Physical Address Register */
	MDSCR_EL1,	/* Monitor Debug System Control Register */
	MDCCINT_EL1,	/* Monitor Debug Comms Channel Interrupt Enable Reg */

	/* Performance Monitors Registers */
	PMCR_EL0,	/* Control Register */
	PMSELR_EL0,	/* Event Counter Selection Register */
	PMEVCNTR0_EL0,	/* Event Counter Register (0-30) */
	PMEVCNTR30_EL0 = PMEVCNTR0_EL0 + 30,
	PMCCNTR_EL0,	/* Cycle Counter Register */
	PMEVTYPER0_EL0,	/* Event Type Register (0-30) */
	PMEVTYPER30_EL0 = PMEVTYPER0_EL0 + 30,
	PMCCFILTR_EL0,	/* Cycle Count Filter Register */
	PMCNTENSET_EL0,	/* Count Enable Set Register */
	PMINTENSET_EL1,	/* Interrupt Enable Set Register */
	PMOVSSET_EL0,	/* Overflow Flag Status Set Register */
	PMSWINC_EL0,	/* Software Increment Register */
	PMUSERENR_EL0,	/* User Enable Register */

	/* 32bit specific registers. Keep them at the end of the range */
	DACR32_EL2,	/* Domain Access Control Register */
	IFSR32_EL2,	/* Instruction Fault Status Register */
	FPEXC32_EL2,	/* Floating-Point Exception Control Register */
	DBGVCR32_EL2,	/* Debug Vector Catch Register */

	NR_SYS_REGS	/* Nothing after this line! */
};

/* 32bit mapping */
#define c0_MPIDR	(MPIDR_EL1 * 2)	/* MultiProcessor ID Register */
#define c0_CSSELR	(CSSELR_EL1 * 2)/* Cache Size Selection Register */
#define c1_SCTLR	(SCTLR_EL1 * 2)	/* System Control Register */
#define c1_ACTLR	(ACTLR_EL1 * 2)	/* Auxiliary Control Register */
#define c1_CPACR	(CPACR_EL1 * 2)	/* Coprocessor Access Control */
#define c2_TTBR0	(TTBR0_EL1 * 2)	/* Translation Table Base Register 0 */
#define c2_TTBR0_high	(c2_TTBR0 + 1)	/* TTBR0 top 32 bits */
#define c2_TTBR1	(TTBR1_EL1 * 2)	/* Translation Table Base Register 1 */
#define c2_TTBR1_high	(c2_TTBR1 + 1)	/* TTBR1 top 32 bits */
#define c2_TTBCR	(TCR_EL1 * 2)	/* Translation Table Base Control R. */
#define c3_DACR		(DACR32_EL2 * 2)/* Domain Access Control Register */
#define c5_DFSR		(ESR_EL1 * 2)	/* Data Fault Status Register */
#define c5_IFSR		(IFSR32_EL2 * 2)/* Instruction Fault Status Register */
#define c5_ADFSR	(AFSR0_EL1 * 2)	/* Auxiliary Data Fault Status R */
#define c5_AIFSR	(AFSR1_EL1 * 2)	/* Auxiliary Instr Fault Status R */
#define c6_DFAR		(FAR_EL1 * 2)	/* Data Fault Address Register */
#define c6_IFAR		(c6_DFAR + 1)	/* Instruction Fault Address Register */
#define c7_PAR		(PAR_EL1 * 2)	/* Physical Address Register */
#define c7_PAR_high	(c7_PAR + 1)	/* PAR top 32 bits */
#define c10_PRRR	(MAIR_EL1 * 2)	/* Primary Region Remap Register */
#define c10_NMRR	(c10_PRRR + 1)	/* Normal Memory Remap Register */
#define c12_VBAR	(VBAR_EL1 * 2)	/* Vector Base Address Register */
#define c13_CID		(CONTEXTIDR_EL1 * 2)	/* Context ID Register */
#define c13_TID_URW	(TPIDR_EL0 * 2)	/* Thread ID, User R/W */
#define c13_TID_URO	(TPIDRRO_EL0 * 2)/* Thread ID, User R/O */
#define c13_TID_PRIV	(TPIDR_EL1 * 2)	/* Thread ID, Privileged */
#define c10_AMAIR0	(AMAIR_EL1 * 2)	/* Aux Memory Attr Indirection Reg */
#define c10_AMAIR1	(c10_AMAIR0 + 1)/* Aux Memory Attr Indirection Reg */
#define c14_CNTKCTL	(CNTKCTL_EL1 * 2) /* Timer Control Register (PL1) */

#define cp14_DBGDSCRext	(MDSCR_EL1 * 2)
#define cp14_DBGBCR0	(DBGBCR0_EL1 * 2)
#define cp14_DBGBVR0	(DBGBVR0_EL1 * 2)
#define cp14_DBGBXVR0	(cp14_DBGBVR0 + 1)
#define cp14_DBGWCR0	(DBGWCR0_EL1 * 2)
#define cp14_DBGWVR0	(DBGWVR0_EL1 * 2)
#define cp14_DBGDCCINT	(MDCCINT_EL1 * 2)

#define NR_COPRO_REGS	(NR_SYS_REGS * 2)

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
	struct kvm_pmu pmu;

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
	u32 halt_poll_invalid;
	u32 halt_wakeup;
	u32 hvc_exit_stat;
	u64 wfe_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 exits;
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
void kvm_arm_halt_guest(struct kvm *kvm);
void kvm_arm_resume_guest(struct kvm *kvm);
void kvm_arm_halt_vcpu(struct kvm_vcpu *vcpu);
void kvm_arm_resume_vcpu(struct kvm_vcpu *vcpu);

u64 __kvm_call_hyp(void *hypfn, ...);
#define kvm_call_hyp(f, ...) __kvm_call_hyp(kvm_ksym_ref(f), ##__VA_ARGS__)

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
	__kvm_call_hyp((void *)boot_pgd_ptr, pgd_ptr,
		       hyp_stack_ptr, vector_ptr);
}

static inline void __cpu_reset_hyp_mode(phys_addr_t boot_pgd_ptr,
					phys_addr_t phys_idmap_start)
{
	/*
	 * Call reset code, and switch back to stub hyp vectors.
	 * Uses __kvm_call_hyp() to avoid kaslr's kvm_ksym_ref() translation.
	 */
	__kvm_call_hyp((void *)kvm_hyp_reset_entry(),
		       boot_pgd_ptr, phys_idmap_start);
}

static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

void kvm_arm_init_debug(void);
void kvm_arm_setup_debug(struct kvm_vcpu *vcpu);
void kvm_arm_clear_debug(struct kvm_vcpu *vcpu);
void kvm_arm_reset_debug_ptr(struct kvm_vcpu *vcpu);
int kvm_arm_vcpu_arch_set_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_get_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_has_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);

static inline void __cpu_init_stage2(void)
{
	u32 parange = kvm_call_hyp(__init_stage2_translation);

	WARN_ONCE(parange < 40,
		  "PARange is %d bits, unsupported configuration!", parange);
}

#endif /* __ARM64_KVM_HOST_H__ */
