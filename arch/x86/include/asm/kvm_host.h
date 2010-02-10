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

#include <linux/kvm.h>
#include <linux/kvm_para.h>
#include <linux/kvm_types.h>

#include <asm/pvclock-abi.h>
#include <asm/desc.h>
#include <asm/mtrr.h>
#include <asm/msr-index.h>

#define KVM_MAX_VCPUS 64
#define KVM_MEMORY_SLOTS 32
/* memory slots that does not exposed to userspace */
#define KVM_PRIVATE_MEM_SLOTS 4

#define KVM_PIO_PAGE_OFFSET 1
#define KVM_COALESCED_MMIO_PAGE_OFFSET 2

#define CR3_PAE_RESERVED_BITS ((X86_CR3_PWT | X86_CR3_PCD) - 1)
#define CR3_NONPAE_RESERVED_BITS ((PAGE_SIZE-1) & ~(X86_CR3_PWT | X86_CR3_PCD))
#define CR3_L_MODE_RESERVED_BITS (CR3_NONPAE_RESERVED_BITS |	\
				  0xFFFFFF0000000000ULL)

#define INVALID_PAGE (~(hpa_t)0)
#define UNMAPPED_GVA (~(gpa_t)0)

/* KVM Hugepage definitions for x86 */
#define KVM_NR_PAGE_SIZES	3
#define KVM_HPAGE_SHIFT(x)	(PAGE_SHIFT + (((x) - 1) * 9))
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

#define KVM_ALIAS_SLOTS 4

#define KVM_PERMILLE_MMU_PAGES 20
#define KVM_MIN_ALLOC_MMU_PAGES 64
#define KVM_MMU_HASH_SHIFT 10
#define KVM_NUM_MMU_PAGES (1 << KVM_MMU_HASH_SHIFT)
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 40
#define KVM_NR_FIXED_MTRR_REGION 88
#define KVM_NR_VAR_MTRR 8

extern spinlock_t kvm_lock;
extern struct list_head vm_list;

struct kvm_vcpu;
struct kvm;

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

#define NR_PTE_CHAIN_ENTRIES 5

struct kvm_pte_chain {
	u64 *parent_ptes[NR_PTE_CHAIN_ENTRIES];
	struct hlist_node link;
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
		unsigned glevels:4;
		unsigned level:4;
		unsigned quadrant:2;
		unsigned pad_for_nice_hex_output:6;
		unsigned direct:1;
		unsigned access:3;
		unsigned invalid:1;
		unsigned cr4_pge:1;
		unsigned nxe:1;
	};
};

struct kvm_mmu_page {
	struct list_head link;
	struct hlist_node hash_link;

	struct list_head oos_link;

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
	DECLARE_BITMAP(slot_bitmap, KVM_MEMORY_SLOTS + KVM_PRIVATE_MEM_SLOTS);
	int multimapped;         /* More than one parent_pte? */
	int root_count;          /* Currently serving as active root */
	bool unsync;
	unsigned int unsync_children;
	union {
		u64 *parent_pte;               /* !multimapped */
		struct hlist_head parent_ptes; /* multimapped, kvm_pte_chain */
	};
	DECLARE_BITMAP(unsync_child_bitmap, 512);
};

struct kvm_pv_mmu_op_buffer {
	void *ptr;
	unsigned len;
	unsigned processed;
	char buf[512] __aligned(sizeof(long));
};

struct kvm_pio_request {
	unsigned long count;
	int cur_count;
	gva_t guest_gva;
	int in;
	int port;
	int size;
	int string;
	int down;
	int rep;
};

/*
 * x86 supports 3 paging modes (4-level 64-bit, 3-level 64-bit, and 2-level
 * 32-bit).  The kvm_mmu structure abstracts the details of the current mmu
 * mode.
 */
struct kvm_mmu {
	void (*new_cr3)(struct kvm_vcpu *vcpu);
	int (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err);
	void (*free)(struct kvm_vcpu *vcpu);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva, u32 access,
			    u32 *error);
	void (*prefetch_page)(struct kvm_vcpu *vcpu,
			      struct kvm_mmu_page *page);
	int (*sync_page)(struct kvm_vcpu *vcpu,
			 struct kvm_mmu_page *sp);
	void (*invlpg)(struct kvm_vcpu *vcpu, gva_t gva);
	hpa_t root_hpa;
	int root_level;
	int shadow_root_level;
	union kvm_mmu_page_role base_role;

	u64 *pae_root;
	u64 rsvd_bits_mask[2][4];
};

