/*
 * VMI specific paravirt-ops implementation
 *
 * Copyright (C) 2005, VMware, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to zach@vmware.com
 *
 */

#include <linux/module.h>
#include <linux/cpu.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <asm/vmi.h>
#include <asm/io.h>
#include <asm/fixmap.h>
#include <asm/apicdef.h>
#include <asm/apic.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/timer.h>
#include <asm/vmi_time.h>
#include <asm/kmap_types.h>
#include <asm/setup.h>

/* Convenient for calling VMI functions indirectly in the ROM */
typedef u32 __attribute__((regparm(1))) (VROMFUNC)(void);
typedef u64 __attribute__((regparm(2))) (VROMLONGFUNC)(int);

#define call_vrom_func(rom,func) \
   (((VROMFUNC *)(rom->func))())

#define call_vrom_long_func(rom,func,arg) \
   (((VROMLONGFUNC *)(rom->func)) (arg))

static struct vrom_header *vmi_rom;
static int disable_pge;
static int disable_pse;
static int disable_sep;
static int disable_tsc;
static int disable_mtrr;
static int disable_noidle;
static int disable_vmi_timer;

/* Cached VMI operations */
static struct {
	void (*cpuid)(void /* non-c */);
	void (*_set_ldt)(u32 selector);
	void (*set_tr)(u32 selector);
	void (*write_idt_entry)(struct desc_struct *, int, u32, u32);
	void (*write_gdt_entry)(struct desc_struct *, int, u32, u32);
	void (*write_ldt_entry)(struct desc_struct *, int, u32, u32);
	void (*set_kernel_stack)(u32 selector, u32 sp0);
	void (*allocate_page)(u32, u32, u32, u32, u32);
	void (*release_page)(u32, u32);
	void (*set_pte)(pte_t, pte_t *, unsigned);
	void (*update_pte)(pte_t *, unsigned);
	void (*set_linear_mapping)(int, void *, u32, u32);
	void (*_flush_tlb)(int);
	void (*set_initial_ap_state)(int, int);
	void (*halt)(void);
  	void (*set_lazy_mode)(int mode);
} vmi_ops;

/* Cached VMI operations */
struct vmi_timer_ops vmi_timer_ops;

/*
 * VMI patching routines.
 */
#define MNEM_CALL 0xe8
#define MNEM_JMP  0xe9
#define MNEM_RET  0xc3

#define IRQ_PATCH_INT_MASK 0
#define IRQ_PATCH_DISABLE  5

static inline void patch_offset(void *insnbuf,
				unsigned long ip, unsigned long dest)
{
        *(unsigned long *)(insnbuf+1) = dest-ip-5;
}

static unsigned patch_internal(int call, unsigned len, void *insnbuf,
			       unsigned long ip)
{
	u64 reloc;
	struct vmi_relocation_info *const rel = (struct vmi_relocation_info *)&reloc;
	reloc = call_vrom_long_func(vmi_rom, get_reloc,	call);
	switch(rel->type) {
		case VMI_RELOCATION_CALL_REL:
			BUG_ON(len < 5);
			*(char *)insnbuf = MNEM_CALL;
			patch_offset(insnbuf, ip, (unsigned long)rel->eip);
			return 5;

		case VMI_RELOCATION_JUMP_REL:
			BUG_ON(len < 5);
			*(char *)insnbuf = MNEM_JMP;
			patch_offset(insnbuf, ip, (unsigned long)rel->eip);
			return 5;

		case VMI_RELOCATION_NOP:
			/* obliterate the whole thing */
			return 0;

		case VMI_RELOCATION_NONE:
			/* leave native code in place */
			break;

		default:
			BUG();
	}
	return len;
}

/*
 * Apply patch if appropriate, return length of new instruction
 * sequence.  The callee does nop padding for us.
 */
static unsigned vmi_patch(u8 type, u16 clobbers, void *insns,
			  unsigned long ip, unsigned len)
{
	switch (type) {
		case PARAVIRT_PATCH(pv_irq_ops.irq_disable):
			return patch_internal(VMI_CALL_DisableInterrupts, len,
					      insns, ip);
		case PARAVIRT_PATCH(pv_irq_ops.irq_enable):
			return patch_internal(VMI_CALL_EnableInterrupts, len,
					      insns, ip);
		case PARAVIRT_PATCH(pv_irq_ops.restore_fl):
			return patch_internal(VMI_CALL_SetInterruptMask, len,
					      insns, ip);
		case PARAVIRT_PATCH(pv_irq_ops.save_fl):
			return patch_internal(VMI_CALL_GetInterruptMask, len,
					      insns, ip);
		case PARAVIRT_PATCH(pv_cpu_ops.iret):
			return patch_internal(VMI_CALL_IRET, len, insns, ip);
		case PARAVIRT_PATCH(pv_cpu_ops.irq_enable_sysexit):
			return patch_internal(VMI_CALL_SYSEXIT, len, insns, ip);
		default:
			break;
	}
	return len;
}

