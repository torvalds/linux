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

#include <linux/bitmap.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/kvm_types.h>
#include <linux/percpu.h>
#include <asm/arch_gicv3.h>
#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/daifflags.h>
#include <asm/fpsimd.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>
#include <asm/kvm_mmio.h>
#include <asm/smp_plat.h>
#include <asm/thread_info.h>

#define __KVM_HAVE_ARCH_INTC_INITIALIZED

#define KVM_USER_MEM_SLOTS 512
#define KVM_HALT_POLL_NS_DEFAULT 500000

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>
#include <kvm/arm_pmu.h>

#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS

/* Will be incremented when KVM_ARM_VCPU_SVE is fully implemented: */
#define KVM_VCPU_MAX_FEATURES 4

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_IRQ_PENDING	KVM_ARCH_REQ(1)
#define KVM_REQ_VCPU_RESET	KVM_ARCH_REQ(2)

DECLARE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

extern unsigned int kvm_sve_max_vl;
int kvm_arm_init_arch_resources(void);

int __attribute_const__ kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_uninit(struct kvm_vcpu *vcpu);
int kvm_arch_vm_ioctl_check_extension(struct kvm *kvm, long ext);
void __extended_idmap_trampoline(phys_addr_t boot_pgd, phys_addr_t idmap_start);

struct kvm_vmid {
	/* The VMID generation used for the virt. memory system */
	u64    vmid_gen;
	u32    vmid;
};

struct kvm_arch {
	struct kvm_vmid vmid;

	/* stage2 entry level table */
	pgd_t *pgd;
	phys_addr_t pgd_phys;

	/* VTCR_EL2 value for this VM */
	u64    vtcr;

	/* The last vcpu id that ran on each physical CPU */
	int __percpu *last_vcpu_ran;

	/* The maximum number of vCPUs depends on the used GIC model */
	int max_vcpus;

	/* Interrupt controller */
	struct vgic_dist	vgic;

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
	u32 esr_el2;		/* Hyp Syndrom Register */
	u64 far_el2;		/* Hyp Fault Address Register */
	u64 hpfar_el2;		/* Hyp IPA Fault Address Register */
	u64 disr_el1;		/* Deferred [SError] Status Register */
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
	ZCR_EL1,	/* SVE Control */
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
	DISR_EL1,	/* Deferred Interrupt Status Register */

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

	struct kvm_vcpu *__hyp_running_vcpu;
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
	void *sve_state;
	unsigned int sve_max_vl;

	/* HYP configuration */
	u64 hcr_el2;
	u32 mdcr_el2;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* State of various workarounds, see kvm_asm.h for bit assignment */
	u64 workaround_flags;

	/* Miscellaneous vcpu state flags */
	u64 flags;

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

	struct thread_info *host_thread_info;	/* hyp VA */
	struct user_fpsimd_state *host_fpsimd_state;	/* hyp VA */

	struct {
		/* {Break,watch}point registers */
		struct kvm_guest_debug_arch regs;
		/* Statistical profiling extension */
		u64 pmscr_el1;
	} host_debug_state;

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

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* Target CPU and feature flags */
	int target;
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* Detect first run of a vcpu */
	bool has_run_once;

	/* Virtual SError ESR to restore when HCR_EL2.VSE is set */
	u64 vsesr_el2;

	/* Additional reset state */
	struct vcpu_reset_state	reset_state;

	/* True when deferrable sysregs are loaded on the physical CPU,
	 * see kvm_vcpu_load_sysregs and kvm_vcpu_put_sysregs. */
	bool sysregs_loaded_on_cpu;
};

/* Pointer to the vcpu's SVE FFR for sve_{save,load}_state() */
#define vcpu_sve_pffr(vcpu) ((void *)((char *)((vcpu)->arch.sve_state) + \
				      sve_ffr_offset((vcpu)->arch.sve_max_vl)))

#define vcpu_sve_state_size(vcpu) ({					\
	size_t __size_ret;						\
	unsigned int __vcpu_vq;						\
									\
	if (WARN_ON(!sve_vl_valid((vcpu)->arch.sve_max_vl))) {		\
		__size_ret = 0;						\
	} else {							\
		__vcpu_vq = sve_vq_from_vl((vcpu)->arch.sve_max_vl);	\
		__size_ret = SVE_SIG_REGS_SIZE(__vcpu_vq);		\
	}								\
									\
	__size_ret;							\
})

