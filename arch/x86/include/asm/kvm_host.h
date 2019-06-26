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

#define KVM_MAX_VCPUS 288
#define KVM_SOFT_MAX_VCPUS 240
#define KVM_MAX_VCPU_ID 1023
#define KVM_USER_MEM_SLOTS 509
/* memory slots that are not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 3
#define KVM_MEM_SLOTS_NUM (KVM_USER_MEM_SLOTS + KVM_PRIVATE_MEM_SLOTS)

#define KVM_HALT_POLL_NS_DEFAULT 200000

#define KVM_IRQCHIP_NUM_PINS  KVM_IOAPIC_NUM_PINS

/* x86-specific vcpu->requests bit members */
#define KVM_REQ_MIGRATE_TIMER		KVM_ARCH_REQ(0)
#define KVM_REQ_REPORT_TPR_ACCESS	KVM_ARCH_REQ(1)
#define KVM_REQ_TRIPLE_FAULT		KVM_ARCH_REQ(2)
#define KVM_REQ_MMU_SYNC		KVM_ARCH_REQ(3)
#define KVM_REQ_CLOCK_UPDATE		KVM_ARCH_REQ(4)
#define KVM_REQ_LOAD_CR3		KVM_ARCH_REQ(5)
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
#define KVM_REQ_GET_VMCS12_PAGES	KVM_ARCH_REQ(24)

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
enum {
	PT_PAGE_TABLE_LEVEL   = 1,
	PT_DIRECTORY_LEVEL    = 2,
	PT_PDPE_LEVEL         = 3,
	/* set max level to the biggest one */
	PT_MAX_HUGEPAGE_LEVEL = PT_PDPE_LEVEL,
};
#define KVM_NR_PAGE_SIZES	(PT_MAX_HUGEPAGE_LEVEL - \
				 PT_PAGE_TABLE_LEVEL + 1)
#define KVM_HPAGE_GFN_SHIFT(x)	(((x) - 1) * 9)
#define KVM_HPAGE_SHIFT(x)	(PAGE_SHIFT + KVM_HPAGE_GFN_SHIFT(x))
#define KVM_HPAGE_SIZE(x)	(1UL << KVM_HPAGE_SHIFT(x))
#define KVM_HPAGE_MASK(x)	(~(KVM_HPAGE_SIZE(x) - 1))
#define KVM_PAGES_PER_HPAGE(x)	(KVM_HPAGE_SIZE(x) / PAGE_SIZE)

static inline gfn_t gfn_to_index(gfn_t gfn, gfn_t base_gfn, int level)
{
	/* KVM_HPAGE_GFN_SHIFT(PT_PAGE_TABLE_LEVEL) must be 0. */
	return (gfn >> KVM_HPAGE_GFN_SHIFT(level)) -
		(base_gfn >> KVM_HPAGE_GFN_SHIFT(level));
}

#define KVM_PERMILLE_MMU_PAGES 20
#define KVM_MIN_ALLOC_MMU_PAGES 64UL
#define KVM_MMU_HASH_SHIFT 12
#define KVM_NUM_MMU_PAGES (1 << KVM_MMU_HASH_SHIFT)
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 80
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
	NR_VCPU_REGS
};

enum kvm_reg_ex {
	VCPU_EXREG_PDPTR = NR_VCPU_REGS,
	VCPU_EXREG_CR3,
	VCPU_EXREG_RFLAGS,
	VCPU_EXREG_SEGMENTS,
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

#include <asm/kvm_emulate.h>

#define KVM_NR_MEM_OBJS 40

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

/*
 * The mask used to denote special SPTEs, which can be either MMIO SPTEs or
 * Access Tracking SPTEs. We use bit 62 instead of bit 63 to avoid conflicting
 * with the SVE bit in EPT PTEs.
 */
#define SPTE_SPECIAL_MASK (1ULL << 62)

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
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

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

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;
	bool unsync;
	bool mmio_cached;

	/*
	 * The following two entries are used to key the shadow page in the
	 * hash table.
	 */
	union kvm_mmu_page_role role;
	gfn_t gfn;