/* CPUID has non-C semantics, and paravirt-ops API doesn't match hardware ISA */
static void vmi_cpuid(unsigned int *ax, unsigned int *bx,
                               unsigned int *cx, unsigned int *dx)
{
	int override = 0;
	if (*ax == 1)
		override = 1;
        asm volatile ("call *%6"
                      : "=a" (*ax),
                        "=b" (*bx),
                        "=c" (*cx),
                        "=d" (*dx)
                      : "0" (*ax), "2" (*cx), "r" (vmi_ops.cpuid));
	if (override) {
		if (disable_pse)
			*dx &= ~X86_FEATURE_PSE;
		if (disable_pge)
			*dx &= ~X86_FEATURE_PGE;
		if (disable_sep)
			*dx &= ~X86_FEATURE_SEP;
		if (disable_tsc)
			*dx &= ~X86_FEATURE_TSC;
		if (disable_mtrr)
			*dx &= ~X86_FEATURE_MTRR;
	}
}

static inline void vmi_maybe_load_tls(struct desc_struct *gdt, int nr, struct desc_struct *new)
{
	if (gdt[nr].a != new->a || gdt[nr].b != new->b)
		write_gdt_entry(gdt, nr, new, 0);
}

static void vmi_load_tls(struct thread_struct *t, unsigned int cpu)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	vmi_maybe_load_tls(gdt, GDT_ENTRY_TLS_MIN + 0, &t->tls_array[0]);
	vmi_maybe_load_tls(gdt, GDT_ENTRY_TLS_MIN + 1, &t->tls_array[1]);
	vmi_maybe_load_tls(gdt, GDT_ENTRY_TLS_MIN + 2, &t->tls_array[2]);
}

static void vmi_set_ldt(const void *addr, unsigned entries)
{
	unsigned cpu = smp_processor_id();
	struct desc_struct desc;

	pack_descriptor(&desc, (unsigned long)addr,
			entries * sizeof(struct desc_struct) - 1,
			DESC_LDT, 0);
	write_gdt_entry(get_cpu_gdt_table(cpu), GDT_ENTRY_LDT, &desc, DESC_LDT);
	vmi_ops._set_ldt(entries ? GDT_ENTRY_LDT*sizeof(struct desc_struct) : 0);
}

static void vmi_set_tr(void)
{
	vmi_ops.set_tr(GDT_ENTRY_TSS*sizeof(struct desc_struct));
}

static void vmi_write_idt_entry(gate_desc *dt, int entry, const gate_desc *g)
{
	u32 *idt_entry = (u32 *)g;
	vmi_ops.write_idt_entry(dt, entry, idt_entry[0], idt_entry[1]);
}

static void vmi_write_gdt_entry(struct desc_struct *dt, int entry,
				const void *desc, int type)
{
	u32 *gdt_entry = (u32 *)desc;
	vmi_ops.write_gdt_entry(dt, entry, gdt_entry[0], gdt_entry[1]);
}

static void vmi_write_ldt_entry(struct desc_struct *dt, int entry,
				const void *desc)
{
	u32 *ldt_entry = (u32 *)desc;
	vmi_ops.write_ldt_entry(dt, entry, ldt_entry[0], ldt_entry[1]);
}

static void vmi_load_sp0(struct tss_struct *tss,
				   struct thread_struct *thread)
{
	tss->x86_tss.sp0 = thread->sp0;

	/* This can only happen when SEP is enabled, no need to test "SEP"arately */
	if (unlikely(tss->x86_tss.ss1 != thread->sysenter_cs)) {
		tss->x86_tss.ss1 = thread->sysenter_cs;
		wrmsr(MSR_IA32_SYSENTER_CS, thread->sysenter_cs, 0);
	}
	vmi_ops.set_kernel_stack(__KERNEL_DS, tss->x86_tss.sp0);
}

