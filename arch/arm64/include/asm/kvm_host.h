/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 *
 * Derived from arch/arm/include/asm/kvm_host.h:
 * Copyright (C) 2012 - Virtual Open Systems and Columbia University
 * Author: Christoffer Dall <c.dall@virtualopensystems.com>
 */

#ifndef __ARM64_KVM_HOST_H__
#define __ARM64_KVM_HOST_H__

#include <linux/arm-smccc.h>
#include <linux/bitmap.h>
#include <linux/types.h>
#include <linux/jump_label.h>
#include <linux/kvm_types.h>
#include <linux/maple_tree.h>
#include <linux/percpu.h>
#include <linux/psci.h>
#include <asm/arch_gicv3.h>
#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/cputype.h>
#include <asm/daifflags.h>
#include <asm/fpsimd.h>
#include <asm/kvm.h>
#include <asm/kvm_asm.h>

#define __KVM_HAVE_ARCH_INTC_INITIALIZED

#define KVM_HALT_POLL_NS_DEFAULT 500000

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>
#include <kvm/arm_pmu.h>

#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS

#define KVM_VCPU_MAX_FEATURES 7

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_IRQ_PENDING	KVM_ARCH_REQ(1)
#define KVM_REQ_VCPU_RESET	KVM_ARCH_REQ(2)
#define KVM_REQ_RECORD_STEAL	KVM_ARCH_REQ(3)
#define KVM_REQ_RELOAD_GICv4	KVM_ARCH_REQ(4)
#define KVM_REQ_RELOAD_PMU	KVM_ARCH_REQ(5)
#define KVM_REQ_SUSPEND		KVM_ARCH_REQ(6)

#define KVM_DIRTY_LOG_MANUAL_CAPS   (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE | \
				     KVM_DIRTY_LOG_INITIALLY_SET)

#define KVM_HAVE_MMU_RWLOCK

/*
 * Mode of operation configurable with kvm-arm.mode early param.
 * See Documentation/admin-guide/kernel-parameters.txt for more information.
 */
enum kvm_mode {
	KVM_MODE_DEFAULT,
	KVM_MODE_PROTECTED,
	KVM_MODE_NV,
	KVM_MODE_NONE,
};
#ifdef CONFIG_KVM
enum kvm_mode kvm_get_mode(void);
#else
static inline enum kvm_mode kvm_get_mode(void) { return KVM_MODE_NONE; };
#endif

DECLARE_STATIC_KEY_FALSE(userspace_irqchip_in_use);

extern unsigned int __ro_after_init kvm_sve_max_vl;
int __init kvm_arm_init_sve(void);

u32 __attribute_const__ kvm_target_cpu(void);
int kvm_reset_vcpu(struct kvm_vcpu *vcpu);
void kvm_arm_vcpu_destroy(struct kvm_vcpu *vcpu);

struct kvm_hyp_memcache {
	phys_addr_t head;
	unsigned long nr_pages;
};

static inline void push_hyp_memcache(struct kvm_hyp_memcache *mc,
				     phys_addr_t *p,
				     phys_addr_t (*to_pa)(void *virt))
{
	*p = mc->head;
	mc->head = to_pa(p);
	mc->nr_pages++;
}

static inline void *pop_hyp_memcache(struct kvm_hyp_memcache *mc,
				     void *(*to_va)(phys_addr_t phys))
{
	phys_addr_t *p = to_va(mc->head);

	if (!mc->nr_pages)
		return NULL;

	mc->head = *p;
	mc->nr_pages--;

	return p;
}

static inline int __topup_hyp_memcache(struct kvm_hyp_memcache *mc,
				       unsigned long min_pages,
				       void *(*alloc_fn)(void *arg),
				       phys_addr_t (*to_pa)(void *virt),
				       void *arg)
{
	while (mc->nr_pages < min_pages) {
		phys_addr_t *p = alloc_fn(arg);

		if (!p)
			return -ENOMEM;
		push_hyp_memcache(mc, p, to_pa);
	}

	return 0;
}

static inline void __free_hyp_memcache(struct kvm_hyp_memcache *mc,
				       void (*free_fn)(void *virt, void *arg),
				       void *(*to_va)(phys_addr_t phys),
				       void *arg)
{
	while (mc->nr_pages)
		free_fn(pop_hyp_memcache(mc, to_va), arg);
}

void free_hyp_memcache(struct kvm_hyp_memcache *mc);
int topup_hyp_memcache(struct kvm_hyp_memcache *mc, unsigned long min_pages);

struct kvm_vmid {
	atomic64_t id;
};

struct kvm_s2_mmu {
	struct kvm_vmid vmid;

	/*
	 * stage2 entry level table
	 *
	 * Two kvm_s2_mmu structures in the same VM can point to the same
	 * pgd here.  This happens when running a guest using a
	 * translation regime that isn't affected by its own stage-2
	 * translation, such as a non-VHE hypervisor running at vEL2, or
	 * for vEL1/EL0 with vHCR_EL2.VM == 0.  In that case, we use the
	 * canonical stage-2 page tables.
	 */
	phys_addr_t	pgd_phys;
	struct kvm_pgtable *pgt;

	/* The last vcpu id that ran on each physical CPU */
	int __percpu *last_vcpu_ran;

	struct kvm_arch *arch;
};

struct kvm_arch_memory_slot {
};

/**
 * struct kvm_smccc_features: Descriptor of the hypercall services exposed to the guests
 *
 * @std_bmap: Bitmap of standard secure service calls
 * @std_hyp_bmap: Bitmap of standard hypervisor service calls
 * @vendor_hyp_bmap: Bitmap of vendor specific hypervisor service calls
 */
