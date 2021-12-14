/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This header defines architecture specific interfaces, x86 version
 */

#ifndef _ASM_X86_KVM_HOST_H
#define _ASM_X86_KVM_HOST_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/tracepoint.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>
#include <linux/irq.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>
#include <linux/perf_event.h>
#include <linux/pvclock_gtod.h>
#include <linux/clocksource.h>
#include <linux/irqbypass.h>
#include <linux/hyperv.h>

#include <asm/apic.h>
#include <asm/pvclock-abi.h>
#include <asm/desc.h>
#include <asm/mtrr.h>
#include <asm/msr-index.h>
#include <asm/asm.h>
#include <asm/kvm_page_track.h>
#include <asm/kvm_vcpu_regs.h>
#include <asm/hyperv-tlfs.h>

#define __KVM_HAVE_ARCH_VCPU_DEBUGFS

#define KVM_MAX_VCPUS 288
#define KVM_SOFT_MAX_VCPUS 240
#define KVM_MAX_VCPU_ID 1023
#define KVM_USER_MEM_SLOTS 509
/* memory slots that are not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 3
#define KVM_MEM_SLOTS_NUM (KVM_USER_MEM_SLOTS + KVM_PRIVATE_MEM_SLOTS)

#define KVM_HALT_POLL_NS_DEFAULT 200000

#define KVM_IRQCHIP_NUM_PINS  KVM_IOAPIC_NUM_PINS

#define KVM_DIRTY_LOG_MANUAL_CAPS   (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE | \
					KVM_DIRTY_LOG_INITIALLY_SET)

/* x86-specific vcpu->requests bit members */
#define KVM_REQ_MIGRATE_TIMER		KVM_ARCH_REQ(0)
#define KVM_REQ_REPORT_TPR_ACCESS	KVM_ARCH_REQ(1)
#define KVM_REQ_TRIPLE_FAULT		KVM_ARCH_REQ(2)
#define KVM_REQ_MMU_SYNC		KVM_ARCH_REQ(3)
#define KVM_REQ_CLOCK_UPDATE		KVM_ARCH_REQ(4)
#define KVM_REQ_LOAD_MMU_PGD		KVM_ARCH_REQ(5)
#define KVM_REQ_EVENT			KVM_ARCH_REQ(6)
#define KVM_REQ_APF_HALT		KVM_ARCH_REQ(7)
#define KVM_REQ_STEAL_UPDATE		KVM_ARCH_REQ(8)
#define KVM_REQ_NMI			KVM_ARCH_REQ(9)
#define KVM_REQ_PMU			KVM_ARCH_REQ(10)
#define KVM_REQ_PMI			KVM_ARCH_REQ(11)
#define KVM_REQ_SMI			KVM_ARCH_REQ(12)
#define KVM_REQ_MASTERCLOCK_UPDATE	KVM_ARCH_REQ(13)
#define KVM_REQ_MCLOCK_INPROGRESS \
	KVM_ARCH_REQ_FLAGS(14, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_SCAN_IOAPIC \
	KVM_ARCH_REQ_FLAGS(15, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_GLOBAL_CLOCK_UPDATE	KVM_ARCH_REQ(16)
#define KVM_REQ_APIC_PAGE_RELOAD \
	KVM_ARCH_REQ_FLAGS(17, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_HV_CRASH		KVM_ARCH_REQ(18)
#define KVM_REQ_IOAPIC_EOI_EXIT		KVM_ARCH_REQ(19)
#define KVM_REQ_HV_RESET		KVM_ARCH_REQ(20)
#define KVM_REQ_HV_EXIT			KVM_ARCH_REQ(21)
#define KVM_REQ_HV_STIMER		KVM_ARCH_REQ(22)
#define KVM_REQ_LOAD_EOI_EXITMAP	KVM_ARCH_REQ(23)
#define KVM_REQ_GET_NESTED_STATE_PAGES	KVM_ARCH_REQ(24)
#define KVM_REQ_APICV_UPDATE \
	KVM_ARCH_REQ_FLAGS(25, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_TLB_FLUSH_CURRENT	KVM_ARCH_REQ(26)
#define KVM_REQ_TLB_FLUSH_GUEST \
	KVM_ARCH_REQ_FLAGS(27, KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_APF_READY		KVM_ARCH_REQ(28)
#define KVM_REQ_MSR_FILTER_CHANGED	KVM_ARCH_REQ(29)

#define CR0_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS \
			  | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM \
			  | X86_CR0_NW | X86_CR0_CD | X86_CR0_PG))

#define CR4_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD | X86_CR4_DE\
			  | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE     \
			  | X86_CR4_PGE | X86_CR4_PCE | X86_CR4_OSFXSR | X86_CR4_PCIDE \
			  | X86_CR4_OSXSAVE | X86_CR4_SMEP | X86_CR4_FSGSBASE \
			  | X86_CR4_OSXMMEXCPT | X86_CR4_LA57 | X86_CR4_VMXE \
			  | X86_CR4_SMAP | X86_CR4_PKE | X86_CR4_UMIP))

#define CR8_RESERVED_BITS (~(unsigned long)X86_CR8_TPR)



#define INVALID_PAGE (~(hpa_t)0)
#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define UNMAPPED_GVA (~(gpa_t)0)

/* KVM Hugepage definitions for x86 */
#define KVM_MAX_HUGEPAGE_LEVEL	PG_LEVEL_1G
#define KVM_NR_PAGE_SIZES	(KVM_MAX_HUGEPAGE_LEVEL - PG_LEVEL_4K + 1)
#define KVM_HPAGE_GFN_SHIFT(x)	(((x) - 1) * 9)
#define KVM_HPAGE_SHIFT(x)	(PAGE_SHIFT + KVM_HPAGE_GFN_SHIFT(x))
#define KVM_HPAGE_SIZE(x)	(1UL << KVM_HPAGE_SHIFT(x))
#define KVM_HPAGE_MASK(x)	(~(KVM_HPAGE_SIZE(x) - 1))
#define KVM_PAGES_PER_HPAGE(x)	(KVM_HPAGE_SIZE(x) / PAGE_SIZE)

static inline gfn_t gfn_to_index(gfn_t gfn, gfn_t base_gfn, int level)
{
	/* KVM_HPAGE_GFN_SHIFT(PG_LEVEL_4K) must be 0. */
	return (gfn >> KVM_HPAGE_GFN_SHIFT(level)) -
		(base_gfn >> KVM_HPAGE_GFN_SHIFT(level));
}

#define KVM_PERMILLE_MMU_PAGES 20
#define KVM_MIN_ALLOC_MMU_PAGES 64UL
#define KVM_MMU_HASH_SHIFT 12
#define KVM_NUM_MMU_PAGES (1 << KVM_MMU_HASH_SHIFT)
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 256
#define KVM_NR_FIXED_MTRR_REGION 88
#define KVM_NR_VAR_MTRR 8

#define ASYNC_PF_PER_VCPU 64

enum kvm_reg {
	VCPU_REGS_RAX = __VCPU_REGS_RAX,
	VCPU_REGS_RCX = __VCPU_REGS_RCX,
	VCPU_REGS_RDX = __VCPU_REGS_RDX,
	VCPU_REGS_RBX = __VCPU_REGS_RBX,
	VCPU_REGS_RSP = __VCPU_REGS_RSP,
	VCPU_REGS_RBP = __VCPU_REGS_RBP,
	VCPU_REGS_RSI = __VCPU_REGS_RSI,
	VCPU_REGS_RDI = __VCPU_REGS_RDI,
#ifdef CONFIG_X86_64
	VCPU_REGS_R8  = __VCPU_REGS_R8,
	VCPU_REGS_R9  = __VCPU_REGS_R9,
	VCPU_REGS_R10 = __VCPU_REGS_R10,
	VCPU_REGS_R11 = __VCPU_REGS_R11,
	VCPU_REGS_R12 = __VCPU_REGS_R12,
	VCPU_REGS_R13 = __VCPU_REGS_R13,
	VCPU_REGS_R14 = __VCPU_REGS_R14,
	VCPU_REGS_R15 = __VCPU_REGS_R15,
#endif
	VCPU_REGS_RIP,
	NR_VCPU_REGS,

	VCPU_EXREG_PDPTR = NR_VCPU_REGS,
	VCPU_EXREG_CR0,
	VCPU_EXREG_CR3,
	VCPU_EXREG_CR4,
	VCPU_EXREG_RFLAGS,
	VCPU_EXREG_SEGMENTS,
	VCPU_EXREG_EXIT_INFO_1,
	VCPU_EXREG_EXIT_INFO_2,
};

enum {
	VCPU_SREG_ES,
	VCPU_SREG_CS,
	VCPU_SREG_SS,
	VCPU_SREG_DS,
	VCPU_SREG_FS,
	VCPU_SREG_GS,
	VCPU_SREG_TR,
	VCPU_SREG_LDTR,
};

enum exit_fastpath_completion {
	EXIT_FASTPATH_NONE,
	EXIT_FASTPATH_REENTER_GUEST,
	EXIT_FASTPATH_EXIT_HANDLED,
};
typedef enum exit_fastpath_completion fastpath_t;

struct x86_emulate_ctxt;
struct x86_exception;
enum x86_intercept;
enum x86_intercept_stage;

#define KVM_NR_DB_REGS	4

#define DR6_BD		(1 << 13)
#define DR6_BS		(1 << 14)
#define DR6_BT		(1 << 15)
#define DR6_RTM		(1 << 16)
#define DR6_FIXED_1	0xfffe0ff0
#define DR6_INIT	0xffff0ff0
#define DR6_VOLATILE	0x0001e00f

#define DR7_BP_EN_MASK	0x000000ff
#define DR7_GE		(1 << 9)
#define DR7_GD		(1 << 13)
#define DR7_FIXED_1	0x00000400
#define DR7_VOLATILE	0xffff2bff

#define PFERR_PRESENT_BIT 0
#define PFERR_WRITE_BIT 1
#define PFERR_USER_BIT 2
#define PFERR_RSVD_BIT 3
#define PFERR_FETCH_BIT 4
#define PFERR_PK_BIT 5
#define PFERR_GUEST_FINAL_BIT 32
#define PFERR_GUEST_PAGE_BIT 33

#define PFERR_PRESENT_MASK (1U << PFERR_PRESENT_BIT)
#define PFERR_WRITE_MASK (1U << PFERR_WRITE_BIT)
#define PFERR_USER_MASK (1U << PFERR_USER_BIT)
#define PFERR_RSVD_MASK (1U << PFERR_RSVD_BIT)
#define PFERR_FETCH_MASK (1U << PFERR_FETCH_BIT)
#define PFERR_PK_MASK (1U << PFERR_PK_BIT)
#define PFERR_GUEST_FINAL_MASK (1ULL << PFERR_GUEST_FINAL_BIT)
#define PFERR_GUEST_PAGE_MASK (1ULL << PFERR_GUEST_PAGE_BIT)

#define PFERR_NESTED_GUEST_PAGE (PFERR_GUEST_PAGE_MASK |	\
				 PFERR_WRITE_MASK |		\
				 PFERR_PRESENT_MASK)

