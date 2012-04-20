/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This header defines architecture specific interfaces, x86 version
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _ASM_X86_KVM_HOST_H
#define _ASM_X86_KVM_HOST_H

#include <linux/types.h>
#include <linux/mm.h>
#include <linux/mmu_notifier.h>
#include <linux/tracepoint.h>
#include <linux/cpumask.h>
#include <linux/irq_work.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>
#include <linux/perf_event.h>

#include <asm/pvclock-abi.h>
#include <asm/desc.h>
#include <asm/mtrr.h>
#include <asm/msr-index.h>
#include <asm/asm.h>

#define KVM_MAX_VCPUS 254
#define KVM_SOFT_MAX_VCPUS 160
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4
#define KVM_MEM_SLOTS_NUM (KVM_MEMORY_SLOTS + KVM_PRIVATE_MEM_SLOTS)

#define KVM_MMIO_SIZE 16

#define KVM_PIO_PAGE_OFFSET 1
#define KVM_COALESCED_MMIO_PAGE_OFFSET 2

#define CR0_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR0_PE | X86_CR0_MP | X86_CR0_EM | X86_CR0_TS \
			  | X86_CR0_ET | X86_CR0_NE | X86_CR0_WP | X86_CR0_AM \
			  | X86_CR0_NW | X86_CR0_CD | X86_CR0_PG))

#define CR3_PAE_RESERVED_BITS ((X86_CR3_PWT | X86_CR3_PCD) - 1)
#define CR3_NONPAE_RESERVED_BITS ((PAGE_SIZE-1) & ~(X86_CR3_PWT | X86_CR3_PCD))
#define CR3_L_MODE_RESERVED_BITS (CR3_NONPAE_RESERVED_BITS |	\
				  0xFFFFFF0000000000ULL)
#define CR4_RESERVED_BITS                                               \
	(~(unsigned long)(X86_CR4_VME | X86_CR4_PVI | X86_CR4_TSD | X86_CR4_DE\
			  | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_MCE     \
			  | X86_CR4_PGE | X86_CR4_PCE | X86_CR4_OSFXSR  \
			  | X86_CR4_OSXSAVE | X86_CR4_SMEP | X86_CR4_RDWRGSFS \
			  | X86_CR4_OSXMMEXCPT | X86_CR4_VMXE))

#define CR8_RESERVED_BITS (~(unsigned long)X86_CR8_TPR)



#define INVALID_PAGE (~(hpa_t)0)
#define VALID_PAGE(x) ((x) != INVALID_PAGE)

#define UNMAPPED_GVA (~(gpa_t)0)

/* KVM Hugepage definitions for x86 */
#define KVM_NR_PAGE_SIZES	3
#define KVM_HPAGE_GFN_SHIFT(x)	(((x) - 1) * 9)
#define KVM_HPAGE_SHIFT(x)	(PAGE_SHIFT + KVM_HPAGE_GFN_SHIFT(x))
#define KVM_HPAGE_SIZE(x)	(1UL << KVM_HPAGE_SHIFT(x))
#define KVM_HPAGE_MASK(x)	(~(KVM_HPAGE_SIZE(x) - 1))
#define KVM_PAGES_PER_HPAGE(x)	(KVM_HPAGE_SIZE(x) / PAGE_SIZE)

#define DE_VECTOR 0
#define DB_VECTOR 1
#define BP_VECTOR 3
#define OF_VECTOR 4
#define BR_VECTOR 5
#define UD_VECTOR 6
#define NM_VECTOR 7
#define DF_VECTOR 8
#define TS_VECTOR 10
#define NP_VECTOR 11
#define SS_VECTOR 12
#define GP_VECTOR 13
#define PF_VECTOR 14
#define MF_VECTOR 16
#define MC_VECTOR 18

#define SELECTOR_TI_MASK (1 << 2)
#define SELECTOR_RPL_MASK 0x03