	u64 *spt;
	/* hold the gfn of each spte inside spt */
	gfn_t *gfns;
	int root_count;          /* Currently serving as active root */
	unsigned int unsync_children;
	struct kvm_rmap_head parent_ptes; /* rmap pointers to parent sptes */
	DECLARE_BITMAP(unsync_child_bitmap, 512);

#ifdef CONFIG_X86_32
	/*
	 * Used out of the mmu-lock to avoid reading spte values while an
	 * update is in progress; see the comments in __get_spte_lockless().
	 */
	int clear_spte_count;
#endif

	/* Number of writes since the last time traversal visited this page.  */
	atomic_t write_flooding_count;
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
	gpa_t cr3;
	hpa_t hpa;
};

#define KVM_MMU_ROOT_INFO_INVALID \
	((struct kvm_mmu_root_info) { .cr3 = INVALID_PAGE, .hpa = INVALID_PAGE })

#define KVM_MMU_NUM_PREV_ROOTS 3

/*
 * x86 supports 4 paging modes (5-level 64-bit, 4-level 64-bit, 3-level 32-bit,
 * and 2-level 32-bit).  The kvm_mmu structure abstracts the details of the
 * current mmu mode.
 */
struct kvm_mmu {
	void (*set_cr3)(struct kvm_vcpu *vcpu, unsigned long root);
	unsigned long (*get_cr3)(struct kvm_vcpu *vcpu);
	u64 (*get_pdptr)(struct kvm_vcpu *vcpu, int index);
	int (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err,
			  bool prefault);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  struct x86_exception *fault);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva, u32 access,
			    struct x86_exception *exception);
	gpa_t (*translate_gpa)(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
			       struct x86_exception *exception);
	int (*sync_page)(struct kvm_vcpu *vcpu,
			 struct kvm_mmu_page *sp);
	void (*invlpg)(struct kvm_vcpu *vcpu, gva_t gva, hpa_t root_hpa);
	void (*update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			   u64 *spte, const void *pte);
	hpa_t root_hpa;
	gpa_t root_cr3;
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
	u64 reprogram_pmi;
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
	unsigned long cr8;
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
	u64 ia32_xss;
	u64 microcode_version;
	u64 arch_capabilities;

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
	 * of the an L2 guest. This context is only initialized for page table
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
	struct kvm_mmu_memory_cache mmu_page_cache;
	struct kvm_mmu_memory_cache mmu_page_header_cache;

	/*
	 * QEMU userspace and the guest each have their own FPU state.
	 * In vcpu_run, we switch between the user, maintained in the
	 * task_struct struct, and guest FPU contexts. While running a VCPU,
	 * the VCPU thread will have the guest FPU context.
	 *
	 * Note that while the PKRU state lives inside the fpu registers,
	 * it is switched out separately at VMENTER and VMEXIT time. The
	 * "guest_fpu" state here contains the guest FPU context, with the
	 * host PRKU bits.
	 */
	struct fpu *guest_fpu;

	u64 xcr0;
	u64 guest_supported_xcr0;
	u32 guest_xstate_size;

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
	struct kvm_cpuid_entry2 cpuid_entries[KVM_MAX_CPUID_ENTRIES];

	int maxphyaddr;

	/* emulate context */

	struct x86_emulate_ctxt emulate_ctxt;
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
		u64 msr_val;
		u64 last_steal;
		struct gfn_to_hva_cache stime;
		struct kvm_steal_time steal;
	} st;

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
	unsigned access;
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
		gfn_t gfns[roundup_pow_of_two(ASYNC_PF_PER_VCPU)];
		struct gfn_to_hva_cache data;
		u64 msr_val;
		u32 id;
		bool send_user_only;
		u32 host_apf_reason;
		unsigned long nested_apf_token;
		bool delivery_as_pf_vmexit;
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

	/*
	 * Indicate whether the access faults on its page table in guest
	 * which is set when fix page fault and used to detect unhandeable
	 * instruction.
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

	/* GPA available */
	bool gpa_available;
	gpa_t gpa_val;

	/* be preempted when it's in kernel-mode(cpl=0) */
	bool preempted_in_kernel;

	/* Flush the L1 Data cache for L1TF mitigation on VMENTER */
	bool l1tf_flush_l1d;

	/* AMD MSRC001_0015 Hardware Configuration */
	u64 msr_hwcr;
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

