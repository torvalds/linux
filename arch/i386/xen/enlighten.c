/*
 * Core of Xen paravirt_ops implementation.
 *
 * This file contains the xen_paravirt_ops structure itself, and the
 * implementations for:
 * - privileged instructions
 * - interrupt flags
 * - segment operations
 * - booting and setup
 *
 * Jeremy Fitzhardinge <jeremy@xensource.com>, XenSource Inc, 2007
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/smp.h>
#include <linux/preempt.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/start_kernel.h>
#include <linux/sched.h>
#include <linux/bootmem.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/page-flags.h>
#include <linux/highmem.h>

#include <xen/interface/xen.h>
#include <xen/interface/physdev.h>
#include <xen/interface/vcpu.h>
#include <xen/features.h>
#include <xen/page.h>

#include <asm/paravirt.h>
#include <asm/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/setup.h>
#include <asm/desc.h>
#include <asm/pgtable.h>

#include "xen-ops.h"
#include "mmu.h"
#include "multicalls.h"

EXPORT_SYMBOL_GPL(hypercall_page);

DEFINE_PER_CPU(enum paravirt_lazy_mode, xen_lazy_mode);

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
DEFINE_PER_CPU(struct vcpu_info, xen_vcpu_info);
DEFINE_PER_CPU(unsigned long, xen_cr3);

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

static void xen_vcpu_setup(int cpu)
{
	per_cpu(xen_vcpu, cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];
}

static void __init xen_banner(void)
{
	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       paravirt_ops.name);
	printk(KERN_INFO "Hypervisor signature: %s\n", xen_start_info->magic);
}

static void xen_cpuid(unsigned int *eax, unsigned int *ebx,
		      unsigned int *ecx, unsigned int *edx)
{
	unsigned maskedx = ~0;

	/*
	 * Mask out inconvenient features, to try and disable as many
	 * unsupported kernel subsystems as possible.
	 */
	if (*eax == 1)
		maskedx = ~((1 << X86_FEATURE_APIC) |  /* disable APIC */
			    (1 << X86_FEATURE_ACPI) |  /* disable ACPI */
			    (1 << X86_FEATURE_ACC));   /* thermal monitoring */

	asm(XEN_EMULATE_PREFIX "cpuid"
		: "=a" (*eax),
		  "=b" (*ebx),
		  "=c" (*ecx),
		  "=d" (*edx)
		: "0" (*eax), "2" (*ecx));
	*edx &= maskedx;
}

static void xen_set_debugreg(int reg, unsigned long val)
{
	HYPERVISOR_set_debugreg(reg, val);
}

static unsigned long xen_get_debugreg(int reg)
{
	return HYPERVISOR_get_debugreg(reg);
}

static unsigned long xen_save_fl(void)
{
	struct vcpu_info *vcpu;
	unsigned long flags;

	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	/* flag has opposite sense of mask */
	flags = !vcpu->evtchn_upcall_mask;
	preempt_enable();

	/* convert to IF type flag
	   -0 -> 0x00000000
	   -1 -> 0xffffffff
	*/
	return (-flags) & X86_EFLAGS_IF;
}

static void xen_restore_fl(unsigned long flags)
{
	struct vcpu_info *vcpu;

	preempt_disable();

	/* convert from IF type flag */
	flags = !(flags & X86_EFLAGS_IF);
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = flags;

	if (flags == 0) {
		/* Unmask then check (avoid races).  We're only protecting
		   against updates by this CPU, so there's no need for
		   anything stronger. */
		barrier();

		if (unlikely(vcpu->evtchn_upcall_pending))
			force_evtchn_callback();
		preempt_enable();
	} else
		preempt_enable_no_resched();
}

static void xen_irq_disable(void)
{
	struct vcpu_info *vcpu;
	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}

static void xen_irq_enable(void)
{
	struct vcpu_info *vcpu;

	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = 0;

	/* Unmask then check (avoid races).  We're only protecting
	   against updates by this CPU, so there's no need for
	   anything stronger. */
	barrier();

	if (unlikely(vcpu->evtchn_upcall_pending))
		force_evtchn_callback();
	preempt_enable();
}