#define IOPL_SHIFT 12

#define KVM_PERMILLE_MMU_PAGES 20
#define KVM_MIN_ALLOC_MMU_PAGES 64
#define KVM_MMU_HASH_SHIFT 10
#define KVM_NUM_MMU_PAGES (1 << KVM_MMU_HASH_SHIFT)
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 80
#define KVM_NR_FIXED_MTRR_REGION 88
#define KVM_NR_VAR_MTRR 8

#define ASYNC_PF_PER_VCPU 64

extern raw_spinlock_t kvm_lock;
extern struct list_head vm_list;

struct kvm_vcpu;
struct kvm;
struct kvm_async_pf;

enum kvm_reg {
	VCPU_REGS_RAX = 0,
	VCPU_REGS_RCX = 1,
	VCPU_REGS_RDX = 2,
	VCPU_REGS_RBX = 3,
	VCPU_REGS_RSP = 4,
	VCPU_REGS_RBP = 5,
	VCPU_REGS_RSI = 6,
	VCPU_REGS_RDI = 7,
#ifdef CONFIG_X86_64
	VCPU_REGS_R8 = 8,
	VCPU_REGS_R9 = 9,
	VCPU_REGS_R10 = 10,
	VCPU_REGS_R11 = 11,
	VCPU_REGS_R12 = 12,
	VCPU_REGS_R13 = 13,
	VCPU_REGS_R14 = 14,
	VCPU_REGS_R15 = 15,
#endif
	VCPU_REGS_RIP,
	NR_VCPU_REGS
};

enum kvm_reg_ex {
	VCPU_EXREG_PDPTR = NR_VCPU_REGS,
	VCPU_EXREG_CR3,
	VCPU_EXREG_RFLAGS,
	VCPU_EXREG_CPL,
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
#define DR6_FIXED_1	0xffff0ff0
#define DR6_VOLATILE	0x0000e00f

#define DR7_BP_EN_MASK	0x000000ff
#define DR7_GE		(1 << 9)
#define DR7_GD		(1 << 13)
#define DR7_FIXED_1	0x00000400
#define DR7_VOLATILE	0xffff23ff

/*
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

/*
 * kvm_mmu_page_role, below, is defined as:
 *
 *   bits 0:3 - total guest paging levels (2-4, or zero for real mode)
 *   bits 4:7 - page table level for this shadow (1-4)
 *   bits 8:9 - page table quadrant for 2-level guests
 *   bit   16 - direct mapping of virtual to physical mapping at gfn
 *              used for real mode and two-dimensional paging
 *   bits 17:19 - common access permissions for all ptes in this shadow page
 */
union kvm_mmu_page_role {
	unsigned word;
	struct {
		unsigned level:4;
		unsigned cr4_pae:1;
		unsigned quadrant:2;
		unsigned pad_for_nice_hex_output:6;
		unsigned direct:1;
		unsigned access:3;
		unsigned invalid:1;
		unsigned nxe:1;
		unsigned cr0_wp:1;
		unsigned smep_andnot_wp:1;
	};
};

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;

	/*
	 * The following two entries are used to key the shadow page in the
	 * hash table.
	 */
	gfn_t gfn;
	union kvm_mmu_page_role role;

	u64 *spt;
	/* hold the gfn of each spte inside spt */
	gfn_t *gfns;
	/*
	 * One bit set per slot which has memory
	 * in this shadow page.
	 */
	DECLARE_BITMAP(slot_bitmap, KVM_MEM_SLOTS_NUM);
	bool unsync;
	int root_count;          /* Currently serving as active root */
	unsigned int unsync_children;
	unsigned long parent_ptes;	/* Reverse mapping for parent_pte */
	DECLARE_BITMAP(unsync_child_bitmap, 512);

#ifdef CONFIG_X86_32
	int clear_spte_count;
#endif

	int write_flooding_count;