struct kvm_vcpu_arch {
	u64 host_tsc;
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
	u64 pdptrs[4]; /* pae */
	u64 efer;
	u64 apic_base;
	struct kvm_lapic *apic;    /* kernel irqchip context */
	int32_t apic_arb_prio;
	int mp_state;
	int sipi_vector;
	u64 ia32_misc_enable_msr;
	bool tpr_access_reporting;

	struct kvm_mmu mmu;
	/* only needed in kvm_pv_mmu_op() path, but it's hot so
	 * put it here to avoid allocation */
	struct kvm_pv_mmu_op_buffer mmu_op_buffer;

	struct kvm_mmu_memory_cache mmu_pte_chain_cache;
	struct kvm_mmu_memory_cache mmu_rmap_desc_cache;
	struct kvm_mmu_memory_cache mmu_page_cache;
	struct kvm_mmu_memory_cache mmu_page_header_cache;

	gfn_t last_pt_write_gfn;
	int   last_pt_write_count;
	u64  *last_pte_updated;
	gfn_t last_pte_gfn;

	struct {
		gfn_t gfn;	/* presumed gfn during guest pte update */
		pfn_t pfn;	/* pfn corresponding to that gfn */
		unsigned long mmu_seq;
	} update_pte;

	struct i387_fxsave_struct host_fx_image;
	struct i387_fxsave_struct guest_fx_image;

	gva_t mmio_fault_cr2;
	struct kvm_pio_request pio;
	void *pio_data;

	u8 event_exit_inst_len;

	struct kvm_queued_exception {
		bool pending;
		bool has_error_code;
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

	gpa_t time;
	struct pvclock_vcpu_time_info hv_clock;
	unsigned int hv_clock_tsc_khz;
	unsigned int time_offset;
	struct page *time_page;

	bool nmi_pending;
	bool nmi_injected;

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

	/* used for guest single stepping over the given code position */
	u16 singlestep_cs;
	unsigned long singlestep_rip;
	/* fields used by HYPER-V emulation */
	u64 hv_vapic;
};

struct kvm_mem_alias {
	gfn_t base_gfn;
	unsigned long npages;
	gfn_t target_gfn;
#define KVM_ALIAS_INVALID     1UL
	unsigned long flags;
};

#define KVM_ARCH_HAS_UNALIAS_INSTANTIATION

struct kvm_mem_aliases {
	struct kvm_mem_alias aliases[KVM_ALIAS_SLOTS];
	int naliases;
};

struct kvm_arch {
	struct kvm_mem_aliases *aliases;

	unsigned int n_free_mmu_pages;
	unsigned int n_requested_mmu_pages;
	unsigned int n_alloc_mmu_pages;
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
	u64 vm_init_tsc;
	s64 kvmclock_offset;

	struct kvm_xen_hvm_config xen_hvm_config;

	/* fields used by HYPER-V emulation */
	u64 hv_guest_os_id;
	u64 hv_hypercall;
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

struct descriptor_table {
	u16 limit;
	unsigned long base;
} __attribute__((packed));

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
	void (*decache_cr4_guest_bits)(struct kvm_vcpu *vcpu);
	void (*set_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	void (*set_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);
	void (*set_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
	void (*set_efer)(struct kvm_vcpu *vcpu, u64 efer);
	void (*get_idt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*set_idt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*get_gdt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*set_gdt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	int (*get_dr)(struct kvm_vcpu *vcpu, int dr, unsigned long *dest);
	int (*set_dr)(struct kvm_vcpu *vcpu, int dr, unsigned long value);
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
				bool has_error_code, u32 error_code);
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

	const struct trace_print_flags *exit_reasons_str;
};

extern struct kvm_x86_ops *kvm_x86_ops;

int kvm_mmu_module_init(void);
void kvm_mmu_module_exit(void);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu);
int kvm_mmu_create(struct kvm_vcpu *vcpu);
int kvm_mmu_setup(struct kvm_vcpu *vcpu);
void kvm_mmu_set_nonpresent_ptes(u64 trap_pte, u64 notrap_pte);
void kvm_mmu_set_base_ptes(u64 base_pte);
void kvm_mmu_set_mask_ptes(u64 user_mask, u64 accessed_mask,
		u64 dirty_mask, u64 nx_mask, u64 x_mask);

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu);
void kvm_mmu_slot_remove_write_access(struct kvm *kvm, int slot);
void kvm_mmu_zap_all(struct kvm *kvm);
unsigned int kvm_mmu_calculate_mmu_pages(struct kvm *kvm);
void kvm_mmu_change_mmu_pages(struct kvm *kvm, unsigned int kvm_nr_mmu_pages);

int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3);

int emulator_write_phys(struct kvm_vcpu *vcpu, gpa_t gpa,
			  const void *val, int bytes);