/* apic attention bits */
#define KVM_APIC_CHECK_VAPIC	0
/*
 * The following bit is set with PV-EOI, unset on EOI.
 * We detect PV-EOI changes by guest by comparing
 * this bit with PV-EOI in guest memory.
 * See the implementation in apic_update_pv_eoi.
 */
#define KVM_APIC_PV_EOI_PENDING	1

struct kvm_kernel_irq_routing_entry;

/*
 * the pages used as guest page table on soft mmu are tracked by
 * kvm_memory_slot.arch.gfn_track which is 16 bits, so the role bits used
 * by indirect shadow page can not be more than 15 bits.
 *
 * Currently, we used 14 bits that are @level, @gpte_is_8_bytes, @quadrant, @access,
 * @nxe, @cr0_wp, @smep_andnot_wp and @smap_andnot_wp.
 */
union kvm_mmu_page_role {
	u32 word;
	struct {
		unsigned level:4;
		unsigned gpte_is_8_bytes:1;
		unsigned quadrant:2;
		unsigned direct:1;
		unsigned access:3;
		unsigned invalid:1;
		unsigned nxe:1;
		unsigned cr0_wp:1;
		unsigned smep_andnot_wp:1;
		unsigned smap_andnot_wp:1;
		unsigned ad_disabled:1;
		unsigned guest_mode:1;
		unsigned :6;

		/*
		 * This is left at the top of the word so that
		 * kvm_memslots_for_spte_role can extract it with a
		 * simple shift.  While there is room, give it a whole
		 * byte so it is also faster to load it from memory.
		 */
		unsigned smm:8;
	};
};

union kvm_mmu_extended_role {
/*
 * This structure complements kvm_mmu_page_role caching everything needed for
 * MMU configuration. If nothing in both these structures changed, MMU
 * re-configuration can be skipped. @valid bit is set on first usage so we don't
 * treat all-zero structure as valid data.
 */
	u32 word;
	struct {
		unsigned int valid:1;
		unsigned int execonly:1;
		unsigned int cr0_pg:1;
		unsigned int cr4_pae:1;
		unsigned int cr4_pse:1;
		unsigned int cr4_pke:1;
		unsigned int cr4_smap:1;
		unsigned int cr4_smep:1;
		unsigned int cr4_la57:1;
		unsigned int maxphyaddr:6;
	};
};

union kvm_mmu_role {
	u64 as_u64;
	struct {
		union kvm_mmu_page_role base;
		union kvm_mmu_extended_role ext;
	};
};

struct kvm_rmap_head {
	unsigned long val;
};

struct kvm_pio_request {
	unsigned long linear_rip;
	unsigned long count;
	int in;
	int port;
	int size;
};

#define PT64_ROOT_MAX_LEVEL 5

struct rsvd_bits_validate {
	u64 rsvd_bits_mask[2][PT64_ROOT_MAX_LEVEL];
	u64 bad_mt_xwr;
};

struct kvm_mmu_root_info {
	gpa_t pgd;
	hpa_t hpa;
};

#define KVM_MMU_ROOT_INFO_INVALID \
	((struct kvm_mmu_root_info) { .pgd = INVALID_PAGE, .hpa = INVALID_PAGE })

#define KVM_MMU_NUM_PREV_ROOTS 3

struct kvm_mmu_page;

/*
 * x86 supports 4 paging modes (5-level 64-bit, 4-level 64-bit, 3-level 32-bit,
 * and 2-level 32-bit).  The kvm_mmu structure abstracts the details of the
 * current mmu mode.
 */
struct kvm_mmu {
	unsigned long (*get_guest_pgd)(struct kvm_vcpu *vcpu);
	u64 (*get_pdptr)(struct kvm_vcpu *vcpu, int index);
	int (*page_fault)(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u32 err,
			  bool prefault);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  struct x86_exception *fault);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gpa_t gva_or_gpa,
			    u32 access, struct x86_exception *exception);
	gpa_t (*translate_gpa)(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
			       struct x86_exception *exception);
	int (*sync_page)(struct kvm_vcpu *vcpu,
			 struct kvm_mmu_page *sp);
	void (*invlpg)(struct kvm_vcpu *vcpu, gva_t gva, hpa_t root_hpa);
	hpa_t root_hpa;
	gpa_t root_pgd;
	union kvm_mmu_role mmu_role;
	u8 root_level;
	u8 shadow_root_level;
	u8 ept_ad;
	bool direct_map;
	struct kvm_mmu_root_info prev_roots[KVM_MMU_NUM_PREV_ROOTS];

	/*
	 * Bitmap; bit set = permission fault
	 * Byte index: page fault error code [4:1]
	 * Bit index: pte permissions in ACC_* format
	 */
	u8 permissions[16];

	/*
	* The pkru_mask indicates if protection key checks are needed.  It
	* consists of 16 domains indexed by page fault error code bits [4:1],
	* with PFEC.RSVD replaced by ACC_USER_MASK from the page tables.
	* Each domain has 2 bits which are ANDed with AD and WD from PKRU.
	*/
	u32 pkru_mask;

	u64 *pae_root;
	u64 *lm_root;

	/*
	 * check zero bits on shadow page table entries, these
	 * bits include not only hardware reserved bits but also
	 * the bits spte never used.
	 */
	struct rsvd_bits_validate shadow_zero_check;

	struct rsvd_bits_validate guest_rsvd_check;

	/* Can have large pages at levels 2..last_nonleaf_level-1. */
	u8 last_nonleaf_level;

	bool nx;

	u64 pdptrs[4]; /* pae */
};

