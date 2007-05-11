#ifndef __KVM_H
#define __KVM_H

/*
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include <linux/types.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/mm.h>

#include "vmx.h"
#include <linux/kvm.h>
#include <linux/kvm_para.h>

#define CR0_PE_MASK (1ULL << 0)
#define CR0_TS_MASK (1ULL << 3)
#define CR0_NE_MASK (1ULL << 5)
#define CR0_WP_MASK (1ULL << 16)
#define CR0_NW_MASK (1ULL << 29)
#define CR0_CD_MASK (1ULL << 30)
#define CR0_PG_MASK (1ULL << 31)

#define CR3_WPT_MASK (1ULL << 3)
#define CR3_PCD_MASK (1ULL << 4)

#define CR3_RESEVED_BITS 0x07ULL
#define CR3_L_MODE_RESEVED_BITS (~((1ULL << 40) - 1) | 0x0fe7ULL)
#define CR3_FLAGS_MASK ((1ULL << 5) - 1)

#define CR4_VME_MASK (1ULL << 0)
#define CR4_PSE_MASK (1ULL << 4)
#define CR4_PAE_MASK (1ULL << 5)
#define CR4_PGE_MASK (1ULL << 7)
#define CR4_VMXE_MASK (1ULL << 13)

#define KVM_GUEST_CR0_MASK \
	(CR0_PG_MASK | CR0_PE_MASK | CR0_WP_MASK | CR0_NE_MASK \
	 | CR0_NW_MASK | CR0_CD_MASK)
#define KVM_VM_CR0_ALWAYS_ON \
	(CR0_PG_MASK | CR0_PE_MASK | CR0_WP_MASK | CR0_NE_MASK)
#define KVM_GUEST_CR4_MASK \
	(CR4_PSE_MASK | CR4_PAE_MASK | CR4_PGE_MASK | CR4_VMXE_MASK | CR4_VME_MASK)
#define KVM_PMODE_VM_CR4_ALWAYS_ON (CR4_VMXE_MASK | CR4_PAE_MASK)
#define KVM_RMODE_VM_CR4_ALWAYS_ON (CR4_VMXE_MASK | CR4_PAE_MASK | CR4_VME_MASK)

#define INVALID_PAGE (~(hpa_t)0)
#define UNMAPPED_GVA (~(gpa_t)0)

#define KVM_MAX_VCPUS 1
#define KVM_ALIAS_SLOTS 4
#define KVM_MEMORY_SLOTS 4
#define KVM_NUM_MMU_PAGES 256
#define KVM_MIN_FREE_MMU_PAGES 5
#define KVM_REFILL_PAGES 25
#define KVM_MAX_CPUID_ENTRIES 40

#define FX_IMAGE_SIZE 512
#define FX_IMAGE_ALIGN 16
#define FX_BUF_SIZE (2 * FX_IMAGE_SIZE + FX_IMAGE_ALIGN)

#define DE_VECTOR 0
#define NM_VECTOR 7
#define DF_VECTOR 8
#define TS_VECTOR 10
#define NP_VECTOR 11
#define SS_VECTOR 12
#define GP_VECTOR 13
#define PF_VECTOR 14

#define SELECTOR_TI_MASK (1 << 2)
#define SELECTOR_RPL_MASK 0x03

#define IOPL_SHIFT 12

#define KVM_PIO_PAGE_OFFSET 1

/*
 * Address types:
 *
 *  gva - guest virtual address
 *  gpa - guest physical address
 *  gfn - guest frame number
 *  hva - host virtual address
 *  hpa - host physical address
 *  hfn - host frame number
 */

typedef unsigned long  gva_t;
typedef u64            gpa_t;
typedef unsigned long  gfn_t;

typedef unsigned long  hva_t;
typedef u64            hpa_t;
typedef unsigned long  hfn_t;

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
 *   bit   16 - "metaphysical" - gfn is not a real page (huge page/real mode)
 *   bits 17:18 - "access" - the user and writable bits of a huge page pde
 */
union kvm_mmu_page_role {
	unsigned word;
	struct {
		unsigned glevels : 4;
		unsigned level : 4;
		unsigned quadrant : 2;
		unsigned pad_for_nice_hex_output : 6;
		unsigned metaphysical : 1;
		unsigned hugepage_access : 2;
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