/* Hyper-V emulation context */
struct kvm_hv {
	struct mutex hv_lock;
	u64 hv_guest_os_id;
	u64 hv_hypercall;
	u64 hv_tsc_page;

	/* Hyper-v based guest crash (NT kernel bugcheck) parameters */
	u64 hv_crash_param[HV_X64_MSR_CRASH_PARAMS];
	u64 hv_crash_ctl;

	HV_REFERENCE_TSC_PAGE tsc_ref;

	struct idr conn_to_evt;

	u64 hv_reenlightenment_control;
	u64 hv_tsc_emulation_control;
	u64 hv_tsc_emulation_status;

	/* How many vCPUs have VP index != vCPU index */
	atomic_t num_mismatched_vp_indexes;
};

enum kvm_irqchip_mode {
	KVM_IRQCHIP_NONE,
	KVM_IRQCHIP_KERNEL,       /* created with KVM_CREATE_IRQCHIP */
	KVM_IRQCHIP_SPLIT,        /* created with KVM_CAP_SPLIT_IRQCHIP */
};

struct kvm_arch {
	unsigned long n_used_mmu_pages;
	unsigned long n_requested_mmu_pages;
	unsigned long n_max_mmu_pages;
	unsigned int indirect_shadow_pages;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	/*
	 * Hash table of struct kvm_mmu_page.
	 */
	struct list_head active_mmu_pages;
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

	bool apic_access_page_done;

	gpa_t wall_clock;

	bool mwait_in_guest;
	bool hlt_in_guest;
	bool pause_in_guest;

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
};

struct kvm_vm_stat {
	ulong mmu_shadow_zapped;
	ulong mmu_pte_write;
	ulong mmu_pte_updated;
	ulong mmu_pde_zapped;
	ulong mmu_flooded;
	ulong mmu_recycled;
	ulong mmu_cache_miss;
	ulong mmu_unsync;
	ulong remote_tlb_flush;
	ulong lpages;
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

struct kvm_x86_ops {
	int (*cpu_has_kvm_support)(void);          /* __init */
	int (*disabled_by_bios)(void);             /* __init */
	int (*hardware_enable)(void);
	void (*hardware_disable)(void);
	void (*check_processor_compatibility)(void *rtn);
	int (*hardware_setup)(void);               /* __init */
	void (*hardware_unsetup)(void);            /* __exit */
	bool (*cpu_has_accelerated_tpr)(void);
	bool (*has_emulated_msr)(int index);
	void (*cpuid_update)(struct kvm_vcpu *vcpu);

	struct kvm *(*vm_alloc)(void);
	void (*vm_free)(struct kvm *);
	int (*vm_init)(struct kvm *kvm);
	void (*vm_destroy)(struct kvm *kvm);

	/* Create, but do not attach this VCPU */
	struct kvm_vcpu *(*vcpu_create)(struct kvm *kvm, unsigned id);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	void (*vcpu_reset)(struct kvm_vcpu *vcpu, bool init_event);

	void (*prepare_guest_switch)(struct kvm_vcpu *vcpu);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);