static void xen_safe_halt(void)
{
	/* Blocking includes an implicit local_irq_enable(). */
	if (HYPERVISOR_sched_op(SCHEDOP_block, 0) != 0)
		BUG();
}

static void xen_halt(void)
{
	if (irqs_disabled())
		HYPERVISOR_vcpu_op(VCPUOP_down, smp_processor_id(), NULL);
	else
		xen_safe_halt();
}

static void xen_set_lazy_mode(enum paravirt_lazy_mode mode)
{
	switch (mode) {
	case PARAVIRT_LAZY_NONE:
		BUG_ON(x86_read_percpu(xen_lazy_mode) == PARAVIRT_LAZY_NONE);
		break;

	case PARAVIRT_LAZY_MMU:
	case PARAVIRT_LAZY_CPU:
		BUG_ON(x86_read_percpu(xen_lazy_mode) != PARAVIRT_LAZY_NONE);
		break;

	case PARAVIRT_LAZY_FLUSH:
		/* flush if necessary, but don't change state */
		if (x86_read_percpu(xen_lazy_mode) != PARAVIRT_LAZY_NONE)
			xen_mc_flush();
		return;
	}

	xen_mc_flush();
	x86_write_percpu(xen_lazy_mode, mode);
}

static unsigned long xen_store_tr(void)
{
	return 0;
}