struct kvm_smccc_features {
	unsigned long std_bmap;
	unsigned long std_hyp_bmap;
	unsigned long vendor_hyp_bmap;
};

typedef unsigned int pkvm_handle_t;

struct kvm_protected_vm {
	pkvm_handle_t handle;
	struct kvm_hyp_memcache teardown_mc;
};

struct kvm_arch {
	struct kvm_s2_mmu mmu;

	/* VTCR_EL2 value for this VM */
	u64    vtcr;

	/* Interrupt controller */
	struct vgic_dist	vgic;

	/* Timers */
	struct arch_timer_vm_data timer_data;

	/* Mandated version of PSCI */
	u32 psci_version;

	/* Protects VM-scoped configuration data */
	struct mutex config_lock;

	/*
	 * If we encounter a data abort without valid instruction syndrome
	 * information, report this to user space.  User space can (and
	 * should) opt in to this feature if KVM_CAP_ARM_NISV_TO_USER is
	 * supported.
	 */
#define KVM_ARCH_FLAG_RETURN_NISV_IO_ABORT_TO_USER	0
	/* Memory Tagging Extension enabled for the guest */
#define KVM_ARCH_FLAG_MTE_ENABLED			1
	/* At least one vCPU has ran in the VM */
#define KVM_ARCH_FLAG_HAS_RAN_ONCE			2
	/*
	 * The following two bits are used to indicate the guest's EL1
	 * register width configuration. A value of KVM_ARCH_FLAG_EL1_32BIT
	 * bit is valid only when KVM_ARCH_FLAG_REG_WIDTH_CONFIGURED is set.
	 * Otherwise, the guest's EL1 register width has not yet been
	 * determined yet.
	 */
#define KVM_ARCH_FLAG_REG_WIDTH_CONFIGURED		3
#define KVM_ARCH_FLAG_EL1_32BIT				4
	/* PSCI SYSTEM_SUSPEND enabled for the guest */
#define KVM_ARCH_FLAG_SYSTEM_SUSPEND_ENABLED		5
	/* VM counter offset */
#define KVM_ARCH_FLAG_VM_COUNTER_OFFSET			6
	/* Timer PPIs made immutable */
#define KVM_ARCH_FLAG_TIMER_PPIS_IMMUTABLE		7
	/* SMCCC filter initialized for the VM */
#define KVM_ARCH_FLAG_SMCCC_FILTER_CONFIGURED		8
	unsigned long flags;

	/*
	 * VM-wide PMU filter, implemented as a bitmap and big enough for
	 * up to 2^10 events (ARMv8.0) or 2^16 events (ARMv8.1+).
	 */
	unsigned long *pmu_filter;
	struct arm_pmu *arm_pmu;

	cpumask_var_t supported_cpus;

	u8 pfr0_csv2;
	u8 pfr0_csv3;
	struct {
		u8 imp:4;
		u8 unimp:4;
	} dfr0_pmuver;

	/* Hypercall features firmware registers' descriptor */
	struct kvm_smccc_features smccc_feat;
	struct maple_tree smccc_filter;

	/*
	 * For an untrusted host VM, 'pkvm.handle' is used to lookup
	 * the associated pKVM instance in the hypervisor.
	 */
	struct kvm_protected_vm pkvm;
};

struct kvm_vcpu_fault_info {
	u64 esr_el2;		/* Hyp Syndrom Register */
	u64 far_el2;		/* Hyp Fault Address Register */
	u64 hpfar_el2;		/* Hyp IPA Fault Address Register */
	u64 disr_el1;		/* Deferred [SError] Status Register */
};

enum vcpu_sysreg {
	__INVALID_SYSREG__,   /* 0 is reserved as an invalid value */
	MPIDR_EL1,	/* MultiProcessor Affinity Register */
	CLIDR_EL1,	/* Cache Level ID Register */
	CSSELR_EL1,	/* Cache Size Selection Register */
	SCTLR_EL1,	/* System Control Register */
	ACTLR_EL1,	/* Auxiliary Control Register */
	CPACR_EL1,	/* Coprocessor Access Control */
	ZCR_EL1,	/* SVE Control */
	TTBR0_EL1,	/* Translation Table Base Register 0 */
	TTBR1_EL1,	/* Translation Table Base Register 1 */
	TCR_EL1,	/* Translation Control Register */
	TCR2_EL1,	/* Extended Translation Control Register */
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
	OSLSR_EL1,	/* OS Lock Status Register */
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
	PMUSERENR_EL0,	/* User Enable Register */

	/* Pointer Authentication Registers in a strict increasing order. */
	APIAKEYLO_EL1,
	APIAKEYHI_EL1,
	APIBKEYLO_EL1,
	APIBKEYHI_EL1,
	APDAKEYLO_EL1,
	APDAKEYHI_EL1,
	APDBKEYLO_EL1,
	APDBKEYHI_EL1,
	APGAKEYLO_EL1,
	APGAKEYHI_EL1,

	ELR_EL1,
	SP_EL1,
	SPSR_EL1,

	CNTVOFF_EL2,
	CNTV_CVAL_EL0,
	CNTV_CTL_EL0,
	CNTP_CVAL_EL0,
	CNTP_CTL_EL0,

	/* Memory Tagging Extension registers */
	RGSR_EL1,	/* Random Allocation Tag Seed Register */
	GCR_EL1,	/* Tag Control Register */
	TFSR_EL1,	/* Tag Fault Status Register (EL1) */
	TFSRE0_EL1,	/* Tag Fault Status Register (EL0) */