struct kvm_tlb_range {
	u64 start_gfn;
	u64 pages;
};

enum pmc_type {
	KVM_PMC_GP = 0,
	KVM_PMC_FIXED,
};

struct kvm_pmc {
	enum pmc_type type;
	u8 idx;
	u64 counter;
	u64 eventsel;
	struct perf_event *perf_event;
	struct kvm_vcpu *vcpu;
	/*
	 * eventsel value for general purpose counters,
	 * ctrl value for fixed counters.
	 */
	u64 current_config;
};

struct kvm_pmu {
	unsigned nr_arch_gp_counters;
	unsigned nr_arch_fixed_counters;
	unsigned available_event_types;
	u64 fixed_ctr_ctrl;
	u64 global_ctrl;
	u64 global_status;
	u64 global_ovf_ctrl;
	u64 counter_bitmask[2];
	u64 global_ctrl_mask;
	u64 global_ovf_ctrl_mask;
	u64 reserved_bits;
	u8 version;
	struct kvm_pmc gp_counters[INTEL_PMC_MAX_GENERIC];
	struct kvm_pmc fixed_counters[INTEL_PMC_MAX_FIXED];
	struct irq_work irq_work;
	DECLARE_BITMAP(reprogram_pmi, X86_PMC_IDX_MAX);
	DECLARE_BITMAP(all_valid_pmc_idx, X86_PMC_IDX_MAX);
	DECLARE_BITMAP(pmc_in_use, X86_PMC_IDX_MAX);

	/*
	 * The gate to release perf_events not marked in
	 * pmc_in_use only once in a vcpu time slice.
	 */
	bool need_cleanup;

	/*
	 * The total number of programmed perf_events and it helps to avoid
	 * redundant check before cleanup if guest don't use vPMU at all.
	 */
	u8 event_count;
};

struct kvm_pmu_ops;

enum {
	KVM_DEBUGREG_BP_ENABLED = 1,
	KVM_DEBUGREG_WONT_EXIT = 2,
	KVM_DEBUGREG_RELOAD = 4,
};

struct kvm_mtrr_range {
	u64 base;
	u64 mask;
	struct list_head node;
};

struct kvm_mtrr {
	struct kvm_mtrr_range var_ranges[KVM_NR_VAR_MTRR];
	mtrr_type fixed_ranges[KVM_NR_FIXED_MTRR_REGION];
	u64 deftype;

	struct list_head head;
};

/* Hyper-V SynIC timer */
struct kvm_vcpu_hv_stimer {
	struct hrtimer timer;
	int index;
	union hv_stimer_config config;
	u64 count;
	u64 exp_time;
	struct hv_message msg;
	bool msg_pending;
};

/* Hyper-V synthetic interrupt controller (SynIC)*/
struct kvm_vcpu_hv_synic {
	u64 version;
	u64 control;
	u64 msg_page;
	u64 evt_page;
	atomic64_t sint[HV_SYNIC_SINT_COUNT];
	atomic_t sint_to_gsi[HV_SYNIC_SINT_COUNT];
	DECLARE_BITMAP(auto_eoi_bitmap, 256);
	DECLARE_BITMAP(vec_bitmap, 256);
	bool active;
	bool dont_zero_synic_pages;
};

/* Hyper-V per vcpu emulation context */
struct kvm_vcpu_hv {
	u32 vp_index;
	u64 hv_vapic;
	s64 runtime_offset;
	struct kvm_vcpu_hv_synic synic;
	struct kvm_hyperv_exit exit;
	struct kvm_vcpu_hv_stimer stimer[HV_SYNIC_STIMER_COUNT];
	DECLARE_BITMAP(stimer_pending_bitmap, HV_SYNIC_STIMER_COUNT);
	cpumask_t tlb_flush;
};

struct kvm_vcpu_arch {
	/*
	 * rip and regs accesses must go through
	 * kvm_{register,rip}_{read,write} functions.
	 */
	unsigned long regs[NR_VCPU_REGS];
	u32 regs_avail;
	u32 regs_dirty;

	unsigned long cr0;
	unsigned long cr0_guest_owned_bits;
	unsigned long cr2;
	unsigned long cr3;
	unsigned long cr4;
	unsigned long cr4_guest_owned_bits;
	unsigned long cr4_guest_rsvd_bits;
	unsigned long cr8;
	u32 host_pkru;
	u32 pkru;
	u32 hflags;
	u64 efer;
	u64 apic_base;
	struct kvm_lapic *apic;    /* kernel irqchip context */
	bool apicv_active;
	bool load_eoi_exitmap_pending;
	DECLARE_BITMAP(ioapic_handled_vectors, 256);
	unsigned long apic_attention;
	int32_t apic_arb_prio;
	int mp_state;
	u64 ia32_misc_enable_msr;
	u64 smbase;
	u64 smi_count;
	bool tpr_access_reporting;
	bool xsaves_enabled;
	u64 ia32_xss;
	u64 microcode_version;
	u64 arch_capabilities;
	u64 perf_capabilities;

	/*
	 * Paging state of the vcpu
	 *
	 * If the vcpu runs in guest mode with two level paging this still saves
	 * the paging mode of the l1 guest. This context is always used to
	 * handle faults.
	 */
	struct kvm_mmu *mmu;

	/* Non-nested MMU for L1 */
	struct kvm_mmu root_mmu;

	/* L1 MMU when running nested */
	struct kvm_mmu guest_mmu;

	/*
	 * Paging state of an L2 guest (used for nested npt)
	 *
	 * This context will save all necessary information to walk page tables
	 * of an L2 guest. This context is only initialized for page table
	 * walking and not for faulting since we never handle l2 page faults on
	 * the host.
	 */
	struct kvm_mmu nested_mmu;

	/*
	 * Pointer to the mmu context currently used for
	 * gva_to_gpa translations.
	 */
	struct kvm_mmu *walk_mmu;

	struct kvm_mmu_memory_cache mmu_pte_list_desc_cache;
	struct kvm_mmu_memory_cache mmu_shadow_page_cache;
	struct kvm_mmu_memory_cache mmu_gfn_array_cache;
	struct kvm_mmu_memory_cache mmu_page_header_cache;

	/*
	 * QEMU userspace and the guest each have their own FPU state.
	 * In vcpu_run, we switch between the user and guest FPU contexts.
	 * While running a VCPU, the VCPU thread will have the guest FPU
	 * context.
	 *
	 * Note that while the PKRU state lives inside the fpu registers,
	 * it is switched out separately at VMENTER and VMEXIT time. The
	 * "guest_fpu" state here contains the guest FPU context, with the
	 * host PRKU bits.
	 */
	struct fpu *user_fpu;
	struct fpu *guest_fpu;

	u64 xcr0;
	u64 guest_supported_xcr0;

	struct kvm_pio_request pio;
	void *pio_data;

	u8 event_exit_inst_len;

	struct kvm_queued_exception {
		bool pending;
		bool injected;
		bool has_error_code;
		u8 nr;
		u32 error_code;
		unsigned long payload;
		bool has_payload;
		u8 nested_apf;
	} exception;

	struct kvm_queued_interrupt {
		bool injected;
		bool soft;
		u8 nr;
	} interrupt;

	int halt_request; /* real mode on Intel only */

	int cpuid_nent;
	struct kvm_cpuid_entry2 *cpuid_entries;

	unsigned long cr3_lm_rsvd_bits;
	int maxphyaddr;
	int max_tdp_level;

	/* emulate context */

	struct x86_emulate_ctxt *emulate_ctxt;
	bool emulate_regs_need_sync_to_vcpu;
	bool emulate_regs_need_sync_from_vcpu;
	int (*complete_userspace_io)(struct kvm_vcpu *vcpu);