static void vmi_flush_tlb_user(void)
{
	vmi_ops._flush_tlb(VMI_FLUSH_TLB);
}

static void vmi_flush_tlb_kernel(void)
{
	vmi_ops._flush_tlb(VMI_FLUSH_TLB | VMI_FLUSH_GLOBAL);
}

/* Stub to do nothing at all; used for delays and unimplemented calls */
static void vmi_nop(void)
{
}

#ifdef CONFIG_HIGHPTE
static void *vmi_kmap_atomic_pte(struct page *page, enum km_type type)
{
	void *va = kmap_atomic(page, type);

	/*
	 * We disable highmem allocations for page tables so we should never
	 * see any calls to kmap_atomic_pte on a highmem page.
	 */

	BUG_ON(PageHighmem(page));

	return va;
}
#endif

static void vmi_allocate_pte(struct mm_struct *mm, unsigned long pfn)
{
	vmi_ops.allocate_page(pfn, VMI_PAGE_L1, 0, 0, 0);
}

static void vmi_allocate_pmd(struct mm_struct *mm, unsigned long pfn)
{
 	/*
	 * This call comes in very early, before mem_map is setup.
	 * It is called only for swapper_pg_dir, which already has
	 * data on it.
	 */
	vmi_ops.allocate_page(pfn, VMI_PAGE_L2, 0, 0, 0);
}

static void vmi_allocate_pmd_clone(unsigned long pfn, unsigned long clonepfn, unsigned long start, unsigned long count)
{
	vmi_ops.allocate_page(pfn, VMI_PAGE_L2 | VMI_PAGE_CLONE, clonepfn, start, count);
}

static void vmi_release_pte(unsigned long pfn)
{
	vmi_ops.release_page(pfn, VMI_PAGE_L1);
}

static void vmi_release_pmd(unsigned long pfn)
{
	vmi_ops.release_page(pfn, VMI_PAGE_L2);
}

/*
 * We use the pgd_free hook for releasing the pgd page:
 */
static void vmi_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	unsigned long pfn = __pa(pgd) >> PAGE_SHIFT;

	vmi_ops.release_page(pfn, VMI_PAGE_L2);
}

/*
 * Helper macros for MMU update flags.  We can defer updates until a flush
 * or page invalidation only if the update is to the current address space
 * (otherwise, there is no flush).  We must check against init_mm, since
 * this could be a kernel update, which usually passes init_mm, although
 * sometimes this check can be skipped if we know the particular function
 * is only called on user mode PTEs.  We could change the kernel to pass
 * current->active_mm here, but in particular, I was unsure if changing
 * mm/highmem.c to do this would still be correct on other architectures.
 */
#define is_current_as(mm, mustbeuser) ((mm) == current->active_mm ||    \
                                       (!mustbeuser && (mm) == &init_mm))
#define vmi_flags_addr(mm, addr, level, user)                           \
        ((level) | (is_current_as(mm, user) ?                           \
                (VMI_PAGE_CURRENT_AS | ((addr) & VMI_PAGE_VA_MASK)) : 0))
#define vmi_flags_addr_defer(mm, addr, level, user)                     \
        ((level) | (is_current_as(mm, user) ?                           \
                (VMI_PAGE_DEFER | VMI_PAGE_CURRENT_AS | ((addr) & VMI_PAGE_VA_MASK)) : 0))

static void vmi_update_pte(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	vmi_ops.update_pte(ptep, vmi_flags_addr(mm, addr, VMI_PAGE_PT, 0));
}

static void vmi_update_pte_defer(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	vmi_ops.update_pte(ptep, vmi_flags_addr_defer(mm, addr, VMI_PAGE_PT, 0));
}

static void vmi_set_pte(pte_t *ptep, pte_t pte)
{
	/* XXX because of set_pmd_pte, this can be called on PT or PD layers */
	vmi_ops.set_pte(pte, ptep, VMI_PAGE_PT);
}

static void vmi_set_pte_at(struct mm_struct *mm, unsigned long addr, pte_t *ptep, pte_t pte)
{
	vmi_ops.set_pte(pte, ptep, vmi_flags_addr(mm, addr, VMI_PAGE_PT, 0));
}

static void vmi_set_pmd(pmd_t *pmdp, pmd_t pmdval)
{
#ifdef CONFIG_X86_PAE
	const pte_t pte = { .pte = pmdval.pmd };
#else
	const pte_t pte = { pmdval.pud.pgd.pgd };
#endif
	vmi_ops.set_pte(pte, (pte_t *)pmdp, VMI_PAGE_PD);
}