	/* Permission Indirection Extension registers */
	PIR_EL1,       /* Permission Indirection Register 1 (EL1) */
	PIRE0_EL1,     /*  Permission Indirection Register 0 (EL1) */

	/* 32bit specific registers. */
	DACR32_EL2,	/* Domain Access Control Register */
	IFSR32_EL2,	/* Instruction Fault Status Register */
	FPEXC32_EL2,	/* Floating-Point Exception Control Register */
	DBGVCR32_EL2,	/* Debug Vector Catch Register */

	/* EL2 registers */
	VPIDR_EL2,	/* Virtualization Processor ID Register */
	VMPIDR_EL2,	/* Virtualization Multiprocessor ID Register */
	SCTLR_EL2,	/* System Control Register (EL2) */
	ACTLR_EL2,	/* Auxiliary Control Register (EL2) */
	HCR_EL2,	/* Hypervisor Configuration Register */
	MDCR_EL2,	/* Monitor Debug Configuration Register (EL2) */
	CPTR_EL2,	/* Architectural Feature Trap Register (EL2) */
	HSTR_EL2,	/* Hypervisor System Trap Register */
	HACR_EL2,	/* Hypervisor Auxiliary Control Register */
	TTBR0_EL2,	/* Translation Table Base Register 0 (EL2) */
	TTBR1_EL2,	/* Translation Table Base Register 1 (EL2) */
	TCR_EL2,	/* Translation Control Register (EL2) */
	VTTBR_EL2,	/* Virtualization Translation Table Base Register */
	VTCR_EL2,	/* Virtualization Translation Control Register */
	SPSR_EL2,	/* EL2 saved program status register */
	ELR_EL2,	/* EL2 exception link register */
	AFSR0_EL2,	/* Auxiliary Fault Status Register 0 (EL2) */
	AFSR1_EL2,	/* Auxiliary Fault Status Register 1 (EL2) */
	ESR_EL2,	/* Exception Syndrome Register (EL2) */
	FAR_EL2,	/* Fault Address Register (EL2) */
	HPFAR_EL2,	/* Hypervisor IPA Fault Address Register */
	MAIR_EL2,	/* Memory Attribute Indirection Register (EL2) */
	AMAIR_EL2,	/* Auxiliary Memory Attribute Indirection Register (EL2) */
	VBAR_EL2,	/* Vector Base Address Register (EL2) */
	RVBAR_EL2,	/* Reset Vector Base Address Register */
	CONTEXTIDR_EL2,	/* Context ID Register (EL2) */
	TPIDR_EL2,	/* EL2 Software Thread ID Register */
	CNTHCTL_EL2,	/* Counter-timer Hypervisor Control register */
	SP_EL2,		/* EL2 Stack Pointer */
	CNTHP_CTL_EL2,
	CNTHP_CVAL_EL2,
	CNTHV_CTL_EL2,
	CNTHV_CVAL_EL2,

	NR_SYS_REGS	/* Nothing after this line! */
};

struct kvm_cpu_context {
	struct user_pt_regs regs;	/* sp = sp_el0 */

	u64	spsr_abt;
	u64	spsr_und;
	u64	spsr_irq;
	u64	spsr_fiq;

	struct user_fpsimd_state fp_regs;

	u64 sys_regs[NR_SYS_REGS];

	struct kvm_vcpu *__hyp_running_vcpu;
};

struct kvm_host_data {
	struct kvm_cpu_context host_ctxt;
};

struct kvm_host_psci_config {
	/* PSCI version used by host. */
	u32 version;

	/* Function IDs used by host if version is v0.1. */
	struct psci_0_1_function_ids function_ids_0_1;

	bool psci_0_1_cpu_suspend_implemented;
	bool psci_0_1_cpu_on_implemented;
	bool psci_0_1_cpu_off_implemented;
	bool psci_0_1_migrate_implemented;
};

extern struct kvm_host_psci_config kvm_nvhe_sym(kvm_host_psci_config);
#define kvm_host_psci_config CHOOSE_NVHE_SYM(kvm_host_psci_config)

extern s64 kvm_nvhe_sym(hyp_physvirt_offset);
#define hyp_physvirt_offset CHOOSE_NVHE_SYM(hyp_physvirt_offset)

extern u64 kvm_nvhe_sym(hyp_cpu_logical_map)[NR_CPUS];
#define hyp_cpu_logical_map CHOOSE_NVHE_SYM(hyp_cpu_logical_map)

struct vcpu_reset_state {
	unsigned long	pc;
	unsigned long	r0;
	bool		be;
	bool		reset;
};

struct kvm_vcpu_arch {
	struct kvm_cpu_context ctxt;

	/*
	 * Guest floating point state
	 *
	 * The architecture has two main floating point extensions,
	 * the original FPSIMD and SVE.  These have overlapping
	 * register views, with the FPSIMD V registers occupying the
	 * low 128 bits of the SVE Z registers.  When the core
	 * floating point code saves the register state of a task it
	 * records which view it saved in fp_type.
	 */
	void *sve_state;
	enum fp_type fp_type;
	unsigned int sve_max_vl;
	u64 svcr;

	/* Stage 2 paging state used by the hardware on next switch */
	struct kvm_s2_mmu *hw_mmu;

	/* Values of trap registers for the guest. */
	u64 hcr_el2;
	u64 mdcr_el2;
	u64 cptr_el2;

	/* Values of trap registers for the host before guest entry. */
	u64 mdcr_el2_host;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

	/* Ownership of the FP regs */
	enum {
		FP_STATE_FREE,
		FP_STATE_HOST_OWNED,
		FP_STATE_GUEST_OWNED,
	} fp_state;

