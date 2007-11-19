#/*
 * Kernel-based Virtual Machine driver for Linux
 *
 * This header defines architecture specific interfaces, x86 version
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef KVM_X86_H
#define KVM_X86_H

#include "kvm.h"

#include <linux/types.h>
#include <linux/mm.h>

#include <linux/kvm.h>
#include <linux/kvm_para.h>

#define CR3_PAE_RESERVED_BITS ((X86_CR3_PWT | X86_CR3_PCD) - 1)
#define CR3_NONPAE_RESERVED_BITS ((PAGE_SIZE-1) & ~(X86_CR3_PWT | X86_CR3_PCD))
#define CR3_L_MODE_RESERVED_BITS (CR3_NONPAE_RESERVED_BITS|0xFFFFFF0000000000ULL)

#define KVM_GUEST_CR0_MASK \
	(X86_CR0_PG | X86_CR0_PE | X86_CR0_WP | X86_CR0_NE \
	 | X86_CR0_NW | X86_CR0_CD)
#define KVM_VM_CR0_ALWAYS_ON \
	(X86_CR0_PG | X86_CR0_PE | X86_CR0_WP | X86_CR0_NE | X86_CR0_TS \
	 | X86_CR0_MP)
#define KVM_GUEST_CR4_MASK \
	(X86_CR4_VME | X86_CR4_PSE | X86_CR4_PAE | X86_CR4_PGE | X86_CR4_VMXE)
#define KVM_PMODE_VM_CR4_ALWAYS_ON (X86_CR4_PAE | X86_CR4_VMXE)
#define KVM_RMODE_VM_CR4_ALWAYS_ON (X86_CR4_VME | X86_CR4_PAE | X86_CR4_VMXE)

#define INVALID_PAGE (~(hpa_t)0)
#define UNMAPPED_GVA (~(gpa_t)0)

#define DE_VECTOR 0
#define UD_VECTOR 6
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

extern spinlock_t kvm_lock;
extern struct list_head vm_list;

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

#include "x86_emulate.h"

struct kvm_vcpu {
	KVM_VCPU_COMM;
	u64 host_tsc;
	int interrupt_window_open;
	unsigned long irq_summary; /* bit vector: 1 per word in irq_pending */
	DECLARE_BITMAP(irq_pending, KVM_NR_INTERRUPTS);
	unsigned long regs[NR_VCPU_REGS]; /* for rsp: vcpu_load_rsp_rip() */
	unsigned long rip;      /* needs vcpu_load_rsp_rip() */

	unsigned long cr0;
	unsigned long cr2;
	unsigned long cr3;
	unsigned long cr4;
	unsigned long cr8;
	u64 pdptrs[4]; /* pae */
	u64 shadow_efer;
	u64 apic_base;
	struct kvm_lapic *apic;    /* kernel irqchip context */
#define VCPU_MP_STATE_RUNNABLE          0
#define VCPU_MP_STATE_UNINITIALIZED     1
#define VCPU_MP_STATE_INIT_RECEIVED     2
#define VCPU_MP_STATE_SIPI_RECEIVED     3
#define VCPU_MP_STATE_HALTED            4
	int mp_state;
	int sipi_vector;
	u64 ia32_misc_enable_msr;

	struct kvm_mmu mmu;

	struct kvm_mmu_memory_cache mmu_pte_chain_cache;
	struct kvm_mmu_memory_cache mmu_rmap_desc_cache;
	struct kvm_mmu_memory_cache mmu_page_cache;
	struct kvm_mmu_memory_cache mmu_page_header_cache;

	gfn_t last_pt_write_gfn;
	int   last_pt_write_count;
	u64  *last_pte_updated;


	struct i387_fxsave_struct host_fx_image;
	struct i387_fxsave_struct guest_fx_image;

	gva_t mmio_fault_cr2;
	struct kvm_pio_request pio;
	void *pio_data;

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
	int halt_request; /* real mode on Intel only */

	int cpuid_nent;
	struct kvm_cpuid_entry cpuid_entries[KVM_MAX_CPUID_ENTRIES];

	/* emulate context */

	struct x86_emulate_ctxt emulate_ctxt;
};

struct kvm_x86_ops {
	int (*cpu_has_kvm_support)(void);          /* __init */
	int (*disabled_by_bios)(void);             /* __init */
	void (*hardware_enable)(void *dummy);      /* __init */
	void (*hardware_disable)(void *dummy);
	void (*check_processor_compatibility)(void *rtn);
	int (*hardware_setup)(void);               /* __init */
	void (*hardware_unsetup)(void);            /* __exit */