	struct rcu_head rcu;
};

struct kvm_pio_request {
	unsigned long count;
	int in;
	int port;
	int size;
};

/*
 * x86 supports 3 paging modes (4-level 64-bit, 3-level 64-bit, and 2-level
 * 32-bit).  The kvm_mmu structure abstracts the details of the current mmu
 * mode.
 */
struct kvm_mmu {
	void (*new_cr3)(struct kvm_vcpu *vcpu);
	void (*set_cr3)(struct kvm_vcpu *vcpu, unsigned long root);
	unsigned long (*get_cr3)(struct kvm_vcpu *vcpu);
	u64 (*get_pdptr)(struct kvm_vcpu *vcpu, int index);
	int (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err,
			  bool prefault);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  struct x86_exception *fault);
	void (*free)(struct kvm_vcpu *vcpu);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva, u32 access,
			    struct x86_exception *exception);
	gpa_t (*translate_gpa)(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access);
	int (*sync_page)(struct kvm_vcpu *vcpu,
			 struct kvm_mmu_page *sp);
	void (*invlpg)(struct kvm_vcpu *vcpu, gva_t gva);
	void (*update_pte)(struct kvm_vcpu *vcpu, struct kvm_mmu_page *sp,
			   u64 *spte, const void *pte);
	hpa_t root_hpa;
	int root_level;
	int shadow_root_level;
	union kvm_mmu_page_role base_role;
	bool direct_map;

	u64 *pae_root;
	u64 *lm_root;
	u64 rsvd_bits_mask[2][4];

	bool nx;

	u64 pdptrs[4]; /* pae */
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
	u8 version;
	struct kvm_pmc gp_counters[X86_PMC_MAX_GENERIC];
	struct kvm_pmc fixed_counters[X86_PMC_MAX_FIXED];
	struct irq_work irq_work;
	u64 reprogram_pmi;
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
	u32 hflags;
	u64 efer;
	u64 apic_base;
	struct kvm_lapic *apic;    /* kernel irqchip context */
	int32_t apic_arb_prio;
	int mp_state;
	int sipi_vector;
	u64 ia32_misc_enable_msr;
	bool tpr_access_reporting;

	/*
	 * Paging state of the vcpu
	 *
	 * If the vcpu runs in guest mode with two level paging this still saves
	 * the paging mode of the l1 guest. This context is always used to
	 * handle faults.
	 */
	struct kvm_mmu mmu;

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

	struct fpu guest_fpu;
	u64 xcr0;

	struct kvm_pio_request pio;
	void *pio_data;

	u8 event_exit_inst_len;

	struct kvm_queued_exception {
		bool pending;
		bool has_error_code;
		bool reinject;
		u8 nr;
		u32 error_code;
	} exception;

	struct kvm_queued_interrupt {
		bool pending;
		bool soft;
		u8 nr;
	} interrupt;

	int halt_request; /* real mode on Intel only */

	int cpuid_nent;
	struct kvm_cpuid_entry2 cpuid_entries[KVM_MAX_CPUID_ENTRIES];
	/* emulate context */

	struct x86_emulate_ctxt emulate_ctxt;
	bool emulate_regs_need_sync_to_vcpu;
	bool emulate_regs_need_sync_from_vcpu;

	gpa_t time;
	struct pvclock_vcpu_time_info hv_clock;
	unsigned int hw_tsc_khz;
	unsigned int time_offset;
	struct page *time_page;

	struct {
		u64 msr_val;
		u64 last_steal;
		u64 accum_steal;
		struct gfn_to_hva_cache stime;
		struct kvm_steal_time steal;
	} st;

	u64 last_guest_tsc;
	u64 last_kernel_ns;
	u64 last_host_tsc;
	u64 tsc_offset_adjustment;
	u64 this_tsc_nsec;
	u64 this_tsc_write;
	u8  this_tsc_generation;
	bool tsc_catchup;
	bool tsc_always_catchup;
	s8 virtual_tsc_shift;
	u32 virtual_tsc_mult;
	u32 virtual_tsc_khz;

	atomic_t nmi_queued;  /* unprocessed asynchronous NMIs */
	unsigned nmi_pending; /* NMI queued after currently running handler */
	bool nmi_injected;    /* Trying to inject an NMI this entry */

	struct mtrr_state_type mtrr_state;
	u32 pat;

	int switch_db_regs;
	unsigned long db[KVM_NR_DB_REGS];
	unsigned long dr6;
	unsigned long dr7;
	unsigned long eff_db[KVM_NR_DB_REGS];

	u64 mcg_cap;
	u64 mcg_status;
	u64 mcg_ctl;
	u64 *mce_banks;

	/* Cache MMIO info */
	u64 mmio_gva;
	unsigned access;
	gfn_t mmio_gfn;

	struct kvm_pmu pmu;

	/* used for guest single stepping over the given code position */
	unsigned long singlestep_rip;

	/* fields used by HYPER-V emulation */
	u64 hv_vapic;

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
	} apf;

	/* OSVW MSRs (AMD only) */
	struct {
		u64 length;
		u64 status;
	} osvw;
};