	/* Configuration flags, set once and for all before the vcpu can run */
	u8 cflags;

	/* Input flags to the hypervisor code, potentially cleared after use */
	u8 iflags;

	/* State flags for kernel bookkeeping, unused by the hypervisor code */
	u8 sflags;

	/*
	 * Don't run the guest (internal implementation need).
	 *
	 * Contrary to the flags above, this is set/cleared outside of
	 * a vcpu context, and thus cannot be mixed with the flags
	 * themselves (or the flag accesses need to be made atomic).
	 */
	bool pause;

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

	struct user_fpsimd_state *host_fpsimd_state;	/* hyp VA */
	struct task_struct *parent_task;

	struct {
		/* {Break,watch}point registers */
		struct kvm_guest_debug_arch regs;
		/* Statistical profiling extension */
		u64 pmscr_el1;
		/* Self-hosted trace */
		u64 trfcr_el1;
	} host_debug_state;

	/* VGIC state */
	struct vgic_cpu vgic_cpu;
	struct arch_timer_cpu timer_cpu;
	struct kvm_pmu pmu;

	/*
	 * Guest registers we preserve during guest debugging.
	 *
	 * These shadow registers are updated by the kvm_handle_sys_reg
	 * trap handler if the guest accesses or updates them while we
	 * are using guest debug.
	 */
	struct {
		u32	mdscr_el1;
		bool	pstate_ss;
	} guest_debug_preserved;

	/* vcpu power state */
	struct kvm_mp_state mp_state;
	spinlock_t mp_state_lock;

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* Target CPU and feature flags */
	int target;
	DECLARE_BITMAP(features, KVM_VCPU_MAX_FEATURES);

	/* Virtual SError ESR to restore when HCR_EL2.VSE is set */
	u64 vsesr_el2;

	/* Additional reset state */
	struct vcpu_reset_state	reset_state;

	/* Guest PV state */
	struct {
		u64 last_steal;
		gpa_t base;
	} steal;

	/* Per-vcpu CCSIDR override or NULL */
	u32 *ccsidr;
};

/*
 * Each 'flag' is composed of a comma-separated triplet:
 *
 * - the flag-set it belongs to in the vcpu->arch structure
 * - the value for that flag
 * - the mask for that flag
 *
 *  __vcpu_single_flag() builds such a triplet for a single-bit flag.
 * unpack_vcpu_flag() extract the flag value from the triplet for
 * direct use outside of the flag accessors.
 */
#define __vcpu_single_flag(_set, _f)	_set, (_f), (_f)

#define __unpack_flag(_set, _f, _m)	_f
#define unpack_vcpu_flag(...)		__unpack_flag(__VA_ARGS__)

#define __build_check_flag(v, flagset, f, m)			\
	do {							\
		typeof(v->arch.flagset) *_fset;			\
								\
		/* Check that the flags fit in the mask */	\
		BUILD_BUG_ON(HWEIGHT(m) != HWEIGHT((f) | (m)));	\
		/* Check that the flags fit in the type */	\
		BUILD_BUG_ON((sizeof(*_fset) * 8) <= __fls(m));	\
	} while (0)

#define __vcpu_get_flag(v, flagset, f, m)			\
	({							\
		__build_check_flag(v, flagset, f, m);		\
								\
		READ_ONCE(v->arch.flagset) & (m);		\
	})

/*
 * Note that the set/clear accessors must be preempt-safe in order to
 * avoid nesting them with load/put which also manipulate flags...
 */
#ifdef __KVM_NVHE_HYPERVISOR__
/* the nVHE hypervisor is always non-preemptible */
#define __vcpu_flags_preempt_disable()
#define __vcpu_flags_preempt_enable()
#else
#define __vcpu_flags_preempt_disable()	preempt_disable()
#define __vcpu_flags_preempt_enable()	preempt_enable()
#endif

#define __vcpu_set_flag(v, flagset, f, m)			\
	do {							\
		typeof(v->arch.flagset) *fset;			\
								\
		__build_check_flag(v, flagset, f, m);		\
								\
		fset = &v->arch.flagset;			\
		__vcpu_flags_preempt_disable();			\
		if (HWEIGHT(m) > 1)				\
			*fset &= ~(m);				\
		*fset |= (f);					\
		__vcpu_flags_preempt_enable();			\
	} while (0)

#define __vcpu_clear_flag(v, flagset, f, m)			\
	do {							\
		typeof(v->arch.flagset) *fset;			\
								\
		__build_check_flag(v, flagset, f, m);		\
								\
		fset = &v->arch.flagset;			\
		__vcpu_flags_preempt_disable();			\
		*fset &= ~(m);					\
		__vcpu_flags_preempt_enable();			\
	} while (0)

#define vcpu_get_flag(v, ...)	__vcpu_get_flag((v), __VA_ARGS__)
#define vcpu_set_flag(v, ...)	__vcpu_set_flag((v), __VA_ARGS__)
#define vcpu_clear_flag(v, ...)	__vcpu_clear_flag((v), __VA_ARGS__)

/* SVE exposed to guest */
#define GUEST_HAS_SVE		__vcpu_single_flag(cflags, BIT(0))
/* SVE config completed */
#define VCPU_SVE_FINALIZED	__vcpu_single_flag(cflags, BIT(1))
/* PTRAUTH exposed to guest */
#define GUEST_HAS_PTRAUTH	__vcpu_single_flag(cflags, BIT(2))

