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
#include <linux/workqueue.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>
#include <linux/perf_event.h>
#include <linux/pvclock_gtod.h>
#include <linux/clocksource.h>
#include <linux/irqbypass.h>
#include <linux/hyperv.h>
#include <linux/kfifo.h>

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

/*
 * CONFIG_KVM_MAX_NR_VCPUS is defined iff CONFIG_KVM!=n, provide a dummy max if
 * KVM is disabled (arbitrarily use the default from CONFIG_KVM_MAX_NR_VCPUS).
 */
#ifdef CONFIG_KVM_MAX_NR_VCPUS
#define KVM_MAX_VCPUS CONFIG_KVM_MAX_NR_VCPUS
#else
#define KVM_MAX_VCPUS 1024
#endif

/*
 * In x86, the VCPU ID corresponds to the APIC ID, and APIC IDs
 * might be larger than the actual number of VCPUs because the
 * APIC ID encodes CPU topology information.
 *
 * In the worst case, we'll need less than one extra bit for the
 * Core ID, and less than one extra bit for the Package (Die) ID,
 * so ratio of 4 should be enough.
 */
#define KVM_VCPU_ID_RATIO 4
#define KVM_MAX_VCPU_IDS (KVM_MAX_VCPUS * KVM_VCPU_ID_RATIO)

/* memory slots that are not exposed to userspace */
#define KVM_INTERNAL_MEM_SLOTS 3

#define KVM_HALT_POLL_NS_DEFAULT 200000

#define KVM_IRQCHIP_NUM_PINS  KVM_IOAPIC_NUM_PINS

#define KVM_DIRTY_LOG_MANUAL_CAPS   (KVM_DIRTY_LOG_MANUAL_PROTECT_ENABLE | \
					KVM_DIRTY_LOG_INITIALLY_SET)

#define KVM_BUS_LOCK_DETECTION_VALID_MODE	(KVM_BUS_LOCK_DETECTION_OFF | \
						 KVM_BUS_LOCK_DETECTION_EXIT)

#define KVM_X86_NOTIFY_VMEXIT_VALID_BITS	(KVM_X86_NOTIFY_VMEXIT_ENABLED | \
						 KVM_X86_NOTIFY_VMEXIT_USER)

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
#ifdef CONFIG_KVM_SMM
#define KVM_REQ_SMI			KVM_ARCH_REQ(12)
#endif
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
	KVM_ARCH_REQ_FLAGS(27, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_APF_READY		KVM_ARCH_REQ(28)
#define KVM_REQ_MSR_FILTER_CHANGED	KVM_ARCH_REQ(29)
#define KVM_REQ_UPDATE_CPU_DIRTY_LOGGING \
	KVM_ARCH_REQ_FLAGS(30, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_MMU_FREE_OBSOLETE_ROOTS \
	KVM_ARCH_REQ_FLAGS(31, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_HV_TLB_FLUSH \
	KVM_ARCH_REQ_FLAGS(32, KVM_REQUEST_WAIT | KVM_REQUEST_NO_WAKEUP)
#define KVM_REQ_UPDATE_PROTECTED_GUEST_STATE	KVM_ARCH_REQ(34)

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
			  | X86_CR4_SMAP | X86_CR4_PKE | X86_CR4_UMIP \
			  | X86_CR4_LAM_SUP))

#define CR8_RESERVED_BITS (~(unsigned long)X86_CR8_TPR)



#define INVALID_PAGE (~(hpa_t)0)
#define VALID_PAGE(x) ((x) != INVALID_PAGE)

/* KVM Hugepage definitions for x86 */
#define KVM_MAX_HUGEPAGE_LEVEL	PG_LEVEL_1G
#define KVM_NR_PAGE_SIZES	(KVM_MAX_HUGEPAGE_LEVEL - PG_LEVEL_4K + 1)
#define KVM_HPAGE_GFN_SHIFT(x)	(((x) - 1) * 9)
#define KVM_HPAGE_SHIFT(x)	(PAGE_SHIFT + KVM_HPAGE_GFN_SHIFT(x))
#define KVM_HPAGE_SIZE(x)	(1UL << KVM_HPAGE_SHIFT(x))
#define KVM_HPAGE_MASK(x)	(~(KVM_HPAGE_SIZE(x) - 1))
#define KVM_PAGES_PER_HPAGE(x)	(KVM_HPAGE_SIZE(x) / PAGE_SIZE)

#define KVM_MEMSLOT_PAGES_TO_MMU_PAGES_RATIO 50
#define KVM_MIN_ALLOC_MMU_PAGES 64UL
#define KVM_MMU_HASH_SHIFT 12
#define KVM_NUM_MMU_PAGES (1 << KVM_MMU_HASH_SHIFT)
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 256
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
union kvm_smram;
enum x86_intercept;
enum x86_intercept_stage;

#define KVM_NR_DB_REGS	4

#define DR6_BUS_LOCK   (1 << 11)
#define DR6_BD		(1 << 13)
#define DR6_BS		(1 << 14)
#define DR6_BT		(1 << 15)
#define DR6_RTM		(1 << 16)
/*
 * DR6_ACTIVE_LOW combines fixed-1 and active-low bits.
 * We can regard all the bits in DR6_FIXED_1 as active_low bits;
 * they will never be 0 for now, but when they are defined
 * in the future it will require no code change.
 *
 * DR6_ACTIVE_LOW is also used as the init/reset value for DR6.
 */
#define DR6_ACTIVE_LOW	0xffff0ff0
#define DR6_VOLATILE	0x0001e80f
#define DR6_FIXED_1	(DR6_ACTIVE_LOW & ~DR6_VOLATILE)

#define DR7_BP_EN_MASK	0x000000ff
#define DR7_GE		(1 << 9)
#define DR7_GD		(1 << 13)
#define DR7_FIXED_1	0x00000400
#define DR7_VOLATILE	0xffff2bff

#define KVM_GUESTDBG_VALID_MASK \
	(KVM_GUESTDBG_ENABLE | \
	KVM_GUESTDBG_SINGLESTEP | \
	KVM_GUESTDBG_USE_HW_BP | \
	KVM_GUESTDBG_USE_SW_BP | \
	KVM_GUESTDBG_INJECT_BP | \
	KVM_GUESTDBG_INJECT_DB | \
	KVM_GUESTDBG_BLOCKIRQ)

#define PFERR_PRESENT_MASK	BIT(0)
#define PFERR_WRITE_MASK	BIT(1)
#define PFERR_USER_MASK		BIT(2)
#define PFERR_RSVD_MASK		BIT(3)
#define PFERR_FETCH_MASK	BIT(4)
#define PFERR_PK_MASK		BIT(5)
#define PFERR_SGX_MASK		BIT(15)
#define PFERR_GUEST_RMP_MASK	BIT_ULL(31)
#define PFERR_GUEST_FINAL_MASK	BIT_ULL(32)
#define PFERR_GUEST_PAGE_MASK	BIT_ULL(33)
#define PFERR_GUEST_ENC_MASK	BIT_ULL(34)
#define PFERR_GUEST_SIZEM_MASK	BIT_ULL(35)
#define PFERR_GUEST_VMPL_MASK	BIT_ULL(36)

/*
 * IMPLICIT_ACCESS is a KVM-defined flag used to correctly perform SMAP checks
 * when emulating instructions that triggers implicit access.
 */
#define PFERR_IMPLICIT_ACCESS	BIT_ULL(48)
/*
 * PRIVATE_ACCESS is a KVM-defined flag us to indicate that a fault occurred
 * when the guest was accessing private memory.
 */
#define PFERR_PRIVATE_ACCESS   BIT_ULL(49)
#define PFERR_SYNTHETIC_MASK   (PFERR_IMPLICIT_ACCESS | PFERR_PRIVATE_ACCESS)

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
 * kvm_mmu_page_role tracks the properties of a shadow page (where shadow page
 * also includes TDP pages) to determine whether or not a page can be used in
 * the given MMU context.  This is a subset of the overall kvm_cpu_role to
 * minimize the size of kvm_memory_slot.arch.gfn_write_track, i.e. allows
 * allocating 2 bytes per gfn instead of 4 bytes per gfn.
 *
 * Upper-level shadow pages having gptes are tracked for write-protection via
 * gfn_write_track.  As above, gfn_write_track is a 16 bit counter, so KVM must
 * not create more than 2^16-1 upper-level shadow pages at a single gfn,
 * otherwise gfn_write_track will overflow and explosions will ensue.
 *
 * A unique shadow page (SP) for a gfn is created if and only if an existing SP
 * cannot be reused.  The ability to reuse a SP is tracked by its role, which
 * incorporates various mode bits and properties of the SP.  Roughly speaking,
 * the number of unique SPs that can theoretically be created is 2^n, where n
 * is the number of bits that are used to compute the role.
 *
 * But, even though there are 19 bits in the mask below, not all combinations
 * of modes and flags are possible:
 *
 *   - invalid shadow pages are not accounted, so the bits are effectively 18
 *
 *   - quadrant will only be used if has_4_byte_gpte=1 (non-PAE paging);
 *     execonly and ad_disabled are only used for nested EPT which has
 *     has_4_byte_gpte=0.  Therefore, 2 bits are always unused.
 *
 *   - the 4 bits of level are effectively limited to the values 2/3/4/5,
 *     as 4k SPs are not tracked (allowed to go unsync).  In addition non-PAE
 *     paging has exactly one upper level, making level completely redundant
 *     when has_4_byte_gpte=1.
 *
 *   - on top of this, smep_andnot_wp and smap_andnot_wp are only set if
 *     cr0_wp=0, therefore these three bits only give rise to 5 possibilities.
 *
 * Therefore, the maximum number of possible upper-level shadow pages for a
 * single gfn is a bit less than 2^13.
 */
union kvm_mmu_page_role {
	u32 word;
	struct {
		unsigned level:4;
		unsigned has_4_byte_gpte:1;
		unsigned quadrant:2;
		unsigned direct:1;
		unsigned access:3;
		unsigned invalid:1;
		unsigned efer_nx:1;
		unsigned cr0_wp:1;
		unsigned smep_andnot_wp:1;
		unsigned smap_andnot_wp:1;
		unsigned ad_disabled:1;
		unsigned guest_mode:1;
		unsigned passthrough:1;
		unsigned :5;

		/*
		 * This is left at the top of the word so that
		 * kvm_memslots_for_spte_role can extract it with a
		 * simple shift.  While there is room, give it a whole
		 * byte so it is also faster to load it from memory.
		 */
		unsigned smm:8;
	};
};

/*
 * kvm_mmu_extended_role complements kvm_mmu_page_role, tracking properties
 * relevant to the current MMU configuration.   When loading CR0, CR4, or EFER,
 * including on nested transitions, if nothing in the full role changes then
 * MMU re-configuration can be skipped. @valid bit is set on first usage so we
 * don't treat all-zero structure as valid data.
 *
 * The properties that are tracked in the extended role but not the page role
 * are for things that either (a) do not affect the validity of the shadow page
 * or (b) are indirectly reflected in the shadow page's role.  For example,
 * CR4.PKE only affects permission checks for software walks of the guest page
 * tables (because KVM doesn't support Protection Keys with shadow paging), and
 * CR0.PG, CR4.PAE, and CR4.PSE are indirectly reflected in role.level.
 *
 * Note, SMEP and SMAP are not redundant with sm*p_andnot_wp in the page role.
 * If CR0.WP=1, KVM can reuse shadow pages for the guest regardless of SMEP and
 * SMAP, but the MMU's permission checks for software walks need to be SMEP and
 * SMAP aware regardless of CR0.WP.
 */
union kvm_mmu_extended_role {
	u32 word;
	struct {
		unsigned int valid:1;
		unsigned int execonly:1;
		unsigned int cr4_pse:1;
		unsigned int cr4_pke:1;
		unsigned int cr4_smap:1;
		unsigned int cr4_smep:1;
		unsigned int cr4_la57:1;
		unsigned int efer_lma:1;
	};
};

union kvm_cpu_role {
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

#define KVM_MMU_ROOT_CURRENT		BIT(0)
#define KVM_MMU_ROOT_PREVIOUS(i)	BIT(1+i)
#define KVM_MMU_ROOTS_ALL		(BIT(1 + KVM_MMU_NUM_PREV_ROOTS) - 1)

#define KVM_HAVE_MMU_RWLOCK

struct kvm_mmu_page;
struct kvm_page_fault;

/*
 * x86 supports 4 paging modes (5-level 64-bit, 4-level 64-bit, 3-level 32-bit,
 * and 2-level 32-bit).  The kvm_mmu structure abstracts the details of the
 * current mmu mode.
 */
struct kvm_mmu {
	unsigned long (*get_guest_pgd)(struct kvm_vcpu *vcpu);
	u64 (*get_pdptr)(struct kvm_vcpu *vcpu, int index);
	int (*page_fault)(struct kvm_vcpu *vcpu, struct kvm_page_fault *fault);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  struct x86_exception *fault);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			    gpa_t gva_or_gpa, u64 access,
			    struct x86_exception *exception);
	int (*sync_spte)(struct kvm_vcpu *vcpu,
			 struct kvm_mmu_page *sp, int i);
	struct kvm_mmu_root_info root;
	union kvm_cpu_role cpu_role;
	union kvm_mmu_page_role root_role;

	/*
	* The pkru_mask indicates if protection key checks are needed.  It
	* consists of 16 domains indexed by page fault error code bits [4:1],
	* with PFEC.RSVD replaced by ACC_USER_MASK from the page tables.
	* Each domain has 2 bits which are ANDed with AD and WD from PKRU.
	*/
	u32 pkru_mask;

	struct kvm_mmu_root_info prev_roots[KVM_MMU_NUM_PREV_ROOTS];

	/*
	 * Bitmap; bit set = permission fault
	 * Byte index: page fault error code [4:1]
	 * Bit index: pte permissions in ACC_* format
	 */
	u8 permissions[16];

	u64 *pae_root;
	u64 *pml4_root;
	u64 *pml5_root;

	/*
	 * check zero bits on shadow page table entries, these
	 * bits include not only hardware reserved bits but also
	 * the bits spte never used.
	 */
	struct rsvd_bits_validate shadow_zero_check;

	struct rsvd_bits_validate guest_rsvd_check;

	u64 pdptrs[4]; /* pae */
};