	hpa_t page_hpa;
	unsigned long slot_bitmap; /* One bit set per slot which has memory
				    * in this shadow page.
				    */
	int multimapped;         /* More than one parent_pte? */
	int root_count;          /* Currently serving as active root */
	union {
		u64 *parent_pte;               /* !multimapped */
		struct hlist_head parent_ptes; /* multimapped, kvm_pte_chain */
	};
};

struct vmcs {
	u32 revision_id;
	u32 abort;
	char data[0];
};

#define vmx_msr_entry kvm_msr_entry

struct kvm_vcpu;

/*
 * x86 supports 3 paging modes (4-level 64-bit, 3-level 64-bit, and 2-level
 * 32-bit).  The kvm_mmu structure abstracts the details of the current mmu
 * mode.
 */
struct kvm_mmu {
	void (*new_cr3)(struct kvm_vcpu *vcpu);
	int (*page_fault)(struct kvm_vcpu *vcpu, gva_t gva, u32 err);
	void (*free)(struct kvm_vcpu *vcpu);
	gpa_t (*gva_to_gpa)(struct kvm_vcpu *vcpu, gva_t gva);
	hpa_t root_hpa;
	int root_level;
	int shadow_root_level;

	u64 *pae_root;
};

#define KVM_NR_MEM_OBJS 20

struct kvm_mmu_memory_cache {
	int nobjs;
	void *objects[KVM_NR_MEM_OBJS];
};

/*
 * We don't want allocation failures within the mmu code, so we preallocate
 * enough memory for a single page fault in a cache.
 */
struct kvm_guest_debug {
	int enabled;
	unsigned long bp[4];
	int singlestep;
};

enum {
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
	NR_VCPU_REGS
};

enum {
	VCPU_SREG_CS,
	VCPU_SREG_DS,
	VCPU_SREG_ES,
	VCPU_SREG_FS,
	VCPU_SREG_GS,
	VCPU_SREG_SS,
	VCPU_SREG_TR,
	VCPU_SREG_LDTR,
};

struct kvm_pio_request {
	unsigned long count;
	int cur_count;
	struct page *guest_pages[2];
	unsigned guest_page_offset;
	int in;
	int size;
	int string;
	int down;
	int rep;
};

struct kvm_stat {
	u32 pf_fixed;
	u32 pf_guest;
	u32 tlb_flush;
	u32 invlpg;

	u32 exits;
	u32 io_exits;
	u32 mmio_exits;
	u32 signal_exits;
	u32 irq_window_exits;
	u32 halt_exits;
	u32 request_irq_exits;
	u32 irq_exits;
};

struct kvm_vcpu {
	struct kvm *kvm;
	union {
		struct vmcs *vmcs;
		struct vcpu_svm *svm;
	};
	struct mutex mutex;
	int   cpu;
	int   launched;
	u64 host_tsc;
	struct kvm_run *run;
	int interrupt_window_open;
	unsigned long irq_summary; /* bit vector: 1 per word in irq_pending */
#define NR_IRQ_WORDS KVM_IRQ_BITMAP_SIZE(unsigned long)
	unsigned long irq_pending[NR_IRQ_WORDS];
	unsigned long regs[NR_VCPU_REGS]; /* for rsp: vcpu_load_rsp_rip() */
	unsigned long rip;      /* needs vcpu_load_rsp_rip() */

	unsigned long cr0;
	unsigned long cr2;
	unsigned long cr3;
	gpa_t para_state_gpa;
	struct page *para_state_page;
	gpa_t hypercall_gpa;
	unsigned long cr4;
	unsigned long cr8;
	u64 pdptrs[4]; /* pae */
	u64 shadow_efer;
	u64 apic_base;
	u64 ia32_misc_enable_msr;
	int nmsrs;
	struct vmx_msr_entry *guest_msrs;
	struct vmx_msr_entry *host_msrs;

	struct list_head free_pages;
	struct kvm_mmu_page page_header_buf[KVM_NUM_MMU_PAGES];
	struct kvm_mmu mmu;

	struct kvm_mmu_memory_cache mmu_pte_chain_cache;
	struct kvm_mmu_memory_cache mmu_rmap_desc_cache;

	gfn_t last_pt_write_gfn;
	int   last_pt_write_count;

	struct kvm_guest_debug guest_debug;

	char fx_buf[FX_BUF_SIZE];
	char *host_fx_image;
	char *guest_fx_image;
	int fpu_active;

	int mmio_needed;
	int mmio_read_completed;
	int mmio_is_write;
	int mmio_size;
	unsigned char mmio_data[8];
	gpa_t mmio_phys_addr;
	gva_t mmio_fault_cr2;
	struct kvm_pio_request pio;
	void *pio_data;