/* Exception pending */
#define PENDING_EXCEPTION	__vcpu_single_flag(iflags, BIT(0))
/*
 * PC increment. Overlaps with EXCEPT_MASK on purpose so that it can't
 * be set together with an exception...
 */
#define INCREMENT_PC		__vcpu_single_flag(iflags, BIT(1))
/* Target EL/MODE (not a single flag, but let's abuse the macro) */
#define EXCEPT_MASK		__vcpu_single_flag(iflags, GENMASK(3, 1))

/* Helpers to encode exceptions with minimum fuss */
#define __EXCEPT_MASK_VAL	unpack_vcpu_flag(EXCEPT_MASK)
#define __EXCEPT_SHIFT		__builtin_ctzl(__EXCEPT_MASK_VAL)
#define __vcpu_except_flags(_f)	iflags, (_f << __EXCEPT_SHIFT), __EXCEPT_MASK_VAL

/*
 * When PENDING_EXCEPTION is set, EXCEPT_MASK can take the following
 * values:
 *
 * For AArch32 EL1:
 */
#define EXCEPT_AA32_UND		__vcpu_except_flags(0)
#define EXCEPT_AA32_IABT	__vcpu_except_flags(1)
#define EXCEPT_AA32_DABT	__vcpu_except_flags(2)
/* For AArch64: */
#define EXCEPT_AA64_EL1_SYNC	__vcpu_except_flags(0)
#define EXCEPT_AA64_EL1_IRQ	__vcpu_except_flags(1)
#define EXCEPT_AA64_EL1_FIQ	__vcpu_except_flags(2)
#define EXCEPT_AA64_EL1_SERR	__vcpu_except_flags(3)
/* For AArch64 with NV: */
#define EXCEPT_AA64_EL2_SYNC	__vcpu_except_flags(4)
#define EXCEPT_AA64_EL2_IRQ	__vcpu_except_flags(5)
#define EXCEPT_AA64_EL2_FIQ	__vcpu_except_flags(6)
#define EXCEPT_AA64_EL2_SERR	__vcpu_except_flags(7)
/* Guest debug is live */
#define DEBUG_DIRTY		__vcpu_single_flag(iflags, BIT(4))
/* Save SPE context if active  */
#define DEBUG_STATE_SAVE_SPE	__vcpu_single_flag(iflags, BIT(5))
/* Save TRBE context if active  */
#define DEBUG_STATE_SAVE_TRBE	__vcpu_single_flag(iflags, BIT(6))
/* vcpu running in HYP context */
#define VCPU_HYP_CONTEXT	__vcpu_single_flag(iflags, BIT(7))

/* SVE enabled for host EL0 */
#define HOST_SVE_ENABLED	__vcpu_single_flag(sflags, BIT(0))
/* SME enabled for EL0 */
#define HOST_SME_ENABLED	__vcpu_single_flag(sflags, BIT(1))
/* Physical CPU not in supported_cpus */
#define ON_UNSUPPORTED_CPU	__vcpu_single_flag(sflags, BIT(2))
/* WFIT instruction trapped */
#define IN_WFIT			__vcpu_single_flag(sflags, BIT(3))
/* vcpu system registers loaded on physical CPU */
#define SYSREGS_ON_CPU		__vcpu_single_flag(sflags, BIT(4))
/* Software step state is Active-pending */
#define DBG_SS_ACTIVE_PENDING	__vcpu_single_flag(sflags, BIT(5))


/* Pointer to the vcpu's SVE FFR for sve_{save,load}_state() */
#define vcpu_sve_pffr(vcpu) (kern_hyp_va((vcpu)->arch.sve_state) +	\
			     sve_ffr_offset((vcpu)->arch.sve_max_vl))

#define vcpu_sve_max_vq(vcpu)	sve_vq_from_vl((vcpu)->arch.sve_max_vl)

#define vcpu_sve_state_size(vcpu) ({					\
	size_t __size_ret;						\
	unsigned int __vcpu_vq;						\
									\
	if (WARN_ON(!sve_vl_valid((vcpu)->arch.sve_max_vl))) {		\
		__size_ret = 0;						\
	} else {							\
		__vcpu_vq = vcpu_sve_max_vq(vcpu);			\
		__size_ret = SVE_SIG_REGS_SIZE(__vcpu_vq);		\
	}								\
									\
	__size_ret;							\
})

#define KVM_GUESTDBG_VALID_MASK (KVM_GUESTDBG_ENABLE | \
				 KVM_GUESTDBG_USE_SW_BP | \
				 KVM_GUESTDBG_USE_HW | \
				 KVM_GUESTDBG_SINGLESTEP)

#define vcpu_has_sve(vcpu) (system_supports_sve() &&			\
			    vcpu_get_flag(vcpu, GUEST_HAS_SVE))

#ifdef CONFIG_ARM64_PTR_AUTH
#define vcpu_has_ptrauth(vcpu)						\
	((cpus_have_final_cap(ARM64_HAS_ADDRESS_AUTH) ||		\
	  cpus_have_final_cap(ARM64_HAS_GENERIC_AUTH)) &&		\
	  vcpu_get_flag(vcpu, GUEST_HAS_PTRAUTH))
#else
#define vcpu_has_ptrauth(vcpu)		false
#endif

#define vcpu_on_unsupported_cpu(vcpu)					\
	vcpu_get_flag(vcpu, ON_UNSUPPORTED_CPU)

#define vcpu_set_on_unsupported_cpu(vcpu)				\
	vcpu_set_flag(vcpu, ON_UNSUPPORTED_CPU)

#define vcpu_clear_on_unsupported_cpu(vcpu)				\
	vcpu_clear_flag(vcpu, ON_UNSUPPORTED_CPU)