enum pmc_type {
	KVM_PMC_GP = 0,
	KVM_PMC_FIXED,
};

struct kvm_pmc {
	enum pmc_type type;
	u8 idx;
	bool is_paused;
	bool intr;
	/*
	 * Base value of the PMC counter, relative to the *consumed* count in
	 * the associated perf_event.  This value includes counter updates from
	 * the perf_event and emulated_count since the last time the counter
	 * was reprogrammed, but it is *not* the current value as seen by the
	 * guest or userspace.
	 *
	 * The count is relative to the associated perf_event so that KVM
	 * doesn't need to reprogram the perf_event every time the guest writes
	 * to the counter.
	 */
	u64 counter;
	/*
	 * PMC events triggered by KVM emulation that haven't been fully
	 * processed, i.e. haven't undergone overflow detection.
	 */
	u64 emulated_counter;
	u64 eventsel;
	struct perf_event *perf_event;
	struct kvm_vcpu *vcpu;
	/*
	 * only for creating or reusing perf_event,
	 * eventsel value for general purpose counters,
	 * ctrl value for fixed counters.
	 */
	u64 current_config;
};

/* More counters may conflict with other existing Architectural MSRs */
#define KVM_MAX(a, b)	((a) >= (b) ? (a) : (b))
#define KVM_MAX_NR_INTEL_GP_COUNTERS	8
#define KVM_MAX_NR_AMD_GP_COUNTERS	6
#define KVM_MAX_NR_GP_COUNTERS		KVM_MAX(KVM_MAX_NR_INTEL_GP_COUNTERS, \
						KVM_MAX_NR_AMD_GP_COUNTERS)

#define KVM_MAX_NR_INTEL_FIXED_COUTNERS	3
#define KVM_MAX_NR_AMD_FIXED_COUTNERS	0
#define KVM_MAX_NR_FIXED_COUNTERS	KVM_MAX(KVM_MAX_NR_INTEL_FIXED_COUTNERS, \
						KVM_MAX_NR_AMD_FIXED_COUTNERS)

struct kvm_pmu {
	u8 version;
	unsigned nr_arch_gp_counters;
	unsigned nr_arch_fixed_counters;
	unsigned available_event_types;
	u64 fixed_ctr_ctrl;
	u64 fixed_ctr_ctrl_rsvd;
	u64 global_ctrl;
	u64 global_status;
	u64 counter_bitmask[2];
	u64 global_ctrl_rsvd;
	u64 global_status_rsvd;
	u64 reserved_bits;
	u64 raw_event_mask;
	struct kvm_pmc gp_counters[KVM_MAX_NR_GP_COUNTERS];
	struct kvm_pmc fixed_counters[KVM_MAX_NR_FIXED_COUNTERS];

	/*
	 * Overlay the bitmap with a 64-bit atomic so that all bits can be
	 * set in a single access, e.g. to reprogram all counters when the PMU
	 * filter changes.
	 */
	union {
		DECLARE_BITMAP(reprogram_pmi, X86_PMC_IDX_MAX);
		atomic64_t __reprogram_pmi;
	};
	DECLARE_BITMAP(all_valid_pmc_idx, X86_PMC_IDX_MAX);
	DECLARE_BITMAP(pmc_in_use, X86_PMC_IDX_MAX);

	u64 ds_area;
	u64 pebs_enable;
	u64 pebs_enable_rsvd;
	u64 pebs_data_cfg;
	u64 pebs_data_cfg_rsvd;