	gpa_t time;
	struct pvclock_vcpu_time_info hv_clock;
	unsigned int hw_tsc_khz;
	struct gfn_to_hva_cache pv_time;
	bool pv_time_enabled;
	/* set guest stopped flag in pvclock flags field */
	bool pvclock_set_guest_stopped_request;

	struct {
		u8 preempted;
		u64 msr_val;
		u64 last_steal;
		struct gfn_to_pfn_cache cache;
	} st;

	u64 l1_tsc_offset;
	u64 tsc_offset;
	u64 last_guest_tsc;
	u64 last_host_tsc;
	u64 tsc_offset_adjustment;
	u64 this_tsc_nsec;
	u64 this_tsc_write;
	u64 this_tsc_generation;
	bool tsc_catchup;
	bool tsc_always_catchup;
	s8 virtual_tsc_shift;
	u32 virtual_tsc_mult;
	u32 virtual_tsc_khz;
	s64 ia32_tsc_adjust_msr;
	u64 msr_ia32_power_ctl;
	u64 tsc_scaling_ratio;

	atomic_t nmi_queued;  /* unprocessed asynchronous NMIs */
	unsigned nmi_pending; /* NMI queued after currently running handler */
	bool nmi_injected;    /* Trying to inject an NMI this entry */
	bool smi_pending;    /* SMI queued after currently running handler */

	struct kvm_mtrr mtrr_state;
	u64 pat;

	unsigned switch_db_regs;
	unsigned long db[KVM_NR_DB_REGS];
	unsigned long dr6;
	unsigned long dr7;
	unsigned long eff_db[KVM_NR_DB_REGS];
	unsigned long guest_debug_dr7;
	u64 msr_platform_info;
	u64 msr_misc_features_enables;

	u64 mcg_cap;
	u64 mcg_status;
	u64 mcg_ctl;
	u64 mcg_ext_ctl;
	u64 *mce_banks;

	/* Cache MMIO info */
	u64 mmio_gva;
	unsigned mmio_access;
	gfn_t mmio_gfn;
	u64 mmio_gen;

	struct kvm_pmu pmu;

	/* used for guest single stepping over the given code position */
	unsigned long singlestep_rip;

	struct kvm_vcpu_hv hyperv;

	cpumask_var_t wbinvd_dirty_mask;

	unsigned long last_retry_eip;
	unsigned long last_retry_addr;

	struct {
		bool halted;
		gfn_t gfns[ASYNC_PF_PER_VCPU];
		struct gfn_to_hva_cache data;
		u64 msr_en_val; /* MSR_KVM_ASYNC_PF_EN */
		u64 msr_int_val; /* MSR_KVM_ASYNC_PF_INT */
		u16 vec;
		u32 id;
		bool send_user_only;
		u32 host_apf_flags;
		unsigned long nested_apf_token;
		bool delivery_as_pf_vmexit;
		bool pageready_pending;
	} apf;

	/* OSVW MSRs (AMD only) */
	struct {
		u64 length;
		u64 status;
	} osvw;

	struct {
		u64 msr_val;
		struct gfn_to_hva_cache data;
	} pv_eoi;

	u64 msr_kvm_poll_control;

	/*
	 * Indicates the guest is trying to write a gfn that contains one or
	 * more of the PTEs used to translate the write itself, i.e. the access
	 * is changing its own translation in the guest page tables.  KVM exits
	 * to userspace if emulation of the faulting instruction fails and this
	 * flag is set, as KVM cannot make forward progress.
	 *
	 * If emulation fails for a write to guest page tables, KVM unprotects
	 * (zaps) the shadow page for the target gfn and resumes the guest to
	 * retry the non-emulatable instruction (on hardware).  Unprotecting the
	 * gfn doesn't allow forward progress for a self-changing access because
	 * doing so also zaps the translation for the gfn, i.e. retrying the
	 * instruction will hit a !PRESENT fault, which results in a new shadow
	 * page and sends KVM back to square one.
	 */
	bool write_fault_to_shadow_pgtable;

	/* set at EPT violation at this point */
	unsigned long exit_qualification;

	/* pv related host specific info */
	struct {
		bool pv_unhalted;
	} pv;

	int pending_ioapic_eoi;
	int pending_external_vector;

	/* be preempted when it's in kernel-mode(cpl=0) */
	bool preempted_in_kernel;

	/* Flush the L1 Data cache for L1TF mitigation on VMENTER */
	bool l1tf_flush_l1d;

	/* Host CPU on which VM-entry was most recently attempted */
	unsigned int last_vmentry_cpu;

	/* AMD MSRC001_0015 Hardware Configuration */
	u64 msr_hwcr;

	/* pv related cpuid info */
	struct {
		/*
		 * value of the eax register in the KVM_CPUID_FEATURES CPUID
		 * leaf.
		 */
		u32 features;

		/*
		 * indicates whether pv emulation should be disabled if features
		 * are not present in the guest's cpuid
		 */
		bool enforce;
	} pv_cpuid;
};

struct kvm_lpage_info {
	int disallow_lpage;
};

struct kvm_arch_memory_slot {
	struct kvm_rmap_head *rmap[KVM_NR_PAGE_SIZES];
	struct kvm_lpage_info *lpage_info[KVM_NR_PAGE_SIZES - 1];
	unsigned short *gfn_track[KVM_PAGE_TRACK_MAX];
};

/*
 * We use as the mode the number of bits allocated in the LDR for the
 * logical processor ID.  It happens that these are all powers of two.
 * This makes it is very easy to detect cases where the APICs are
 * configured for multiple modes; in that case, we cannot use the map and
 * hence cannot use kvm_irq_delivery_to_apic_fast either.
 */
#define KVM_APIC_MODE_XAPIC_CLUSTER          4
#define KVM_APIC_MODE_XAPIC_FLAT             8
#define KVM_APIC_MODE_X2APIC                16

struct kvm_apic_map {
	struct rcu_head rcu;
	u8 mode;
	u32 max_apic_id;
	union {
		struct kvm_lapic *xapic_flat_map[8];
		struct kvm_lapic *xapic_cluster_map[16][4];
	};
	struct kvm_lapic *phys_map[];
};

/* Hyper-V synthetic debugger (SynDbg)*/
struct kvm_hv_syndbg {
	struct {
		u64 control;
		u64 status;
		u64 send_page;
		u64 recv_page;
		u64 pending_page;
	} control;
	u64 options;
};

/* Hyper-V emulation context */
struct kvm_hv {
	struct mutex hv_lock;
	u64 hv_guest_os_id;
	u64 hv_hypercall;
	u64 hv_tsc_page;

	/* Hyper-v based guest crash (NT kernel bugcheck) parameters */
	u64 hv_crash_param[HV_X64_MSR_CRASH_PARAMS];
	u64 hv_crash_ctl;

	struct ms_hyperv_tsc_page tsc_ref;

	struct idr conn_to_evt;

	u64 hv_reenlightenment_control;
	u64 hv_tsc_emulation_control;
	u64 hv_tsc_emulation_status;

	/* How many vCPUs have VP index != vCPU index */
	atomic_t num_mismatched_vp_indexes;

	struct hv_partition_assist_pg *hv_pa_pg;
	struct kvm_hv_syndbg hv_syndbg;
};

struct msr_bitmap_range {
	u32 flags;
	u32 nmsrs;
	u32 base;
	unsigned long *bitmap;
};

enum kvm_irqchip_mode {
	KVM_IRQCHIP_NONE,
	KVM_IRQCHIP_KERNEL,       /* created with KVM_CREATE_IRQCHIP */
	KVM_IRQCHIP_SPLIT,        /* created with KVM_CAP_SPLIT_IRQCHIP */
};

struct kvm_x86_msr_filter {
	u8 count;
	bool default_allow:1;
	struct msr_bitmap_range ranges[16];
};

#define APICV_INHIBIT_REASON_DISABLE    0
#define APICV_INHIBIT_REASON_HYPERV     1
#define APICV_INHIBIT_REASON_NESTED     2
#define APICV_INHIBIT_REASON_IRQWIN     3
#define APICV_INHIBIT_REASON_PIT_REINJ  4
#define APICV_INHIBIT_REASON_X2APIC	5