	int sigset_active;
	sigset_t sigset;

	struct kvm_stat stat;

	struct {
		int active;
		u8 save_iopl;
		struct kvm_save_segment {
			u16 selector;
			unsigned long base;
			u32 limit;
			u32 ar;
		} tr, es, ds, fs, gs;
	} rmode;

	int cpuid_nent;
	struct kvm_cpuid_entry cpuid_entries[KVM_MAX_CPUID_ENTRIES];
};

struct kvm_mem_alias {
	gfn_t base_gfn;
	unsigned long npages;
	gfn_t target_gfn;
};

struct kvm_memory_slot {
	gfn_t base_gfn;
	unsigned long npages;
	unsigned long flags;
	struct page **phys_mem;
	unsigned long *dirty_bitmap;
};

struct kvm {
	spinlock_t lock; /* protects everything except vcpus */
	int naliases;
	struct kvm_mem_alias aliases[KVM_ALIAS_SLOTS];
	int nmemslots;
	struct kvm_memory_slot memslots[KVM_MEMORY_SLOTS];
	/*
	 * Hash table of struct kvm_mmu_page.
	 */
	struct list_head active_mmu_pages;
	int n_free_mmu_pages;
	struct hlist_head mmu_page_hash[KVM_NUM_MMU_PAGES];
	struct kvm_vcpu vcpus[KVM_MAX_VCPUS];
	int memory_config_version;
	int busy;
	unsigned long rmap_overflow;
	struct list_head vm_list;
	struct file *filp;
};

struct descriptor_table {
	u16 limit;
	unsigned long base;
} __attribute__((packed));

struct kvm_arch_ops {
	int (*cpu_has_kvm_support)(void);          /* __init */
	int (*disabled_by_bios)(void);             /* __init */
	void (*hardware_enable)(void *dummy);      /* __init */
	void (*hardware_disable)(void *dummy);
	int (*hardware_setup)(void);               /* __init */
	void (*hardware_unsetup)(void);            /* __exit */

	int (*vcpu_create)(struct kvm_vcpu *vcpu);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);

	void (*vcpu_load)(struct kvm_vcpu *vcpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);
	void (*vcpu_decache)(struct kvm_vcpu *vcpu);