	/*
	 * If a guest counter is cross-mapped to host counter with different
	 * index, its PEBS capability will be temporarily disabled.
	 *
	 * The user should make sure that this mask is updated
	 * after disabling interrupts and before perf_guest_get_msrs();
	 */
	u64 host_cross_mapped_mask;

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
};

struct kvm_mtrr {
	u64 var[KVM_NR_VAR_MTRR * 2];
	u64 fixed_64k;
	u64 fixed_16k[2];
	u64 fixed_4k[8];
	u64 deftype;
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

/* The maximum number of entries on the TLB flush fifo. */
#define KVM_HV_TLB_FLUSH_FIFO_SIZE (16)
/*
 * Note: the following 'magic' entry is made up by KVM to avoid putting
 * anything besides GVA on the TLB flush fifo. It is theoretically possible
 * to observe a request to flush 4095 PFNs starting from 0xfffffffffffff000
 * which will look identical. KVM's action to 'flush everything' instead of
 * flushing these particular addresses is, however, fully legitimate as
 * flushing more than requested is always OK.
 */
#define KVM_HV_TLB_FLUSHALL_ENTRY  ((u64)-1)

enum hv_tlb_flush_fifos {
	HV_L1_TLB_FLUSH_FIFO,
	HV_L2_TLB_FLUSH_FIFO,
	HV_NR_TLB_FLUSH_FIFOS,
};

struct kvm_vcpu_hv_tlb_flush_fifo {
	spinlock_t write_lock;
	DECLARE_KFIFO(entries, u64, KVM_HV_TLB_FLUSH_FIFO_SIZE);
};

/* Hyper-V per vcpu emulation context */
struct kvm_vcpu_hv {
	struct kvm_vcpu *vcpu;
	u32 vp_index;
	u64 hv_vapic;
	s64 runtime_offset;
	struct kvm_vcpu_hv_synic synic;
	struct kvm_hyperv_exit exit;
	struct kvm_vcpu_hv_stimer stimer[HV_SYNIC_STIMER_COUNT];
	DECLARE_BITMAP(stimer_pending_bitmap, HV_SYNIC_STIMER_COUNT);
	bool enforce_cpuid;
	struct {
		u32 features_eax; /* HYPERV_CPUID_FEATURES.EAX */
		u32 features_ebx; /* HYPERV_CPUID_FEATURES.EBX */
		u32 features_edx; /* HYPERV_CPUID_FEATURES.EDX */
		u32 enlightenments_eax; /* HYPERV_CPUID_ENLIGHTMENT_INFO.EAX */
		u32 enlightenments_ebx; /* HYPERV_CPUID_ENLIGHTMENT_INFO.EBX */
		u32 syndbg_cap_eax; /* HYPERV_CPUID_SYNDBG_PLATFORM_CAPABILITIES.EAX */
		u32 nested_eax; /* HYPERV_CPUID_NESTED_FEATURES.EAX */
		u32 nested_ebx; /* HYPERV_CPUID_NESTED_FEATURES.EBX */
	} cpuid_cache;

	struct kvm_vcpu_hv_tlb_flush_fifo tlb_flush_fifo[HV_NR_TLB_FLUSH_FIFOS];

	/* Preallocated buffer for handling hypercalls passing sparse vCPU set */
	u64 sparse_banks[HV_MAX_SPARSE_VCPU_BANKS];

	struct hv_vp_assist_page vp_assist_page;

	struct {
		u64 pa_page_gpa;
		u64 vm_id;
		u32 vp_id;
	} nested;
};

struct kvm_hypervisor_cpuid {
	u32 base;
	u32 limit;
};

#ifdef CONFIG_KVM_XEN
/* Xen HVM per vcpu emulation context */
struct kvm_vcpu_xen {
	u64 hypercall_rip;
	u32 current_runstate;
	u8 upcall_vector;
	struct gfn_to_pfn_cache vcpu_info_cache;
	struct gfn_to_pfn_cache vcpu_time_info_cache;
	struct gfn_to_pfn_cache runstate_cache;
	struct gfn_to_pfn_cache runstate2_cache;
	u64 last_steal;
	u64 runstate_entry_time;
	u64 runstate_times[4];
	unsigned long evtchn_pending_sel;
	u32 vcpu_id; /* The Xen / ACPI vCPU ID */
	u32 timer_virq;
	u64 timer_expires; /* In guest epoch */
	atomic_t timer_pending;
	struct hrtimer timer;
	int poll_evtchn;
	struct timer_list poll_timer;
	struct kvm_hypervisor_cpuid cpuid;
};
#endif

struct kvm_queued_exception {
	bool pending;
	bool injected;
	bool has_error_code;
	u8 vector;
	u32 error_code;
	unsigned long payload;
	bool has_payload;
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
	bool load_eoi_exitmap_pending;
	DECLARE_BITMAP(ioapic_handled_vectors, 256);
	unsigned long apic_attention;
	int32_t apic_arb_prio;
	int mp_state;
	u64 ia32_misc_enable_msr;
	u64 smbase;
	u64 smi_count;
	bool at_instruction_boundary;
	bool tpr_access_reporting;
	bool xfd_no_write_intercept;
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
	struct kvm_mmu_memory_cache mmu_shadowed_info_cache;
	struct kvm_mmu_memory_cache mmu_page_header_cache;

	/*
	 * QEMU userspace and the guest each have their own FPU state.
	 * In vcpu_run, we switch between the user and guest FPU contexts.
	 * While running a VCPU, the VCPU thread will have the guest FPU
	 * context.
	 *
	 * Note that while the PKRU state lives inside the fpu registers,
	 * it is switched out separately at VMENTER and VMEXIT time. The
	 * "guest_fpstate" state here contains the guest FPU context, with the
	 * host PRKU bits.
	 */
	struct fpu_guest guest_fpu;

	u64 xcr0;
	u64 guest_supported_xcr0;

	struct kvm_pio_request pio;
	void *pio_data;
	void *sev_pio_data;
	unsigned sev_pio_count;

	u8 event_exit_inst_len;

	bool exception_from_userspace;

	/* Exceptions to be injected to the guest. */
	struct kvm_queued_exception exception;
	/* Exception VM-Exits to be synthesized to L1. */
	struct kvm_queued_exception exception_vmexit;

	struct kvm_queued_interrupt {
		bool injected;
		bool soft;
		u8 nr;
	} interrupt;

	int halt_request; /* real mode on Intel only */

	int cpuid_nent;
	struct kvm_cpuid_entry2 *cpuid_entries;
	struct kvm_hypervisor_cpuid kvm_cpuid;
	bool is_amd_compatible;

	/*
	 * FIXME: Drop this macro and use KVM_NR_GOVERNED_FEATURES directly
	 * when "struct kvm_vcpu_arch" is no longer defined in an
	 * arch/x86/include/asm header.  The max is mostly arbitrary, i.e.
	 * can be increased as necessary.
	 */
#define KVM_MAX_NR_GOVERNED_FEATURES BITS_PER_LONG

	/*
	 * Track whether or not the guest is allowed to use features that are
	 * governed by KVM, where "governed" means KVM needs to manage state
	 * and/or explicitly enable the feature in hardware.  Typically, but
	 * not always, governed features can be used by the guest if and only
	 * if both KVM and userspace want to expose the feature to the guest.
	 */
	struct {
		DECLARE_BITMAP(enabled, KVM_MAX_NR_GOVERNED_FEATURES);
	} governed_features;

	u64 reserved_gpa_bits;
	int maxphyaddr;

	/* emulate context */

	struct x86_emulate_ctxt *emulate_ctxt;
	bool emulate_regs_need_sync_to_vcpu;
	bool emulate_regs_need_sync_from_vcpu;
	int (*complete_userspace_io)(struct kvm_vcpu *vcpu);

	gpa_t time;
	struct pvclock_vcpu_time_info hv_clock;
	unsigned int hw_tsc_khz;
	struct gfn_to_pfn_cache pv_time;
	/* set guest stopped flag in pvclock flags field */
	bool pvclock_set_guest_stopped_request;

	struct {
		u8 preempted;
		u64 msr_val;
		u64 last_steal;
		struct gfn_to_hva_cache cache;
	} st;

	u64 l1_tsc_offset;
	u64 tsc_offset; /* current tsc offset */
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
	u64 l1_tsc_scaling_ratio;
	u64 tsc_scaling_ratio; /* current scaling ratio */

	atomic_t nmi_queued;  /* unprocessed asynchronous NMIs */
	/* Number of NMIs pending injection, not including hardware vNMIs. */
	unsigned int nmi_pending;
	bool nmi_injected;    /* Trying to inject an NMI this entry */
	bool smi_pending;    /* SMI queued after currently running handler */
	u8 handling_intr_from_guest;

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
	u64 *mci_ctl2_banks;

	/* Cache MMIO info */
	u64 mmio_gva;
	unsigned mmio_access;
	gfn_t mmio_gfn;
	u64 mmio_gen;

	struct kvm_pmu pmu;

