/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2020-2023 Loongson Technology Corporation Limited
 */

#ifndef __ASM_LOONGARCH_KVM_HOST_H__
#define __ASM_LOONGARCH_KVM_HOST_H__

#include <linux/cpumask.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/kvm.h>
#include <linux/kvm_types.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/threads.h>
#include <linux/types.h>

#include <asm/inst.h>
#include <asm/kvm_mmu.h>
#include <asm/loongarch.h>

/* Loongarch KVM register ids */
#define KVM_GET_IOC_CSR_IDX(id)		((id & KVM_CSR_IDX_MASK) >> LOONGARCH_REG_SHIFT)
#define KVM_GET_IOC_CPUCFG_IDX(id)	((id & KVM_CPUCFG_IDX_MASK) >> LOONGARCH_REG_SHIFT)

#define KVM_MAX_VCPUS			256
#define KVM_MAX_CPUCFG_REGS		21

#define KVM_HALT_POLL_NS_DEFAULT	500000
#define KVM_REQ_TLB_FLUSH_GPA		KVM_ARCH_REQ(0)
#define KVM_REQ_STEAL_UPDATE		KVM_ARCH_REQ(1)
#define KVM_REQ_PMU			KVM_ARCH_REQ(2)

#define KVM_GUESTDBG_SW_BP_MASK		\
	(KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP)
#define KVM_GUESTDBG_VALID_MASK		\
	(KVM_GUESTDBG_ENABLE | KVM_GUESTDBG_USE_SW_BP | KVM_GUESTDBG_SINGLESTEP)

#define KVM_DIRTY_LOG_MANUAL_CAPS	\
	(KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE | KVM_DIRTY_LOG_INITIALLY_SET)

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
	u64 pages;
	u64 hugepages;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 int_exits;
	u64 idle_exits;
	u64 cpucfg_exits;
	u64 signal_exits;
	u64 hypercall_exits;
};

#define KVM_MEM_HUGEPAGE_CAPABLE	(1UL << 0)
#define KVM_MEM_HUGEPAGE_INCAPABLE	(1UL << 1)
struct kvm_arch_memory_slot {
	unsigned long flags;
};

#define HOST_MAX_PMNUM			16
struct kvm_context {
	unsigned long vpid_cache;
	struct kvm_vcpu *last_vcpu;
	/* Host PMU CSR */
	u64 perf_ctrl[HOST_MAX_PMNUM];
	u64 perf_cntr[HOST_MAX_PMNUM];
};

struct kvm_world_switch {
	int (*exc_entry)(void);
	int (*enter_guest)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	unsigned long page_order;
};

#define MAX_PGTABLE_LEVELS	4

/*
 * Physical CPUID is used for interrupt routing, there are different
 * definitions about physical cpuid on different hardwares.
 *
 *  For LOONGARCH_CSR_CPUID register, max CPUID size if 512
 *  For IPI hardware, max destination CPUID size 1024
 *  For extioi interrupt controller, max destination CPUID size is 256
 *  For msgint interrupt controller, max supported CPUID size is 65536
 *
 * Currently max CPUID is defined as 256 for KVM hypervisor, in future
 * it will be expanded to 4096, including 16 packages at most. And every
 * package supports at most 256 vcpus
 */
#define KVM_MAX_PHYID		256

struct kvm_phyid_info {
	struct kvm_vcpu	*vcpu;
	bool		enabled;
};

struct kvm_phyid_map {
	int max_phyid;
	struct kvm_phyid_info phys_map[KVM_MAX_PHYID];
};

struct kvm_arch {
	/* Guest physical mm */
	kvm_pte_t *pgd;
	unsigned long gpa_size;
	unsigned long invalid_ptes[MAX_PGTABLE_LEVELS];
	unsigned int  pte_shifts[MAX_PGTABLE_LEVELS];
	unsigned int  root_level;
	spinlock_t    phyid_map_lock;
	struct kvm_phyid_map  *phyid_map;
	/* Enabled PV features */
	unsigned long pv_features;