	/* Create, but do not attach this VCPU */
	struct kvm_vcpu *(*vcpu_create)(struct kvm *kvm, unsigned id);
	void (*vcpu_free)(struct kvm_vcpu *vcpu);
	int (*vcpu_reset)(struct kvm_vcpu *vcpu);

	void (*prepare_guest_switch)(struct kvm_vcpu *vcpu);
	void (*vcpu_load)(struct kvm_vcpu *vcpu, int cpu);
	void (*vcpu_put)(struct kvm_vcpu *vcpu);
	void (*vcpu_decache)(struct kvm_vcpu *vcpu);

	int (*set_guest_debug)(struct kvm_vcpu *vcpu,
			       struct kvm_debug_guest *dbg);
	void (*guest_debug_pre)(struct kvm_vcpu *vcpu);
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

	void (*tlb_flush)(struct kvm_vcpu *vcpu);
	void (*inject_page_fault)(struct kvm_vcpu *vcpu,
				  unsigned long addr, u32 err_code);

	void (*inject_gp)(struct kvm_vcpu *vcpu, unsigned err_code);

	void (*run)(struct kvm_vcpu *vcpu, struct kvm_run *run);
	int (*handle_exit)(struct kvm_run *run, struct kvm_vcpu *vcpu);
	void (*skip_emulated_instruction)(struct kvm_vcpu *vcpu);
	void (*patch_hypercall)(struct kvm_vcpu *vcpu,
				unsigned char *hypercall_addr);
	int (*get_irq)(struct kvm_vcpu *vcpu);
	void (*set_irq)(struct kvm_vcpu *vcpu, int vec);
	void (*inject_pending_irq)(struct kvm_vcpu *vcpu);
	void (*inject_pending_vectors)(struct kvm_vcpu *vcpu,
				       struct kvm_run *run);

	int (*set_tss_addr)(struct kvm *kvm, unsigned int addr);
};

extern struct kvm_x86_ops *kvm_x86_ops;

int kvm_mmu_page_fault(struct kvm_vcpu *vcpu, gva_t gva, u32 error_code);

static inline void kvm_mmu_free_some_pages(struct kvm_vcpu *vcpu)
{
	if (unlikely(vcpu->kvm->n_free_mmu_pages < KVM_MIN_FREE_MMU_PAGES))
		__kvm_mmu_free_some_pages(vcpu);
}

static inline int kvm_mmu_reload(struct kvm_vcpu *vcpu)
{
	if (likely(vcpu->mmu.root_hpa != INVALID_PAGE))
		return 0;

	return kvm_mmu_load(vcpu);
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
	return vcpu->cr4 & X86_CR4_PAE;
}

static inline int is_pse(struct kvm_vcpu *vcpu)
{
	return vcpu->cr4 & X86_CR4_PSE;
}

static inline int is_paging(struct kvm_vcpu *vcpu)
{
	return vcpu->cr0 & X86_CR0_PG;
}

int load_pdptrs(struct kvm_vcpu *vcpu, unsigned long cr3);
int complete_pio(struct kvm_vcpu *vcpu);

static inline struct kvm_mmu_page *page_header(hpa_t shadow_page)
{
	struct page *page = pfn_to_page(shadow_page >> PAGE_SHIFT);

	return (struct kvm_mmu_page *)page_private(page);
}

static inline u16 read_fs(void)
{
	u16 seg;
	asm("mov %%fs, %0" : "=g"(seg));
	return seg;
}

static inline u16 read_gs(void)
{
	u16 seg;
	asm("mov %%gs, %0" : "=g"(seg));
	return seg;
}

static inline u16 read_ldt(void)
{
	u16 ldt;
	asm("sldt %0" : "=g"(ldt));
	return ldt;
}

static inline void load_fs(u16 sel)
{
	asm("mov %0, %%fs" : : "rm"(sel));
}

static inline void load_gs(u16 sel)
{
	asm("mov %0, %%gs" : : "rm"(sel));
}

#ifndef load_ldt
static inline void load_ldt(u16 sel)
{
	asm("lldt %0" : : "rm"(sel));
}
#endif

static inline void get_idt(struct descriptor_table *table)
{
	asm("sidt %0" : "=m"(*table));
}

static inline void get_gdt(struct descriptor_table *table)
{
	asm("sgdt %0" : "=m"(*table));
}

static inline unsigned long read_tr_base(void)
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

static inline void fx_save(struct i387_fxsave_struct *image)
{
	asm("fxsave (%0)":: "r" (image));
}

static inline void fx_restore(struct i387_fxsave_struct *image)
{
	asm("fxrstor (%0)":: "r" (image));
}

static inline void fpu_init(void)
{
	asm("finit");
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