	/* used for guest single stepping over the given code position */
	unsigned long singlestep_rip;

#ifdef CONFIG_KVM_HYPERV
	bool hyperv_enabled;
	struct kvm_vcpu_hv *hyperv;
#endif
#ifdef CONFIG_KVM_XEN
	struct kvm_vcpu_xen xen;
#endif
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
	int last_vmentry_cpu;

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

	/* Protected Guests */
	bool guest_state_protected;

	/*
	 * Set when PDPTS were loaded directly by the userspace without
	 * reading the guest memory
	 */
	bool pdptrs_from_userspace;

#if IS_ENABLED(CONFIG_HYPERV)
	hpa_t hv_root_tdp;
#endif
};

struct kvm_lpage_info {
	int disallow_lpage;
};

struct kvm_arch_memory_slot {
	struct kvm_rmap_head *rmap[KVM_NR_PAGE_SIZES];
	struct kvm_lpage_info *lpage_info[KVM_NR_PAGE_SIZES - 1];
	unsigned short *gfn_write_track;
};

/*
 * Track the mode of the optimized logical map, as the rules for decoding the
 * destination vary per mode.  Enabling the optimized logical map requires all
 * software-enabled local APIs to be in the same mode, each addressable APIC to
 * be mapped to only one MDA, and each MDA to map to at most one APIC.
 */
enum kvm_apic_logical_mode {
	/* All local APICs are software disabled. */
	KVM_APIC_MODE_SW_DISABLED,
	/* All software enabled local APICs in xAPIC cluster addressing mode. */
	KVM_APIC_MODE_XAPIC_CLUSTER,
	/* All software enabled local APICs in xAPIC flat addressing mode. */
	KVM_APIC_MODE_XAPIC_FLAT,
	/* All software enabled local APICs in x2APIC mode. */
	KVM_APIC_MODE_X2APIC,
	/*
	 * Optimized map disabled, e.g. not all local APICs in the same logical
	 * mode, same logical ID assigned to multiple APICs, etc.
	 */
	KVM_APIC_MODE_MAP_DISABLED,
};

struct kvm_apic_map {
	struct rcu_head rcu;
	enum kvm_apic_logical_mode logical_mode;
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

/* Current state of Hyper-V TSC page clocksource */
enum hv_tsc_page_status {
	/* TSC page was not set up or disabled */
	HV_TSC_PAGE_UNSET = 0,
	/* TSC page MSR was written by the guest, update pending */
	HV_TSC_PAGE_GUEST_CHANGED,
	/* TSC page update was triggered from the host side */
	HV_TSC_PAGE_HOST_CHANGED,
	/* TSC page was properly set up and is currently active  */
	HV_TSC_PAGE_SET,
	/* TSC page was set up with an inaccessible GPA */
	HV_TSC_PAGE_BROKEN,
};

#ifdef CONFIG_KVM_HYPERV
/* Hyper-V emulation context */
struct kvm_hv {
	struct mutex hv_lock;
	u64 hv_guest_os_id;
	u64 hv_hypercall;
	u64 hv_tsc_page;
	enum hv_tsc_page_status hv_tsc_page_status;

	/* Hyper-v based guest crash (NT kernel bugcheck) parameters */
	u64 hv_crash_param[HV_X64_MSR_CRASH_PARAMS];
	u64 hv_crash_ctl;

	struct ms_hyperv_tsc_page tsc_ref;

	struct idr conn_to_evt;

	u64 hv_reenlightenment_control;
	u64 hv_tsc_emulation_control;
	u64 hv_tsc_emulation_status;
	u64 hv_invtsc_control;

	/* How many vCPUs have VP index != vCPU index */
	atomic_t num_mismatched_vp_indexes;

	/*
	 * How many SynICs use 'AutoEOI' feature
	 * (protected by arch.apicv_update_lock)
	 */
	unsigned int synic_auto_eoi_used;

	struct kvm_hv_syndbg hv_syndbg;

	bool xsaves_xsavec_checked;
};
#endif

struct msr_bitmap_range {
	u32 flags;
	u32 nmsrs;
	u32 base;
	unsigned long *bitmap;
};

#ifdef CONFIG_KVM_XEN
/* Xen emulation context */
struct kvm_xen {
	struct mutex xen_lock;
	u32 xen_version;
	bool long_mode;
	bool runstate_update_flag;
	u8 upcall_vector;
	struct gfn_to_pfn_cache shinfo_cache;
	struct idr evtchn_ports;
	unsigned long poll_mask[BITS_TO_LONGS(KVM_MAX_VCPUS)];
};
#endif

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

struct kvm_x86_pmu_event_filter {
	__u32 action;
	__u32 nevents;
	__u32 fixed_counter_bitmap;
	__u32 flags;
	__u32 nr_includes;
	__u32 nr_excludes;
	__u64 *includes;
	__u64 *excludes;
	__u64 events[];
};

enum kvm_apicv_inhibit {

	/********************************************************************/
	/* INHIBITs that are relevant to both Intel's APICv and AMD's AVIC. */
	/********************************************************************/

	/*
	 * APIC acceleration is disabled by a module parameter
	 * and/or not supported in hardware.
	 */
	APICV_INHIBIT_REASON_DISABLED,

	/*
	 * APIC acceleration is inhibited because AutoEOI feature is
	 * being used by a HyperV guest.
	 */
	APICV_INHIBIT_REASON_HYPERV,

	/*
	 * APIC acceleration is inhibited because the userspace didn't yet
	 * enable the kernel/split irqchip.
	 */
	APICV_INHIBIT_REASON_ABSENT,

	/* APIC acceleration is inhibited because KVM_GUESTDBG_BLOCKIRQ
	 * (out of band, debug measure of blocking all interrupts on this vCPU)
	 * was enabled, to avoid AVIC/APICv bypassing it.
	 */
	APICV_INHIBIT_REASON_BLOCKIRQ,

	/*
	 * APICv is disabled because not all vCPUs have a 1:1 mapping between
	 * APIC ID and vCPU, _and_ KVM is not applying its x2APIC hotplug hack.
	 */
	APICV_INHIBIT_REASON_PHYSICAL_ID_ALIASED,

	/*
	 * For simplicity, the APIC acceleration is inhibited
	 * first time either APIC ID or APIC base are changed by the guest
	 * from their reset values.
	 */
	APICV_INHIBIT_REASON_APIC_ID_MODIFIED,
	APICV_INHIBIT_REASON_APIC_BASE_MODIFIED,

	/******************************************************/
	/* INHIBITs that are relevant only to the AMD's AVIC. */
	/******************************************************/

	/*
	 * AVIC is inhibited on a vCPU because it runs a nested guest.
	 *
	 * This is needed because unlike APICv, the peers of this vCPU
	 * cannot use the doorbell mechanism to signal interrupts via AVIC when
	 * a vCPU runs nested.
	 */
	APICV_INHIBIT_REASON_NESTED,

	/*
	 * On SVM, the wait for the IRQ window is implemented with pending vIRQ,
	 * which cannot be injected when the AVIC is enabled, thus AVIC
	 * is inhibited while KVM waits for IRQ window.
	 */
	APICV_INHIBIT_REASON_IRQWIN,

	/*
	 * PIT (i8254) 're-inject' mode, relies on EOI intercept,
	 * which AVIC doesn't support for edge triggered interrupts.
	 */
	APICV_INHIBIT_REASON_PIT_REINJ,

	/*
	 * AVIC is disabled because SEV doesn't support it.
	 */
	APICV_INHIBIT_REASON_SEV,

	/*
	 * AVIC is disabled because not all vCPUs with a valid LDR have a 1:1
	 * mapping between logical ID and vCPU.
	 */
	APICV_INHIBIT_REASON_LOGICAL_ID_ALIASED,