struct kvm_arch {
	unsigned long n_used_mmu_pages;
	unsigned long n_requested_mmu_pages;
	unsigned long n_max_mmu_pages;
	unsigned int indirect_shadow_pages;
	u8 mmu_valid_gen;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	/*
	 * Hash table of struct kvm_mmu_page.
	 */
	struct list_head active_mmu_pages;
	struct list_head zapped_obsolete_pages;
	struct list_head lpage_disallowed_mmu_pages;
	struct kvm_page_track_notifier_node mmu_sp_tracker;
	struct kvm_page_track_notifier_head track_notifier_head;

	struct list_head assigned_dev_head;
	struct iommu_domain *iommu_domain;
	bool iommu_noncoherent;
#define __KVM_HAVE_ARCH_NONCOHERENT_DMA
	atomic_t noncoherent_dma_count;
#define __KVM_HAVE_ARCH_ASSIGNED_DEVICE
	atomic_t assigned_device_count;
	struct kvm_pic *vpic;
	struct kvm_ioapic *vioapic;
	struct kvm_pit *vpit;
	atomic_t vapics_in_nmi_mode;
	struct mutex apic_map_lock;
	struct kvm_apic_map *apic_map;
	atomic_t apic_map_dirty;

	bool apic_access_page_done;
	unsigned long apicv_inhibit_reasons;

	gpa_t wall_clock;

	bool mwait_in_guest;
	bool hlt_in_guest;
	bool pause_in_guest;
	bool cstate_in_guest;

	unsigned long irq_sources_bitmap;
	s64 kvmclock_offset;
	raw_spinlock_t tsc_write_lock;
	u64 last_tsc_nsec;
	u64 last_tsc_write;
	u32 last_tsc_khz;
	u64 cur_tsc_nsec;
	u64 cur_tsc_write;
	u64 cur_tsc_offset;
	u64 cur_tsc_generation;
	int nr_vcpus_matched_tsc;

	spinlock_t pvclock_gtod_sync_lock;
	bool use_master_clock;
	u64 master_kernel_ns;
	u64 master_cycle_now;
	struct delayed_work kvmclock_update_work;
	struct delayed_work kvmclock_sync_work;

	struct kvm_xen_hvm_config xen_hvm_config;

	/* reads protected by irq_srcu, writes by irq_lock */
	struct hlist_head mask_notifier_list;

	struct kvm_hv hyperv;

	#ifdef CONFIG_KVM_MMU_AUDIT
	int audit_point;
	#endif

	bool backwards_tsc_observed;
	bool boot_vcpu_runs_old_kvmclock;
	u32 bsp_vcpu_id;

	u64 disabled_quirks;

	enum kvm_irqchip_mode irqchip_mode;
	u8 nr_reserved_ioapic_pins;

	bool disabled_lapic_found;

	bool x2apic_format;
	bool x2apic_broadcast_quirk_disabled;

	bool guest_can_read_msr_platform_info;
	bool exception_payload_enabled;

	bool bus_lock_detection_enabled;

	/* Deflect RDMSR and WRMSR to user space when they trigger a #GP */
	u32 user_space_msr_mask;

	struct kvm_x86_msr_filter __rcu *msr_filter;

	struct kvm_pmu_event_filter *pmu_event_filter;
	struct task_struct *nx_lpage_recovery_thread;

	/*
	 * Whether the TDP MMU is enabled for this VM. This contains a
	 * snapshot of the TDP MMU module parameter from when the VM was
	 * created and remains unchanged for the life of the VM. If this is
	 * true, TDP MMU handler functions will run for various MMU
	 * operations.
	 */
	bool tdp_mmu_enabled;

	/* List of struct tdp_mmu_pages being used as roots */
	struct list_head tdp_mmu_roots;
	/* List of struct tdp_mmu_pages not being used as roots */
	struct list_head tdp_mmu_pages;
};

struct kvm_vm_stat {
	ulong mmu_shadow_zapped;
	ulong mmu_pte_write;
	ulong mmu_pde_zapped;
	ulong mmu_flooded;
	ulong mmu_recycled;
	ulong mmu_cache_miss;
	ulong mmu_unsync;
	ulong remote_tlb_flush;
	ulong lpages;
	ulong nx_lpage_splits;
	ulong max_mmu_page_hash_collisions;
};

struct kvm_vcpu_stat {
	u64 pf_fixed;
	u64 pf_guest;
	u64 tlb_flush;
	u64 invlpg;

	u64 exits;
	u64 io_exits;
	u64 mmio_exits;
	u64 signal_exits;
	u64 irq_window_exits;
	u64 nmi_window_exits;
	u64 l1d_flush;
	u64 halt_exits;
	u64 halt_successful_poll;
	u64 halt_attempted_poll;
	u64 halt_poll_invalid;
	u64 halt_wakeup;
	u64 request_irq_exits;
	u64 irq_exits;
	u64 host_state_reload;
	u64 fpu_reload;
	u64 insn_emulation;
	u64 insn_emulation_fail;
	u64 hypercalls;
	u64 irq_injections;
	u64 nmi_injections;
	u64 req_event;
	u64 halt_poll_success_ns;
	u64 halt_poll_fail_ns;
};

struct x86_instruction_info;

struct msr_data {
	bool host_initiated;
	u32 index;
	u64 data;
};

struct kvm_lapic_irq {
	u32 vector;
	u16 delivery_mode;
	u16 dest_mode;
	bool level;
	u16 trig_mode;
	u32 shorthand;
	u32 dest_id;
	bool msi_redir_hint;
};

static inline u16 kvm_lapic_irq_dest_mode(bool dest_mode_logical)
{
	return dest_mode_logical ? APIC_DEST_LOGICAL : APIC_DEST_PHYSICAL;
}

struct kvm_x86_ops {
	int (*hardware_enable)(void);
	void (*hardware_disable)(void);
	void (*hardware_unsetup)(void);
	bool (*cpu_has_accelerated_tpr)(void);
	bool (*has_emulated_msr)(u32 index);
	void (*vcpu_after_set_cpuid)(struct kvm_vcpu *vcpu);

	unsigned int vm_size;
	int (*vm_init)(struct kvm *kvm);
	void (*vm_destroy)(struct kvm *kvm);

	/* Create, but do not attach this VCPU */
	int (*vcpu_create)(struct kvm_vcpu *vcpu);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	void (*vcpu_reset)(struct kvm_vcpu *vcpu, bool init_event);