#ifdef CONFIG_X86_PAE

static void vmi_set_pte_atomic(pte_t *ptep, pte_t pteval)
{
	/*
	 * XXX This is called from set_pmd_pte, but at both PT
	 * and PD layers so the VMI_PAGE_PT flag is wrong.  But
	 * it is only called for large page mapping changes,
	 * the Xen backend, doesn't support large pages, and the
	 * ESX backend doesn't depend on the flag.
	 */
	set_64bit((unsigned long long *)ptep,pte_val(pteval));
	vmi_ops.update_pte(ptep, VMI_PAGE_PT);
}

static void vmi_set_pud(pud_t *pudp, pud_t pudval)
{
	/* Um, eww */
	const pte_t pte = { .pte = pudval.pgd.pgd };
	vmi_ops.set_pte(pte, (pte_t *)pudp, VMI_PAGE_PDP);
}

static void vmi_pte_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	const pte_t pte = { .pte = 0 };
	vmi_ops.set_pte(pte, ptep, vmi_flags_addr(mm, addr, VMI_PAGE_PT, 0));
}

static void vmi_pmd_clear(pmd_t *pmd)
{
	const pte_t pte = { .pte = 0 };
	vmi_ops.set_pte(pte, (pte_t *)pmd, VMI_PAGE_PD);
}
#endif

#ifdef CONFIG_SMP
static void __devinit
vmi_startup_ipi_hook(int phys_apicid, unsigned long start_eip,
		     unsigned long start_esp)
{
	struct vmi_ap_state ap;

	/* Default everything to zero.  This is fine for most GPRs. */
	memset(&ap, 0, sizeof(struct vmi_ap_state));

	ap.gdtr_limit = GDT_SIZE - 1;
	ap.gdtr_base = (unsigned long) get_cpu_gdt_table(phys_apicid);

	ap.idtr_limit = IDT_ENTRIES * 8 - 1;
	ap.idtr_base = (unsigned long) idt_table;

	ap.ldtr = 0;

	ap.cs = __KERNEL_CS;
	ap.eip = (unsigned long) start_eip;
	ap.ss = __KERNEL_DS;
	ap.esp = (unsigned long) start_esp;

	ap.ds = __USER_DS;
	ap.es = __USER_DS;
	ap.fs = __KERNEL_PERCPU;
	ap.gs = __KERNEL_STACK_CANARY;

	ap.eflags = 0;

#ifdef CONFIG_X86_PAE
	/* efer should match BSP efer. */
	if (cpu_has_nx) {
		unsigned l, h;
		rdmsr(MSR_EFER, l, h);
		ap.efer = (unsigned long long) h << 32 | l;
	}
#endif

	ap.cr3 = __pa(swapper_pg_dir);
	/* Protected mode, paging, AM, WP, NE, MP. */
	ap.cr0 = 0x80050023;
	ap.cr4 = mmu_cr4_features;
	vmi_ops.set_initial_ap_state((u32)&ap, phys_apicid);
}
#endif

static void vmi_start_context_switch(struct task_struct *prev)
{
	paravirt_start_context_switch(prev);
	vmi_ops.set_lazy_mode(2);
}

static void vmi_end_context_switch(struct task_struct *next)
{
	vmi_ops.set_lazy_mode(0);
	paravirt_end_context_switch(next);
}

static void vmi_enter_lazy_mmu(void)
{
	paravirt_enter_lazy_mmu();
	vmi_ops.set_lazy_mode(1);
}

static void vmi_leave_lazy_mmu(void)
{
	vmi_ops.set_lazy_mode(0);
	paravirt_leave_lazy_mmu();
}