	NR_APICV_INHIBIT_REASONS,
};

#define __APICV_INHIBIT_REASON(reason)			\
	{ BIT(APICV_INHIBIT_REASON_##reason), #reason }

#define APICV_INHIBIT_REASONS				\
	__APICV_INHIBIT_REASON(DISABLED),		\
	__APICV_INHIBIT_REASON(HYPERV),			\
	__APICV_INHIBIT_REASON(ABSENT),			\
	__APICV_INHIBIT_REASON(BLOCKIRQ),		\
	__APICV_INHIBIT_REASON(PHYSICAL_ID_ALIASED),	\
	__APICV_INHIBIT_REASON(APIC_ID_MODIFIED),	\
	__APICV_INHIBIT_REASON(APIC_BASE_MODIFIED),	\
	__APICV_INHIBIT_REASON(NESTED),			\
	__APICV_INHIBIT_REASON(IRQWIN),			\
	__APICV_INHIBIT_REASON(PIT_REINJ),		\
	__APICV_INHIBIT_REASON(SEV),			\
	__APICV_INHIBIT_REASON(LOGICAL_ID_ALIASED)

struct kvm_arch {
	unsigned long n_used_mmu_pages;
	unsigned long n_requested_mmu_pages;
	unsigned long n_max_mmu_pages;
	unsigned int indirect_shadow_pages;
	u8 mmu_valid_gen;
	u8 vm_type;
	bool has_private_mem;
	bool has_protected_state;
	bool pre_fault_allowed;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	struct list_head active_mmu_pages;
	struct list_head zapped_obsolete_pages;
	/*
	 * A list of kvm_mmu_page structs that, if zapped, could possibly be
	 * replaced by an NX huge page.  A shadow page is on this list if its
	 * existence disallows an NX huge page (nx_huge_page_disallowed is set)
	 * and there are no other conditions that prevent a huge page, e.g.
	 * the backing host page is huge, dirtly logging is not enabled for its
	 * memslot, etc...  Note, zapping shadow pages on this list doesn't
	 * guarantee an NX huge page will be created in its stead, e.g. if the
	 * guest attempts to execute from the region then KVM obviously can't
	 * create an NX huge page (without hanging the guest).
	 */
	struct list_head possible_nx_huge_pages;
#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
	struct kvm_page_track_notifier_head track_notifier_head;
#endif
	/*
	 * Protects marking pages unsync during page faults, as TDP MMU page
	 * faults only take mmu_lock for read.  For simplicity, the unsync
	 * pages lock is always taken when marking pages unsync regardless of
	 * whether mmu_lock is held for read or write.
	 */
	spinlock_t mmu_unsync_pages_lock;

	u64 shadow_mmio_value;

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
	struct kvm_apic_map __rcu *apic_map;
	atomic_t apic_map_dirty;

	bool apic_access_memslot_enabled;
	bool apic_access_memslot_inhibited;

	/* Protects apicv_inhibit_reasons */
	struct rw_semaphore apicv_update_lock;
	unsigned long apicv_inhibit_reasons;

	gpa_t wall_clock;

	bool mwait_in_guest;
	bool hlt_in_guest;
	bool pause_in_guest;
	bool cstate_in_guest;

	unsigned long irq_sources_bitmap;
	s64 kvmclock_offset;

	/*
	 * This also protects nr_vcpus_matched_tsc which is read from a
	 * preemption-disabled region, so it must be a raw spinlock.
	 */
	raw_spinlock_t tsc_write_lock;
	u64 last_tsc_nsec;
	u64 last_tsc_write;
	u32 last_tsc_khz;
	u64 last_tsc_offset;
	u64 cur_tsc_nsec;
	u64 cur_tsc_write;
	u64 cur_tsc_offset;
	u64 cur_tsc_generation;
	int nr_vcpus_matched_tsc;

	u32 default_tsc_khz;
	bool user_set_tsc;
	u64 apic_bus_cycle_ns;

	seqcount_raw_spinlock_t pvclock_sc;
	bool use_master_clock;
	u64 master_kernel_ns;
	u64 master_cycle_now;
	struct delayed_work kvmclock_update_work;
	struct delayed_work kvmclock_sync_work;

	struct kvm_xen_hvm_config xen_hvm_config;

	/* reads protected by irq_srcu, writes by irq_lock */
	struct hlist_head mask_notifier_list;

#ifdef CONFIG_KVM_HYPERV
	struct kvm_hv hyperv;
#endif

#ifdef CONFIG_KVM_XEN
	struct kvm_xen xen;
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

	bool triple_fault_event;

	bool bus_lock_detection_enabled;
	bool enable_pmu;

	u32 notify_window;
	u32 notify_vmexit_flags;
	/*
	 * If exit_on_emulation_error is set, and the in-kernel instruction
	 * emulator fails to emulate an instruction, allow userspace
	 * the opportunity to look at it.
	 */
	bool exit_on_emulation_error;

	/* Deflect RDMSR and WRMSR to user space when they trigger a #GP */
	u32 user_space_msr_mask;
	struct kvm_x86_msr_filter __rcu *msr_filter;

	u32 hypercall_exit_enabled;

	/* Guest can access the SGX PROVISIONKEY. */
	bool sgx_provisioning_allowed;

	struct kvm_x86_pmu_event_filter __rcu *pmu_event_filter;
	struct task_struct *nx_huge_page_recovery_thread;

#ifdef CONFIG_X86_64
	/* The number of TDP MMU pages across all roots. */
	atomic64_t tdp_mmu_pages;

	/*
	 * List of struct kvm_mmu_pages being used as roots.
	 * All struct kvm_mmu_pages in the list should have
	 * tdp_mmu_page set.
	 *
	 * For reads, this list is protected by:
	 *	the MMU lock in read mode + RCU or
	 *	the MMU lock in write mode
	 *
	 * For writes, this list is protected by tdp_mmu_pages_lock; see
	 * below for the details.
	 *
	 * Roots will remain in the list until their tdp_mmu_root_count
	 * drops to zero, at which point the thread that decremented the
	 * count to zero should removed the root from the list and clean
	 * it up, freeing the root after an RCU grace period.
	 */
	struct list_head tdp_mmu_roots;

	/*
	 * Protects accesses to the following fields when the MMU lock
	 * is held in read mode:
	 *  - tdp_mmu_roots (above)
	 *  - the link field of kvm_mmu_page structs used by the TDP MMU
	 *  - possible_nx_huge_pages;
	 *  - the possible_nx_huge_page_link field of kvm_mmu_page structs used
	 *    by the TDP MMU
	 * Because the lock is only taken within the MMU lock, strictly
	 * speaking it is redundant to acquire this lock when the thread
	 * holds the MMU lock in write mode.  However it often simplifies
	 * the code to do so.
	 */
	spinlock_t tdp_mmu_pages_lock;
#endif /* CONFIG_X86_64 */

	/*
	 * If set, at least one shadow root has been allocated. This flag
	 * is used as one input when determining whether certain memslot
	 * related allocations are necessary.
	 */
	bool shadow_root_allocated;

#ifdef CONFIG_KVM_EXTERNAL_WRITE_TRACKING
	/*
	 * If set, the VM has (or had) an external write tracking user, and
	 * thus all write tracking metadata has been allocated, even if KVM
	 * itself isn't using write tracking.
	 */
	bool external_write_tracking_enabled;
#endif

#if IS_ENABLED(CONFIG_HYPERV)
	hpa_t	hv_root_tdp;
	spinlock_t hv_root_tdp_lock;
	struct hv_partition_assist_pg *hv_pa_pg;
#endif
	/*
	 * VM-scope maximum vCPU ID. Used to determine the size of structures
	 * that increase along with the maximum vCPU ID, in which case, using
	 * the global KVM_MAX_VCPU_IDS may lead to significant memory waste.
	 */
	u32 max_vcpu_ids;

	bool disable_nx_huge_pages;

	/*
	 * Memory caches used to allocate shadow pages when performing eager
	 * page splitting. No need for a shadowed_info_cache since eager page
	 * splitting only allocates direct shadow pages.
	 *
	 * Protected by kvm->slots_lock.
	 */
	struct kvm_mmu_memory_cache split_shadow_page_cache;
	struct kvm_mmu_memory_cache split_page_header_cache;

	/*
	 * Memory cache used to allocate pte_list_desc structs while splitting
	 * huge pages. In the worst case, to split one huge page, 512
	 * pte_list_desc structs are needed to add each lower level leaf sptep
	 * to the rmap plus 1 to extend the parent_ptes rmap of the lower level
	 * page table.
	 *
	 * Protected by kvm->slots_lock.
	 */
#define SPLIT_DESC_CACHE_MIN_NR_OBJECTS (SPTE_ENT_PER_PAGE + 1)
	struct kvm_mmu_memory_cache split_desc_cache;
};

struct kvm_vm_stat {
	struct kvm_vm_stat_generic generic;
	u64 mmu_shadow_zapped;
	u64 mmu_pte_write;
	u64 mmu_pde_zapped;
	u64 mmu_flooded;
	u64 mmu_recycled;
	u64 mmu_cache_miss;
	u64 mmu_unsync;
	union {
		struct {
			atomic64_t pages_4k;
			atomic64_t pages_2m;
			atomic64_t pages_1g;
		};
		atomic64_t pages[KVM_NR_PAGE_SIZES];
	};
	u64 nx_lpage_splits;
	u64 max_mmu_page_hash_collisions;
	u64 max_mmu_rmap_size;
};

struct kvm_vcpu_stat {
	struct kvm_vcpu_stat_generic generic;
	u64 pf_taken;
	u64 pf_fixed;
	u64 pf_emulate;
	u64 pf_spurious;
	u64 pf_fast;
	u64 pf_mmio_spte_created;
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
	u64 nested_run;
	u64 directed_yield_attempted;
	u64 directed_yield_successful;
	u64 preemption_reported;
	u64 preemption_other;
	u64 guest_mode;
	u64 notify_window_exits;
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
	const char *name;

	int (*check_processor_compatibility)(void);

	int (*hardware_enable)(void);
	void (*hardware_disable)(void);
	void (*hardware_unsetup)(void);
	bool (*has_emulated_msr)(struct kvm *kvm, u32 index);
	void (*vcpu_after_set_cpuid)(struct kvm_vcpu *vcpu);

	unsigned int vm_size;
	int (*vm_init)(struct kvm *kvm);
	void (*vm_destroy)(struct kvm *kvm);

	/* Create, but do not attach this VCPU */
	int (*vcpu_precreate)(struct kvm *kvm);
	int (*vcpu_create)(struct kvm_vcpu *vcpu);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	void (*vcpu_reset)(struct kvm_vcpu *vcpu, bool init_event);

	void (*prepare_switch_to_guest)(struct kvm_vcpu *vcpu);
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
	bool (*is_valid_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	void (*set_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	void (*post_set_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);
	bool (*is_valid_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
	void (*set_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
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
	bool (*get_if_flag)(struct kvm_vcpu *vcpu);

	void (*flush_tlb_all)(struct kvm_vcpu *vcpu);
	void (*flush_tlb_current)(struct kvm_vcpu *vcpu);
#if IS_ENABLED(CONFIG_HYPERV)
	int  (*flush_remote_tlbs)(struct kvm *kvm);
	int  (*flush_remote_tlbs_range)(struct kvm *kvm, gfn_t gfn,
					gfn_t nr_pages);
#endif

	/*
	 * Flush any TLB entries associated with the given GVA.
	 * Does not need to flush GPA->HPA mappings.
	 * Can potentially get non-canonical addresses through INVLPGs, which
	 * the implementation may choose to ignore if appropriate.
	 */
	void (*flush_tlb_gva)(struct kvm_vcpu *vcpu, gva_t addr);

	/*
	 * Flush any TLB entries created by the guest.  Like tlb_flush_gva(),
	 * does not need to flush GPA->HPA mappings.
	 */
	void (*flush_tlb_guest)(struct kvm_vcpu *vcpu);

	int (*vcpu_pre_run)(struct kvm_vcpu *vcpu);
	enum exit_fastpath_completion (*vcpu_run)(struct kvm_vcpu *vcpu,
						  bool force_immediate_exit);
	int (*handle_exit)(struct kvm_vcpu *vcpu,
		enum exit_fastpath_completion exit_fastpath);
	int (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*update_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*set_interrupt_shadow)(struct kvm_vcpu *vcpu, int mask);
	u32 (*get_interrupt_shadow)(struct kvm_vcpu *vcpu);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
	void (*inject_irq)(struct kvm_vcpu *vcpu, bool reinjected);
	void (*inject_nmi)(struct kvm_vcpu *vcpu);
	void (*inject_exception)(struct kvm_vcpu *vcpu);
	void (*cancel_injection)(struct kvm_vcpu *vcpu);
	int (*interrupt_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	int (*nmi_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	bool (*get_nmi_mask)(struct kvm_vcpu *vcpu);
	void (*set_nmi_mask)(struct kvm_vcpu *vcpu, bool masked);
	/* Whether or not a virtual NMI is pending in hardware. */
	bool (*is_vnmi_pending)(struct kvm_vcpu *vcpu);
	/*
	 * Attempt to pend a virtual NMI in hardware.  Returns %true on success
	 * to allow using static_call_ret0 as the fallback.
	 */
	bool (*set_vnmi_pending)(struct kvm_vcpu *vcpu);
	void (*enable_nmi_window)(struct kvm_vcpu *vcpu);
	void (*enable_irq_window)(struct kvm_vcpu *vcpu);
	void (*update_cr8_intercept)(struct kvm_vcpu *vcpu, int tpr, int irr);
	const unsigned long required_apicv_inhibits;
	bool allow_apicv_in_x2apic_without_x2apic_virtualization;
	void (*refresh_apicv_exec_ctrl)(struct kvm_vcpu *vcpu);
	void (*hwapic_irr_update)(struct kvm_vcpu *vcpu, int max_irr);
	void (*hwapic_isr_update)(int isr);
	void (*load_eoi_exitmap)(struct kvm_vcpu *vcpu, u64 *eoi_exit_bitmap);
	void (*set_virtual_apic_mode)(struct kvm_vcpu *vcpu);
	void (*set_apic_access_page_addr)(struct kvm_vcpu *vcpu);
	void (*deliver_interrupt)(struct kvm_lapic *apic, int delivery_mode,
				  int trig_mode, int vector);
	int (*sync_pir_to_irr)(struct kvm_vcpu *vcpu);
	int (*set_tss_addr)(struct kvm *kvm, unsigned int addr);
	int (*set_identity_map_addr)(struct kvm *kvm, u64 ident_addr);
	u8 (*get_mt_mask)(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio);

	void (*load_mmu_pgd)(struct kvm_vcpu *vcpu, hpa_t root_hpa,
			     int root_level);

	bool (*has_wbinvd_exit)(void);

	u64 (*get_l2_tsc_offset)(struct kvm_vcpu *vcpu);
	u64 (*get_l2_tsc_multiplier)(struct kvm_vcpu *vcpu);
	void (*write_tsc_offset)(struct kvm_vcpu *vcpu);
	void (*write_tsc_multiplier)(struct kvm_vcpu *vcpu);

	/*
	 * Retrieve somewhat arbitrary exit information.  Intended to
	 * be used only from within tracepoints or error paths.
	 */
	void (*get_exit_info)(struct kvm_vcpu *vcpu, u32 *reason,
			      u64 *info1, u64 *info2,
			      u32 *exit_int_info, u32 *exit_int_info_err_code);

	int (*check_intercept)(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage,
			       struct x86_exception *exception);
	void (*handle_exit_irqoff)(struct kvm_vcpu *vcpu);

	/*
	 * Size of the CPU's dirty log buffer, i.e. VMX's PML buffer.  A zero
	 * value indicates CPU dirty logging is unsupported or disabled.
	 */
	int cpu_dirty_log_size;
	void (*update_cpu_dirty_logging)(struct kvm_vcpu *vcpu);

	const struct kvm_x86_nested_ops *nested_ops;

	void (*vcpu_blocking)(struct kvm_vcpu *vcpu);
	void (*vcpu_unblocking)(struct kvm_vcpu *vcpu);

	int (*pi_update_irte)(struct kvm *kvm, unsigned int host_irq,
			      uint32_t guest_irq, bool set);
	void (*pi_start_assignment)(struct kvm *kvm);
	void (*apicv_pre_state_restore)(struct kvm_vcpu *vcpu);
	void (*apicv_post_state_restore)(struct kvm_vcpu *vcpu);
	bool (*dy_apicv_has_pending_interrupt)(struct kvm_vcpu *vcpu);

	int (*set_hv_timer)(struct kvm_vcpu *vcpu, u64 guest_deadline_tsc,
			    bool *expired);
	void (*cancel_hv_timer)(struct kvm_vcpu *vcpu);

	void (*setup_mce)(struct kvm_vcpu *vcpu);

#ifdef CONFIG_KVM_SMM
	int (*smi_allowed)(struct kvm_vcpu *vcpu, bool for_injection);
	int (*enter_smm)(struct kvm_vcpu *vcpu, union kvm_smram *smram);
	int (*leave_smm)(struct kvm_vcpu *vcpu, const union kvm_smram *smram);
	void (*enable_smi_window)(struct kvm_vcpu *vcpu);
#endif

	int (*dev_get_attr)(u32 group, u64 attr, u64 *val);
	int (*mem_enc_ioctl)(struct kvm *kvm, void __user *argp);
	int (*mem_enc_register_region)(struct kvm *kvm, struct kvm_enc_region *argp);
	int (*mem_enc_unregister_region)(struct kvm *kvm, struct kvm_enc_region *argp);
	int (*vm_copy_enc_context_from)(struct kvm *kvm, unsigned int source_fd);
	int (*vm_move_enc_context_from)(struct kvm *kvm, unsigned int source_fd);
	void (*guest_memory_reclaimed)(struct kvm *kvm);

	int (*get_msr_feature)(struct kvm_msr_entry *entry);

	int (*check_emulate_instruction)(struct kvm_vcpu *vcpu, int emul_type,
					 void *insn, int insn_len);

	bool (*apic_init_signal_blocked)(struct kvm_vcpu *vcpu);
	int (*enable_l2_tlb_flush)(struct kvm_vcpu *vcpu);

	void (*migrate_timers)(struct kvm_vcpu *vcpu);
	void (*msr_filter_changed)(struct kvm_vcpu *vcpu);
	int (*complete_emulated_msr)(struct kvm_vcpu *vcpu, int err);

	void (*vcpu_deliver_sipi_vector)(struct kvm_vcpu *vcpu, u8 vector);

	/*
	 * Returns vCPU specific APICv inhibit reasons
	 */
	unsigned long (*vcpu_get_apicv_inhibit_reasons)(struct kvm_vcpu *vcpu);

	gva_t (*get_untagged_addr)(struct kvm_vcpu *vcpu, gva_t gva, unsigned int flags);
	void *(*alloc_apic_backing_page)(struct kvm_vcpu *vcpu);
	int (*gmem_prepare)(struct kvm *kvm, kvm_pfn_t pfn, gfn_t gfn, int max_order);
	void (*gmem_invalidate)(kvm_pfn_t start, kvm_pfn_t end);
	int (*private_max_mapping_level)(struct kvm *kvm, kvm_pfn_t pfn);
};

struct kvm_x86_nested_ops {
	void (*leave_nested)(struct kvm_vcpu *vcpu);
	bool (*is_exception_vmexit)(struct kvm_vcpu *vcpu, u8 vector,
				    u32 error_code);
	int (*check_events)(struct kvm_vcpu *vcpu);
	bool (*has_events)(struct kvm_vcpu *vcpu, bool for_injection);
	void (*triple_fault)(struct kvm_vcpu *vcpu);
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
	void (*hv_inject_synthetic_vmexit_post_tlb_flush)(struct kvm_vcpu *vcpu);
};

struct kvm_x86_init_ops {
	int (*hardware_setup)(void);
	unsigned int (*handle_intel_pt_intr)(void);

	struct kvm_x86_ops *runtime_ops;
	struct kvm_pmu_ops *pmu_ops;
};

struct kvm_arch_async_pf {
	u32 token;
	gfn_t gfn;
	unsigned long cr3;
	bool direct_map;
	u64 error_code;
};

extern u32 __read_mostly kvm_nr_uret_msrs;
extern bool __read_mostly allow_smaller_maxphyaddr;
extern bool __read_mostly enable_apicv;
extern struct kvm_x86_ops kvm_x86_ops;

#define kvm_x86_call(func) static_call(kvm_x86_##func)
#define kvm_pmu_call(func) static_call(kvm_x86_pmu_##func)

#define KVM_X86_OP(func) \
	DECLARE_STATIC_CALL(kvm_x86_##func, *(((struct kvm_x86_ops *)0)->func));
#define KVM_X86_OP_OPTIONAL KVM_X86_OP
#define KVM_X86_OP_OPTIONAL_RET0 KVM_X86_OP
#include <asm/kvm-x86-ops.h>

int kvm_x86_vendor_init(struct kvm_x86_init_ops *ops);
void kvm_x86_vendor_exit(void);

#define __KVM_HAVE_ARCH_VM_ALLOC
static inline struct kvm *kvm_arch_alloc_vm(void)
{
	return __vmalloc(kvm_x86_ops.vm_size, GFP_KERNEL_ACCOUNT | __GFP_ZERO);
}

#define __KVM_HAVE_ARCH_VM_FREE
void kvm_arch_free_vm(struct kvm *kvm);

#if IS_ENABLED(CONFIG_HYPERV)
#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS
static inline int kvm_arch_flush_remote_tlbs(struct kvm *kvm)
{
	if (kvm_x86_ops.flush_remote_tlbs &&
	    !kvm_x86_call(flush_remote_tlbs)(kvm))
		return 0;
	else
		return -ENOTSUPP;
}

#define __KVM_HAVE_ARCH_FLUSH_REMOTE_TLBS_RANGE
static inline int kvm_arch_flush_remote_tlbs_range(struct kvm *kvm, gfn_t gfn,
						   u64 nr_pages)
{
	if (!kvm_x86_ops.flush_remote_tlbs_range)
		return -EOPNOTSUPP;

	return kvm_x86_call(flush_remote_tlbs_range)(kvm, gfn, nr_pages);
}
#endif /* CONFIG_HYPERV */

enum kvm_intr_type {
	/* Values are arbitrary, but must be non-zero. */
	KVM_HANDLING_IRQ = 1,
	KVM_HANDLING_NMI,
};

/* Enable perf NMI and timer modes to work, and minimise false positives. */
#define kvm_arch_pmi_in_guest(vcpu) \
	((vcpu) && (vcpu)->arch.handling_intr_from_guest && \
	 (!!in_nmi() == ((vcpu)->arch.handling_intr_from_guest == KVM_HANDLING_NMI)))

void __init kvm_mmu_x86_module_init(void);
int kvm_mmu_vendor_module_init(void);
void kvm_mmu_vendor_module_exit(void);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu);
int kvm_mmu_create(struct kvm_vcpu *vcpu);
void kvm_mmu_init_vm(struct kvm *kvm);
void kvm_mmu_uninit_vm(struct kvm *kvm);

void kvm_mmu_init_memslot_memory_attributes(struct kvm *kvm,
					    struct kvm_memory_slot *slot);

void kvm_mmu_after_set_cpuid(struct kvm_vcpu *vcpu);
void kvm_mmu_reset_context(struct kvm_vcpu *vcpu);
void kvm_mmu_slot_remove_write_access(struct kvm *kvm,
				      const struct kvm_memory_slot *memslot,
				      int start_level);
void kvm_mmu_slot_try_split_huge_pages(struct kvm *kvm,
				       const struct kvm_memory_slot *memslot,
				       int target_level);
void kvm_mmu_try_split_huge_pages(struct kvm *kvm,
				  const struct kvm_memory_slot *memslot,
				  u64 start, u64 end,
				  int target_level);
void kvm_mmu_zap_collapsible_sptes(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot);
void kvm_mmu_slot_leaf_clear_dirty(struct kvm *kvm,
				   const struct kvm_memory_slot *memslot);
void kvm_mmu_invalidate_mmio_sptes(struct kvm *kvm, u64 gen);
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned long kvm_nr_mmu_pages);
void kvm_zap_gfn_range(struct kvm *kvm, gfn_t gfn_start, gfn_t gfn_end);

int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3);

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

/*
 * EMULTYPE_NO_DECODE - Set when re-emulating an instruction (after completing
 *			userspace I/O) to indicate that the emulation context
 *			should be reused as is, i.e. skip initialization of
 *			emulation context, instruction fetch and decode.
 *
 * EMULTYPE_TRAP_UD - Set when emulating an intercepted #UD from hardware.
 *		      Indicates that only select instructions (tagged with
 *		      EmulateOnUD) should be emulated (to minimize the emulator
 *		      attack surface).  See also EMULTYPE_TRAP_UD_FORCED.
 *
 * EMULTYPE_SKIP - Set when emulating solely to skip an instruction, i.e. to
 *		   decode the instruction length.  For use *only* by
 *		   kvm_x86_ops.skip_emulated_instruction() implementations if
 *		   EMULTYPE_COMPLETE_USER_EXIT is not set.
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
 *			VMware backdoor emulation handles select instructions
 *			and reinjects the #GP for all other cases.
 *
 * EMULTYPE_PF - Set when emulating MMIO by way of an intercepted #PF, in which
 *		 case the CR2/GPA value pass on the stack is valid.
 *
 * EMULTYPE_COMPLETE_USER_EXIT - Set when the emulator should update interruptibility
 *				 state and inject single-step #DBs after skipping
 *				 an instruction (after completing userspace I/O).
 *
 * EMULTYPE_WRITE_PF_TO_SP - Set when emulating an intercepted page fault that
 *			     is attempting to write a gfn that contains one or
 *			     more of the PTEs used to translate the write itself,
 *			     and the owning page table is being shadowed by KVM.
 *			     If emulation of the faulting instruction fails and
 *			     this flag is set, KVM will exit to userspace instead
 *			     of retrying emulation as KVM cannot make forward
 *			     progress.
 *
 *			     If emulation fails for a write to guest page tables,
 *			     KVM unprotects (zaps) the shadow page for the target
 *			     gfn and resumes the guest to retry the non-emulatable
 *			     instruction (on hardware).  Unprotecting the gfn
 *			     doesn't allow forward progress for a self-changing
 *			     access because doing so also zaps the translation for
 *			     the gfn, i.e. retrying the instruction will hit a
 *			     !PRESENT fault, which results in a new shadow page
 *			     and sends KVM back to square one.
 */
#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
#define EMULTYPE_ALLOW_RETRY_PF	    (1 << 3)
#define EMULTYPE_TRAP_UD_FORCED	    (1 << 4)
#define EMULTYPE_VMWARE_GP	    (1 << 5)
#define EMULTYPE_PF		    (1 << 6)
#define EMULTYPE_COMPLETE_USER_EXIT (1 << 7)
#define EMULTYPE_WRITE_PF_TO_SP	    (1 << 8)

int kvm_emulate_instruction(struct kvm_vcpu *vcpu, int emulation_type);
int kvm_emulate_instruction_from_buffer(struct kvm_vcpu *vcpu,
					void *insn, int insn_len);
void __kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu,
					  u64 *data, u8 ndata);
void kvm_prepare_emulation_failure_exit(struct kvm_vcpu *vcpu);

void kvm_enable_efer_bits(u64);
bool kvm_valid_efer(struct kvm_vcpu *vcpu, u64 efer);
int __kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data, bool host_initiated);
int kvm_get_msr(struct kvm_vcpu *vcpu, u32 index, u64 *data);
int kvm_set_msr(struct kvm_vcpu *vcpu, u32 index, u64 data);
int kvm_emulate_rdmsr(struct kvm_vcpu *vcpu);
int kvm_emulate_wrmsr(struct kvm_vcpu *vcpu);
int kvm_emulate_as_nop(struct kvm_vcpu *vcpu);
int kvm_emulate_invd(struct kvm_vcpu *vcpu);
int kvm_emulate_mwait(struct kvm_vcpu *vcpu);
int kvm_handle_invalid_op(struct kvm_vcpu *vcpu);
int kvm_emulate_monitor(struct kvm_vcpu *vcpu);

int kvm_fast_pio(struct kvm_vcpu *vcpu, int size, unsigned short port, int in);
int kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int kvm_emulate_halt_noskip(struct kvm_vcpu *vcpu);
int kvm_emulate_ap_reset_hold(struct kvm_vcpu *vcpu);
int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu);

void kvm_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
void kvm_set_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
int kvm_load_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector, int seg);
void kvm_vcpu_deliver_sipi_vector(struct kvm_vcpu *vcpu, u8 vector);

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int idt_index,
		    int reason, bool has_error_code, u32 error_code);

void kvm_post_set_cr0(struct kvm_vcpu *vcpu, unsigned long old_cr0, unsigned long cr0);
void kvm_post_set_cr4(struct kvm_vcpu *vcpu, unsigned long old_cr4, unsigned long cr4);
int kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
int kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3);
int kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
int kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8);
int kvm_set_dr(struct kvm_vcpu *vcpu, int dr, unsigned long val);
unsigned long kvm_get_dr(struct kvm_vcpu *vcpu, int dr);
unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw);
int kvm_emulate_xsetbv(struct kvm_vcpu *vcpu);