	int (*set_guest_debug)(struct kvm_vcpu *vcpu,
			       struct kvm_debug_guest *dbg);
	int (*get_msr)(struct kvm_vcpu *vcpu, u32 msr_index, u64 *pdata);
	int (*set_msr)(struct kvm_vcpu *vcpu, u32 msr_index, u64 data);
	u64 (*get_segment_base)(struct kvm_vcpu *vcpu, int seg);
	void (*get_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	void (*set_segment)(struct kvm_vcpu *vcpu,
			    struct kvm_segment *var, int seg);
	void (*get_cs_db_l_bits)(struct kvm_vcpu *vcpu, int *db, int *l);
	void (*decache_cr4_guest_bits)(struct kvm_vcpu *vcpu);
	void (*set_cr0)(struct kvm_vcpu *vcpu, unsigned long cr0);
	void (*set_cr3)(struct kvm_vcpu *vcpu, unsigned long cr3);
	void (*set_cr4)(struct kvm_vcpu *vcpu, unsigned long cr4);
	void (*set_efer)(struct kvm_vcpu *vcpu, u64 efer);
	void (*get_idt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*set_idt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*get_gdt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	void (*set_gdt)(struct kvm_vcpu *vcpu, struct descriptor_table *dt);
	unsigned long (*get_dr)(struct kvm_vcpu *vcpu, int dr);
	void (*set_dr)(struct kvm_vcpu *vcpu, int dr, unsigned long value,
		       int *exception);
	void (*cache_regs)(struct kvm_vcpu *vcpu);
	void (*decache_regs)(struct kvm_vcpu *vcpu);
	unsigned long (*get_rflags)(struct kvm_vcpu *vcpu);
	void (*set_rflags)(struct kvm_vcpu *vcpu, unsigned long rflags);

	void (*invlpg)(struct kvm_vcpu *vcpu, gva_t addr);
	void (*tlb_flush)(struct kvm_vcpu *vcpu);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  unsigned long addr, u32 err_code);

	void (*inject_gp)(struct kvm_vcpu *vcpu, unsigned err_code);

	int (*run)(struct kvm_vcpu *vcpu, struct kvm_run *run);
	int (*vcpu_setup)(struct kvm_vcpu *vcpu);
	void (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
};

extern struct kvm_arch_ops *kvm_arch_ops;

#define kvm_printf(kvm, fmt ...) printk(KERN_DEBUG fmt)
#define vcpu_printf(vcpu, fmt...) kvm_printf(vcpu->kvm, fmt)

int kvm_init_arch(struct kvm_arch_ops *ops, struct module *module);
void kvm_exit_arch(void);

int kvm_mmu_module_init(void);
void kvm_mmu_module_exit(void);

void kvm_mmu_destroy(struct kvm_vcpu *vcpu);
int kvm_mmu_create(struct kvm_vcpu *vcpu);
int kvm_mmu_setup(struct kvm_vcpu *vcpu);

int kvm_mmu_reset_context(struct kvm_vcpu *vcpu);
void kvm_mmu_slot_remove_write_access(struct kvm_vcpu *vcpu, int slot);
void kvm_mmu_zap_all(struct kvm_vcpu *vcpu);

hpa_t gpa_to_hpa(struct kvm_vcpu *vcpu, gpa_t gpa);
#define HPA_MSB ((sizeof(hpa_t) * 8) - 1)
#define HPA_ERR_MASK ((hpa_t)1 << HPA_MSB)
static inline int is_error_hpa(hpa_t hpa) { return hpa >> HPA_MSB; }
hpa_t gva_to_hpa(struct kvm_vcpu *vcpu, gva_t gva);
struct page *gva_to_page(struct kvm_vcpu *vcpu, gva_t gva);

void kvm_emulator_want_group7_invlpg(void);

extern hpa_t bad_page_address;

struct page *gfn_to_page(struct kvm *kvm, gfn_t gfn);
struct kvm_memory_slot *gfn_to_memslot(struct kvm *kvm, gfn_t gfn);
void mark_page_dirty(struct kvm *kvm, gfn_t gfn);

enum emulation_result {
	EMULATE_DONE,       /* no further processing */
	EMULATE_DO_MMIO,      /* kvm_run filled with mmio request */
	EMULATE_FAIL,         /* can't emulate this instruction */
};

int emulate_instruction(struct kvm_vcpu *vcpu, struct kvm_run *run,
			unsigned long cr2, u16 error_code);
void realmode_lgdt(struct kvm_vcpu *vcpu, u16 size, unsigned long address);
void realmode_lidt(struct kvm_vcpu *vcpu, u16 size, unsigned long address);
void realmode_lmsw(struct kvm_vcpu *vcpu, unsigned long msw,
		   unsigned long *rflags);

unsigned long realmode_get_cr(struct kvm_vcpu *vcpu, int cr);
void realmode_set_cr(struct kvm_vcpu *vcpu, int cr, unsigned long value,
		     unsigned long *rflags);

struct x86_emulate_ctxt;

int kvm_setup_pio(struct kvm_vcpu *vcpu, struct kvm_run *run, int in,
		  int size, unsigned long count, int string, int down,
		  gva_t address, int rep, unsigned port);
void kvm_emulate_cpuid(struct kvm_vcpu *vcpu);
int emulate_invlpg(struct kvm_vcpu *vcpu, gva_t address);
int emulate_clts(struct kvm_vcpu *vcpu);
int emulator_get_dr(struct x86_emulate_ctxt* ctxt, int dr,
		    unsigned long *dest);
int emulator_set_dr(struct x86_emulate_ctxt *ctxt, int dr,
		    unsigned long value);

void set_cr0(struct kvm_vcpu *vcpu, unsigned long cr0);
void set_cr3(struct kvm_vcpu *vcpu, unsigned long cr0);
void set_cr4(struct kvm_vcpu *vcpu, unsigned long cr0);
void set_cr8(struct kvm_vcpu *vcpu, unsigned long cr0);
void lmsw(struct kvm_vcpu *vcpu, unsigned long msw);

int kvm_get_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 *pdata);
int kvm_set_msr_common(struct kvm_vcpu *vcpu, u32 msr, u64 data);

void fx_init(struct kvm_vcpu *vcpu);

void load_msrs(struct vmx_msr_entry *e, int n);
void save_msrs(struct vmx_msr_entry *e, int n);
void kvm_resched(struct kvm_vcpu *vcpu);

int kvm_read_guest(struct kvm_vcpu *vcpu,
	       gva_t addr,
	       unsigned long size,
	       void *dest);

int kvm_write_guest(struct kvm_vcpu *vcpu,
		gva_t addr,
		unsigned long size,
		void *data);

unsigned long segment_base(u16 selector);

void kvm_mmu_pre_write(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes);
void kvm_mmu_post_write(struct kvm_vcpu *vcpu, gpa_t gpa, int bytes);
int kvm_mmu_unprotect_page_virt(struct kvm_vcpu *vcpu, gva_t gva);
void kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu);