	void (*update_bp_intercept)(struct kvm_vcpu *vcpu);
	int (*get_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr);
	int (*set_msr)(struct kvm_vcpu *vcpu, struct msr_data *msr);
	u64 (*get_segment_base)(struct kvm_vcpu *vcpu, int seg);
	void (*get_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	int (*get_cpl)(struct kvm_vcpu *vcpu);
	void (*set_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	void (*get_cs_db_l_bits)(struct kvm_vcpu *vcpu, int *db, int *l);
	void (*decache_cr0_guest_bits)(struct kvm_vcpu *vcpu);
	void (*decache_cr3)(struct kvm_vcpu *vcpu);
	void (*decache_cr4_guest_bits)(struct kvm_vcpu *vcpu);
	void (*set_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	void (*set_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);
	int (*set_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
	void (*set_efer)(struct kvm_vcpu *vcpu, u64 efer);
	void (*get_idt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*set_idt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*get_gdt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	void (*set_gdt)(struct kvm_vcpu *vcpu, struct desc_ptr *dt);
	u64 (*get_dr6)(struct kvm_vcpu *vcpu);
	void (*set_dr6)(struct kvm_vcpu *vcpu, unsigned long value);
	void (*sync_dirty_debug_regs)(struct kvm_vcpu *vcpu);
	void (*set_dr7)(struct kvm_vcpu *vcpu, unsigned long value);
	void (*cache_reg)(struct kvm_vcpu *vcpu, enum kvm_reg reg);
	unsigned long (*get_rflags)(struct kvm_vcpu *vcpu);
	void (*set_rflags)(struct kvm_vcpu *vcpu, unsigned long rflags);

	void (*tlb_flush)(struct kvm_vcpu *vcpu, bool invalidate_gpa);
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

	void (*run)(struct kvm_vcpu *vcpu);
	int (*handle_exit)(struct kvm_vcpu *vcpu);
	void (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*set_interrupt_shadow)(struct kvm_vcpu *vcpu, int mask);
	u32 (*get_interrupt_shadow)(struct kvm_vcpu *vcpu);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
	void (*set_irq)(struct kvm_vcpu *vcpu);
	void (*set_nmi)(struct kvm_vcpu *vcpu);
	void (*queue_exception)(struct kvm_vcpu *vcpu);
	void (*cancel_injection)(struct kvm_vcpu *vcpu);
	int (*interrupt_allowed)(struct kvm_vcpu *vcpu);
	int (*nmi_allowed)(struct kvm_vcpu *vcpu);
	bool (*get_nmi_mask)(struct kvm_vcpu *vcpu);
	void (*set_nmi_mask)(struct kvm_vcpu *vcpu, bool masked);
	void (*enable_nmi_window)(struct kvm_vcpu *vcpu);
	void (*enable_irq_window)(struct kvm_vcpu *vcpu);
	void (*update_cr8_intercept)(struct kvm_vcpu *vcpu, int tpr, int irr);
	bool (*get_enable_apicv)(struct kvm_vcpu *vcpu);
	void (*refresh_apicv_exec_ctrl)(struct kvm_vcpu *vcpu);
	void (*hwapic_irr_update)(struct kvm_vcpu *vcpu, int max_irr);
	void (*hwapic_isr_update)(struct kvm_vcpu *vcpu, int isr);
	bool (*guest_apic_has_interrupt)(struct kvm_vcpu *vcpu);
	void (*load_eoi_exitmap)(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap);
	void (*set_virtual_apic_mode)(struct kvm_vcpu *vcpu);
	void (*set_apic_access_page_addr)(struct kvm_vcpu *vcpu, hpa_t hpa);
	void (*deliver_posted_interrupt)(struct kvm_vcpu *vcpu, int vector);
	int (*sync_pir_to_irr)(struct kvm_vcpu *vcpu);
	int (*set_tss_addr)(struct kvm *kvm, unsigned int addr);
	int (*set_identity_map_addr)(struct kvm *kvm, u64 ident_addr);
	int (*get_tdp_level)(struct kvm_vcpu *vcpu);
	u64 (*get_mt_mask)(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio);
	int (*get_lpage_level)(void);
	bool (*rdtscp_supported)(void);
	bool (*invpcid_supported)(void);

	void (*set_tdp_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);

	void (*set_supported_cpuid)(u32 func, struct kvm_cpuid_entry2 *entry);

	bool (*has_wbinvd_exit)(void);

	u64 (*read_l1_tsc_offset)(struct kvm_vcpu *vcpu);
	/* Returns actual tsc_offset set in active VMCS */
	u64 (*write_l1_tsc_offset)(struct kvm_vcpu *vcpu, u64 offset);

	void (*get_exit_info)(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2);

	int (*check_intercept)(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage);
	void (*handle_external_intr)(struct kvm_vcpu *vcpu);
	bool (*mpx_supported)(void);
	bool (*xsaves_supported)(void);
	bool (*umip_emulated)(void);
	bool (*pt_supported)(void);

	int (*check_nested_events)(struct kvm_vcpu *vcpu, bool external_intr);
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
	int (*write_log_dirty)(struct kvm_vcpu *vcpu);

	/* pmu operations of sub-arch */
	const struct kvm_pmu_ops *pmu_ops;

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

	int (*set_hv_timer)(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
			    bool *expired);
	void (*cancel_hv_timer)(struct kvm_vcpu *vcpu);

	void (*setup_mce)(struct kvm_vcpu *vcpu);

	int (*get_nested_state)(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				unsigned user_data_size);
	int (*set_nested_state)(struct kvm_vcpu *vcpu,
				struct kvm_nested_state __user *user_kvm_nested_state,
				struct kvm_nested_state *kvm_state);
	void (*get_vmcs12_pages)(struct kvm_vcpu *vcpu);

	int (*smi_allowed)(struct kvm_vcpu *vcpu);
	int (*pre_enter_smm)(struct kvm_vcpu *vcpu, char *smstate);
	int (*pre_leave_smm)(struct kvm_vcpu *vcpu, const char *smstate);
	int (*enable_smi_window)(struct kvm_vcpu *vcpu);

	int (*mem_enc_op)(struct kvm *kvm, void __user *argp);
	int (*mem_enc_reg_region)(struct kvm *kvm, struct kvm_enc_region *argp);
	int (*mem_enc_unreg_region)(struct kvm *kvm, struct kvm_enc_region *argp);

	int (*get_msr_feature)(struct kvm_msr_entry *entry);

	int (*nested_enable_evmcs)(struct kvm_vcpu *vcpu,
				   uint16_t *vmcs_version);
	uint16_t (*nested_get_evmcs_version)(struct kvm_vcpu *vcpu);

	bool (*need_emulation_on_page_fault)(struct kvm_vcpu *vcpu);
};

struct kvm_arch_async_pf {
	u32 token;
	gfn_t gfn;
	unsigned long cr3;
	bool direct_map;
};

extern struct kvm_x86_ops *kvm_x86_ops;
extern struct kmem_cache *x86_fpu_cache;

#define __KVM_HAVE_ARCH_VM_ALLOC
static inline struct kvm *kvm_arch_alloc_vm(void)
{
	return kvm_x86_ops->vm_alloc();
}

static inline void kvm_arch_free_vm(struct kvm *kvm)
{
	return kvm_x86_ops->vm_free(kvm);
}

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLB
static inline int kvm_arch_flush_remote_tlb(struct kvm *kvm)
{
	if (kvm_x86_ops->tlb_remote_flush &&
	    !kvm_x86_ops->tlb_remote_flush(kvm))
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
				      struct kvm_memory_slot *memslot);
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

enum emulation_result {
	EMULATE_DONE,         /* no further processing */
	EMULATE_USER_EXIT,    /* kvm_run ready for userspace exit */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
#define EMULTYPE_ALLOW_RETRY	    (1 << 3)
#define EMULTYPE_NO_UD_ON_FAIL	    (1 << 4)
#define EMULTYPE_VMWARE		    (1 << 5)
int kvm_emulate_instruction(struct kvm_vcpu *vcpu, int emulation_type);
int kvm_emulate_instruction_from_buffer(struct kvm_vcpu *vcpu,
					void *insn, int insn_len);

void kvm_enable_efer_bits(u64);
bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer);
int kvm_get_msr(struct kvm_vcpu *vcpu, struct msr_data *msr);
int kvm_set_msr(struct kvm_vcpu *vcpu, struct msr_data *msr);

struct x86_emulate_ctxt;

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
void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_requeue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault);
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

void kvm_vcpu_deactivate_apicv(struct kvm_vcpu *vcpu);

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t gva, u64 error_code,
		       void *insn, int insn_len);
void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva);
void kvm_mmu_invpcid_gva(struct kvm_vcpu *vcpu, gva_t gva, unsigned long pcid);
void kvm_mmu_new_cr3(struct kvm_vcpu *vcpu, gpa_t new_cr3, bool skip_tlb_flush);

void kvm_enable_tdp(void);
void kvm_disable_tdp(void);

static inline gpa_t translate_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access,
				  struct x86_exception *exception)
{
	return gpa;
}

static inline struct kvm_mmu_page *page_header(hpa_t shadow_page)
{
	struct page *page = pfn_to_page(shadow_page >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

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
#define HF_HIF_MASK		(1 << 1)
#define HF_VINTR_MASK		(1 << 2)
#define HF_NMI_MASK		(1 << 3)
#define HF_IRET_MASK		(1 << 4)
#define HF_GUEST_MASK		(1 << 5) /* VCPU is in guest-mode */
#define HF_SMM_MASK		(1 << 6)
#define HF_SMM_INSIDE_NMI_MASK	(1 << 7)

#define __KVM_VCPU_MULTIPLE_ADDRESS_SPACE
#define KVM_ADDRESS_SPACE_NUM 2

#define kvm_arch_vcpu_memslots_id(vcpu) ((vcpu)->arch.hflags & HF_SMM_MASK ? 1 : 0)
#define kvm_memslots_for_spte_role(kvm, role) __kvm_memslots(kvm, (role).smm)

/*
 * Hardware virtualization extension instructions may fault if a
 * reboot turns off virtualization while processes are running.
 * Trap the fault and ignore the instruction if that happens.
 */
asmlinkage void kvm_spurious_fault(void);

#define ____kvm_handle_fault_on_reboot(insn, cleanup_insn)	\
	"666: " insn "\n\t" \
	"668: \n\t"                           \
	".pushsection .fixup, \"ax\" \n" \
	"667: \n\t" \
	cleanup_insn "\n\t"		      \
	"cmpb $0, kvm_rebooting \n\t"	      \
	"jne 668b \n\t"      		      \
	__ASM_SIZE(push) " $666b \n\t"	      \
	"jmp kvm_spurious_fault \n\t"	      \
	".popsection \n\t" \
	_ASM_EXTABLE(666b, 667b)

#define __kvm_handle_fault_on_reboot(insn)		\
	____kvm_handle_fault_on_reboot(insn, "")

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva_range(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_age_hva(struct kvm *kvm, unsigned long start, unsigned long end);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);
int kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int kvm_cpu_has_injectable_intr(struct kvm_vcpu *v);
int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);
void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event);
void kvm_vcpu_reload_apic_access_page(struct kvm_vcpu *vcpu);

int kvm_pv_send_ipi(struct kvm *kvm, unsigned long ipi_bitmap_low,
		    unsigned long ipi_bitmap_high, u32 min,
		    unsigned long icr, int op_64_bit);

u64 kvm_get_arch_capabilities(void);
void kvm_define_shared_msr(unsigned index, u32 msr);
int kvm_set_shared_msr(unsigned index, u64 val, u64 mask);

u64 kvm_scale_tsc(struct kvm_vcpu *vcpu, u64 tsc);
u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc);

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu);
bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip);