	s64 time_offset;
	struct kvm_context __percpu *vmcs;
};

#define CSR_MAX_NUMS		0x800

struct loongarch_csrs {
	unsigned long csrs[CSR_MAX_NUMS];
};

/* Resume Flags */
#define RESUME_HOST		0
#define RESUME_GUEST		1

enum emulation_result {
	EMULATE_DONE,		/* no further processing */
	EMULATE_DO_MMIO,	/* kvm_run filled with MMIO request */
	EMULATE_DO_IOCSR,	/* handle IOCSR request */
	EMULATE_FAIL,		/* can't emulate this instruction */
	EMULATE_EXCEPT,		/* A guest exception has been generated */
};

#define KVM_LARCH_FPU		(0x1 << 0)
#define KVM_LARCH_LSX		(0x1 << 1)
#define KVM_LARCH_LASX		(0x1 << 2)
#define KVM_LARCH_LBT		(0x1 << 3)
#define KVM_LARCH_PMU		(0x1 << 4)
#define KVM_LARCH_SWCSR_LATEST	(0x1 << 5)
#define KVM_LARCH_HWCSR_USABLE	(0x1 << 6)

#define LOONGARCH_PV_FEAT_UPDATED	BIT_ULL(63)
#define LOONGARCH_PV_FEAT_MASK		(BIT(KVM_FEATURE_IPI) |		\
					 BIT(KVM_FEATURE_STEAL_TIME) |	\
					 BIT(KVM_FEATURE_VIRT_EXTIOI))

struct kvm_vcpu_arch {
	/*
	 * Switch pointer-to-function type to unsigned long
	 * for loading the value into register directly.
	 */
	unsigned long host_eentry;
	unsigned long guest_eentry;

	/* Pointers stored here for easy accessing from assembly code */
	int (*handle_exit)(struct kvm_run *run, struct kvm_vcpu *vcpu);

	/* Host registers preserved across guest mode execution */
	unsigned long host_sp;
	unsigned long host_tp;
	unsigned long host_pgd;

	/* Host CSRs are used when handling exits from guest */
	unsigned long badi;
	unsigned long badv;
	unsigned long host_ecfg;
	unsigned long host_estat;
	unsigned long host_percpu;

	/* GPRs */
	unsigned long gprs[32];
	unsigned long pc;

	/* Which auxiliary state is loaded (KVM_LARCH_*) */
	unsigned int aux_inuse;

	/* FPU state */
	struct loongarch_fpu fpu FPU_ALIGN;
	struct loongarch_lbt lbt;

	/* CSR state */
	struct loongarch_csrs *csr;

	/* Guest max PMU CSR id */
	int max_pmu_csrid;

	/* GPR used as IO source/target */
	u32 io_gpr;

	/* KVM register to control count timer */
	u32 count_ctl;
	struct hrtimer swtimer;

	/* Bitmask of intr that are pending */
	unsigned long irq_pending;
	/* Bitmask of pending intr to be cleared */
	unsigned long irq_clear;

	/* Bitmask of exceptions that are pending */
	unsigned long exception_pending;
	unsigned int  esubcode;

	/* Cache for pages needed inside spinlock regions */
	struct kvm_mmu_memory_cache mmu_page_cache;

	/* vcpu's vpid */
	u64 vpid;
	gpa_t flush_gpa;

	/* Frequency of stable timer in Hz */
	u64 timer_mhz;
	ktime_t expire;

	/* Last CPU the vCPU state was loaded on */
	int last_sched_cpu;
	/* mp state */
	struct kvm_mp_state mp_state;
	/* cpucfg */
	u32 cpucfg[KVM_MAX_CPUCFG_REGS];