int kvm_pv_mmu_op(struct kvm_vcpu *vcpu, unsigned long bytes,
		  gpa_t addr, unsigned long *ret);
u8 kvm_get_guest_memory_type(struct kvm_vcpu *vcpu, gfn_t gfn);

extern bool tdp_enabled;

enum emulation_result {
	EMULATE_DONE,       /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with mmio request */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

#define EMULTYPE_NO_DECODE	    (1 << 0)
#define EMULTYPE_TRAP_UD	    (1 << 1)
#define EMULTYPE_SKIP		    (1 << 2)
int emulate_instruction(struct kvm_vcpu *vcpu,
			unsigned long cr2, u16 error_code, int emulation_type);
void kvm_report_emulation_failure(struct kvm_vcpu *cvpu, const char *context);
void realmode_lgdt(struct kvm_vcpu *vcpu, u16 size, unsigned long address);
void realmode_lidt(struct kvm_vcpu *vcpu, u16 size, unsigned long address);
void realmode_lmsw(struct kvm_vcpu *vcpu, unsigned long msw,
		   unsigned long *rflags);

unsigned long realmode_get_cr(struct kvm_vcpu *vcpu, int cr);
void realmode_set_cr(struct kvm_vcpu *vcpu, int cr, unsigned long value,
		     unsigned long *rflags);
void kvm_enable_efer_bits(u64);
int kvm_get_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 *data);
int kvm_set_msr(struct kvm_vcpu *vcpu, u32 msr_index, u64 data);

struct x86_emulate_ctxt;

int kvm_emulate_pio(struct kvm_vcpu *vcpu, int in,
		     int size, unsigned port);
int kvm_emulate_pio_string(struct kvm_vcpu *vcpu, int in,
			   int size, unsigned long count, int down,
			    gva_t address, int rep, unsigned port);
void kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int kvm_emulate_halt(struct kvm_vcpu *vcpu);
int emulate_invlpg(struct kvm_vcpu *vcpu, gva_t address);
int emulate_clts(struct kvm_vcpu *vcpu);
int emulator_get_dr(struct x86_emulate_ctxt *ctxt, int dr,
		    unsigned long *dest);
int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr,
		    unsigned long value);

void kvm_get_segment(struct kvm_vcpu *vcpu, struct kvm_segment *var, int seg);
int kvm_load_segment_descriptor(struct kvm_vcpu *vcpu, u16 selector,
				int type_bits, int seg);

int kvm_task_switch(struct kvm_vcpu *vcpu, u16 tss_selector, int reason);

void kvm_set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void kvm_set_cr3(struct kvm_vcpu *vcpu, unsigned long cr3);
void kvm_set_cr4(struct kvm_vcpu *vcpu, unsigned long cr4);
void kvm_set_cr8(struct kvm_vcpu *vcpu, unsigned long cr8);
unsigned long kvm_get_cr8(struct kvm_vcpu *vcpu);
void kvm_lmsw(struct kvm_vcpu *vcpu, unsigned long msw);
void kvm_get_cs_db_l_bits(struct kvm_vcpu *vcpu, int *db, int *l);

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data);

unsigned long kvm_get_rflags(struct kvm_vcpu *vcpu);
void kvm_set_rflags(struct kvm_vcpu *vcpu, unsigned long rflags);

void kvm_queue_exception(struct kvm_vcpu *vcpu, unsigned nr);
void kvm_queue_exception_e(struct kvm_vcpu *vcpu, unsigned nr, u32 error_code);
void kvm_inject_page_fault(struct kvm_vcpu *vcpu, unsigned long cr2,
			   u32 error_code);
bool kvm_require_cpl(struct kvm_vcpu *vcpu, int required_cpl);

int kvm_pic_set_irq(void *opaque, int irq, int level);

void kvm_inject_nmi(struct kvm_vcpu *vcpu);

void fx_init(struct kvm_vcpu *vcpu);

int emulator_write_emulated(unsigned long addr,
			    const void *val,
			    unsigned int bytes,
			    struct kvm_vcpu *vcpu);

unsigned long segment_base(u16 selector);

void kvm_mmu_flush_tlb(struct kvm_vcpu *vcpu);
void kvm_mmu_pte_write(struct kvm_vcpu *vcpu, gpa_t gpa,
		       const u8 *new, int bytes,
		       bool guest_initiated);