/* vcpu_arch flags field values: */
#define KVM_ARM64_DEBUG_DIRTY		(1 << 0)
#define KVM_ARM64_FP_ENABLED		(1 << 1) /* guest FP regs loaded */
#define KVM_ARM64_FP_HOST		(1 << 2) /* host FP regs loaded */
#define KVM_ARM64_HOST_SVE_IN_USE	(1 << 3) /* backup for host TIF_SVE */
#define KVM_ARM64_HOST_SVE_ENABLED	(1 << 4) /* SVE enabled for EL0 */
#define KVM_ARM64_GUEST_HAS_SVE		(1 << 5) /* SVE exposed to guest */
#define KVM_ARM64_VCPU_SVE_FINALIZED	(1 << 6) /* SVE config completed */

#define vcpu_has_sve(vcpu) (system_supports_sve() && \
			    ((vcpu)->arch.flags & KVM_ARM64_GUEST_HAS_SVE))

#define vcpu_gp_regs(v)		(&(v)->arch.ctxt.gp_regs)

/*
 * Only use __vcpu_sys_reg if you know you want the memory backed version of a
 * register, and not the one most recently accessed by a running VCPU.  For
 * example, for userspace access or for system registers that are never context
 * switched, but only emulated.
 */
#define __vcpu_sys_reg(v,r)	((v)->arch.ctxt.sys_regs[(r)])

u64 vcpu_read_sys_reg(const struct kvm_vcpu *vcpu, int reg);
void vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg);

/*
 * CP14 and CP15 live in the same array, as they are backed by the
 * same system registers.
 */
#define vcpu_cp14(v,r)		((v)->arch.ctxt.copro[(r)])
#define vcpu_cp15(v,r)		((v)->arch.ctxt.copro[(r)])

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

int kvm_vcpu_preferred_target(struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int __kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

int __kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva_range(struct kvm *kvm,
			unsigned long start, unsigned long end);
int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);

struct kvm_vcpu *kvm_arm_get_running_vcpu(void);
struct kvm_vcpu * __percpu *kvm_get_running_vcpus(void);
void kvm_arm_halt_guest(struct kvm *kvm);
void kvm_arm_resume_guest(struct kvm *kvm);

u64 __kvm_call_hyp(void *hypfn, ...);

/*
 * The couple of isb() below are there to guarantee the same behaviour
 * on VHE as on !VHE, where the eret to EL1 acts as a context
 * synchronization event.
 */