static inline int __init check_vmi_rom(struct vrom_header *rom)
{
	struct pci_header *pci;
	struct pnp_header *pnp;
	const char *manufacturer = "UNKNOWN";
	const char *product = "UNKNOWN";
	const char *license = "unspecified";

	if (rom->rom_signature != 0xaa55)
		return 0;
	if (rom->vrom_signature != VMI_SIGNATURE)
		return 0;
	if (rom->api_version_maj != VMI_API_REV_MAJOR ||
	    rom->api_version_min+1 < VMI_API_REV_MINOR+1) {
		printk(KERN_WARNING "VMI: Found mismatched rom version %d.%d\n",
				rom->api_version_maj,
				rom->api_version_min);
		return 0;
	}

	/*
	 * Relying on the VMI_SIGNATURE field is not 100% safe, so check
	 * the PCI header and device type to make sure this is really a
	 * VMI device.
	 */
	if (!rom->pci_header_offs) {
		printk(KERN_WARNING "VMI: ROM does not contain PCI header.\n");
		return 0;
	}

	pci = (struct pci_header *)((char *)rom+rom->pci_header_offs);
	if (pci->vendorID != PCI_VENDOR_ID_VMWARE ||
	    pci->deviceID != PCI_DEVICE_ID_VMWARE_VMI) {
		/* Allow it to run... anyways, but warn */
		printk(KERN_WARNING "VMI: ROM from unknown manufacturer\n");
	}

	if (rom->pnp_header_offs) {
		pnp = (struct pnp_header *)((char *)rom+rom->pnp_header_offs);
		if (pnp->manufacturer_offset)
			manufacturer = (const char *)rom+pnp->manufacturer_offset;
		if (pnp->product_offset)
			product = (const char *)rom+pnp->product_offset;
	}

	if (rom->license_offs)
		license = (char *)rom+rom->license_offs;

	printk(KERN_INFO "VMI: Found %s %s, API version %d.%d, ROM version %d.%d\n",
		manufacturer, product,
		rom->api_version_maj, rom->api_version_min,
		pci->rom_version_maj, pci->rom_version_min);

	/* Don't allow BSD/MIT here for now because we don't want to end up
	   with any binary only shim layers */
	if (strcmp(license, "GPL") && strcmp(license, "GPL v2")) {
		printk(KERN_WARNING "VMI: Non GPL license `%s' found for ROM. Not used.\n",
			license);
		return 0;
	}

	return 1;
}

/*
 * Probe for the VMI option ROM
 */
static inline int __init probe_vmi_rom(void)
{
	unsigned long base;

	/* VMI ROM is in option ROM area, check signature */
	for (base = 0xC0000; base < 0xE0000; base += 2048) {
		struct vrom_header *romstart;
		romstart = (struct vrom_header *)isa_bus_to_virt(base);
		if (check_vmi_rom(romstart)) {
			vmi_rom = romstart;
			return 1;
		}
	}
	return 0;
}

/*
 * VMI setup common to all processors
 */
void vmi_bringup(void)
{
 	/* We must establish the lowmem mapping for MMU ops to work */
	if (vmi_ops.set_linear_mapping)
		vmi_ops.set_linear_mapping(0, (void *)__PAGE_OFFSET, MAXMEM_PFN, 0);
}

/*
 * Return a pointer to a VMI function or NULL if unimplemented
 */
static void *vmi_get_function(int vmicall)
{
	u64 reloc;
	const struct vmi_relocation_info *rel = (struct vmi_relocation_info *)&reloc;
	reloc = call_vrom_long_func(vmi_rom, get_reloc,	vmicall);
	BUG_ON(rel->type == VMI_RELOCATION_JUMP_REL);
	if (rel->type == VMI_RELOCATION_CALL_REL)
		return (void *)rel->eip;
	else
		return NULL;
}

/*
 * Helper macro for making the VMI paravirt-ops fill code readable.
 * For unimplemented operations, fall back to default, unless nop
 * is returned by the ROM.
 */