	/* paravirt steal time */
	struct {
		u64 guest_addr;
		u64 last_steal;
		struct gfn_to_hva_cache cache;
	} st;
};

static inline unsigned long readl_sw_gcsr(struct loongarch_csrs *csr, int reg)
{
	return csr->csrs[reg];
}

static inline void writel_sw_gcsr(struct loongarch_csrs *csr, int reg, unsigned long val)
{
	csr->csrs[reg] = val;
}

static inline bool kvm_guest_has_fpu(struct kvm_vcpu_arch *arch)
{
	return arch->cpucfg[2] & CPUCFG2_FP;
}

static inline bool kvm_guest_has_lsx(struct kvm_vcpu_arch *arch)
{
	return arch->cpucfg[2] & CPUCFG2_LSX;
}

static inline bool kvm_guest_has_lasx(struct kvm_vcpu_arch *arch)
{
	return arch->cpucfg[2] & CPUCFG2_LASX;
}

static inline bool kvm_guest_has_lbt(struct kvm_vcpu_arch *arch)
{
	return arch->cpucfg[2] & (CPUCFG2_X86BT | CPUCFG2_ARMBT | CPUCFG2_MIPSBT);
}

static inline bool kvm_guest_has_pmu(struct kvm_vcpu_arch *arch)
{
	return arch->cpucfg[6] & CPUCFG6_PMP;
}

static inline int kvm_get_pmu_num(struct kvm_vcpu_arch *arch)
{
	return (arch->cpucfg[6] & CPUCFG6_PMNUM) >> CPUCFG6_PMNUM_SHIFT;
}

/* Debug: dump vcpu state */
int kvm_arch_vcpu_dump_regs(struct kvm_vcpu *vcpu);

/* MMU handling */
void kvm_flush_tlb_all(void);
void kvm_flush_tlb_gpa(struct kvm_vcpu *vcpu, unsigned long gpa);
int kvm_handle_mm_fault(struct kvm_vcpu *vcpu, unsigned long badv, bool write);

int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end, bool blockable);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);

static inline void update_pc(struct kvm_vcpu_arch *arch)
{
	arch->pc += 4;
}

/*
 * kvm_is_ifetch_fault() - Find whether a TLBL exception is due to ifetch fault.
 * @vcpu:	Virtual CPU.
 *
 * Returns:	Whether the TLBL exception was likely due to an instruction
 *		fetch fault rather than a data load fault.
 */
static inline bool kvm_is_ifetch_fault(struct kvm_vcpu_arch *arch)
{
	return arch->pc == arch->badv;
}

/* Misc */
static inline void kvm_arch_hardware_unsetup(void) {}
static inline void kvm_arch_sync_events(struct kvm *kvm) {}
static inline void kvm_arch_memslots_updated(struct kvm *kvm, u64 gen) {}
static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}
static inline void kvm_arch_free_memslot(struct kvm *kvm, struct kvm_memory_slot *slot) {}
void kvm_check_vpid(struct kvm_vcpu *vcpu);
enum hrtimer_restart kvm_swtimer_wakeup(struct hrtimer *timer);
void kvm_arch_flush_remote_tlbs_memslot(struct kvm *kvm, const struct kvm_memory_slot *memslot);
void kvm_init_vmcs(struct kvm *kvm);
void kvm_exc_entry(void);
int  kvm_enter_guest(struct kvm_run *run, struct kvm_vcpu *vcpu);

extern unsigned long vpid_mask;
extern const unsigned long kvm_exception_size;
extern const unsigned long kvm_enter_guest_size;
extern struct kvm_world_switch *kvm_loongarch_ops;

#define SW_GCSR		(1 << 0)
#define HW_GCSR		(1 << 1)
#define INVALID_GCSR	(1 << 2)

int get_gcsr_flag(int csr);
void set_hw_gcsr(int csr_id, unsigned long val);

#endif /* __ASM_LOONGARCH_KVM_HOST_H__ */