struct kvm_lpage_info {
	unsigned long rmap_pde;
	int write_count;
};

struct kvm_arch_memory_slot {
	struct kvm_lpage_info *lpage_info[KVM_NR_PAGE_SIZES - 1];
};

struct kvm_arch {
	unsigned int n_used_mmu_pages;
	unsigned int n_requested_mmu_pages;
	unsigned int n_max_mmu_pages;
	unsigned int indirect_shadow_pages;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	/*
	 * Hash table of struct kvm_mmu_page.
	 */
	struct list_head active_mmu_pages;
	struct list_head assigned_dev_head;
	struct iommu_domain *iommu_domain;
	int iommu_flags;
	struct kvm_pic *vpic;
	struct kvm_ioapic *vioapic;
	struct kvm_pit *vpit;
	int vapics_in_nmi_mode;

	unsigned int tss_addr;
	struct page *apic_access_page;

	gpa_t wall_clock;

	struct page *ept_identity_pagetable;
	bool ept_identity_pagetable_done;
	gpa_t ept_identity_map_addr;

	unsigned long irq_sources_bitmap;
	s64 kvmclock_offset;
	raw_spinlock_t tsc_write_lock;
	u64 last_tsc_nsec;
	u64 last_tsc_write;
	u32 last_tsc_khz;
	u64 cur_tsc_nsec;
	u64 cur_tsc_write;
	u64 cur_tsc_offset;
	u8  cur_tsc_generation;

	struct kvm_xen_hvm_config xen_hvm_config;

	/* fields used by HYPER-V emulation */
	u64 hv_guest_os_id;
	u64 hv_hypercall;

	atomic_t reader_counter;

	#ifdef CONFIG_KVM_MMU_AUDIT
	int audit_point;
	#endif
};

struct kvm_vm_stat {
	u32 mmu_shadow_zapped;
	u32 mmu_pte_write;
	u32 mmu_pte_updated;
	u32 mmu_pde_zapped;
	u32 mmu_flooded;
	u32 mmu_recycled;
	u32 mmu_cache_miss;
	u32 mmu_unsync;
	u32 remote_tlb_flush;
	u32 lpages;
};

struct kvm_vcpu_stat {
	u32 pf_fixed;
	u32 pf_guest;
	u32 tlb_flush;
	u32 invlpg;

	u32 exits;
	u32 io_exits;
	u32 mmio_exits;
	u32 signal_exits;
	u32 irq_window_exits;
	u32 nmi_window_exits;
	u32 halt_exits;
	u32 halt_wakeup;
	u32 request_irq_exits;
	u32 irq_exits;
	u32 host_state_reload;
	u32 efer_reload;
	u32 fpu_reload;
	u32 insn_emulation;
	u32 insn_emulation_fail;
	u32 hypercalls;
	u32 irq_injections;
	u32 nmi_injections;
};

