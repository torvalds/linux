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
#include <asm/vncr_mapping.h>

#define __KVM_HAVE_ARCH_INTC_INITIALIZED

#define KVM_HALT_POLL_NS_DEFAULT 500000

#include <kvm/arm_vgic.h>
#include <kvm/arm_arch_timer.h>
#include <kvm/arm_pmu.h>

#define KVM_MAX_VCPUS VGIC_V3_MAX_CPUS

#define KVM_VCPU_MAX_FEATURES 7
#define KVM_VCPU_VALID_FEATURES	(BIT(KVM_VCPU_MAX_FEATURES) - 1)

#define KVM_REQ_SLEEP \
	KVM_ARCH_REQ_FLAGS(0, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_IRQ_PENDING		KVM_ARCH_REQ(1)
#define KVM_REQ_VCPU_RESET		KVM_ARCH_REQ(2)
#define KVM_REQ_RECORD_STEAL		KVM_ARCH_REQ(3)
#define KVM_REQ_RELOAD_GICv4		KVM_ARCH_REQ(4)
#define KVM_REQ_RELOAD_PMU		KVM_ARCH_REQ(5)
#define KVM_REQ_SUSPEND			KVM_ARCH_REQ(6)
#define KVM_REQ_RESYNC_PMU_EL0		KVM_ARCH_REQ(7)
#define KVM_REQ_NESTED_S2_UNMAP		KVM_ARCH_REQ(8)
#define KVM_REQ_GUEST_HYP_IRQ_PENDING	KVM_ARCH_REQ(9)

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

extern unsigned int __ro_after_init kvm_sve_max_vl;
extern unsigned int __ro_after_init kvm_host_sve_max_vl;
int __init kvm_arm_init_sve(void);

u32 __attribute_const__ kvm_target_cpu(void);
void kvm_reset_vcpu(struct kvm_vcpu *vcpu);
void kvm_arm_vcpu_destroy(struct kvm_vcpu *vcpu);

struct kvm_hyp_memcache {
	phys_addr_t head;
	unsigned long nr_pages;
	struct pkvm_mapping *mapping; /* only used from EL1 */

#define	HYP_MEMCACHE_ACCOUNT_STAGE2	BIT(1)
	unsigned long flags;
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
	phys_addr_t *p = to_va(mc->head & PAGE_MASK);

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

	/*
	 * VTCR value used on the host. For a non-NV guest (or a NV
	 * guest that runs in a context where its own S2 doesn't
	 * apply), its T0SZ value reflects that of the IPA size.
	 *
	 * For a shadow S2 MMU, T0SZ reflects the PARange exposed to
	 * the guest.
	 */
	u64	vtcr;

	/* The last vcpu id that ran on each physical CPU */
	int __percpu *last_vcpu_ran;

#define KVM_ARM_EAGER_SPLIT_CHUNK_SIZE_DEFAULT 0
	/*
	 * Memory cache used to split
	 * KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE worth of huge pages. It
	 * is used to allocate stage2 page tables while splitting huge
	 * pages. The choice of KVM_CAP_ARM_EAGER_SPLIT_CHUNK_SIZE
	 * influences both the capacity of the split page cache, and
	 * how often KVM reschedules. Be wary of raising CHUNK_SIZE
	 * too high.
	 *
	 * Protected by kvm->slots_lock.
	 */
	struct kvm_mmu_memory_cache split_page_cache;
	uint64_t split_page_chunk_size;

	struct kvm_arch *arch;

	/*
	 * For a shadow stage-2 MMU, the virtual vttbr used by the
	 * host to parse the guest S2.
	 * This either contains:
	 * - the virtual VTTBR programmed by the guest hypervisor with
         *   CnP cleared
	 * - The value 1 (VMID=0, BADDR=0, CnP=1) if invalid
	 *
	 * We also cache the full VTCR which gets used for TLB invalidation,
	 * taking the ARM ARM's "Any of the bits in VTCR_EL2 are permitted
	 * to be cached in a TLB" to the letter.
	 */
	u64	tlb_vttbr;
	u64	tlb_vtcr;

	/*
	 * true when this represents a nested context where virtual
	 * HCR_EL2.VM == 1
	 */
	bool	nested_stage2_enabled;

	/*
	 * true when this MMU needs to be unmapped before being used for a new
	 * purpose.
	 */
	bool	pending_unmap;

	/*
	 *  0: Nobody is currently using this, check vttbr for validity
	 * >0: Somebody is actively using this.
	 */
	atomic_t refcnt;
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
	unsigned long vendor_hyp_bmap; /* Function numbers 0-63 */
	unsigned long vendor_hyp_bmap_2; /* Function numbers 64-127 */
};

typedef unsigned int pkvm_handle_t;

struct kvm_protected_vm {
	pkvm_handle_t handle;
	struct kvm_hyp_memcache teardown_mc;
	struct kvm_hyp_memcache stage2_teardown_mc;
	bool enabled;
};

struct kvm_mpidr_data {
	u64			mpidr_mask;
	DECLARE_FLEX_ARRAY(u16, cmpidr_to_idx);
};

static inline u16 kvm_mpidr_index(struct kvm_mpidr_data *data, u64 mpidr)
{
	unsigned long index = 0, mask = data->mpidr_mask;
	unsigned long aff = mpidr & MPIDR_HWID_BITMASK;

	bitmap_gather(&index, &aff, &mask, fls(mask));

	return index;
}

struct kvm_sysreg_masks;

enum fgt_group_id {
	__NO_FGT_GROUP__,
	HFGxTR_GROUP,
	HDFGRTR_GROUP,
	HDFGWTR_GROUP = HDFGRTR_GROUP,
	HFGITR_GROUP,
	HAFGRTR_GROUP,

	/* Must be last */
	__NR_FGT_GROUP_IDS__
};

struct kvm_arch {
	struct kvm_s2_mmu mmu;

	/*
	 * Fine-Grained UNDEF, mimicking the FGT layout defined by the
	 * architecture. We track them globally, as we present the
	 * same feature-set to all vcpus.
	 *
	 * Index 0 is currently spare.
	 */
	u64 fgu[__NR_FGT_GROUP_IDS__];

	/*
	 * Stage 2 paging state for VMs with nested S2 using a virtual
	 * VMID.
	 */
	struct kvm_s2_mmu *nested_mmus;
	size_t nested_mmus_size;
	int nested_mmus_next;

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
	/* The vCPU feature set for the VM is configured */
#define KVM_ARCH_FLAG_VCPU_FEATURES_CONFIGURED		3
	/* PSCI SYSTEM_SUSPEND enabled for the guest */
#define KVM_ARCH_FLAG_SYSTEM_SUSPEND_ENABLED		4
	/* VM counter offset */
#define KVM_ARCH_FLAG_VM_COUNTER_OFFSET			5
	/* Timer PPIs made immutable */
#define KVM_ARCH_FLAG_TIMER_PPIS_IMMUTABLE		6
	/* Initial ID reg values loaded */
#define KVM_ARCH_FLAG_ID_REGS_INITIALIZED		7
	/* Fine-Grained UNDEF initialised */
#define KVM_ARCH_FLAG_FGU_INITIALIZED			8
	/* SVE exposed to guest */
#define KVM_ARCH_FLAG_GUEST_HAS_SVE			9
	/* MIDR_EL1, REVIDR_EL1, and AIDR_EL1 are writable from userspace */
#define KVM_ARCH_FLAG_WRITABLE_IMP_ID_REGS		10
	unsigned long flags;

	/* VM-wide vCPU feature set */
	DECLARE_BITMAP(vcpu_features, KVM_VCPU_MAX_FEATURES);

	/* MPIDR to vcpu index mapping, optional */
	struct kvm_mpidr_data *mpidr_data;

	/*
	 * VM-wide PMU filter, implemented as a bitmap and big enough for
	 * up to 2^10 events (ARMv8.0) or 2^16 events (ARMv8.1+).
	 */
	unsigned long *pmu_filter;
	struct arm_pmu *arm_pmu;

	cpumask_var_t supported_cpus;

	/* PMCR_EL0.N value for the guest */
	u8 pmcr_n;

	/* Iterator for idreg debugfs */
	u8	idreg_debugfs_iter;

	/* Hypercall features firmware registers' descriptor */
	struct kvm_smccc_features smccc_feat;
	struct maple_tree smccc_filter;

	/*
	 * Emulated CPU ID registers per VM
	 * (Op0, Op1, CRn, CRm, Op2) of the ID registers to be saved in it
	 * is (3, 0, 0, crm, op2), where 1<=crm<8, 0<=op2<8.
	 *
	 * These emulated idregs are VM-wide, but accessed from the context of a vCPU.
	 * Atomic access to multiple idregs are guarded by kvm_arch.config_lock.
	 */
#define IDREG_IDX(id)		(((sys_reg_CRm(id) - 1) << 3) | sys_reg_Op2(id))
#define KVM_ARM_ID_REG_NUM	(IDREG_IDX(sys_reg(3, 0, 0, 7, 7)) + 1)
	u64 id_regs[KVM_ARM_ID_REG_NUM];

	u64 midr_el1;
	u64 revidr_el1;
	u64 aidr_el1;
	u64 ctr_el0;

	/* Masks for VNCR-backed and general EL2 sysregs */
	struct kvm_sysreg_masks	*sysreg_masks;

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

/*
 * VNCR() just places the VNCR_capable registers in the enum after
 * __VNCR_START__, and the value (after correction) to be an 8-byte offset
 * from the VNCR base. As we don't require the enum to be otherwise ordered,
 * we need the terrible hack below to ensure that we correctly size the
 * sys_regs array, no matter what.
 *
 * The __MAX__ macro has been lifted from Sean Eron Anderson's wonderful
 * treasure trove of bit hacks:
 * https://graphics.stanford.edu/~seander/bithacks.html#IntegerMinOrMax
 */
#define __MAX__(x,y)	((x) ^ (((x) ^ (y)) & -((x) < (y))))
#define VNCR(r)						\
	__before_##r,					\
	r = __VNCR_START__ + ((VNCR_ ## r) / 8),	\
	__after_##r = __MAX__(__before_##r - 1, r)

#define MARKER(m)				\
	m, __after_##m = m - 1

enum vcpu_sysreg {
	__INVALID_SYSREG__,   /* 0 is reserved as an invalid value */
	MPIDR_EL1,	/* MultiProcessor Affinity Register */
	CLIDR_EL1,	/* Cache Level ID Register */
	CSSELR_EL1,	/* Cache Size Selection Register */
	TPIDR_EL0,	/* Thread ID, User R/W */
	TPIDRRO_EL0,	/* Thread ID, User R/O */
	TPIDR_EL1,	/* Thread ID, Privileged */
	CNTKCTL_EL1,	/* Timer Control Register (EL1) */
	PAR_EL1,	/* Physical Address Register */
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

	/* Memory Tagging Extension registers */
	RGSR_EL1,	/* Random Allocation Tag Seed Register */
	GCR_EL1,	/* Tag Control Register */
	TFSRE0_EL1,	/* Tag Fault Status Register (EL0) */

	POR_EL0,	/* Permission Overlay Register 0 (EL0) */

	/* FP/SIMD/SVE */
	SVCR,
	FPMR,

	/* 32bit specific registers. */
	DACR32_EL2,	/* Domain Access Control Register */
	IFSR32_EL2,	/* Instruction Fault Status Register */
	FPEXC32_EL2,	/* Floating-Point Exception Control Register */
	DBGVCR32_EL2,	/* Debug Vector Catch Register */

	/* EL2 registers */
	SCTLR_EL2,	/* System Control Register (EL2) */
	ACTLR_EL2,	/* Auxiliary Control Register (EL2) */
	CPTR_EL2,	/* Architectural Feature Trap Register (EL2) */
	HACR_EL2,	/* Hypervisor Auxiliary Control Register */
	ZCR_EL2,	/* SVE Control Register (EL2) */
	TTBR0_EL2,	/* Translation Table Base Register 0 (EL2) */
	TTBR1_EL2,	/* Translation Table Base Register 1 (EL2) */
	TCR_EL2,	/* Translation Control Register (EL2) */
	PIRE0_EL2,	/* Permission Indirection Register 0 (EL2) */
	PIR_EL2,	/* Permission Indirection Register 1 (EL2) */
	POR_EL2,	/* Permission Overlay Register 2 (EL2) */
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
	SP_EL2,		/* EL2 Stack Pointer */
	CNTHP_CTL_EL2,
	CNTHP_CVAL_EL2,
	CNTHV_CTL_EL2,
	CNTHV_CVAL_EL2,

	/* Anything from this can be RES0/RES1 sanitised */
	MARKER(__SANITISED_REG_START__),
	TCR2_EL2,	/* Extended Translation Control Register (EL2) */
	MDCR_EL2,	/* Monitor Debug Configuration Register (EL2) */
	CNTHCTL_EL2,	/* Counter-timer Hypervisor Control register */

	/* Any VNCR-capable reg goes after this point */
	MARKER(__VNCR_START__),

	VNCR(SCTLR_EL1),/* System Control Register */
	VNCR(ACTLR_EL1),/* Auxiliary Control Register */
	VNCR(CPACR_EL1),/* Coprocessor Access Control */
	VNCR(ZCR_EL1),	/* SVE Control */
	VNCR(TTBR0_EL1),/* Translation Table Base Register 0 */
	VNCR(TTBR1_EL1),/* Translation Table Base Register 1 */
	VNCR(TCR_EL1),	/* Translation Control Register */
	VNCR(TCR2_EL1),	/* Extended Translation Control Register */
	VNCR(ESR_EL1),	/* Exception Syndrome Register */
	VNCR(AFSR0_EL1),/* Auxiliary Fault Status Register 0 */
	VNCR(AFSR1_EL1),/* Auxiliary Fault Status Register 1 */
	VNCR(FAR_EL1),	/* Fault Address Register */
	VNCR(MAIR_EL1),	/* Memory Attribute Indirection Register */
	VNCR(VBAR_EL1),	/* Vector Base Address Register */
	VNCR(CONTEXTIDR_EL1),	/* Context ID Register */
	VNCR(AMAIR_EL1),/* Aux Memory Attribute Indirection Register */
	VNCR(MDSCR_EL1),/* Monitor Debug System Control Register */
	VNCR(ELR_EL1),
	VNCR(SP_EL1),
	VNCR(SPSR_EL1),
	VNCR(TFSR_EL1),	/* Tag Fault Status Register (EL1) */
	VNCR(VPIDR_EL2),/* Virtualization Processor ID Register */
	VNCR(VMPIDR_EL2),/* Virtualization Multiprocessor ID Register */
	VNCR(HCR_EL2),	/* Hypervisor Configuration Register */
	VNCR(HSTR_EL2),	/* Hypervisor System Trap Register */
	VNCR(VTTBR_EL2),/* Virtualization Translation Table Base Register */
	VNCR(VTCR_EL2),	/* Virtualization Translation Control Register */
	VNCR(TPIDR_EL2),/* EL2 Software Thread ID Register */
	VNCR(HCRX_EL2),	/* Extended Hypervisor Configuration Register */

	/* Permission Indirection Extension registers */
	VNCR(PIR_EL1),	 /* Permission Indirection Register 1 (EL1) */
	VNCR(PIRE0_EL1), /*  Permission Indirection Register 0 (EL1) */

	VNCR(POR_EL1),	/* Permission Overlay Register 1 (EL1) */

	VNCR(HFGRTR_EL2),
	VNCR(HFGWTR_EL2),
	VNCR(HFGITR_EL2),
	VNCR(HDFGRTR_EL2),
	VNCR(HDFGWTR_EL2),
	VNCR(HAFGRTR_EL2),

	VNCR(CNTVOFF_EL2),
	VNCR(CNTV_CVAL_EL0),
	VNCR(CNTV_CTL_EL0),
	VNCR(CNTP_CVAL_EL0),
	VNCR(CNTP_CTL_EL0),

	VNCR(ICH_LR0_EL2),
	VNCR(ICH_LR1_EL2),
	VNCR(ICH_LR2_EL2),
	VNCR(ICH_LR3_EL2),
	VNCR(ICH_LR4_EL2),
	VNCR(ICH_LR5_EL2),
	VNCR(ICH_LR6_EL2),
	VNCR(ICH_LR7_EL2),
	VNCR(ICH_LR8_EL2),
	VNCR(ICH_LR9_EL2),
	VNCR(ICH_LR10_EL2),
	VNCR(ICH_LR11_EL2),
	VNCR(ICH_LR12_EL2),
	VNCR(ICH_LR13_EL2),
	VNCR(ICH_LR14_EL2),
	VNCR(ICH_LR15_EL2),

	VNCR(ICH_AP0R0_EL2),
	VNCR(ICH_AP0R1_EL2),
	VNCR(ICH_AP0R2_EL2),
	VNCR(ICH_AP0R3_EL2),
	VNCR(ICH_AP1R0_EL2),
	VNCR(ICH_AP1R1_EL2),
	VNCR(ICH_AP1R2_EL2),
	VNCR(ICH_AP1R3_EL2),
	VNCR(ICH_HCR_EL2),
	VNCR(ICH_VMCR_EL2),

	NR_SYS_REGS	/* Nothing after this line! */
};

struct kvm_sysreg_masks {
	struct {
		u64	res0;
		u64	res1;
	} mask[NR_SYS_REGS - __SANITISED_REG_START__];
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

	/* This pointer has to be 4kB aligned. */
	u64 *vncr_array;
};

struct cpu_sve_state {
	__u64 zcr_el1;

	/*
	 * Ordering is important since __sve_save_state/__sve_restore_state
	 * relies on it.
	 */
	__u32 fpsr;
	__u32 fpcr;

	/* Must be SVE_VQ_BYTES (128 bit) aligned. */
	__u8 sve_regs[];
};

/*
 * This structure is instantiated on a per-CPU basis, and contains
 * data that is:
 *
 * - tied to a single physical CPU, and
 * - either have a lifetime that does not extend past vcpu_put()
 * - or is an invariant for the lifetime of the system
 *
 * Use host_data_ptr(field) as a way to access a pointer to such a
 * field.
 */
struct kvm_host_data {
#define KVM_HOST_DATA_FLAG_HAS_SPE			0
#define KVM_HOST_DATA_FLAG_HAS_TRBE			1
#define KVM_HOST_DATA_FLAG_TRBE_ENABLED			4
#define KVM_HOST_DATA_FLAG_EL1_TRACING_CONFIGURED	5
	unsigned long flags;

	struct kvm_cpu_context host_ctxt;

	/*
	 * Hyp VA.
	 * sve_state is only used in pKVM and if system_supports_sve().
	 */
	struct cpu_sve_state *sve_state;

	/* Used by pKVM only. */
	u64	fpmr;

	/* Ownership of the FP regs */
	enum {
		FP_STATE_FREE,
		FP_STATE_HOST_OWNED,
		FP_STATE_GUEST_OWNED,
	} fp_owner;

	/*
	 * host_debug_state contains the host registers which are
	 * saved and restored during world switches.
	 */
	struct {
		/* {Break,watch}point registers */
		struct kvm_guest_debug_arch regs;
		/* Statistical profiling extension */
		u64 pmscr_el1;
		/* Self-hosted trace */
		u64 trfcr_el1;
		/* Values of trap registers for the host before guest entry. */
		u64 mdcr_el2;
	} host_debug_state;

	/* Guest trace filter value */
	u64 trfcr_while_in_guest;

	/* Number of programmable event counters (PMCR_EL0.N) for this CPU */
	unsigned int nr_event_counters;

	/* Number of debug breakpoints/watchpoints for this CPU (minus 1) */
	unsigned int debug_brps;
	unsigned int debug_wrps;
};

struct kvm_host_psci_config {
	/* PSCI version used by host. */
	u32 version;
	u32 smccc_version;

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

	/* Stage 2 paging state used by the hardware on next switch */
	struct kvm_s2_mmu *hw_mmu;

	/* Values of trap registers for the guest. */
	u64 hcr_el2;
	u64 hcrx_el2;
	u64 mdcr_el2;

	/* Exception Information */
	struct kvm_vcpu_fault_info fault;

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
	 * registers of the vcpu as the guest sees them.
	 *
	 * external_debug_state contains the debug values we want to debug the
	 * guest. This is set via the KVM_SET_GUEST_DEBUG ioctl.
	 */
	struct kvm_guest_debug_arch vcpu_debug_state;
	struct kvm_guest_debug_arch external_debug_state;
	u64 external_mdscr_el1;

	enum {
		VCPU_DEBUG_FREE,
		VCPU_DEBUG_HOST_OWNED,
		VCPU_DEBUG_GUEST_OWNED,
	} debug_owner;

	/* VGIC state */
	struct vgic_cpu vgic_cpu;
	struct arch_timer_cpu timer_cpu;
	struct kvm_pmu pmu;

	/* vcpu power state */
	struct kvm_mp_state mp_state;
	spinlock_t mp_state_lock;

	/* Cache some mmu pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* Pages to top-up the pKVM/EL2 guest pool */
	struct kvm_hyp_memcache pkvm_memcache;

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

/* KVM_ARM_VCPU_INIT completed */
#define VCPU_INITIALIZED	__vcpu_single_flag(cflags, BIT(0))
/* SVE config completed */
#define VCPU_SVE_FINALIZED	__vcpu_single_flag(cflags, BIT(1))
/* pKVM VCPU setup completed */
#define VCPU_PKVM_FINALIZED	__vcpu_single_flag(cflags, BIT(2))

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

/* Physical CPU not in supported_cpus */
#define ON_UNSUPPORTED_CPU	__vcpu_single_flag(sflags, BIT(0))
/* WFIT instruction trapped */
#define IN_WFIT			__vcpu_single_flag(sflags, BIT(1))
/* vcpu system registers loaded on physical CPU */
#define SYSREGS_ON_CPU		__vcpu_single_flag(sflags, BIT(2))
/* Software step state is Active-pending for external debug */
#define HOST_SS_ACTIVE_PENDING	__vcpu_single_flag(sflags, BIT(3))
/* Software step state is Active pending for guest debug */
#define GUEST_SS_ACTIVE_PENDING __vcpu_single_flag(sflags, BIT(4))
/* PMUSERENR for the guest EL0 is on physical CPU */
#define PMUSERENR_ON_CPU	__vcpu_single_flag(sflags, BIT(5))
/* WFI instruction trapped */
#define IN_WFI			__vcpu_single_flag(sflags, BIT(6))
/* KVM is currently emulating a nested ERET */
#define IN_NESTED_ERET		__vcpu_single_flag(sflags, BIT(7))


/* Pointer to the vcpu's SVE FFR for sve_{save,load}_state() */
#define vcpu_sve_pffr(vcpu) (kern_hyp_va((vcpu)->arch.sve_state) +	\
			     sve_ffr_offset((vcpu)->arch.sve_max_vl))

#define vcpu_sve_max_vq(vcpu)	sve_vq_from_vl((vcpu)->arch.sve_max_vl)

#define vcpu_sve_zcr_elx(vcpu)						\
	(unlikely(is_hyp_ctxt(vcpu)) ? ZCR_EL2 : ZCR_EL1)

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

#define kvm_has_sve(kvm)	(system_supports_sve() &&		\
				 test_bit(KVM_ARCH_FLAG_GUEST_HAS_SVE, &(kvm)->arch.flags))

#ifdef __KVM_NVHE_HYPERVISOR__
#define vcpu_has_sve(vcpu)	kvm_has_sve(kern_hyp_va((vcpu)->kvm))
#else
#define vcpu_has_sve(vcpu)	kvm_has_sve((vcpu)->kvm)
#endif

#ifdef CONFIG_ARM64_PTR_AUTH
#define vcpu_has_ptrauth(vcpu)						\
	((cpus_have_final_cap(ARM64_HAS_ADDRESS_AUTH) ||		\
	  cpus_have_final_cap(ARM64_HAS_GENERIC_AUTH)) &&		\
	 (vcpu_has_feature(vcpu, KVM_ARM_VCPU_PTRAUTH_ADDRESS) ||       \
	  vcpu_has_feature(vcpu, KVM_ARM_VCPU_PTRAUTH_GENERIC)))
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
 *
 * Don't bother with VNCR-based accesses in the nVHE code, it has no
 * business dealing with NV.
 */
static inline u64 *___ctxt_sys_reg(const struct kvm_cpu_context *ctxt, int r)
{
#if !defined (__KVM_NVHE_HYPERVISOR__)
	if (unlikely(cpus_have_final_cap(ARM64_HAS_NESTED_VIRT) &&
		     r >= __VNCR_START__ && ctxt->vncr_array))
		return &ctxt->vncr_array[r - __VNCR_START__];
#endif
	return (u64 *)&ctxt->sys_regs[r];
}

#define __ctxt_sys_reg(c,r)						\
	({								\
		BUILD_BUG_ON(__builtin_constant_p(r) &&			\
			     (r) >= NR_SYS_REGS);			\
		___ctxt_sys_reg(c, r);					\
	})

#define ctxt_sys_reg(c,r)	(*__ctxt_sys_reg(c,r))

u64 kvm_vcpu_apply_reg_masks(const struct kvm_vcpu *, enum vcpu_sysreg, u64);
#define __vcpu_sys_reg(v,r)						\
	(*({								\
		const struct kvm_cpu_context *ctxt = &(v)->arch.ctxt;	\
		u64 *__r = __ctxt_sys_reg(ctxt, (r));			\
		if (vcpu_has_nv((v)) && (r) >= __SANITISED_REG_START__)	\
			*__r = kvm_vcpu_apply_reg_masks((v), (r), *__r);\
		__r;							\
	}))

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
	case TCR2_EL1:		*val = read_sysreg_s(SYS_TCR2_EL12);	break;
	case PIR_EL1:		*val = read_sysreg_s(SYS_PIR_EL12);	break;
	case PIRE0_EL1:		*val = read_sysreg_s(SYS_PIRE0_EL12);	break;
	case POR_EL1:		*val = read_sysreg_s(SYS_POR_EL12);	break;
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
	case SPSR_EL1:		*val = read_sysreg_s(SYS_SPSR_EL12);	break;
	case PAR_EL1:		*val = read_sysreg_par();		break;
	case DACR32_EL2:	*val = read_sysreg_s(SYS_DACR32_EL2);	break;
	case IFSR32_EL2:	*val = read_sysreg_s(SYS_IFSR32_EL2);	break;
	case DBGVCR32_EL2:	*val = read_sysreg_s(SYS_DBGVCR32_EL2);	break;
	case ZCR_EL1:		*val = read_sysreg_s(SYS_ZCR_EL12);	break;
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
	case TCR2_EL1:		write_sysreg_s(val, SYS_TCR2_EL12);	break;
	case PIR_EL1:		write_sysreg_s(val, SYS_PIR_EL12);	break;
	case PIRE0_EL1:		write_sysreg_s(val, SYS_PIRE0_EL12);	break;
	case POR_EL1:		write_sysreg_s(val, SYS_POR_EL12);	break;
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
	case SPSR_EL1:		write_sysreg_s(val, SYS_SPSR_EL12);	break;
	case PAR_EL1:		write_sysreg_s(val, SYS_PAR_EL1);	break;
	case DACR32_EL2:	write_sysreg_s(val, SYS_DACR32_EL2);	break;
	case IFSR32_EL2:	write_sysreg_s(val, SYS_IFSR32_EL2);	break;
	case DBGVCR32_EL2:	write_sysreg_s(val, SYS_DBGVCR32_EL2);	break;
	case ZCR_EL1:		write_sysreg_s(val, SYS_ZCR_EL12);	break;
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

void kvm_arm_halt_guest(struct kvm *kvm);
void kvm_arm_resume_guest(struct kvm *kvm);

#define vcpu_has_run_once(vcpu)	(!!READ_ONCE((vcpu)->pid))

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

int handle_exit(struct kvm_vcpu *vcpu, int exception_index);
void handle_exit_early(struct kvm_vcpu *vcpu, int exception_index);

int kvm_handle_cp14_load_store(struct kvm_vcpu *vcpu);
int kvm_handle_cp14_32(struct kvm_vcpu *vcpu);
int kvm_handle_cp14_64(struct kvm_vcpu *vcpu);
int kvm_handle_cp15_32(struct kvm_vcpu *vcpu);
int kvm_handle_cp15_64(struct kvm_vcpu *vcpu);
int kvm_handle_sys_reg(struct kvm_vcpu *vcpu);
int kvm_handle_cp10_id(struct kvm_vcpu *vcpu);

void kvm_sys_regs_create_debugfs(struct kvm *kvm);
void kvm_reset_sys_regs(struct kvm_vcpu *vcpu);

int __init kvm_sys_reg_table_init(void);
struct sys_reg_desc;
int __init populate_sysreg_config(const struct sys_reg_desc *sr,
				  unsigned int idx);
int __init populate_nv_trap_config(void);

bool lock_all_vcpus(struct kvm *kvm);
void unlock_all_vcpus(struct kvm *kvm);

void kvm_calculate_traps(struct kvm_vcpu *vcpu);

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

/*
 * How we access per-CPU host data depends on the where we access it from,
 * and the mode we're in:
 *
 * - VHE and nVHE hypervisor bits use their locally defined instance
 *
 * - the rest of the kernel use either the VHE or nVHE one, depending on
 *   the mode we're running in.
 *
 *   Unless we're in protected mode, fully deprivileged, and the nVHE
 *   per-CPU stuff is exclusively accessible to the protected EL2 code.
 *   In this case, the EL1 code uses the *VHE* data as its private state
 *   (which makes sense in a way as there shouldn't be any shared state
 *   between the host and the hypervisor).
 *
 * Yes, this is all totally trivial. Shoot me now.
 */
#if defined(__KVM_NVHE_HYPERVISOR__) || defined(__KVM_VHE_HYPERVISOR__)
#define host_data_ptr(f)	(&this_cpu_ptr(&kvm_host_data)->f)
#else
#define host_data_ptr(f)						\
	(static_branch_unlikely(&kvm_protected_mode_initialized) ?	\
	 &this_cpu_ptr(&kvm_host_data)->f :				\
	 &this_cpu_ptr_hyp_sym(kvm_host_data)->f)
#endif

#define host_data_test_flag(flag)					\
	(test_bit(KVM_HOST_DATA_FLAG_##flag, host_data_ptr(flags)))
#define host_data_set_flag(flag)					\
	set_bit(KVM_HOST_DATA_FLAG_##flag, host_data_ptr(flags))
#define host_data_clear_flag(flag)					\
	clear_bit(KVM_HOST_DATA_FLAG_##flag, host_data_ptr(flags))

/* Check whether the FP regs are owned by the guest */
static inline bool guest_owns_fp_regs(void)
{
	return *host_data_ptr(fp_owner) == FP_STATE_GUEST_OWNED;
}

/* Check whether the FP regs are owned by the host */
static inline bool host_owns_fp_regs(void)
{
	return *host_data_ptr(fp_owner) == FP_STATE_HOST_OWNED;
}

static inline void kvm_init_host_cpu_context(struct kvm_cpu_context *cpu_ctxt)
{
	/* The host's MPIDR is immutable, so let's set it up at boot time */
	ctxt_sys_reg(cpu_ctxt, MPIDR_EL1) = read_cpuid_mpidr();
}

static inline bool kvm_system_needs_idmapped_vectors(void)
{
	return cpus_have_final_cap(ARM64_SPECTRE_V3A);
}

void kvm_init_host_debug_data(void);
void kvm_vcpu_load_debug(struct kvm_vcpu *vcpu);
void kvm_vcpu_put_debug(struct kvm_vcpu *vcpu);
void kvm_debug_set_guest_ownership(struct kvm_vcpu *vcpu);
void kvm_debug_handle_oslar(struct kvm_vcpu *vcpu, u64 val);

#define kvm_vcpu_os_lock_enabled(vcpu)		\
	(!!(__vcpu_sys_reg(vcpu, OSLSR_EL1) & OSLSR_EL1_OSLK))

#define kvm_debug_regs_in_use(vcpu)		\
	((vcpu)->arch.debug_owner != VCPU_DEBUG_FREE)
#define kvm_host_owns_debug_regs(vcpu)		\
	((vcpu)->arch.debug_owner == VCPU_DEBUG_HOST_OWNED)
#define kvm_guest_owns_debug_regs(vcpu)		\
	((vcpu)->arch.debug_owner == VCPU_DEBUG_GUEST_OWNED)

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
int kvm_vm_ioctl_get_reg_writable_masks(struct kvm *kvm,
					struct reg_mask_range *range);

/* Guest/host FPSIMD coordination helpers */
int kvm_arch_vcpu_run_map_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_load_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_ctxflush_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_ctxsync_fp(struct kvm_vcpu *vcpu);
void kvm_arch_vcpu_put_fp(struct kvm_vcpu *vcpu);

static inline bool kvm_pmu_counter_deferred(struct perf_event_attr *attr)
{
	return (!has_vhe() && attr->exclude_host);
}

#ifdef CONFIG_KVM
void kvm_set_pmu_events(u64 set, struct perf_event_attr *attr);
void kvm_clr_pmu_events(u64 clr);
bool kvm_set_pmuserenr(u64 val);
void kvm_enable_trbe(void);
void kvm_disable_trbe(void);
void kvm_tracing_set_el1_configuration(u64 trfcr_while_in_guest);
#else
static inline void kvm_set_pmu_events(u64 set, struct perf_event_attr *attr) {}
static inline void kvm_clr_pmu_events(u64 clr) {}
static inline bool kvm_set_pmuserenr(u64 val)
{
	return false;
}
static inline void kvm_enable_trbe(void) {}
static inline void kvm_disable_trbe(void) {}
static inline void kvm_tracing_set_el1_configuration(u64 trfcr_while_in_guest) {}
#endif

void kvm_vcpu_load_vhe(struct kvm_vcpu *vcpu);
void kvm_vcpu_put_vhe(struct kvm_vcpu *vcpu);

int __init kvm_set_ipa_limit(void);
u32 kvm_get_pa_bits(struct kvm *kvm);

#define __KVM_HAVE_ARCH_VM_ALLOC
struct kvm *kvm_arch_alloc_vm(void);

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS_RANGE

#define kvm_vm_is_protected(kvm)	(is_protected_kvm_enabled() && (kvm)->arch.pkvm.enabled)

#define vcpu_is_protected(vcpu)		kvm_vm_is_protected((vcpu)->kvm)

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

static inline bool __vcpu_has_feature(const struct kvm_arch *ka, int feature)
{
	return test_bit(feature, ka->vcpu_features);
}

#define kvm_vcpu_has_feature(k, f)	__vcpu_has_feature(&(k)->arch, (f))
#define vcpu_has_feature(v, f)	__vcpu_has_feature(&(v)->kvm->arch, (f))

#define kvm_vcpu_initialized(v) vcpu_get_flag(vcpu, VCPU_INITIALIZED)

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

static inline u64 *__vm_id_reg(struct kvm_arch *ka, u32 reg)
{
	switch (reg) {
	case sys_reg(3, 0, 0, 1, 0) ... sys_reg(3, 0, 0, 7, 7):
		return &ka->id_regs[IDREG_IDX(reg)];
	case SYS_CTR_EL0:
		return &ka->ctr_el0;
	case SYS_MIDR_EL1:
		return &ka->midr_el1;
	case SYS_REVIDR_EL1:
		return &ka->revidr_el1;
	case SYS_AIDR_EL1:
		return &ka->aidr_el1;
	default:
		WARN_ON_ONCE(1);
		return NULL;
	}
}

#define kvm_read_vm_id_reg(kvm, reg)					\
	({ u64 __val = *__vm_id_reg(&(kvm)->arch, reg); __val; })

void kvm_set_vm_id_reg(struct kvm *kvm, u32 reg, u64 val);

#define __expand_field_sign_unsigned(id, fld, val)			\
	((u64)SYS_FIELD_VALUE(id, fld, val))

#define __expand_field_sign_signed(id, fld, val)			\
	({								\
		u64 __val = SYS_FIELD_VALUE(id, fld, val);		\
		sign_extend64(__val, id##_##fld##_WIDTH - 1);		\
	})

#define get_idreg_field_unsigned(kvm, id, fld)				\
	({								\
		u64 __val = kvm_read_vm_id_reg((kvm), SYS_##id);	\
		FIELD_GET(id##_##fld##_MASK, __val);			\
	})

#define get_idreg_field_signed(kvm, id, fld)				\
	({								\
		u64 __val = get_idreg_field_unsigned(kvm, id, fld);	\
		sign_extend64(__val, id##_##fld##_WIDTH - 1);		\
	})

#define get_idreg_field_enum(kvm, id, fld)				\
	get_idreg_field_unsigned(kvm, id, fld)

#define kvm_cmp_feat_signed(kvm, id, fld, op, limit)			\
	(get_idreg_field_signed((kvm), id, fld) op __expand_field_sign_signed(id, fld, limit))

#define kvm_cmp_feat_unsigned(kvm, id, fld, op, limit)			\
	(get_idreg_field_unsigned((kvm), id, fld) op __expand_field_sign_unsigned(id, fld, limit))

#define kvm_cmp_feat(kvm, id, fld, op, limit)				\
	(id##_##fld##_SIGNED ?						\
	 kvm_cmp_feat_signed(kvm, id, fld, op, limit) :			\
	 kvm_cmp_feat_unsigned(kvm, id, fld, op, limit))

#define kvm_has_feat(kvm, id, fld, limit)				\
	kvm_cmp_feat(kvm, id, fld, >=, limit)

#define kvm_has_feat_enum(kvm, id, fld, val)				\
	kvm_cmp_feat_unsigned(kvm, id, fld, ==, val)

#define kvm_has_feat_range(kvm, id, fld, min, max)			\
	(kvm_cmp_feat(kvm, id, fld, >=, min) &&				\
	kvm_cmp_feat(kvm, id, fld, <=, max))

/* Check for a given level of PAuth support */
#define kvm_has_pauth(k, l)						\
	({								\
		bool pa, pi, pa3;					\
									\
		pa  = kvm_has_feat((k), ID_AA64ISAR1_EL1, APA, l);	\
		pa &= kvm_has_feat((k), ID_AA64ISAR1_EL1, GPA, IMP);	\
		pi  = kvm_has_feat((k), ID_AA64ISAR1_EL1, API, l);	\
		pi &= kvm_has_feat((k), ID_AA64ISAR1_EL1, GPI, IMP);	\
		pa3  = kvm_has_feat((k), ID_AA64ISAR2_EL1, APA3, l);	\
		pa3 &= kvm_has_feat((k), ID_AA64ISAR2_EL1, GPA3, IMP);	\
									\
		(pa + pi + pa3) == 1;					\
	})

#define kvm_has_fpmr(k)					\
	(system_supports_fpmr() &&			\
	 kvm_has_feat((k), ID_AA64PFR2_EL1, FPMR, IMP))

#define kvm_has_tcr2(k)				\
	(kvm_has_feat((k), ID_AA64MMFR3_EL1, TCRX, IMP))

#define kvm_has_s1pie(k)				\
	(kvm_has_feat((k), ID_AA64MMFR3_EL1, S1PIE, IMP))

#define kvm_has_s1poe(k)				\
	(kvm_has_feat((k), ID_AA64MMFR3_EL1, S1POE, IMP))

#endif /* __ARM64_KVM_HOST_H__ */