int kvm_get_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, struct msr_data *msr);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu);
void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);
int kvm_emulate_rdpmc(struct kvm_vcpu *vcpu);

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_queue_exception_p(struct kvm_vcpu *vcpu, unsigned nr, unsigned long payload);
void kvm_requeue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_requeue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_inject_page_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault);
void kvm_inject_emulated_page_fault(struct kvm_vcpu *vcpu,
				    struct x86_exception *fault);
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

int kvm_pic_set_irq(struct kvm_pic *pic, int irq, int irq_source_id, int level);
void kvm_pic_clear_all(struct kvm_pic *pic, int irq_source_id);

void kvm_inject_nmi(struct kvm_vcpu *vcpu);
int kvm_get_nr_pending_nmis(struct kvm_vcpu *vcpu);

void kvm_update_dr7(struct kvm_vcpu *vcpu);

int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn);
void kvm_mmu_free_roots(struct kvm *kvm, struct kvm_mmu *mmu,
			ulong roots_to_free);
void kvm_mmu_free_guest_mode_roots(struct kvm *kvm, struct kvm_mmu *mmu);
gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception);

bool kvm_apicv_activated(struct kvm *kvm);
bool kvm_vcpu_apicv_activated(struct kvm_vcpu *vcpu);
void __kvm_vcpu_update_apicv(struct kvm_vcpu *vcpu);
void __kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				      enum kvm_apicv_inhibit reason, bool set);
void kvm_set_or_clear_apicv_inhibit(struct kvm *kvm,
				    enum kvm_apicv_inhibit reason, bool set);