struct x86_instruction_info;

struct kvm_x86_ops {
	int (*cpu_has_kvm_support)(void);          /* __init */
	int (*disabled_by_bios)(void);             /* __init */
	int (*hardware_enable)(void *dummy);
	void (*hardware_disable)(void *dummy);
	void (*check_processor_compatibility)(void *rtn);
	int (*hardware_setup)(void);               /* __init */
	void (*hardware_unsetup)(void);            /* __exit */
	bool (*cpu_has_accelerated_tpr)(void);
	void (*cpuid_update)(struct kvm_vcpu *vcpu);

	/* Create, but do not attach this VCPU */
	struct kvm_vcpu *(*vcpu_create)(struct kvm *kvm, unsigned id);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	int (*vcpu_reset)(struct kvm_vcpu *vcpu);

	void (*prepare_guest_switch)(struct kvm_vcpu *vcpu);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);

	void (*set_guest_debug)(struct kvm_vcpu *vcpu,
				struct kvm_guest_debug *dbg);
	int (*get_msr)(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata);
	int (*set_msr)(struct kvm_vcpu *vcpu, u32 msr_index, u64 data);
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
	void (*set_dr7)(struct kvm_vcpu *vcpu, unsigned long value);
	void (*cache_reg)(struct kvm_vcpu *vcpu, enum kvm_reg reg);
	unsigned long (*get_rflags)(struct kvm_vcpu *vcpu);
	void (*set_rflags)(struct kvm_vcpu *vcpu, unsigned long rflags);
	void (*fpu_activate)(struct kvm_vcpu *vcpu);
	void (*fpu_deactivate)(struct kvm_vcpu *vcpu);

	void (*tlb_flush)(struct kvm_vcpu *vcpu);

	void (*run)(struct kvm_vcpu *vcpu);
	int (*handle_exit)(struct kvm_vcpu *vcpu);
	void (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*set_interrupt_shadow)(struct kvm_vcpu *vcpu, int mask);
	u32 (*get_interrupt_shadow)(struct kvm_vcpu *vcpu, int mask);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
	void (*set_irq)(struct kvm_vcpu *vcpu);
	void (*set_nmi)(struct kvm_vcpu *vcpu);
	void (*queue_exception)(struct kvm_vcpu *vcpu, unsigned nr,
				bool has_error_code, u32 error_code,
				bool reinject);
	void (*cancel_injection)(struct kvm_vcpu *vcpu);
	int (*interrupt_allowed)(struct kvm_vcpu *vcpu);
	int (*nmi_allowed)(struct kvm_vcpu *vcpu);
	bool (*get_nmi_mask)(struct kvm_vcpu *vcpu);
	void (*set_nmi_mask)(struct kvm_vcpu *vcpu, bool masked);
	void (*enable_nmi_window)(struct kvm_vcpu *vcpu);
	void (*enable_irq_window)(struct kvm_vcpu *vcpu);
	void (*update_cr8_intercept)(struct kvm_vcpu *vcpu, int tpr, int irr);
	int (*set_tss_addr)(struct kvm *kvm, unsigned int addr);
	int (*get_tdp_level)(void);
	u64 (*get_mt_mask)(struct kvm_vcpu *vcpu, gfn_t gfn, bool is_mmio);
	int (*get_lpage_level)(void);
	bool (*rdtscp_supported)(void);
	void (*adjust_tsc_offset)(struct kvm_vcpu *vcpu, s64 adjustment, bool host);

	void (*set_tdp_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);

	void (*set_supported_cpuid)(u32 func, struct kvm_cpuid_entry2 *entry);

	bool (*has_wbinvd_exit)(void);

	void (*set_tsc_khz)(struct kvm_vcpu *vcpu, u32 user_tsc_khz, bool scale);
	void (*write_tsc_offset)(struct kvm_vcpu *vcpu, u64 offset);

	u64 (*compute_tsc_offset)(struct kvm_vcpu *vcpu, u64 target_tsc);
	u64 (*read_l1_tsc)(struct kvm_vcpu *vcpu);

	void (*get_exit_info)(struct kvm_vcpu *vcpu, u64 *info1, u64 *info2);

	int (*check_intercept)(struct kvm_vcpu *vcpu,
			       struct x86_instruction_info *info,
			       enum x86_intercept_stage stage);
};