	void (*prepare_guest_switch)(struct kvm_vcpu *vcpu);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);

	void (*update_exception_bitmap)(struct kvm_vcpu *vcpu);
	int (*get_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr);
	int (*set_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr);
	u64 (*get_segment_base)(struct kvm_vcpu *vcpu, int seg);
	void (*get_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	int (*get_cpl)(struct kvm_vcpu *vcpu);
	void (*set_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	void (*get_cs_db_l_bits)(struct kvm_vcpu *vcpu, int *db, int *l);
	void (*set_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	int (*set_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
	int (*set_efer)(struct kvm_vcpu *vcpu, u64 efer);
	void (*get_idt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*set_idt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*get_gdt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*set_gdt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*sync_dirty_debug_regs)(struct kvm_vcpu *vcpu);
	void (*set_dr7)(struct kvm_vcpu *vcpu, unsigned long value);
	void (*cache_reg)(struct kvm_vcpu *vcpu, enum kvm_reg reg);
	unsigned long (*get_rflags)(struct kvm_vcpu *vcpu);
	void (*set_rflags)(struct kvm_vcpu *vcpu, unsigned long rflags);

	void (*tlb_flush_all)(struct kvm_vcpu *vcpu);
	void (*tlb_flush_current)(struct kvm_vcpu *vcpu);
	int  (*tlb_remote_flush)(struct kvm *kvm);
	int  (*tlb_remote_flush_with_range)(struct kvm *kvm,
			struct kvm_tlb_range *range);

	/*
	 * Flush any TLB entries associated with the given GVA.
	 * Does not need to flush GPA->HPA mappings.
	 * Can potentially get non-canonical addresses through INVLPGs, which
	 * the implementation may choose to ignore if appropriate.
	 */
	void (*tlb_flush_gva)(struct kvm_vcpu *vcpu, gva_t addr);

	/*
	 * Flush any TLB entries created by the guest.  Like tlb_flush_gva(),
	 * does not need to flush GPA->HPA mappings.
	 */
	void (*tlb_flush_guest)(struct kvm_vcpu *vcpu);

	enum exit_fastpath_completion (*run)(struct kvm_vcpu *vcpu);
	int (*handle_exit)(struct kvm_vcpu *vcpu,
		enum exit_fastpath_completion exit_fastpath);
	int (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*update_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*set_interrupt_shadow)(struct kvm_vcpu *vcpu, int mask);
	u32 (*get_interrupt_shadow)(struct kvm_vcpu *vcpu);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
	void (*set_irq)(struct kvm_vcpu *vcpu);
	void (*set_nmi)(struct kvm_vcpu *vcpu);
	void (*queue_exception)(struct kvm_vcpu *vcpu);
	void (*cancel_injection)(struct kvm_vcpu *vcpu);
	int (*interrupt_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	int (*nmi_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	bool (*get_nmi_mask)(struct kvm_vcpu *vcpu);
	void (*set_nmi_mask)(struct kvm_vcpu *vcpu, bool masked);
	void (*enable_nmi_window)(struct kvm_vcpu *vcpu);
	void (*enable_irq_window)(struct kvm_vcpu *vcpu);
	void (*update_cr8_intercept)(struct kvm_vcpu *vcpu, int tpr, int irr);
	bool (*check_apicv_inhibit_reasons)(ulong bit);
	void (*pre_update_apicv_exec_ctrl)(struct kvm *kvm, bool activate);
	void (*refresh_apicv_exec_ctrl)(struct kvm_vcpu *vcpu);
	void (*hwapic_irr_update)(struct kvm_vcpu *vcpu, int max_irr);
	void (*hwapic_isr_update)(struct kvm_vcpu *vcpu, int isr);
	bool (*guest_apic_has_interrupt)(struct kvm_vcpu *vcpu);
	void (*load_eoi_exitmap)(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap);
	void (*set_virtual_apic_mode)(struct kvm_vcpu *vcpu);
	void (*set_apic_access_page_addr)(struct kvm_vcpu *vcpu);
	int (*deliver_posted_interrupt)(struct kvm_vcpu *vcpu, int vector);
	int (*sync_pir_to_irr)(struct kvm_vcpu *vcpu);
	int (*set_tss_addr)(struct kvm *kvm, unsigned int addr);
	int (*set_identity_map_addr)(struct kvm *kvm, u64 ident_addr);
	u64 (*get_mt_mask)(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio);

	void (*load_mmu_pgd)(struct kvm_vcpu *vcpu, unsigned long pgd,
			     int pgd_level);

	bool (*has_wbinvd_exit)(void);

	/* Returns actual tsc_offset set in active VMCS */
	u64 (*write_l1_tsc_offset)(struct kvm_vcpu *vcpu, u64 offset);

	/*
	 * Retrieve somewhat arbitrary exit information.  Intended to be used
	 * only from within tracepoints to avoid VMREADs when tracing is off.
	 */
	void (*get_exit_info)(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2,
			      u32 *exit_int_info, u32 *exit_int_info_err_code);

	int (*check_intercept)(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage,
			       struct x86_exception *exception);
	void (*handle_exit_irqoff)(struct kvm_vcpu *vcpu);

	void (*request_immediate_exit)(struct kvm_vcpu *vcpu);

	void (*sched_in)(struct kvm_vcpu *kvm, int cpu);

	/*
	 * Arch-specific dirty logging hooks. These hooks are only supposed to
	 * be valid if the specific arch has hardware-accelerated dirty logging
	 * mechanism. Currently only for PML on VMX.
	 *
	 *  - slot_enable_log_dirty:
	 *	called when enabling log dirty mode for the slot.
	 *  - slot_disable_log_dirty:
	 *	called when disabling log dirty mode for the slot.
	 *	also called when slot is created with log dirty disabled.
	 *  - flush_log_dirty:
	 *	called before reporting dirty_bitmap to userspace.
	 *  - enable_log_dirty_pt_masked:
	 *	called when reenabling log dirty for the GFNs in the mask after
	 *	corresponding bits are cleared in slot->dirty_bitmap.
	 */
	void (*slot_enable_log_dirty)(struct kvm *kvm,
				      struct kvm_memory_slot *slot);
	void (*slot_disable_log_dirty)(struct kvm *kvm,
				       struct kvm_memory_slot *slot);
	void (*flush_log_dirty)(struct kvm *kvm);
	void (*enable_log_dirty_pt_masked)(struct kvm *kvm,
					   struct kvm_memory_slot *slot,
					   gfn_t offset, unsigned long mask);

	/* pmu operations of sub-arch */
	const struct kvm_pmu_ops *pmu_ops;
	const struct kvm_x86_nested_ops *nested_ops;

	/*
	 * Architecture specific hooks for vCPU blocking due to
	 * HLT instruction.
	 * Returns for .pre_block():
	 *    - 0 means continue to block the vCPU.
	 *    - 1 means we cannot block the vCPU since some event
	 *        happens during this period, such as, 'ON' bit in
	 *        posted-interrupts descriptor is set.
	 */
	int (*pre_block)(struct kvm_vcpu *vcpu);
	void (*post_block)(struct kvm_vcpu *vcpu);

	void (*vcpu_blocking)(struct kvm_vcpu *vcpu);
	void (*vcpu_unblocking)(struct kvm_vcpu *vcpu);

	int (*update_pi_irte)(struct kvm *kvm, unsigned int host_irq,
			      uint32_t guest_irq, bool set);
	void (*apicv_post_state_restore)(struct kvm_vcpu *vcpu);
	bool (*dy_apicv_has_pending_interrupt)(struct kvm_vcpu *vcpu);

	int (*set_hv_timer)(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
			    bool *expired);
	void (*cancel_hv_timer)(struct kvm_vcpu *vcpu);

	void (*setup_mce)(struct kvm_vcpu *vcpu);

	int (*smi_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	int (*pre_enter_smm)(struct kvm_vcpu *vcpu, char *smstate);
	int (*pre_leave_smm)(struct kvm_vcpu *vcpu, const char *smstate);
	void (*enable_smi_window)(struct kvm_vcpu *vcpu);

	int (*mem_enc_op)(struct kvm *kvm, void __user *argp);
	int (*mem_enc_reg_region)(struct kvm *kvm, struct kvm_enc_region *argp);
	int (*mem_enc_unreg_region)(struct kvm *kvm, struct kvm_enc_region *argp);

	int (*get_msr_feature)(struct kvm_msr_entry *entry);

	bool (*can_emulate_instruction)(struct kvm_vcpu *vcpu, void *insn, int insn_len);

	bool (*apic_init_signal_blocked)(struct kvm_vcpu *vcpu);
	int (*enable_direct_tlbflush)(struct kvm_vcpu *vcpu);

	void (*migrate_timers)(struct kvm_vcpu *vcpu);
	void (*msr_filter_changed)(struct kvm_vcpu *vcpu);
};

struct kvm_x86_nested_ops {
	int (*check_events)(struct kvm_vcpu *vcpu);
	bool (*hv_timer_pending)(struct kvm_vcpu *vcpu);
	int (*get_state)(struct kvm_vcpu *vcpu,
			 struct kvm_nested_state __user *user_kvm_nested_state,
			 unsigned user_data_size);
	int (*set_state)(struct kvm_vcpu *vcpu,
			 struct kvm_nested_state __user *user_kvm_nested_state,
			 struct kvm_nested_state *kvm_state);
	bool (*get_nested_state_pages)(struct kvm_vcpu *vcpu);
	int (*write_log_dirty)(struct kvm_vcpu *vcpu, gpa_t l2_gpa);

	int (*enable_evmcs)(struct kvm_vcpu *vcpu,
			    uint16_t *vmcs_version);
	uint16_t (*get_evmcs_version)(struct kvm_vcpu *vcpu);
};

struct kvm_x86_init_ops {
	int (*cpu_has_kvm_support)(void);
	int (*disabled_by_bios)(void);
	int (*check_processor_compatibility)(void);
	int (*hardware_setup)(void);

	struct kvm_x86_ops *runtime_ops;
};

struct kvm_arch_async_pf {
	u32 token;
	gfn_t gfn;
	unsigned long cr3;
	bool direct_map;
};

extern u64 __read_mostly host_efer;
extern bool __read_mostly allow_smaller_maxphyaddr;
extern struct kvm_x86_ops kvm_x86_ops;

#define __KVM_HAVE_ARCH_VM_ALLOC
static inline struct kvm *kvm_arch_alloc_vm(void)
{
	return __vmalloc(kvm_x86_ops.vm_size, GFP_KERNEL_ACCOUNT | __GFP_ZERO);
}
void kvm_arch_free_vm(struct kvm *kvm);

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLB
static inline int kvm_arch_flush_remote_tlb(struct kvm *kvm)
{
	if (kvm_x86_ops.tlb_remote_flush &&
	    !kvm_x86_ops.tlb_remote_flush(kvm))
		return 0;
	else
		return -ENOTSUPP;
}

int kvm_mmu_module_init(void);
void kvm_mmu_module_exit(void);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu);
int kvm_mmu_create(struct kvm_vcpu *vcpu);
void kvm_mmu_init_vm(struct kvm *kvm);
void kvm_mmu_uninit_vm(struct kvm *kvm);
void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask, u64 p_mask,
		u64 acc_track_mask, u64 me_mask);

void kvm_mmu_reset_context(struct kvm_vcpu *vcpu);
void kvm_mmu_slot_remove_write_access(struct kvm *kvm,
				      struct kvm_memory_slot *memslot,
				      int start_level);
void kvm_mmu_zap_collapsible_sptes(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot);
void kvm_mmu_slot_leaf_clear_dirty(struct kvm *kvm,
				   struct kvm_memory_slot *memslot);
void kvm_mmu_slot_largepage_remove_write_access(struct kvm *kvm,
					struct kvm_memory_slot *memslot);
void kvm_mmu_slot_set_dirty(struct kvm *kvm,
			    struct kvm_memory_slot *memslot);
void kvm_mmu_clear_dirty_pt_masked(struct kvm *kvm,
				   struct kvm_memory_slot *slot,
				   gfn_t gfn_offset, unsigned long mask);
void kvm_mmu_zap_all(struct kvm *kvm);
void kvm_mmu_invalidate_mmio_sptes(struct kvm *kvm, u64 gen);
unsigned long kvm_mmu_calculate_default_mmu_pages(struct kvm *kvm);
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned long kvm_nr_mmu_pages);

int load_pdptrs(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu, unsigned long cr3);
bool pdptrs_changed(struct kvm_vcpu *vcpu);

int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			  const void *val, int bytes);

struct kvm_irq_mask_notifier {
	void (*func)(struct kvm_irq_mask_notifier *kimn, bool masked);
	int irq;
	struct hlist_node link;
};

void kvm_register_irq_mask_notifier(struct kvm *kvm, int irq,
				    struct kvm_irq_mask_notifier *kimn);
void kvm_unregister_irq_mask_notifier(struct kvm *kvm, int irq,
				      struct kvm_irq_mask_notifier *kimn);
void kvm_fire_mask_notifiers(struct kvm *kvm, unsigned irqchip, unsigned pin,
			     bool mask);

extern bool tdp_enabled;

u64 vcpu_tsc_khz(struct kvm_vcpu *vcpu);

/* control of guest tsc rate supported? */
extern bool kvm_has_tsc_control;
/* maximum supported tsc_khz for guests */
extern u32  kvm_max_guest_tsc_khz;
/* number of bits of the fractional part of the TSC scaling ratio */
extern u8   kvm_tsc_scaling_ratio_frac_bits;
/* maximum allowed value of TSC scaling ratio */
extern u64  kvm_max_tsc_scaling_ratio;
/* 1ull << kvm_tsc_scaling_ratio_frac_bits */
extern u64  kvm_default_tsc_scaling_ratio;

extern u64 kvm_mce_cap_supported;

/*
 * EMULTYPE_NO_DECODE - Set when re-emulating an instruction (after completing
 *			userspace I/O) to indicate that the emulation context
 *			should be resued as is, i.e. skip initialization of
 *			emulation context, instruction fetch and decode.
 *
 * EMULTYPE_TRAP_UD - Set when emulating an intercepted #UD from hardware.
 *		      Indicates that only select instructions (tagged with
 *		      EmulateOnUD) should be emulated (to minimize the emulator
 *		      attack surface).  See also EMULTYPE_TRAP_UD_FORCED.
 *
 * EMULTYPE_SKIP - Set when emulating solely to skip an instruction, i.e. to
 *		   decode the instruction length.  For use *only* by
 *		   kvm_x86_ops.skip_emulated_instruction() implementations.
 *
 * EMULTYPE_ALLOW_RETRY_PF - Set when the emulator should resume the guest to
 *			     retry native execution under certain conditions,
 *			     Can only be set in conjunction with EMULTYPE_PF.
 *
 * EMULTYPE_TRAP_UD_FORCED - Set when emulating an intercepted #UD that was
 *			     triggered by KVM's magic "force emulation" prefix,
 *			     which is opt in via module param (off by default).
 *			     Bypasses EmulateOnUD restriction despite emulating
 *			     due to an intercepted #UD (see EMULTYPE_TRAP_UD).
 *			     Used to test the full emulator from userspace.
 *
 * EMULTYPE_VMWARE_GP - Set when emulating an intercepted #GP for VMware
 *			backdoor emulation, which is opt in via module param.
 *			VMware backoor emulation handles select instructions
 *			and reinjects the #GP for all other cases.
 *
 * EMULTYPE_PF - Set when emulating MMIO by way of an intercepted #PF, in which
 *		 case the CR2/GPA value pass on the stack is valid.
 */
#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
#define EMULTYPE_ALLOW_RETRY_PF	    (1 << 3)
#define EMULTYPE_TRAP_UD_FORCED	    (1 << 4)
#define EMULTYPE_VMWARE_GP	    (1 << 5)
#define EMULTYPE_PF		    (1 << 6)

int kvm_emulate_instruction(struct kvm_vcpu *vcpu, int emulation_type);
int kvm_emulate_instruction_from_buffer(struct kvm_vcpu *vcpu,
					void *insn, int insn_len);

void kvm_enable_efer_bits(u64);
bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer);
int __kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data, bool host_initiated);
int kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data);
int kvm_set_msr(struct kvm_vcpu *vcpu, u32 index, u64 data);
int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu);
int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu);