static inline void kvm_set_apicv_inhibit(struct kvm *kvm,
					 enum kvm_apicv_inhibit reason)
{
	kvm_set_or_clear_apicv_inhibit(kvm, reason, true);
}

static inline void kvm_clear_apicv_inhibit(struct kvm *kvm,
					   enum kvm_apicv_inhibit reason)
{
	kvm_set_or_clear_apicv_inhibit(kvm, reason, false);
}

unsigned long __kvm_emulate_hypercall(struct kvm_vcpu *vcpu, unsigned long nr,
				      unsigned long a0, unsigned long a1,
				      unsigned long a2, unsigned long a3,
				      int op_64_bit, int cpl);
int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gpa_t cr2_or_gpa, u64 error_code,
		       void *insn, int insn_len);
void kvm_mmu_print_sptes(struct kvm_vcpu *vcpu, gpa_t gpa, const char *msg);
void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva);
void kvm_mmu_invalidate_addr(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu,
			     u64 addr, unsigned long roots);
void kvm_mmu_invpcid_gva(struct kvm_vcpu *vcpu, gva_t gva, unsigned long pcid);
void kvm_mmu_new_pgd(struct kvm_vcpu *vcpu, gpa_t new_pgd);

void kvm_configure_mmu(bool enable_tdp, int tdp_forced_root_level,
		       int tdp_max_root_level, int tdp_huge_page_level);