int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva);
void __kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu);
int kvm_mmu_load(struct kvm_vcpu *vcpu);
void kvm_mmu_unload(struct kvm_vcpu *vcpu);
void kvm_mmu_sync_roots(struct kvm_vcpu *vcpu);
gpa_t kvm_mmu_gva_to_gpa_read(struct kvm_vcpu *vcpu, gva_t gva, u32 *error);
gpa_t kvm_mmu_gva_to_gpa_fetch(struct kvm_vcpu *vcpu, gva_t gva, u32 *error);
gpa_t kvm_mmu_gva_to_gpa_write(struct kvm_vcpu *vcpu, gva_t gva, u32 *error);
gpa_t kvm_mmu_gva_to_gpa_system(struct kvm_vcpu *vcpu, gva_t gva, u32 *error);

int kvm_emulate_hypercall(struct kvm_vcpu *vcpu);

int kvm_fix_hypercall(struct kvm_vcpu *vcpu);

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t gva, u32 error_code);
void kvm_mmu_invlpg(struct kvm_vcpu *vcpu, gva_t gva);

void kvm_enable_tdp(void);
void kvm_disable_tdp(void);

int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3);
int complete_pio(struct kvm_vcpu *vcpu);
bool kvm_check_iopl(struct kvm_vcpu *vcpu);

struct kvm_memory_slot *gfn_to_memslot_unaliased(struct kvm *kvm, gfn_t gfn);

static inline struct kvm_mmu_page *page_header(hpa_t shadow_page)
{
	struct page *page = pfn_to_page(shadow_page >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

static inline u16 kvm_read_fs(void)
{
	u16 seg;
	asm("mov %%fs, %0" : "=g"(seg));
	return seg;
}

static inline u16 kvm_read_gs(void)
{
	u16 seg;
	asm("mov %%gs, %0" : "=g"(seg));
	return seg;
}

static inline u16 kvm_read_ldt(void)
{
	u16 ldt;
	asm("sldt %0" : "=g"(ldt));
	return ldt;
}

static inline void kvm_load_fs(u16 sel)
{
	asm("mov %0, %%fs" : : "rm"(sel));
}

static inline void kvm_load_gs(u16 sel)
{
	asm("mov %0, %%gs" : : "rm"(sel));
}

static inline void kvm_load_ldt(u16 sel)
{
	asm("lldt %0" : : "rm"(sel));
}

static inline void kvm_get_idt(struct descriptor_table *table)
{
	asm("sidt %0" : "=m"(*table));
}

static inline void kvm_get_gdt(struct descriptor_table *table)
{
	asm("sgdt %0" : "=m"(*table));
}

static inline unsigned long kvm_read_tr_base(void)
{
	u16 tr;
	asm("str %0" : "=g"(tr));
	return segment_base(tr);
}

#ifdef CONFIG_X86_64
static inline unsigned long read_msr(unsigned long msr)
{
	u64 value;

	rdmsrl(msr, value);
	return value;
}
#endif

static inline void kvm_fx_save(struct i387_fxsave_struct *image)
{
	asm("fxsave (%0)":: "r" (image));
}

static inline void kvm_fx_restore(struct i387_fxsave_struct *image)
{
	asm("fxrstor (%0)":: "r" (image));
}

static inline void kvm_fx_finit(void)
{
	asm("finit");
}

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

/*
 * Hardware virtualization extension instructions may fault if a
 * reboot turns off virtualization while processes are running.
 * Trap the fault and ignore the instruction if that happens.
 */
asmlinkage void kvm_handle_fault_on_reboot(void);

#define __kvm_handle_fault_on_reboot(insn) \
	"666: " insn "\n\t" \
	".pushsection .fixup, \"ax\" \n" \
	"667: \n\t" \
	__ASM_SIZE(push) " $666b \n\t"	      \
	"jmp kvm_handle_fault_on_reboot \n\t" \
	".popsection \n\t" \
	".pushsection __ex_table, \"a\" \n\t" \
	_ASM_PTR " 666b, 667b \n\t" \
	".popsection"

#define KVM_ARCH_WANT_MMU_NOTIFIER
int kvm_unmap_hva(struct kvm *kvm, unsigned long hva);
int kvm_age_hva(struct kvm *kvm, unsigned long hva);
void kvm_set_spte_hva(struct kvm *kvm, unsigned long hva, pte_t pte);
int cpuid_maxphyaddr(struct kvm_vcpu *vcpu);
int kvm_cpu_has_interrupt(struct kvm_vcpu *vcpu);
int kvm_arch_interrupt_allowed(struct kvm_vcpu *vcpu);
int kvm_cpu_get_interrupt(struct kvm_vcpu *v);

void kvm_define_shared_msr(unsigned index, u32 msr);
void kvm_set_shared_msr(unsigned index, u64 val, u64 mask);

#endif /* _ASM_X86_KVM_HOST_H */