#define vcpu_gp_regs(v)		(&(v)->arch.ctxt.regs)

/*
 * Only use __vcpu_sys_reg/ctxt_sys_reg if you know you want the
 * memory backed version of a register, and not the one most recently
 * accessed by a running VCPU.  For example, for userspace access or
 * for system registers that are never context switched, but only
 * emulated.
 */
#define __ctxt_sys_reg(c,r)	(&(c)->sys_regs[(r)])

#define ctxt_sys_reg(c,r)	(*__ctxt_sys_reg(c,r))

#define __vcpu_sys_reg(v,r)	(ctxt_sys_reg(&(v)->arch.ctxt, (r)))

u64 vcpu_read_sys_reg(const struct kvm_vcpu *vcpu, int reg);
void vcpu_write_sys_reg(struct kvm_vcpu *vcpu, u64 val, int reg);

static inline bool __vcpu_read_sys_reg_from_cpu(int reg, u64 *val)
{
	/*
	 * *** VHE ONLY ***
	 *
	 * System registers listed in the switch are not saved on every
	 * exit from the guest but are only saved on vcpu_put.
	 *
	 * Note that MPIDR_EL1 for the guest is set by KVM via VMPIDR_EL2 but
	 * should never be listed below, because the guest cannot modify its
	 * own MPIDR_EL1 and MPIDR_EL1 is accessed for VCPU A from VCPU B's
	 * thread when emulating cross-VCPU communication.
	 */
	if (!has_vhe())
		return false;

	switch (reg) {
	case SCTLR_EL1:		*val = read_sysreg_s(SYS_SCTLR_EL12);	break;
	case CPACR_EL1:		*val = read_sysreg_s(SYS_CPACR_EL12);	break;
	case TTBR0_EL1:		*val = read_sysreg_s(SYS_TTBR0_EL12);	break;
	case TTBR1_EL1:		*val = read_sysreg_s(SYS_TTBR1_EL12);	break;
	case TCR_EL1:		*val = read_sysreg_s(SYS_TCR_EL12);	break;
	case ESR_EL1:		*val = read_sysreg_s(SYS_ESR_EL12);	break;
	case AFSR0_EL1:		*val = read_sysreg_s(SYS_AFSR0_EL12);	break;
	case AFSR1_EL1:		*val = read_sysreg_s(SYS_AFSR1_EL12);	break;
	case FAR_EL1:		*val = read_sysreg_s(SYS_FAR_EL12);	break;
	case MAIR_EL1:		*val = read_sysreg_s(SYS_MAIR_EL12);	break;
	case VBAR_EL1:		*val = read_sysreg_s(SYS_VBAR_EL12);	break;
	case CONTEXTIDR_EL1:	*val = read_sysreg_s(SYS_CONTEXTIDR_EL12);break;
	case TPIDR_EL0:		*val = read_sysreg_s(SYS_TPIDR_EL0);	break;
	case TPIDRRO_EL0:	*val = read_sysreg_s(SYS_TPIDRRO_EL0);	break;
	case TPIDR_EL1:		*val = read_sysreg_s(SYS_TPIDR_EL1);	break;
	case AMAIR_EL1:		*val = read_sysreg_s(SYS_AMAIR_EL12);	break;
	case CNTKCTL_EL1:	*val = read_sysreg_s(SYS_CNTKCTL_EL12);	break;
	case ELR_EL1:		*val = read_sysreg_s(SYS_ELR_EL12);	break;
	case PAR_EL1:		*val = read_sysreg_par();		break;
	case DACR32_EL2:	*val = read_sysreg_s(SYS_DACR32_EL2);	break;
	case IFSR32_EL2:	*val = read_sysreg_s(SYS_IFSR32_EL2);	break;
	case DBGVCR32_EL2:	*val = read_sysreg_s(SYS_DBGVCR32_EL2);	break;
	default:		return false;
	}

	return true;
}

static inline bool __vcpu_write_sys_reg_to_cpu(u64 val, int reg)
{
	/*
	 * *** VHE ONLY ***
	 *
	 * System registers listed in the switch are not restored on every
	 * entry to the guest but are only restored on vcpu_load.
	 *
	 * Note that MPIDR_EL1 for the guest is set by KVM via VMPIDR_EL2 but
	 * should never be listed below, because the MPIDR should only be set
	 * once, before running the VCPU, and never changed later.
	 */
	if (!has_vhe())
		return false;

	switch (reg) {
	case SCTLR_EL1:		write_sysreg_s(val, SYS_SCTLR_EL12);	break;
	case CPACR_EL1:		write_sysreg_s(val, SYS_CPACR_EL12);	break;
	case TTBR0_EL1:		write_sysreg_s(val, SYS_TTBR0_EL12);	break;
	case TTBR1_EL1:		write_sysreg_s(val, SYS_TTBR1_EL12);	break;
	case TCR_EL1:		write_sysreg_s(val, SYS_TCR_EL12);	break;
	case ESR_EL1:		write_sysreg_s(val, SYS_ESR_EL12);	break;
	case AFSR0_EL1:		write_sysreg_s(val, SYS_AFSR0_EL12);	break;
	case AFSR1_EL1:		write_sysreg_s(val, SYS_AFSR1_EL12);	break;
	case FAR_EL1:		write_sysreg_s(val, SYS_FAR_EL12);	break;
	case MAIR_EL1:		write_sysreg_s(val, SYS_MAIR_EL12);	break;
	case VBAR_EL1:		write_sysreg_s(val, SYS_VBAR_EL12);	break;
	case CONTEXTIDR_EL1:	write_sysreg_s(val, SYS_CONTEXTIDR_EL12);break;
	case TPIDR_EL0:		write_sysreg_s(val, SYS_TPIDR_EL0);	break;
	case TPIDRRO_EL0:	write_sysreg_s(val, SYS_TPIDRRO_EL0);	break;
	case TPIDR_EL1:		write_sysreg_s(val, SYS_TPIDR_EL1);	break;
	case AMAIR_EL1:		write_sysreg_s(val, SYS_AMAIR_EL12);	break;
	case CNTKCTL_EL1:	write_sysreg_s(val, SYS_CNTKCTL_EL12);	break;
	case ELR_EL1:		write_sysreg_s(val, SYS_ELR_EL12);	break;
	case PAR_EL1:		write_sysreg_s(val, SYS_PAR_EL1);	break;
	case DACR32_EL2:	write_sysreg_s(val, SYS_DACR32_EL2);	break;
	case IFSR32_EL2:	write_sysreg_s(val, SYS_IFSR32_EL2);	break;
	case DBGVCR32_EL2:	write_sysreg_s(val, SYS_DBGVCR32_EL2);	break;
	default:		return false;
	}

	return true;
}

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 hvc_exit_stat;
	u64 wfe_exit_stat;
	u64 wfi_exit_stat;
	u64 mmio_exit_user;
	u64 mmio_exit_kernel;
	u64 signal_exits;
	u64 exits;
};