struct kvm_arch_async_pf {
	u32 token;
	gfn_t gfn;
	unsigned long cr3;
	bool direct_map;
};

extern struct kvm_x86_ops *kvm_x86_ops;

static inline void adjust_tsc_offset_guest(struct kvm_vcpu *vcpu,
					   s64 adjustment)
{
	kvm_x86_ops->adjust_tsc_offset(vcpu, adjustment, false);
}

static inline void adjust_tsc_offset_host(struct kvm_vcpu *vcpu, s64 adjustment)
{
	kvm_x86_ops->adjust_tsc_offset(vcpu, adjustment, true);
}

int kvm_mmu_module_init(void);
void kvm_mmu_module_exit(void);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu);
int kvm_mmu_create(struct kvm_vcpu *vcpu);
int kvm_mmu_setup(struct kvm_vcpu *vcpu);
void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask);

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu);
void kvm_mmu_slot_remove_write_access(struct kvm *kvm, int slot);
int kvm_mmu_rmap_write_protect(struct kvm *kvm, u64 gfn,
			       struct kvm_memory_slot *slot);
void kvm_mmu_zap_all(struct kvm *kvm);
unsigned int kvm_mmu_calculate_mmu_pages(struct kvm *kvm);
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned int kvm_nr_mmu_pages);

int load_pdptrs(struct kvm_vcpu *vcpu, struct kvm_mmu *mmu, unsigned long cr3);

int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			  const void *val, int bytes);
u8 kvm_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn);

extern bool tdp_enabled;

u64 vcpu_tsc_khz(struct kvm_vcpu *vcpu);

/* control of guest tsc rate supported? */
extern bool kvm_has_tsc_control;
/* minimum supported tsc_khz for guests */
extern u32  kvm_min_guest_tsc_khz;
/* maximum supported tsc_khz for guests */
extern u32  kvm_max_guest_tsc_khz;

enum emulation_result {
	EMULATE_DONE,       /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with mmio request */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
#define EMULTYPE_RETRY		    (1 << 3)
int x86_emulate_instruction(struct kvm_vcpu *vcpu, unsigned long cr2,
			    int emulation_type, void *insn, int insn_len);

static inline int emulate_instruction(struct kvm_vcpu *vcpu,
			int emulation_type)
{
	return x86_emulate_instruction(vcpu, 0, emulation_type, NULL, 0);
}

void kvm_enable_efer_bits(u64);
int kvm_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *data);
int kvm_set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data);

struct x86_emulate_ctxt;

int kvm_fast_pio_out(struct kvm_vcpu *vcpu, int size, unsigned short port);
void kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int kvm_emulate_wbinvd(struct kvm_vcpu *vcpu);

void kvm_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
int kvm_load_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector, int seg);

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

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data);

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
void kvm_propagate_fault(struct kvm_vcpu *vcpu, struct x86_exception *fault);
bool kvm_require_cpl(struct kvm_vcpu *vcpu, int required_cpl);

int kvm_pic_set_irq(void *opaque, int irq, int level);

void kvm_inject_nmi(struct kvm_vcpu *vcpu);

int fx_init(struct kvm_vcpu *vcpu);