#ifdef CONFIG_KVM_PRIVATE_MEM
#define kvm_arch_has_private_mem(kvm) ((kvm)->arch.has_private_mem)
#else
#define kvm_arch_has_private_mem(kvm) false
#endif

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

#define HF_GUEST_MASK		(1 << 0) /* VCPU is in guest-mode */

#ifdef CONFIG_KVM_SMM
#define HF_SMM_MASK		(1 << 1)
#define HF_SMM_INSIDE_NMI_MASK	(1 << 2)

# define KVM_MAX_NR_ADDRESS_SPACES	2
/* SMM is currently unsupported for guests with private memory. */
# define kvm_arch_nr_memslot_as_ids(kvm) (kvm_arch_has_private_mem(kvm) ? 1 : 2)
# define kvm_arch_vcpu_memslots_id(vcpu) ((vcpu)->arch.hflags & HF_SMM_MASK ? 1 : 0)
# define kvm_memslots_for_spte_role(kvm, role) __kvm_memslots(kvm, (role).smm)
#else
# define kvm_memslots_for_spte_role(kvm, role) __kvm_memslots(kvm, 0)
#endif

int kvm_cpu_has_injectable_intr(struct kvm_vcpu *v);
int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_cpu_has_extint(struct kvm_vcpu *v);
int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);
void kvm_vcpu_reset(struct kvm_vcpu *vcpu, bool init_event);

int kvm_pv_send_ipi(struct kvm *kvm, unsigned long ipi_bitmap_low,
		    unsigned long ipi_bitmap_high, u32 min,
		    unsigned long icr, int op_64_bit);

int kvm_add_user_return_msr(u32 msr);
int kvm_find_user_return_msr(u32 msr);
int kvm_set_user_return_msr(unsigned index, u64 val, u64 mask);

static inline bool kvm_is_supported_user_return_msr(u32 msr)
{
	return kvm_find_user_return_msr(msr) >= 0;
}

u64 kvm_scale_tsc(u64 tsc, u64 ratio);
u64 kvm_read_l1_tsc(struct kvm_vcpu *vcpu, u64 host_tsc);
u64 kvm_calc_nested_tsc_offset(u64 l1_offset, u64 l2_offset, u64 l2_multiplier);
u64 kvm_calc_nested_tsc_multiplier(u64 l1_multiplier, u64 l2_multiplier);

unsigned long kvm_get_linear_rip(struct kvm_vcpu *vcpu);
bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip);

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

void __user *__x86_set_memory_region(struct kvm *kvm, int id, gpa_t gpa,
				     u32 size);
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
	kvm_x86_call(vcpu_blocking)(vcpu);
}

static inline void kvm_arch_vcpu_unblocking(struct kvm_vcpu *vcpu)
{
	kvm_x86_call(vcpu_unblocking)(vcpu);
}

static inline int kvm_cpu_get_apicid(int mps_cpu)
{
#ifdef CONFIG_X86_LOCAL_APIC
	return default_cpu_present_to_apicid(mps_cpu);
#else
	WARN_ON_ONCE(1);
	return BAD_APICID;
#endif
}

int memslot_rmap_alloc(struct kvm_memory_slot *slot, unsigned long npages);

#define KVM_CLOCK_VALID_FLAGS						\
	(KVM_CLOCK_TSC_STABLE | KVM_CLOCK_REALTIME | KVM_CLOCK_HOST_TSC)

#define KVM_X86_VALID_QUIRKS			\
	(KVM_X86_QUIRK_LINT0_REENABLED |	\
	 KVM_X86_QUIRK_CD_NW_CLEARED |		\
	 KVM_X86_QUIRK_LAPIC_MMIO_HOLE |	\
	 KVM_X86_QUIRK_OUT_7E_INC_RIP |		\
	 KVM_X86_QUIRK_MISC_ENABLE_NO_MWAIT |	\
	 KVM_X86_QUIRK_FIX_HYPERCALL_INSN |	\
	 KVM_X86_QUIRK_MWAIT_NEVER_UD_FAULTS)

/*
 * KVM previously used a u32 field in kvm_run to indicate the hypercall was
 * initiated from long mode. KVM now sets bit 0 to indicate long mode, but the
 * remaining 31 lower bits must be 0 to preserve ABI.
 */
#define KVM_EXIT_HYPERCALL_MBZ		GENMASK_ULL(31, 1)

#endif /* _ASM_X86_KVM_HOST_H */