#define para_fill(opname, vmicall)				\
do {								\
	reloc = call_vrom_long_func(vmi_rom, get_reloc,		\
				    VMI_CALL_##vmicall);	\
	if (rel->type == VMI_RELOCATION_CALL_REL) 		\
		opname = (void *)rel->eip;			\
	else if (rel->type == VMI_RELOCATION_NOP) 		\
		opname = (void *)vmi_nop;			\
	else if (rel->type != VMI_RELOCATION_NONE)		\
		printk(KERN_WARNING "VMI: Unknown relocation "	\
				    "type %d for " #vmicall"\n",\
					rel->type);		\
} while (0)

/*
 * Helper macro for making the VMI paravirt-ops fill code readable.
 * For cached operations which do not match the VMI ROM ABI and must
 * go through a tranlation stub.  Ignore NOPs, since it is not clear
 * a NOP * VMI function corresponds to a NOP paravirt-op when the
 * functions are not in 1-1 correspondence.
 */
#define para_wrap(opname, wrapper, cache, vmicall)		\
do {								\
	reloc = call_vrom_long_func(vmi_rom, get_reloc,		\
				    VMI_CALL_##vmicall);	\
	BUG_ON(rel->type == VMI_RELOCATION_JUMP_REL);		\
	if (rel->type == VMI_RELOCATION_CALL_REL) {		\
		opname = wrapper;				\
		vmi_ops.cache = (void *)rel->eip;		\
	}							\
} while (0)

/*
 * Activate the VMI interface and switch into paravirtualized mode
 */
static inline int __init activate_vmi(void)
{
	short kernel_cs;
	u64 reloc;
	const struct vmi_relocation_info *rel = (struct vmi_relocation_info *)&reloc;

	/*
	 * Prevent page tables from being allocated in highmem, even if
	 * CONFIG_HIGHPTE is enabled.
	 */
	__userpte_alloc_gfp &= ~__GFP_HIGHMEM;

	if (call_vrom_func(vmi_rom, vmi_init) != 0) {
		printk(KERN_ERR "VMI ROM failed to initialize!");
		return 0;
	}
	savesegment(cs, kernel_cs);

	pv_info.paravirt_enabled = 1;
	pv_info.kernel_rpl = kernel_cs & SEGMENT_RPL_MASK;
	pv_info.name = "vmi [deprecated]";

	pv_init_ops.patch = vmi_patch;

	/*
	 * Many of these operations are ABI compatible with VMI.
	 * This means we can fill in the paravirt-ops with direct
	 * pointers into the VMI ROM.  If the calling convention for
	 * these operations changes, this code needs to be updated.
	 *
	 * Exceptions
	 *  CPUID paravirt-op uses pointers, not the native ISA
	 *  halt has no VMI equivalent; all VMI halts are "safe"
	 *  no MSR support yet - just trap and emulate.  VMI uses the
	 *    same ABI as the native ISA, but Linux wants exceptions
	 *    from bogus MSR read / write handled
	 *  rdpmc is not yet used in Linux
	 */

	/* CPUID is special, so very special it gets wrapped like a present */
	para_wrap(pv_cpu_ops.cpuid, vmi_cpuid, cpuid, CPUID);

	para_fill(pv_cpu_ops.clts, CLTS);
	para_fill(pv_cpu_ops.get_debugreg, GetDR);
	para_fill(pv_cpu_ops.set_debugreg, SetDR);
	para_fill(pv_cpu_ops.read_cr0, GetCR0);
	para_fill(pv_mmu_ops.read_cr2, GetCR2);
	para_fill(pv_mmu_ops.read_cr3, GetCR3);
	para_fill(pv_cpu_ops.read_cr4, GetCR4);
	para_fill(pv_cpu_ops.write_cr0, SetCR0);
	para_fill(pv_mmu_ops.write_cr2, SetCR2);
	para_fill(pv_mmu_ops.write_cr3, SetCR3);
	para_fill(pv_cpu_ops.write_cr4, SetCR4);

	para_fill(pv_irq_ops.save_fl.func, GetInterruptMask);
	para_fill(pv_irq_ops.restore_fl.func, SetInterruptMask);
	para_fill(pv_irq_ops.irq_disable.func, DisableInterrupts);
	para_fill(pv_irq_ops.irq_enable.func, EnableInterrupts);

	para_fill(pv_cpu_ops.wbinvd, WBINVD);
	para_fill(pv_cpu_ops.read_tsc, RDTSC);

	/* The following we emulate with trap and emulate for now */
	/* paravirt_ops.read_msr = vmi_rdmsr */
	/* paravirt_ops.write_msr = vmi_wrmsr */
	/* paravirt_ops.rdpmc = vmi_rdpmc */

	/* TR interface doesn't pass TR value, wrap */
	para_wrap(pv_cpu_ops.load_tr_desc, vmi_set_tr, set_tr, SetTR);

	/* LDT is special, too */
	para_wrap(pv_cpu_ops.set_ldt, vmi_set_ldt, _set_ldt, SetLDT);

	para_fill(pv_cpu_ops.load_gdt, SetGDT);
	para_fill(pv_cpu_ops.load_idt, SetIDT);
	para_fill(pv_cpu_ops.store_gdt, GetGDT);
	para_fill(pv_cpu_ops.store_idt, GetIDT);
	para_fill(pv_cpu_ops.store_tr, GetTR);
	pv_cpu_ops.load_tls = vmi_load_tls;
	para_wrap(pv_cpu_ops.write_ldt_entry, vmi_write_ldt_entry,
		  write_ldt_entry, WriteLDTEntry);
	para_wrap(pv_cpu_ops.write_gdt_entry, vmi_write_gdt_entry,
		  write_gdt_entry, WriteGDTEntry);
	para_wrap(pv_cpu_ops.write_idt_entry, vmi_write_idt_entry,
		  write_idt_entry, WriteIDTEntry);
	para_wrap(pv_cpu_ops.load_sp0, vmi_load_sp0, set_kernel_stack, UpdateKernelStack);
	para_fill(pv_cpu_ops.set_iopl_mask, SetIOPLMask);
	para_fill(pv_cpu_ops.io_delay, IODelay);

	para_wrap(pv_cpu_ops.start_context_switch, vmi_start_context_switch,
		  set_lazy_mode, SetLazyMode);
	para_wrap(pv_cpu_ops.end_context_switch, vmi_end_context_switch,
		  set_lazy_mode, SetLazyMode);

	para_wrap(pv_mmu_ops.lazy_mode.enter, vmi_enter_lazy_mmu,
		  set_lazy_mode, SetLazyMode);
	para_wrap(pv_mmu_ops.lazy_mode.leave, vmi_leave_lazy_mmu,
		  set_lazy_mode, SetLazyMode);

	/* user and kernel flush are just handled with different flags to FlushTLB */
	para_wrap(pv_mmu_ops.flush_tlb_user, vmi_flush_tlb_user, _flush_tlb, FlushTLB);
	para_wrap(pv_mmu_ops.flush_tlb_kernel, vmi_flush_tlb_kernel, _flush_tlb, FlushTLB);
	para_fill(pv_mmu_ops.flush_tlb_single, InvalPage);

	/*
	 * Until a standard flag format can be agreed on, we need to
	 * implement these as wrappers in Linux.  Get the VMI ROM
	 * function pointers for the two backend calls.
	 */
#ifdef CONFIG_X86_PAE
	vmi_ops.set_pte = vmi_get_function(VMI_CALL_SetPxELong);
	vmi_ops.update_pte = vmi_get_function(VMI_CALL_UpdatePxELong);
#else
	vmi_ops.set_pte = vmi_get_function(VMI_CALL_SetPxE);
	vmi_ops.update_pte = vmi_get_function(VMI_CALL_UpdatePxE);
#endif

	if (vmi_ops.set_pte) {
		pv_mmu_ops.set_pte = vmi_set_pte;
		pv_mmu_ops.set_pte_at = vmi_set_pte_at;
		pv_mmu_ops.set_pmd = vmi_set_pmd;
#ifdef CONFIG_X86_PAE
		pv_mmu_ops.set_pte_atomic = vmi_set_pte_atomic;
		pv_mmu_ops.set_pud = vmi_set_pud;
		pv_mmu_ops.pte_clear = vmi_pte_clear;
		pv_mmu_ops.pmd_clear = vmi_pmd_clear;
#endif
	}

	if (vmi_ops.update_pte) {
		pv_mmu_ops.pte_update = vmi_update_pte;
		pv_mmu_ops.pte_update_defer = vmi_update_pte_defer;
	}

	vmi_ops.allocate_page = vmi_get_function(VMI_CALL_AllocatePage);
	if (vmi_ops.allocate_page) {
		pv_mmu_ops.alloc_pte = vmi_allocate_pte;
		pv_mmu_ops.alloc_pmd = vmi_allocate_pmd;
		pv_mmu_ops.alloc_pmd_clone = vmi_allocate_pmd_clone;
	}

	vmi_ops.release_page = vmi_get_function(VMI_CALL_ReleasePage);
	if (vmi_ops.release_page) {
		pv_mmu_ops.release_pte = vmi_release_pte;
		pv_mmu_ops.release_pmd = vmi_release_pmd;
		pv_mmu_ops.pgd_free = vmi_pgd_free;
	}

	/* Set linear is needed in all cases */
	vmi_ops.set_linear_mapping = vmi_get_function(VMI_CALL_SetLinearMapping);
#ifdef CONFIG_HIGHPTE
	if (vmi_ops.set_linear_mapping)
		pv_mmu_ops.kmap_atomic_pte = vmi_kmap_atomic_pte;
#endif

	/*
	 * These MUST always be patched.  Don't support indirect jumps
	 * through these operations, as the VMI interface may use either
	 * a jump or a call to get to these operations, depending on
	 * the backend.  They are performance critical anyway, so requiring
	 * a patch is not a big problem.
	 */
	pv_cpu_ops.irq_enable_sysexit = (void *)0xfeedbab0;
	pv_cpu_ops.iret = (void *)0xbadbab0;

#ifdef CONFIG_SMP
	para_wrap(pv_apic_ops.startup_ipi_hook, vmi_startup_ipi_hook, set_initial_ap_state, SetInitialAPState);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
       para_fill(apic->read, APICRead);
       para_fill(apic->write, APICWrite);
#endif

	/*
	 * Check for VMI timer functionality by probing for a cycle frequency method
	 */
	reloc = call_vrom_long_func(vmi_rom, get_reloc, VMI_CALL_GetCycleFrequency);
	if (!disable_vmi_timer && rel->type != VMI_RELOCATION_NONE) {
		vmi_timer_ops.get_cycle_frequency = (void *)rel->eip;
		vmi_timer_ops.get_cycle_counter =
			vmi_get_function(VMI_CALL_GetCycleCounter);
		vmi_timer_ops.get_wallclock =
			vmi_get_function(VMI_CALL_GetWallclockTime);
		vmi_timer_ops.wallclock_updated =
			vmi_get_function(VMI_CALL_WallclockUpdated);
		vmi_timer_ops.set_alarm = vmi_get_function(VMI_CALL_SetAlarm);
		vmi_timer_ops.cancel_alarm =
			 vmi_get_function(VMI_CALL_CancelAlarm);
		x86_init.timers.timer_init = vmi_time_init;
#ifdef CONFIG_X86_LOCAL_APIC
		x86_init.timers.setup_percpu_clockev = vmi_time_bsp_init;
		x86_cpuinit.setup_percpu_clockev = vmi_time_ap_init;
#endif
		pv_time_ops.sched_clock = vmi_sched_clock;
		x86_platform.calibrate_tsc = vmi_tsc_khz;
		x86_platform.get_wallclock = vmi_get_wallclock;
		x86_platform.set_wallclock = vmi_set_wallclock;

		/* We have true wallclock functions; disable CMOS clock sync */
		no_sync_cmos_clock = 1;
	} else {
		disable_noidle = 1;
		disable_vmi_timer = 1;
	}

	para_fill(pv_irq_ops.safe_halt, Halt);

	/*
	 * Alternative instruction rewriting doesn't happen soon enough
	 * to convert VMI_IRET to a call instead of a jump; so we have
	 * to do this before IRQs get reenabled.  Fortunately, it is
	 * idempotent.
	 */
	apply_paravirt(__parainstructions, __parainstructions_end);

	vmi_bringup();

	return 1;
}

#undef para_fill

void __init vmi_init(void)
{
	if (!vmi_rom)
		probe_vmi_rom();
	else
		check_vmi_rom(vmi_rom);

	/* In case probing for or validating the ROM failed, basil */
	if (!vmi_rom)
		return;

	reserve_top_address(-vmi_rom->virtual_top);

#ifdef CONFIG_X86_IO_APIC
	/* This is virtual hardware; timer routing is wired correctly */
	no_timer_check = 1;
#endif
}

void __init vmi_activate(void)
{
	unsigned long flags;

	if (!vmi_rom)
		return;

	local_irq_save(flags);
	activate_vmi();
	local_irq_restore(flags & X86_EFLAGS_IF);
}

static int __init parse_vmi(char *arg)
{
	if (!arg)
		return -EINVAL;

	if (!strcmp(arg, "disable_pge")) {
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_PGE);
		disable_pge = 1;
	} else if (!strcmp(arg, "disable_pse")) {
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_PSE);
		disable_pse = 1;
	} else if (!strcmp(arg, "disable_sep")) {
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_SEP);
		disable_sep = 1;
	} else if (!strcmp(arg, "disable_tsc")) {
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_TSC);
		disable_tsc = 1;
	} else if (!strcmp(arg, "disable_mtrr")) {
		clear_cpu_cap(&boot_cpu_data, X86_FEATURE_MTRR);
		disable_mtrr = 1;
	} else if (!strcmp(arg, "disable_timer")) {
		disable_vmi_timer = 1;
		disable_noidle = 1;
	} else if (!strcmp(arg, "disable_noidle"))
		disable_noidle = 1;
	return 0;
}

early_param("vmi", parse_vmi);