void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu);
void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		       const u8 *new, int bytes);
int kvm_mmu_unprotect_page(struct kvm *kvm, gfn_t gfn);
int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva);
void __kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu);
int kvm_mmu_load(struct kvm_vcpu *vcpu);
void kvm_mmu_unload(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu);
gpa_t translate_nested_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access);
gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva,
			      struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_fetch(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva,
			       struct x86_exception *exception);
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva,
				struct x86_exception *exception);

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t gva, u32 error_code,
		       void *insn, int insn_len);
void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva);

void kvm_enable_tdp(void);
void kvm_disable_tdp(void);

int complete_pio(struct kvm_vcpu *vcpu);
bool kvm_check_iopl(struct kvm_vcpu *vcpu);

static inline gpa_t translate_gpa(struct kvm_vcpu *vcpu, gpa_t gpa, u32 access)
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

/*
 * Hardware virtualization extension instructions may fault if a
 * reboot turns off virtualization while processes are running.
 * Trap the fault and ignore the instruction if that happens.
 */
asmlinkage void kvm_spurious_fault(void);
extern bool kvm_rebooting;

#define ____kvm_handle_fault_on_reboot(insn, cleanup_insn)	\
	"666: " insn "\n\t" \
	"668: \n\t"                           \
	".pushsection .fixup, \"ax\" \n" \
	"667: \n\t" \
	cleanup_insn "\n\t"		      \
	"cmpb $0, kvm_rebooting \n\t"	      \
	"jne 668b \n\t"      		      \
	__ASM_SIZE(push) " $666b \n\t"	      \
	"call kvm_spurious_fault \n\t"	      \
	".popsection \n\t" \
	_ASM_EXTABLE(666b, 667b)

#define __kvm_handle_fault_on_reboot(insn)		\
	____kvm_handle_fault_on_reboot(insn, "")

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_age_hva(struct kvm *kvm, unsigned long hva);
int kvm_test_age_hva(struct kvm *kvm, unsigned long hva);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int cpuid_maxphyaddr(struct kvm_vcpu *vcpu);
int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);

void kvm_define_shared_msr(unsigned index, u32 msr);
void kvm_set_shared_msr(unsigned index, u64 val, u64 mask);

bool kvm_is_linear_rip(struct kvm_vcpu *vcpu, unsigned long linear_rip);

void kvm_arch_async_page_not_present(struct kvm_vcpu *vcpu,
				     struct kvm_async_pf *work);
void kvm_arch_async_page_present(struct kvm_vcpu *vcpu,
				 struct kvm_async_pf *work);
void kvm_arch_async_page_ready(struct kvm_vcpu *vcpu,
			       struct kvm_async_pf *work);
bool kvm_arch_can_inject_async_page_present(struct kvm_vcpu *vcpu);
extern bool kvm_find_async_pf_gfn(struct kvm_vcpu *vcpu, gfn_t gfn);

void kvm_complete_insn_gp(struct kvm_vcpu *vcpu, int err);

int kvm_is_in_guest(void);

void kvm_pmu_init(struct kvm_vcpu *vcpu);
void kvm_pmu_destroy(struct kvm_vcpu *vcpu);
void kvm_pmu_reset(struct kvm_vcpu *vcpu);
void kvm_pmu_cpuid_update(struct kvm_vcpu *vcpu);
bool kvm_pmu_msr(struct kvm_vcpu *vcpu, u32 msr);
int kvm_pmu_get_msr(struct kvm_vcpu *vcpu, u32 msr, u64 *data);
int kvm_pmu_set_msr(struct kvm_vcpu *vcpu, u32 msr, u64 data);
int kvm_pmu_read_pmc(struct kvm_vcpu *vcpu, unsigned pmc, u64 *data);
void kvm_handle_pmu_event(struct kvm_vcpu *vcpu);
void kvm_deliver_pmi(struct kvm_vcpu *vcpu);

#endif /* _ASM_X86_KVM_HOST_H */