void kvm_make_mclock_inprogress_request(struct kvm *kvm);
void kvm_make_scan_ioapic_request(struct kvm *kvm);

void kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work);
void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work);
void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
			       struct kvm_async_pf *work);
bool kvm_arch_can_inject_async_page_present(struct kvm_vcpu *vcpu);
extern bool kvm_find_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn);

int kvm_skip_emulated_instruction(struct kvm_vcpu *vcpu);
int kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err);
void __kvm_request_immediate_exit(struct kvm_vcpu *vcpu);

int kvm_is_in_guest(void);

int __x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa, u32 size);
int x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa, u32 size);
bool kvm_vcpu_is_reset_bsp(struct kvm_vcpu *vcpu);
bool kvm_vcpu_is_bsp(struct kvm_vcpu *vcpu);

bool kvm_intr_is_single_vcpu(struct kvm *kvm, struct kvm_lapic_irq *irq,
			     struct kvm_vcpu **dest_vcpu);

void kvm_set_msi_irq(struct kvm *kvm, struct kvm_kernel_irq_routing_entry *e,
		     struct kvm_lapic_irq *irq);

static inline void kvm_arch_vcpu_blocking(struct kvm_vcpu *vcpu)
{
	if (kvm_x86_ops->vcpu_blocking)
		kvm_x86_ops->vcpu_blocking(vcpu);
}

static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	if (kvm_x86_ops->vcpu_unblocking)
		kvm_x86_ops->vcpu_unblocking(vcpu);
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