void kvm_vcpu_preferred_target(struct kvm_vcpu_init *init);
unsigned long kvm_arm_num_regs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_reg_indices(struct kvm_vcpu *vcpu, u64 __user *indices);
int kvm_arm_get_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);
int kvm_arm_set_reg(struct kvm_vcpu *vcpu, const struct kvm_one_reg *reg);

unsigned long kvm_arm_num_sys_reg_descs(struct kvm_vcpu *vcpu);
int kvm_arm_copy_sys_reg_indices(struct kvm_vcpu *vcpu, u64 __user *uindices);

int __kvm_arm_vcpu_get_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

int __kvm_arm_vcpu_set_events(struct kvm_vcpu *vcpu,
			      struct kvm_vcpu_events *events);

#define KVM_ARCH_WANT_MMU_NOTIFIER

void kvm_arm_halt_guest(struct kvm *kvm);
void kvm_arm_resume_guest(struct kvm *kvm);

#define vcpu_has_run_once(vcpu)	!!rcu_access_pointer((vcpu)->pid)

#ifndef __KVM_NVHE_HYPERVISOR__
#define kvm_call_hyp_nvhe(f, ...)						\
	({								\
		struct arm_smccc_res res;				\
									\
		arm_smccc_1_1_hvc(KVM_HOST_SMCCC_FUNC(f),		\
				  ##__VA_ARGS__, &res);			\
		WARN_ON(res.a0 != SMCCC_RET_SUCCESS);			\
									\
		res.a1;							\
	})

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
			kvm_call_hyp_nvhe(f, ##__VA_ARGS__);		\
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
			ret = kvm_call_hyp_nvhe(f, ##__VA_ARGS__);	\
		}							\
									\
		ret;							\
	})
#else /* __KVM_NVHE_HYPERVISOR__ */
#define kvm_call_hyp(f, ...) f(__VA_ARGS__)
#define kvm_call_hyp_ret(f, ...) f(__VA_ARGS__)
#define kvm_call_hyp_nvhe(f, ...) f(__VA_ARGS__)
#endif /* __KVM_NVHE_HYPERVISOR__ */

void force_vm_exit(const cpumask_t *mask);

int handle_exit(struct kvm_vcpu *vcpu, int exception_index);
void handle_exit_early(struct kvm_vcpu *vcpu, int exception_index);

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu);
int kvm_handle_cp14_32(struct kvm_vcpu *vcpu);
int kvm_handle_cp14_64(struct kvm_vcpu *vcpu);
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu);
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu);
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu);
int kvm_handle_cp10_id(struct kvm_vcpu *vcpu);

void kvm_reset_sys_regs(struct kvm_vcpu *vcpu);

int __init kvm_sys_reg_table_init(void);

bool lock_all_vcpus(struct kvm *kvm);
void unlock_all_vcpus(struct kvm *kvm);

/* MMIO helpers */
void kvm_mmio_write_buf(void *buf, unsigned int len, unsigned long data);
unsigned long kvm_mmio_read_buf(const void *buf, unsigned int len);

int kvm_handle_mmio_return(struct kvm_vcpu *vcpu);
int io_mem_abort(struct kvm_vcpu *vcpu, phys_addr_t fault_ipa);

/*
 * Returns true if a Performance Monitoring Interrupt (PMI), a.k.a. perf event,
 * arrived in guest context.  For arm64, any event that arrives while a vCPU is
 * loaded is considered to be "in guest".
 */
static inline bool kvm_arch_pmi_in_guest(struct kvm_vcpu *vcpu)
{
	return IS_ENABLED(CONFIG_GUEST_PERF_EVENTS) && !!vcpu;
}

long kvm_hypercall_pv_features(struct kvm_vcpu *vcpu);
gpa_t kvm_init_stolen_time(struct kvm_vcpu *vcpu);
void kvm_update_stolen_time(struct kvm_vcpu *vcpu);

bool kvm_arm_pvtime_supported(void);
int kvm_arm_pvtime_set_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);
int kvm_arm_pvtime_get_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);
int kvm_arm_pvtime_has_attr(struct kvm_vcpu *vcpu,
			    struct kvm_device_attr *attr);