int kvm_hypercall(struct kvm_vcpu *vcpu, struct kvm_run *run);

static inline int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t gva,
				     u32 error_code)
{
	if (unlikely(vcpu->kvm->n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES))
		kvm_mmu_free_some_pages(vcpu);
	return vcpu->mmu.page_fault(vcpu, gva, error_code);
}

static inline int is_long_mode(struct kvm_vcpu *vcpu)
{
#ifdef CONFIG_X86_64
	return vcpu->shadow_efer & EFER_LME;
#else
	return 0;
#endif
}

static inline int is_pae(struct kvm_vcpu *vcpu)
{
	return vcpu->cr4 & CR4_PAE_MASK;
}

static inline int is_pse(struct kvm_vcpu *vcpu)
{
	return vcpu->cr4 & CR4_PSE_MASK;
}

static inline int is_paging(struct kvm_vcpu *vcpu)
{
	return vcpu->cr0 & CR0_PG_MASK;
}

static inline int memslot_id(struct kvm *kvm, struct kvm_memory_slot *slot)
{
	return slot - kvm->memslots;
}

static inline struct kvm_mmu_page *page_header(hpa_t shadow_page)
{
	struct page *page = pfn_to_page(shadow_page >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

static inline u16 read_fs(void)
{
	u16 seg;
	asm ("mov %%fs, %0" : "=g"(seg));
	return seg;
}

static inline u16 read_gs(void)
{
	u16 seg;
	asm ("mov %%gs, %0" : "=g"(seg));
	return seg;
}

static inline u16 read_ldt(void)
{
	u16 ldt;
	asm ("sldt %0" : "=g"(ldt));
	return ldt;
}

static inline void load_fs(u16 sel)
{
	asm ("mov %0, %%fs" : : "rm"(sel));
}

static inline void load_gs(u16 sel)
{
	asm ("mov %0, %%gs" : : "rm"(sel));
}

#ifndef load_ldt
static inline void load_ldt(u16 sel)
{
	asm ("lldt %0" : : "rm"(sel));
}
#endif

static inline void get_idt(struct descriptor_table *table)
{
	asm ("sidt %0" : "=m"(*table));
}

static inline void get_gdt(struct descriptor_table *table)
{
	asm ("sgdt %0" : "=m"(*table));
}

static inline unsigned long read_tr_base(void)
{
	u16 tr;
	asm ("str %0" : "=g"(tr));
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

static inline void fx_save(void *image)
{
	asm ("fxsave (%0)":: "r" (image));
}

static inline void fx_restore(void *image)
{
	asm ("fxrstor (%0)":: "r" (image));
}

static inline void fpu_init(void)
{
	asm ("finit");
}

static inline u32 get_rdx_init_val(void)
{
	return 0x600; /* P6 family */
}

#define ASM_VMX_VMCLEAR_RAX       ".byte 0x66, 0x0f, 0xc7, 0x30"
#define ASM_VMX_VMLAUNCH          ".byte 0x0f, 0x01, 0xc2"
#define ASM_VMX_VMRESUME          ".byte 0x0f, 0x01, 0xc3"
#define ASM_VMX_VMPTRLD_RAX       ".byte 0x0f, 0xc7, 0x30"
#define ASM_VMX_VMREAD_RDX_RAX    ".byte 0x0f, 0x78, 0xd0"
#define ASM_VMX_VMWRITE_RAX_RDX   ".byte 0x0f, 0x79, 0xd0"
#define ASM_VMX_VMWRITE_RSP_RDX   ".byte 0x0f, 0x79, 0xd4"
#define ASM_VMX_VMXOFF            ".byte 0x0f, 0x01, 0xc4"
#define ASM_VMX_VMXON_RAX         ".byte 0xf3, 0x0f, 0xc7, 0x30"

#define MSR_IA32_TIME_STAMP_COUNTER		0x010

#define TSS_IOPB_BASE_OFFSET 0x66
#define TSS_BASE_SIZE 0x68
#define TSS_IOPB_SIZE (65536 / 8)
#define TSS_REDIRECTION_SIZE (256 / 8)
#define RMODE_TSS_SIZE (TSS_BASE_SIZE + TSS_REDIRECTION_SIZE + TSS_IOPB_SIZE + 1)

#endif