#define kvm_call_hyp(f, ...)						\
	do {								\
		if (has_vhe()) {					\
			f(__VA_ARGS__);					\
			isb();						\
		} else {						\
			__kvm_call_hyp(kvm_ksym_ref(f), ##__VA_ARGS__); \
		}							\
	} while(0)

#define kvm_call_hyp_ret(f, ...)					\
	({								\
		typeof(f(__VA_ARGS__)) ret;				\
									\
		if (has_vhe()) {					\
			ret = f(__VA_ARGS__);				\
			isb();						\
		} else {						\
			ret = __kvm_call_hyp(kvm_ksym_ref(f),		\
					     ##__VA_ARGS__);		\
		}							\
									\
		ret;							\
	})

void force_vm_exit(const cpumask_t *mask);
void kvm_mmu_wp_memory_region(struct kvm *kvm, int slot);

int handle_exit(struct kvm_vcpu *vcpu, struct kvm_run *run,
		int exception_index);
void handle_exit_early(struct kvm_vcpu *vcpu, struct kvm_run *run,
		       int exception_index);

int kvm_perf_init(void);
int kvm_perf_teardown(void);

void kvm_set_sei_esr(struct kvm_vcpu *vcpu, u64 syndrome);

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr);

DECLARE_PER_CPU(kvm_cpu_context_t, kvm_host_cpu_state);

static inline void kvm_init_host_cpu_context(kvm_cpu_context_t *cpu_ctxt,
					     int cpu)
{
	/* The host's MPIDR is immutable, so let's set it up at boot time */
	cpu_ctxt->sys_regs[MPIDR_EL1] = cpu_logical_map(cpu);
}

void __kvm_enable_ssbs(void);

static inline void __cpu_init_hyp_mode(phys_addr_t pgd_ptr,
				       unsigned long hyp_stack_ptr,
				       unsigned long vector_ptr)
{
	/*
	 * Calculate the raw per-cpu offset without a translation from the
	 * kernel's mapping to the linear mapping, and store it in tpidr_el2
	 * so that we can use adr_l to access per-cpu variables in EL2.
	 */
	u64 tpidr_el2 = ((u64)this_cpu_ptr(&kvm_host_cpu_state) -
			 (u64)kvm_ksym_ref(kvm_host_cpu_state));

	/*
	 * Call initialization code, and switch to the full blown HYP code.
	 * If the cpucaps haven't been finalized yet, something has gone very
	 * wrong, and hyp will crash and burn when it uses any
	 * cpus_have_const_cap() wrapper.
	 */
	BUG_ON(!static_branch_likely(&arm64_const_caps_ready));
	__kvm_call_hyp((void *)pgd_ptr, hyp_stack_ptr, vector_ptr, tpidr_el2);

	/*
	 * Disabling SSBD on a non-VHE system requires us to enable SSBS
	 * at EL2.
	 */
	if (!has_vhe() && this_cpu_has_cap(ARM64_SSBS) &&
	    arm64_get_ssbd_state() == ARM64_SSBD_FORCE_DISABLE) {
		kvm_call_hyp(__kvm_enable_ssbs);
	}
}

static inline bool kvm_arch_requires_vhe(void)
{
	/*
	 * The Arm architecture specifies that implementation of SVE
	 * requires VHE also to be implemented.  The KVM code for arm64
	 * relies on this when SVE is present:
	 */
	if (system_supports_sve())
		return true;

	/* Some implementations have defects that confine them to VHE */
	if (cpus_have_cap(ARM64_WORKAROUND_1165522))
		return true;

	return false;
}

static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
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

static inline void __cpu_init_stage2(void) {}

/* Guest/host FPSIMD coordination helpers */
int kvm_arch_vcpu_run_map_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_ctxsync_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_put_fp(struct kvm_vcpu *vcpu);

#ifdef CONFIG_KVM /* Avoid conflicts with core headers if CONFIG_KVM=n */
static inline int kvm_arch_vcpu_run_pid_change(struct kvm_vcpu *vcpu)
{
	return kvm_arch_vcpu_run_map_fp(vcpu);
}
#endif

static inline void kvm_arm_vhe_guest_enter(void)
{
	local_daif_mask();

	/*
	 * Having IRQs masked via PMR when entering the guest means the GIC
	 * will not signal the CPU of interrupts of lower priority, and the
	 * only way to get out will be via guest exceptions.
	 * Naturally, we want to avoid this.
	 */
	if (system_uses_irq_prio_masking()) {
		gic_write_pmr(GIC_PRIO_IRQON);
		dsb(sy);
	}
}

static inline void kvm_arm_vhe_guest_exit(void)
{
	/*
	 * local_daif_restore() takes care to properly restore PSTATE.DAIF
	 * and the GIC PMR if the host is using IRQ priorities.
	 */
	local_daif_restore(DAIF_PROCCTX_NOIRQ);

	/*
	 * When we exit from the guest we change a number of CPU configuration
	 * parameters, such as traps.  Make sure these changes take effect
	 * before running the host or additional guests.
	 */
	isb();
}

static inline bool kvm_arm_harden_branch_predictor(void)
{
	return cpus_have_const_cap(ARM64_HARDEN_BRANCH_PREDICTOR);
}

#define KVM_SSBD_UNKNOWN		-1
#define KVM_SSBD_FORCE_DISABLE		0
#define KVM_SSBD_KERNEL		1
#define KVM_SSBD_FORCE_ENABLE		2
#define KVM_SSBD_MITIGATED		3

static inline int kvm_arm_have_ssbd(void)
{
	switch (arm64_get_ssbd_state()) {
	case ARM64_SSBD_FORCE_DISABLE:
		return KVM_SSBD_FORCE_DISABLE;
	case ARM64_SSBD_KERNEL:
		return KVM_SSBD_KERNEL;
	case ARM64_SSBD_FORCE_ENABLE:
		return KVM_SSBD_FORCE_ENABLE;
	case ARM64_SSBD_MITIGATED:
		return KVM_SSBD_MITIGATED;
	case ARM64_SSBD_UNKNOWN:
	default:
		return KVM_SSBD_UNKNOWN;
	}
}

void kvm_vcpu_load_sysregs(struct kvm_vcpu *vcpu);
void kvm_vcpu_put_sysregs(struct kvm_vcpu *vcpu);

void kvm_set_ipa_limit(void);

#define __KVM_HAVE_ARCH_VM_ALLOC
struct kvm *kvm_arch_alloc_vm(void);
void kvm_arch_free_vm(struct kvm *kvm);

int kvm_arm_setup_stage2(struct kvm *kvm, unsigned long type);

int kvm_arm_vcpu_finalize(struct kvm_vcpu *vcpu, int what);
bool kvm_arm_vcpu_is_finalized(struct kvm_vcpu *vcpu);

#define kvm_arm_vcpu_sve_finalized(vcpu) \
	((vcpu)->arch.flags & KVM_ARM64_VCPU_SVE_FINALIZED)

#endif /* __ARM64_KVM_HOST_H__ */