static void xen_set_ldt(const void *addr, unsigned entries)
{
	unsigned long linear_addr = (unsigned long)addr;
	struct mmuext_op *op;
	struct multicall_space mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_SET_LDT;
	if (linear_addr) {
		/* ldt my be vmalloced, use arbitrary_virt_to_machine */
		xmaddr_t maddr;
		maddr = arbitrary_virt_to_machine((unsigned long)addr);
		linear_addr = (unsigned long)maddr.maddr;
	}
	op->arg1.linear_addr = linear_addr;
	op->arg2.nr_ents = entries;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_load_gdt(const struct Xgt_desc_struct *dtr)
{
	unsigned long *frames;
	unsigned long va = dtr->address;
	unsigned int size = dtr->size + 1;
	unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	int f;
	struct multicall_space mcs;

	/* A GDT can be up to 64k in size, which corresponds to 8192
	   8-byte entries, or 16 4k pages.. */

	BUG_ON(size > 65536);
	BUG_ON(va & ~PAGE_MASK);

	mcs = xen_mc_entry(sizeof(*frames) * pages);
	frames = mcs.args;

	for (f = 0; va < dtr->address + size; va += PAGE_SIZE, f++) {
		frames[f] = virt_to_mfn(va);
		make_lowmem_page_readonly((void *)va);
	}

	MULTI_set_gdt(mcs.mc, frames, size / sizeof(struct desc_struct));

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void load_TLS_descriptor(struct thread_struct *t,
				unsigned int cpu, unsigned int i)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	xmaddr_t maddr = virt_to_machine(&gdt[GDT_ENTRY_TLS_MIN+i]);
	struct multicall_space mc = __xen_mc_entry(0);

	MULTI_update_descriptor(mc.mc, maddr.maddr, t->tls_array[i]);
}

static void xen_load_tls(struct thread_struct *t, unsigned int cpu)
{
	xen_mc_batch();

	load_TLS_descriptor(t, cpu, 0);
	load_TLS_descriptor(t, cpu, 1);
	load_TLS_descriptor(t, cpu, 2);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_write_ldt_entry(struct desc_struct *dt, int entrynum,
				u32 low, u32 high)
{
	unsigned long lp = (unsigned long)&dt[entrynum];
	xmaddr_t mach_lp = virt_to_machine(lp);
	u64 entry = (u64)high << 32 | low;

	xen_mc_flush();
	if (HYPERVISOR_update_descriptor(mach_lp.maddr, entry))
		BUG();
}

static int cvt_gate_to_trap(int vector, u32 low, u32 high,
			    struct trap_info *info)
{
	u8 type, dpl;

	type = (high >> 8) & 0x1f;
	dpl = (high >> 13) & 3;

	if (type != 0xf && type != 0xe)
		return 0;

	info->vector = vector;
	info->address = (high & 0xffff0000) | (low & 0x0000ffff);
	info->cs = low >> 16;
	info->flags = dpl;
	/* interrupt gates clear IF */
	if (type == 0xe)
		info->flags |= 4;

	return 1;
}

/* Locations of each CPU's IDT */
static DEFINE_PER_CPU(struct Xgt_desc_struct, idt_desc);

/* Set an IDT entry.  If the entry is part of the current IDT, then
   also update Xen. */
static void xen_write_idt_entry(struct desc_struct *dt, int entrynum,
				u32 low, u32 high)
{

	int cpu = smp_processor_id();
	unsigned long p = (unsigned long)&dt[entrynum];
	unsigned long start = per_cpu(idt_desc, cpu).address;
	unsigned long end = start + per_cpu(idt_desc, cpu).size + 1;

	xen_mc_flush();

	write_dt_entry(dt, entrynum, low, high);

	if (p >= start && (p + 8) <= end) {
		struct trap_info info[2];

		info[1].address = 0;

		if (cvt_gate_to_trap(entrynum, low, high, &info[0]))
			if (HYPERVISOR_set_trap_table(info))
				BUG();
	}
}

/* Load a new IDT into Xen.  In principle this can be per-CPU, so we
   hold a spinlock to protect the static traps[] array (static because
   it avoids allocation, and saves stack space). */
static void xen_load_idt(const struct Xgt_desc_struct *desc)
{
	static DEFINE_SPINLOCK(lock);
	static struct trap_info traps[257];

	int cpu = smp_processor_id();
	unsigned in, out, count;

	per_cpu(idt_desc, cpu) = *desc;

	count = (desc->size+1) / 8;
	BUG_ON(count > 256);

	spin_lock(&lock);
	for (in = out = 0; in < count; in++) {
		const u32 *entry = (u32 *)(desc->address + in * 8);

		if (cvt_gate_to_trap(in, entry[0], entry[1], &traps[out]))
			out++;
	}
	traps[out].address = 0;

	xen_mc_flush();
	if (HYPERVISOR_set_trap_table(traps))
		BUG();

	spin_unlock(&lock);
}

/* Write a GDT descriptor entry.  Ignore LDT descriptors, since
   they're handled differently. */
static void xen_write_gdt_entry(struct desc_struct *dt, int entry,
				u32 low, u32 high)
{
	switch ((high >> 8) & 0xff) {
	case DESCTYPE_LDT:
	case DESCTYPE_TSS:
		/* ignore */
		break;

	default: {
		xmaddr_t maddr = virt_to_machine(&dt[entry]);
		u64 desc = (u64)high << 32 | low;

		xen_mc_flush();
		if (HYPERVISOR_update_descriptor(maddr.maddr, desc))
			BUG();
	}

	}
}

static void xen_load_esp0(struct tss_struct *tss,
				   struct thread_struct *thread)
{
	struct multicall_space mcs = xen_mc_entry(0);
	MULTI_stack_switch(mcs.mc, __KERNEL_DS, thread->esp0);
	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_set_iopl_mask(unsigned mask)
{
	struct physdev_set_iopl set_iopl;

	/* Force the change at ring 0. */
	set_iopl.iopl = (mask == 0) ? 1 : (mask >> 12) & 3;
	HYPERVISOR_physdev_op(PHYSDEVOP_set_iopl, &set_iopl);
}

static void xen_io_delay(void)
{
}

#ifdef CONFIG_X86_LOCAL_APIC
static unsigned long xen_apic_read(unsigned long reg)
{
	return 0;
}
#endif

static void xen_flush_tlb(void)
{
	struct mmuext_op op;

	op.cmd = MMUEXT_TLB_FLUSH_LOCAL;
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

static void xen_flush_tlb_single(unsigned long addr)
{
	struct mmuext_op op;

	op.cmd = MMUEXT_INVLPG_LOCAL;
	op.arg1.linear_addr = addr & PAGE_MASK;
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

static unsigned long xen_read_cr2(void)
{
	return x86_read_percpu(xen_vcpu)->arch.cr2;
}

static void xen_write_cr4(unsigned long cr4)
{
	/* never allow TSC to be disabled */
	native_write_cr4(cr4 & ~X86_CR4_TSD);
}

/*
 * Page-directory addresses above 4GB do not fit into architectural %cr3.
 * When accessing %cr3, or equivalent field in vcpu_guest_context, guests
 * must use the following accessor macros to pack/unpack valid MFNs.
 *
 * Note that Xen is using the fact that the pagetable base is always
 * page-aligned, and putting the 12 MSB of the address into the 12 LSB
 * of cr3.
 */
#define xen_pfn_to_cr3(pfn) (((unsigned)(pfn) << 12) | ((unsigned)(pfn) >> 20))
#define xen_cr3_to_pfn(cr3) (((unsigned)(cr3) >> 12) | ((unsigned)(cr3) << 20))

static unsigned long xen_read_cr3(void)
{
	return x86_read_percpu(xen_cr3);
}

static void xen_write_cr3(unsigned long cr3)
{
	if (cr3 == x86_read_percpu(xen_cr3)) {
		/* just a simple tlb flush */
		xen_flush_tlb();
		return;
	}

	x86_write_percpu(xen_cr3, cr3);


	{
		struct mmuext_op *op;
		struct multicall_space mcs = xen_mc_entry(sizeof(*op));
		unsigned long mfn = pfn_to_mfn(PFN_DOWN(cr3));

		op = mcs.args;
		op->cmd = MMUEXT_NEW_BASEPTR;
		op->arg1.mfn = mfn;

		MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

		xen_mc_issue(PARAVIRT_LAZY_CPU);
	}
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static __init void xen_alloc_pt_init(struct mm_struct *mm, u32 pfn)
{
	BUG_ON(mem_map);	/* should only be used early */
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static void xen_alloc_pt(struct mm_struct *mm, u32 pfn)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(virt_to_page(mm->pgd))) {
		SetPagePinned(page);

		if (!PageHighMem(page))
			make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
		else
			/* make sure there are no stray mappings of
			   this page */
			kmap_flush_unused();
	}
}

/* This should never happen until we're OK to use struct page */
static void xen_release_pt(u32 pfn)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(page)) {
		if (!PageHighMem(page))
			make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
	}
}

#ifdef CONFIG_HIGHPTE
static void *xen_kmap_atomic_pte(struct page *page, enum km_type type)
{
	pgprot_t prot = PAGE_KERNEL;

	if (PagePinned(page))
		prot = PAGE_KERNEL_RO;

	if (0 && PageHighMem(page))
		printk("mapping highpte %lx type %d prot %s\n",
		       page_to_pfn(page), type,
		       (unsigned long)pgprot_val(prot) & _PAGE_RW ? "WRITE" : "READ");

	return kmap_atomic_prot(page, type, prot);
}
#endif

static __init pte_t mask_rw_pte(pte_t *ptep, pte_t pte)
{
	/* If there's an existing pte, then don't allow _PAGE_RW to be set */
	if (pte_val_ma(*ptep) & _PAGE_PRESENT)
		pte = __pte_ma(((pte_val_ma(*ptep) & _PAGE_RW) | ~_PAGE_RW) &
			       pte_val_ma(pte));

	return pte;
}

/* Init-time set_pte while constructing initial pagetables, which
   doesn't allow RO pagetable pages to be remapped RW */
static __init void xen_set_pte_init(pte_t *ptep, pte_t pte)
{
	pte = mask_rw_pte(ptep, pte);

	xen_set_pte(ptep, pte);
}

static __init void xen_pagetable_setup_start(pgd_t *base)
{
	pgd_t *xen_pgd = (pgd_t *)xen_start_info->pt_base;

	/* special set_pte for pagetable initialization */
	paravirt_ops.set_pte = xen_set_pte_init;

	init_mm.pgd = base;
	/*
	 * copy top-level of Xen-supplied pagetable into place.	 For
	 * !PAE we can use this as-is, but for PAE it is a stand-in
	 * while we copy the pmd pages.
	 */
	memcpy(base, xen_pgd, PTRS_PER_PGD * sizeof(pgd_t));

	if (PTRS_PER_PMD > 1) {
		int i;
		/*
		 * For PAE, need to allocate new pmds, rather than
		 * share Xen's, since Xen doesn't like pmd's being
		 * shared between address spaces.
		 */
		for (i = 0; i < PTRS_PER_PGD; i++) {
			if (pgd_val_ma(xen_pgd[i]) & _PAGE_PRESENT) {
				pmd_t *pmd = (pmd_t *)alloc_bootmem_low_pages(PAGE_SIZE);

				memcpy(pmd, (void *)pgd_page_vaddr(xen_pgd[i]),
				       PAGE_SIZE);

				make_lowmem_page_readonly(pmd);

				set_pgd(&base[i], __pgd(1 + __pa(pmd)));
			} else
				pgd_clear(&base[i]);
		}
	}

	/* make sure zero_page is mapped RO so we can use it in pagetables */
	make_lowmem_page_readonly(empty_zero_page);
	make_lowmem_page_readonly(base);
	/*
	 * Switch to new pagetable.  This is done before
	 * pagetable_init has done anything so that the new pages
	 * added to the table can be prepared properly for Xen.
	 */
	xen_write_cr3(__pa(base));
}

static __init void xen_pagetable_setup_done(pgd_t *base)
{
	/* This will work as long as patching hasn't happened yet
	   (which it hasn't) */
	paravirt_ops.alloc_pt = xen_alloc_pt;
	paravirt_ops.set_pte = xen_set_pte;

	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		/*
		 * Create a mapping for the shared info page.
		 * Should be set_fixmap(), but shared_info is a machine
		 * address with no corresponding pseudo-phys address.
		 */
		set_pte_mfn(fix_to_virt(FIX_PARAVIRT_BOOTMAP),
			    PFN_DOWN(xen_start_info->shared_info),
			    PAGE_KERNEL);

		HYPERVISOR_shared_info =
			(struct shared_info *)fix_to_virt(FIX_PARAVIRT_BOOTMAP);

	} else
		HYPERVISOR_shared_info =
			(struct shared_info *)__va(xen_start_info->shared_info);

	/* Actually pin the pagetable down, but we can't set PG_pinned
	   yet because the page structures don't exist yet. */
	{
		struct mmuext_op op;
#ifdef CONFIG_X86_PAE
		op.cmd = MMUEXT_PIN_L3_TABLE;
#else
		op.cmd = MMUEXT_PIN_L3_TABLE;
#endif
		op.arg1.mfn = pfn_to_mfn(PFN_DOWN(__pa(base)));
		if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
			BUG();
	}

	xen_vcpu_setup(smp_processor_id());
}

static const struct paravirt_ops xen_paravirt_ops __initdata = {
	.paravirt_enabled = 1,
	.shared_kernel_pmd = 0,

	.name = "Xen",
	.banner = xen_banner,

	.patch = paravirt_patch_default,

	.memory_setup = xen_memory_setup,
	.arch_setup = xen_arch_setup,
	.init_IRQ = xen_init_IRQ,
	.post_allocator_init = xen_mark_init_mm_pinned,

	.time_init = xen_time_init,
	.set_wallclock = xen_set_wallclock,
	.get_wallclock = xen_get_wallclock,
	.get_cpu_khz = xen_cpu_khz,
	.sched_clock = xen_clocksource_read,

	.cpuid = xen_cpuid,

	.set_debugreg = xen_set_debugreg,
	.get_debugreg = xen_get_debugreg,

	.clts = native_clts,

	.read_cr0 = native_read_cr0,
	.write_cr0 = native_write_cr0,

	.read_cr2 = xen_read_cr2,
	.write_cr2 = native_write_cr2,

	.read_cr3 = xen_read_cr3,
	.write_cr3 = xen_write_cr3,

	.read_cr4 = native_read_cr4,
	.read_cr4_safe = native_read_cr4_safe,
	.write_cr4 = xen_write_cr4,

	.save_fl = xen_save_fl,
	.restore_fl = xen_restore_fl,
	.irq_disable = xen_irq_disable,
	.irq_enable = xen_irq_enable,
	.safe_halt = xen_safe_halt,
	.halt = xen_halt,
	.wbinvd = native_wbinvd,

	.read_msr = native_read_msr_safe,
	.write_msr = native_write_msr_safe,
	.read_tsc = native_read_tsc,
	.read_pmc = native_read_pmc,

	.iret = (void *)&hypercall_page[__HYPERVISOR_iret],
	.irq_enable_sysexit = NULL,  /* never called */

	.load_tr_desc = paravirt_nop,
	.set_ldt = xen_set_ldt,
	.load_gdt = xen_load_gdt,
	.load_idt = xen_load_idt,
	.load_tls = xen_load_tls,

	.store_gdt = native_store_gdt,
	.store_idt = native_store_idt,
	.store_tr = xen_store_tr,

	.write_ldt_entry = xen_write_ldt_entry,
	.write_gdt_entry = xen_write_gdt_entry,
	.write_idt_entry = xen_write_idt_entry,
	.load_esp0 = xen_load_esp0,

	.set_iopl_mask = xen_set_iopl_mask,
	.io_delay = xen_io_delay,

#ifdef CONFIG_X86_LOCAL_APIC
	.apic_write = paravirt_nop,
	.apic_write_atomic = paravirt_nop,
	.apic_read = xen_apic_read,
	.setup_boot_clock = paravirt_nop,
	.setup_secondary_clock = paravirt_nop,
	.startup_ipi_hook = paravirt_nop,
#endif

	.flush_tlb_user = xen_flush_tlb,
	.flush_tlb_kernel = xen_flush_tlb,
	.flush_tlb_single = xen_flush_tlb_single,

	.pte_update = paravirt_nop,
	.pte_update_defer = paravirt_nop,

	.pagetable_setup_start = xen_pagetable_setup_start,
	.pagetable_setup_done = xen_pagetable_setup_done,

	.alloc_pt = xen_alloc_pt_init,
	.release_pt = xen_release_pt,
	.alloc_pd = paravirt_nop,
	.alloc_pd_clone = paravirt_nop,
	.release_pd = paravirt_nop,

#ifdef CONFIG_HIGHPTE
	.kmap_atomic_pte = xen_kmap_atomic_pte,
#endif

	.set_pte = NULL,	/* see xen_pagetable_setup_* */
	.set_pte_at = xen_set_pte_at,
	.set_pmd = xen_set_pmd,

	.pte_val = xen_pte_val,
	.pgd_val = xen_pgd_val,

	.make_pte = xen_make_pte,
	.make_pgd = xen_make_pgd,

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = xen_set_pte_atomic,
	.set_pte_present = xen_set_pte_at,
	.set_pud = xen_set_pud,
	.pte_clear = xen_pte_clear,
	.pmd_clear = xen_pmd_clear,

	.make_pmd = xen_make_pmd,
	.pmd_val = xen_pmd_val,
#endif	/* PAE */

	.activate_mm = xen_activate_mm,
	.dup_mmap = xen_dup_mmap,
	.exit_mmap = xen_exit_mmap,

	.set_lazy_mode = xen_set_lazy_mode,
};

/* First C function to be called on Xen boot */
asmlinkage void __init xen_start_kernel(void)
{
	pgd_t *pgd;

	if (!xen_start_info)
		return;

	BUG_ON(memcmp(xen_start_info->magic, "xen-3.0", 7) != 0);

	/* Install Xen paravirt ops */
	paravirt_ops = xen_paravirt_ops;

	xen_setup_features();

	/* Get mfn list */
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		phys_to_machine_mapping = (unsigned long *)xen_start_info->mfn_list;

	pgd = (pgd_t *)xen_start_info->pt_base;

	init_pg_tables_end = __pa(pgd) + xen_start_info->nr_pt_frames*PAGE_SIZE;

	init_mm.pgd = pgd; /* use the Xen pagetables to start */

	/* keep using Xen gdt for now; no urgent need to change it */

	x86_write_percpu(xen_cr3, __pa(pgd));
	xen_vcpu_setup(0);

	paravirt_ops.kernel_rpl = 1;
	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		paravirt_ops.kernel_rpl = 0;

	/* set the limit of our address space */
	reserve_top_address(-HYPERVISOR_VIRT_START + 2 * PAGE_SIZE);

	/* set up basic CPUID stuff */
	cpu_detect(&new_cpu_data);
	new_cpu_data.hard_math = 1;
	new_cpu_data.x86_capability[0] = cpuid_edx(1);

	/* Poke various useful things into boot_params */
	LOADER_TYPE = (9 << 4) | 0;
	INITRD_START = xen_start_info->mod_start ? __pa(xen_start_info->mod_start) : 0;
	INITRD_SIZE = xen_start_info->mod_len;

	/* Start the world */
	start_kernel();
}