extern unsigned int __ro_after_init kvm_arm_vmid_bits;
int __init kvm_arm_vmid_alloc_init(void);
void __init kvm_arm_vmid_alloc_free(void);
void kvm_arm_vmid_update(struct kvm_vmid *kvm_vmid);
void kvm_arm_vmid_clear_active(void);

static inline void kvm_arm_pvtime_vcpu_init(struct kvm_vcpu_arch *vcpu_arch)
{
	vcpu_arch->steal.base = INVALID_GPA;
}

static inline bool kvm_arm_is_pvtime_enabled(struct kvm_vcpu_arch *vcpu_arch)
{
	return (vcpu_arch->steal.base != INVALID_GPA);
}

void kvm_set_sei_esr(struct kvm_vcpu *vcpu, u64 syndrome);

struct kvm_vcpu *kvm_mpidr_to_vcpu(struct kvm *kvm, unsigned long mpidr);

DECLARE_KVM_HYP_PER_CPU(struct kvm_host_data, kvm_host_data);

static inline void kvm_init_host_cpu_context(struct kvm_cpu_context *cpu_ctxt)
{
	/* The host's MPIDR is immutable, so let's set it up at boot time */
	ctxt_sys_reg(cpu_ctxt, MPIDR_EL1) = read_cpuid_mpidr();
}

static inline bool kvm_system_needs_idmapped_vectors(void)
{
	return cpus_have_const_cap(ARM64_SPECTRE_V3A);
}

void kvm_arm_vcpu_ptrauth_trap(struct kvm_vcpu *vcpu);

static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_sched_in(struct kvm_vcpu *vcpu, int cpu) {}

void kvm_arm_init_debug(void);
void kvm_arm_vcpu_init_debug(struct kvm_vcpu *vcpu);
void kvm_arm_setup_debug(struct kvm_vcpu *vcpu);
void kvm_arm_clear_debug(struct kvm_vcpu *vcpu);
void kvm_arm_reset_debug_ptr(struct kvm_vcpu *vcpu);

#define kvm_vcpu_os_lock_enabled(vcpu)		\
	(!!(__vcpu_sys_reg(vcpu, OSLSR_EL1) & OSLSR_EL1_OSLK))

int kvm_arm_vcpu_arch_set_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_get_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);
int kvm_arm_vcpu_arch_has_attr(struct kvm_vcpu *vcpu,
			       struct kvm_device_attr *attr);

int kvm_vm_ioctl_mte_copy_tags(struct kvm *kvm,
			       struct kvm_arm_copy_mte_tags *copy_tags);
int kvm_vm_ioctl_set_counter_offset(struct kvm *kvm,
				    struct kvm_arm_counter_offset *offset);

/* Guest/host FPSIMD coordination helpers */
int kvm_arch_vcpu_run_map_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_ctxflush_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_ctxsync_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_put_fp(struct kvm_vcpu *vcpu);
void kvm_vcpu_unshare_task_fp(struct kvm_vcpu *vcpu);

static inline bool kvm_pmu_counter_deferred(struct perf_event_attr *attr)
{
	return (!has_vhe() && attr->exclude_host);
}

/* Flags for host debug state */
void kvm_arch_vcpu_load_debug_state_flags(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_put_debug_state_flags(struct kvm_vcpu *vcpu);

#ifdef CONFIG_KVM
void kvm_set_pmu_events(u32 set, struct perf_event_attr *attr);
void kvm_clr_pmu_events(u32 clr);
#else
static inline void kvm_set_pmu_events(u32 set, struct perf_event_attr *attr) {}
static inline void kvm_clr_pmu_events(u32 clr) {}
#endif

void kvm_vcpu_load_sysregs_vhe(struct kvm_vcpu *vcpu);
void kvm_vcpu_put_sysregs_vhe(struct kvm_vcpu *vcpu);

int __init kvm_set_ipa_limit(void);

#define __KVM_HAVE_ARCH_VM_ALLOC
struct kvm *kvm_arch_alloc_vm(void);

static inline bool kvm_vm_is_protected(struct kvm *kvm)
{
	return false;
}

void kvm_init_protected_traps(struct kvm_vcpu *vcpu);

int kvm_arm_vcpu_finalize(struct kvm_vcpu *vcpu, int feature);
bool kvm_arm_vcpu_is_finalized(struct kvm_vcpu *vcpu);

#define kvm_arm_vcpu_sve_finalized(vcpu) vcpu_get_flag(vcpu, VCPU_SVE_FINALIZED)

#define kvm_has_mte(kvm)					\
	(system_supports_mte() &&				\
	 test_bit(KVM_ARCH_FLAG_MTE_ENABLED, &(kvm)->arch.flags))

#define kvm_supports_32bit_el0()				\
	(system_supports_32bit_el0() &&				\
	 !static_branch_unlikely(&arm64_mismatched_32bit_el0))

#define kvm_vm_has_ran_once(kvm)					\
	(test_bit(KVM_ARCH_FLAG_HAS_RAN_ONCE, &(kvm)->arch.flags))

int kvm_trng_call(struct kvm_vcpu *vcpu);
#ifdef CONFIG_KVM
extern phys_addr_t hyp_mem_base;
extern phys_addr_t hyp_mem_size;
void __init kvm_hyp_reserve(void);
#else
static inline void kvm_hyp_reserve(void) { }
#endif

void kvm_arm_vcpu_power_off(struct kvm_vcpu *vcpu);
bool kvm_arm_vcpu_stopped(struct kvm_vcpu *vcpu);

#endif /* __ARM64_KVM_HOST_H__ */