int kvm_fast_pio(struct kvm_vcpu *vcpu, int size, unsigned short port, int in);
int kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int kvm_vcpu_halt(struct kvm_vcpu *vcpu);
int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu);

void kvm_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
int kvm_load_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector, int seg);
void kvm_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector);

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int idt_index,
		    int reason, bool has_error_code, u32 error_code);

int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3);
int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8);
int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val);
int kvm_get_dr(struct kvm_vcpu *vcpu, int dr, unsigned long *val);
unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw);
void kvm_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l);
int kvm_set_xcr(struct kvm_vcpu *vcpu, u32 index, u64 xcr);

int kvm_get_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu);
void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
bool kvm_rdpmc(struct kvm_vcpu *vcpu);

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_queue_exception_p(struct kvm_vcpu *vcpu, unsigned nr, unsigned long payload);
void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_requeue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault);
bool kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
				    struct x86_exception *fault);
int kvm_read_guest_page_mmu(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gfn_t gfn, void *data, int offset, int len,
			    u32 access);
bool kvm_require_cpl(struct kvm_vcpu *vcpu, int required_cpl);
bool kvm_require_dr(struct kvm_vcpu *vcpu, int dr);

static inline int __kvm_irq_line_state(unsigned long *irq_state,
				       int irq_source_id, int level)
{
	/* Logical OR for level trig interrupt */
	if (level)
		__set_bit(irq_source_id, irq_state);
	else
		__clear_bit(irq_source_id, irq_state);

	return !!(*irq_state);
}

#define KVM_MMU_ROOT_CURRENT		BIT(0)
#define KVM_MMU_ROOT_PREVIOUS(i)	BIT(1+i)
#define KVM_MMU_ROOTS_ALL		(~0UL)

int kvm_pic_set_irq(struct kvm_pic *pic, int irq, int irq_source_id, int level);
void kvm_pic_clear_all(struct kvm_pic *pic, int irq_source_id);

void kvm_inject_nmi(struct kvm_vcpu *vcpu);

void kvm_update_dr7(struct kvm_vcpu *vcpu);

int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn);
int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva);
void __kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu);
int kvm_mmu_load(struct kvm_vcpu *vcpu);
void kvm_mmu_unload(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu);
void kvm_mmu_free_roots(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			ulong roots_to_free);
gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
			   struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_fetch(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception);

bool kvm_apicv_activated(struct kvm *kvm);
void kvm_apicv_init(struct kvm *kvm, bool enable);
void kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu);
void kvm_request_apicv_update(struct kvm *kvm, bool activate,
			      unsigned long bit);

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u64 error_code,
		       void *insn, int insn_len);
void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva);
void kvm_mmu_invalidate_gva(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gva_t gva, hpa_t root_hpa);
void kvm_mmu_invpcid_gva(struct kvm_vcpu *vcpu, gva_t gva, unsigned long pcid);
void kvm_mmu_new_pgd(struct kvm_vcpu *vcpu, gpa_t new_pgd, bool skip_tlb_flush,
		     bool skip_mmu_sync);

void kvm_configure_mmu(bool enable_tdp, int tdp_max_root_level,
		       int tdp_huge_page_level);

static inline u16 kvm_read_ldt(void)
{
	u16 ldt;
	asm("sldt %0" : "=g"(ldt));
	return ldt;
}

static inline void kvm_load_ldt(u16 sel)
{
	asm("lldt %0" : : "rm"(sel));
}

#ifdef CONFIG_X86_64
static inline unsigned long read_msr(unsigned long msr)
{
	u64 value;

	rdmsrl(msr, value);
	return value;
}
#endif

static inline u32 get_rdx_init_val(void)
{
	return 0x600; /* P6 family */
}

static inline void kvm_inject_gp(struct kvm_vcpu *vcpu, u32 error_code)
{
	kvm_queue_exception_e(vcpu, GP_VECTOR, error_code);
}

#define TSS_IOPB_BASE_OFFSET 0x66
#define TSS_BASE_SIZE 0x68
#define TSS_IOPB_SIZE (65536 / 8)
#define TSS_REDIRECTION_SIZE (256 / 8)
#define RMODE_TSS_SIZE							\
	(TSS_BASE_SIZE + TSS_REDIRECTION_SIZE + TSS_IOPB_SIZE + 1)

enum {
	TASK_SWITCH_CALL = 0,
	TASK_SWITCH_IRET = 1,
	TASK_SWITCH_JMP = 2,
	TASK_SWITCH_GATE = 3,
};

#define HF_GIF_MASK		(1 << 0)
#define HF_NMI_MASK		(1 << 3)
#define HF_IRET_MASK		(1 << 4)
#define HF_GUEST_MASK		(1 << 5) /* VCPU is in guest-mode */
#define HF_SMM_MASK		(1 << 6)
#define HF_SMM_INSIDE_NMI_MASK	(1 << 7)

#define __KVM_VCPU_MULTIPLE_ADDRESS_SPACE
#define KVM_ADDRESS_SPACE_NUM 2

#define kvm_arch_vcpu_memslots_id(vcpu) ((vcpu)->arch.hflags & HF_SMM_MASK ? 1 : 0)
#define kvm_memslots_for_spte_role(kvm, role) __kvm_memslots(kvm, (role).smm)

asmlinkage void kvm_spurious_fault(void);

/*
 * Hardware virtualization extension instructions may fault if a
 * reboot turns off virtualization while processes are running.
 * Usually after catching the fault we just panic; during reboot
 * instead the instruction is ignored.
 */
#define __kvm_handle_fault_on_reboot(insn)				\
	"666: \n\t"							\
	insn "\n\t"							\
	"jmp	668f \n\t"						\
	"667: \n\t"							\
	"1: \n\t"							\
	".pushsection .discard.instr_begin \n\t"			\
	".long 1b - . \n\t"						\
	".popsection \n\t"						\
	"call	kvm_spurious_fault \n\t"				\
	"1: \n\t"							\
	".pushsection .discard.instr_end \n\t"				\
	".long 1b - . \n\t"						\
	".popsection \n\t"						\
	"668: \n\t"							\
	_ASM_EXTABLE(666b, 667b)

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end,
			unsigned flags);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);
int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int kvm_cpu_has_injectable_intr(struct kvm_vcpu *v);
int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_cpu_has_extint(struct kvm_vcpu *v);
int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);
void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event);
void kvm_vcpu_reload_apic_access_page(struct kvm_vcpu *vcpu);

int kvm_pv_send_ipi(struct kvm *kvm, unsigned long ipi_bitmap_low,
		    unsigned long ipi_bitmap_high, u32 min,
		    unsigned long icr, int op_64_bit);

void kvm_define_user_return_msr(unsigned index, u32 msr);
int kvm_probe_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned index, u64 val, u64 mask);

u64 kvm_scale_tsc(struct kvm_vcpu *vcpu, u64 tsc);
u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc);

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu);
bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip);

void kvm_make_mclock_inprogress_request(struct kvm *kvm);
void kvm_make_scan_ioapic_request(struct kvm *kvm);
void kvm_make_scan_ioapic_request_mask(struct kvm *kvm,
				       unsigned long *vcpu_bitmap);

bool kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work);
void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work);
void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
			       struct kvm_async_pf *work);
void kvm_arch_async_page_present_queued(struct kvm_vcpu *vcpu);
bool kvm_arch_can_dequeue_async_page_present(struct kvm_vcpu *vcpu);
extern bool kvm_find_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn);

int kvm_skip_emulated_instruction(struct kvm_vcpu *vcpu);
int kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err);
void __kvm_request_immediate_exit(struct kvm_vcpu *vcpu);

int kvm_is_in_guest(void);

int __x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa, u32 size);
bool kvm_vcpu_is_reset_bsp(struct kvm_vcpu *vcpu);
bool kvm_vcpu_is_bsp(struct kvm_vcpu *vcpu);

bool kvm_intr_is_single_vcpu(struct kvm *kvm, struct kvm_lapic_irq *irq,
			     struct kvm_vcpu **dest_vcpu);

void kvm_set_msi_irq(struct kvm *kvm, struct kvm_kernel_irq_routing_entry *e,
		     struct kvm_lapic_irq *irq);

static inline bool kvm_irq_is_postable(struct kvm_lapic_irq *irq)
{
	/* We can only post Fixed and LowPrio IRQs */
	return (irq->delivery_mode == APIC_DM_FIXED ||
		irq->delivery_mode == APIC_DM_LOWEST);
}

static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	if (kvm_x86_ops.vcpu_blocking)
		kvm_x86_ops.vcpu_blocking(vcpu);
}

static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	if (kvm_x86_ops.vcpu_unblocking)
		kvm_x86_ops.vcpu_unblocking(vcpu);
}

static inline void kvm_arch_vcpu_block_finish(struct kvm_vcpu *vcpu) {}

static inline int kvm_cpu_get_apicid(int mps_cpu)
{
#ifdef CONFIG_X86_LOCAL_APIC
	return default_cpu_present_to_apicid(mps_cpu);
#else
	WARN_ON_ONCE(1);
	return BAD_APICID;
#endif
}

#define put_smstate(type, buf, offset, val)                      \
	*(type *)((buf) + (offset) - 0x7e00) = val

#define GET_SMSTATE(type, buf, offset)		\
	(*(type *)((buf) + (offset) - 0x7e00))

#endif /* _ASM_X86_KVM_HOST_H */
