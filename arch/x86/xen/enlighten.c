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
#include <linux/hardirq.h>
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
#include <xen/interface/sched.h>
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
#include <asm/tlbflush.h>
#include <asm/reboot.h>

#include "xen-ops.h"
#include "mmu.h"
#include "multicalls.h"

EXPORT_SYMBOL_GPL(hypercall_page);

DEFINE_PER_CPU(struct vcpu_info *, xen_vcpu);
DEFINE_PER_CPU(struct vcpu_info, xen_vcpu_info);

/*
 * Note about cr3 (pagetable base) values:
 *
 * xen_cr3 contains the current logical cr3 value; it contains the
 * last set cr3.  This may not be the current effective cr3, because
 * its update may be being lazily deferred.  However, a vcpu looking
 * at its own cr3 can use this value knowing that it everything will
 * be self-consistent.
 *
 * xen_current_cr3 contains the actual vcpu cr3; it is set once the
 * hypercall to set the vcpu cr3 is complete (so it may be a little
 * out of date, but it will never be set early).  If one vcpu is
 * looking at another vcpu's cr3 value, it should use this variable.
 */
DEFINE_PER_CPU(unsigned long, xen_cr3);	 /* cr3 stored as physaddr */
DEFINE_PER_CPU(unsigned long, xen_current_cr3);	 /* actual vcpu cr3 */

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

static /* __initdata */ struct shared_info dummy_shared_info;

/*
 * Point at some empty memory to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
struct shared_info *HYPERVISOR_shared_info = (void *)&dummy_shared_info;

/*
 * Flag to determine whether vcpu info placement is available on all
 * VCPUs.  We assume it is to start with, and then set it to zero on
 * the first failure.  This is because it can succeed on some VCPUs
 * and not others, since it can involve hypervisor memory allocation,
 * or because the guest failed to guarantee all the appropriate
 * constraints on all VCPUs (ie buffer can't cross a page boundary).
 *
 * Note that any particular CPU may be using a placed vcpu structure,
 * but we can only optimise if the all are.
 *
 * 0: not available, 1: available
 */
static int have_vcpu_info_placement = 1;

static void __init xen_vcpu_setup(int cpu)
{
	struct vcpu_register_vcpu_info info;
	int err;
	struct vcpu_info *vcpup;

	per_cpu(xen_vcpu, cpu) = &HYPERVISOR_shared_info->vcpu_info[cpu];

	if (!have_vcpu_info_placement)
		return;		/* already tested, not available */

	vcpup = &per_cpu(xen_vcpu_info, cpu);

	info.mfn = virt_to_mfn(vcpup);
	info.offset = offset_in_page(vcpup);

	printk(KERN_DEBUG "trying to map vcpu_info %d at %p, mfn %llx, offset %d\n",
	       cpu, vcpup, info.mfn, info.offset);

	/* Check to see if the hypervisor will put the vcpu_info
	   structure where we want it, which allows direct access via
	   a percpu-variable. */
	err = HYPERVISOR_vcpu_op(VCPUOP_register_vcpu_info, cpu, &info);

	if (err) {
		printk(KERN_DEBUG "register_vcpu_info failed: err=%d\n", err);
		have_vcpu_info_placement = 0;
	} else {
		/* This cpu is using the registered vcpu info, even if
		   later ones fail to. */
		per_cpu(xen_vcpu, cpu) = vcpup;

		printk(KERN_DEBUG "cpu %d using vcpu_info at %p\n",
		       cpu, vcpup);
	}
}

static void __init xen_banner(void)
{
	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       pv_info.name);
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

	vcpu = x86_read_percpu(xen_vcpu);

	/* flag has opposite sense of mask */
	flags = !vcpu->evtchn_upcall_mask;

	/* convert to IF type flag
	   -0 -> 0x00000000
	   -1 -> 0xffffffff
	*/
	return (-flags) & X86_EFLAGS_IF;
}

static void xen_restore_fl(unsigned long flags)
{
	struct vcpu_info *vcpu;

	/* convert from IF type flag */
	flags = !(flags & X86_EFLAGS_IF);

	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = flags;
	preempt_enable_no_resched();

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	if (flags == 0) {
		preempt_check_resched();
		barrier(); /* unmask then check (avoid races) */
		if (unlikely(vcpu->evtchn_upcall_pending))
			force_evtchn_callback();
	}
}

static void xen_irq_disable(void)
{
	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	x86_read_percpu(xen_vcpu)->evtchn_upcall_mask = 1;
	preempt_enable_no_resched();
}

static void xen_irq_enable(void)
{
	struct vcpu_info *vcpu;

	/* There's a one instruction preempt window here.  We need to
	   make sure we're don't switch CPUs between getting the vcpu
	   pointer and updating the mask. */
	preempt_disable();
	vcpu = x86_read_percpu(xen_vcpu);
	vcpu->evtchn_upcall_mask = 0;
	preempt_enable_no_resched();

	/* Doesn't matter if we get preempted here, because any
	   pending event will get dealt with anyway. */

	barrier(); /* unmask then check (avoid races) */
	if (unlikely(vcpu->evtchn_upcall_pending))
		force_evtchn_callback();
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

static void xen_leave_lazy(void)
{
	paravirt_leave_lazy(paravirt_get_lazy_mode());
	xen_mc_flush();
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

	/*
	 * XXX sleazy hack: If we're being called in a lazy-cpu zone,
	 * it means we're in a context switch, and %gs has just been
	 * saved.  This means we can zero it out to prevent faults on
	 * exit from the hypervisor if the next process has no %gs.
	 * Either way, it has been saved, and the new value will get
	 * loaded properly.  This will go away as soon as Xen has been
	 * modified to not save/restore %gs for normal hypercalls.
	 */
	if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_CPU)
		loadsegment(gs, 0);
}

static void xen_write_ldt_entry(struct desc_struct *dt, int entrynum,
				u32 low, u32 high)
{
	unsigned long lp = (unsigned long)&dt[entrynum];
	xmaddr_t mach_lp = virt_to_machine(lp);
	u64 entry = (u64)high << 32 | low;

	preempt_disable();

	xen_mc_flush();
	if (HYPERVISOR_update_descriptor(mach_lp.maddr, entry))
		BUG();

	preempt_enable();
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
	unsigned long p = (unsigned long)&dt[entrynum];
	unsigned long start, end;

	preempt_disable();

	start = __get_cpu_var(idt_desc).address;
	end = start + __get_cpu_var(idt_desc).size + 1;

	xen_mc_flush();

	write_dt_entry(dt, entrynum, low, high);

	if (p >= start && (p + 8) <= end) {
		struct trap_info info[2];

		info[1].address = 0;

		if (cvt_gate_to_trap(entrynum, low, high, &info[0]))
			if (HYPERVISOR_set_trap_table(info))
				BUG();
	}

	preempt_enable();
}

static void xen_convert_trap_info(const struct Xgt_desc_struct *desc,
				  struct trap_info *traps)
{
	unsigned in, out, count;

	count = (desc->size+1) / 8;
	BUG_ON(count > 256);

	for (in = out = 0; in < count; in++) {
		const u32 *entry = (u32 *)(desc->address + in * 8);

		if (cvt_gate_to_trap(in, entry[0], entry[1], &traps[out]))
			out++;
	}
	traps[out].address = 0;
}

void xen_copy_trap_info(struct trap_info *traps)
{
	const struct Xgt_desc_struct *desc = &__get_cpu_var(idt_desc);

	xen_convert_trap_info(desc, traps);
}

/* Load a new IDT into Xen.  In principle this can be per-CPU, so we
   hold a spinlock to protect the static traps[] array (static because
   it avoids allocation, and saves stack space). */
static void xen_load_idt(const struct Xgt_desc_struct *desc)
{
	static DEFINE_SPINLOCK(lock);
	static struct trap_info traps[257];

	spin_lock(&lock);

	__get_cpu_var(idt_desc) = *desc;

	xen_convert_trap_info(desc, traps);

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
	preempt_disable();

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

	preempt_enable();
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

static void xen_apic_write(unsigned long reg, unsigned long val)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}
#endif

static void xen_flush_tlb(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_TLB_FLUSH_LOCAL;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static void xen_flush_tlb_single(unsigned long addr)
{
	struct mmuext_op *op;
	struct multicall_space mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_INVLPG_LOCAL;
	op->arg1.linear_addr = addr & PAGE_MASK;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static void xen_flush_tlb_others(const cpumask_t *cpus, struct mm_struct *mm,
				 unsigned long va)
{
	struct {
		struct mmuext_op op;
		cpumask_t mask;
	} *args;
	cpumask_t cpumask = *cpus;
	struct multicall_space mcs;

	/*
	 * A couple of (to be removed) sanity checks:
	 *
	 * - current CPU must not be in mask
	 * - mask must exist :)
	 */
	BUG_ON(cpus_empty(cpumask));
	BUG_ON(cpu_isset(smp_processor_id(), cpumask));
	BUG_ON(!mm);

	/* If a CPU which we ran on has gone down, OK. */
	cpus_and(cpumask, cpumask, cpu_online_map);
	if (cpus_empty(cpumask))
		return;

	mcs = xen_mc_entry(sizeof(*args));
	args = mcs.args;
	args->mask = cpumask;
	args->op.arg2.vcpumask = &args->mask;

	if (va == TLB_FLUSH_ALL) {
		args->op.cmd = MMUEXT_TLB_FLUSH_MULTI;
	} else {
		args->op.cmd = MMUEXT_INVLPG_MULTI;
		args->op.arg1.linear_addr = va;
	}

	MULTI_mmuext_op(mcs.mc, &args->op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);
}

static void xen_write_cr2(unsigned long cr2)
{
	x86_read_percpu(xen_vcpu)->arch.cr2 = cr2;
}

static unsigned long xen_read_cr2(void)
{
	return x86_read_percpu(xen_vcpu)->arch.cr2;
}

static unsigned long xen_read_cr2_direct(void)
{
	return x86_read_percpu(xen_vcpu_info.arch.cr2);
}

static void xen_write_cr4(unsigned long cr4)
{
	/* Just ignore cr4 changes; Xen doesn't allow us to do
	   anything anyway. */
}

static unsigned long xen_read_cr3(void)
{
	return x86_read_percpu(xen_cr3);
}

static void set_current_cr3(void *v)
{
	x86_write_percpu(xen_current_cr3, (unsigned long)v);
}

static void xen_write_cr3(unsigned long cr3)
{
	struct mmuext_op *op;
	struct multicall_space mcs;
	unsigned long mfn = pfn_to_mfn(PFN_DOWN(cr3));

	BUG_ON(preemptible());

	mcs = xen_mc_entry(sizeof(*op));  /* disables interrupts */

	/* Update while interrupts are disabled, so its atomic with
	   respect to ipis */
	x86_write_percpu(xen_cr3, cr3);

	op = mcs.args;
	op->cmd = MMUEXT_NEW_BASEPTR;
	op->arg1.mfn = mfn;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	/* Update xen_update_cr3 once the batch has actually
	   been submitted. */
	xen_mc_callback(set_current_cr3, (void *)cr3);

	xen_mc_issue(PARAVIRT_LAZY_CPU);  /* interrupts restored */
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static __init void xen_alloc_pt_init(struct mm_struct *mm, u32 pfn)
{
	BUG_ON(mem_map);	/* should only be used early */
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

static void pin_pagetable_pfn(unsigned level, unsigned long pfn)
{
	struct mmuext_op op;
	op.cmd = level;
	op.arg1.mfn = pfn_to_mfn(pfn);
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static void xen_alloc_pt(struct mm_struct *mm, u32 pfn)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(virt_to_page(mm->pgd))) {
		SetPagePinned(page);

		if (!PageHighMem(page)) {
			make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
			pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
		} else
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
		if (!PageHighMem(page)) {
			pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
			make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
		}
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
	pv_mmu_ops.set_pte = xen_set_pte_init;

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
	pv_mmu_ops.alloc_pt = xen_alloc_pt;
	pv_mmu_ops.set_pte = xen_set_pte;

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
		unsigned level;

#ifdef CONFIG_X86_PAE
		level = MMUEXT_PIN_L3_TABLE;
#else
		level = MMUEXT_PIN_L2_TABLE;
#endif

		pin_pagetable_pfn(level, PFN_DOWN(__pa(base)));
	}
}

/* This is called once we have the cpu_possible_map */
void __init xen_setup_vcpu_info_placement(void)
{
	int cpu;

	for_each_possible_cpu(cpu)
		xen_vcpu_setup(cpu);

	/* xen_vcpu_setup managed to place the vcpu_info within the
	   percpu area for all cpus, so make use of it */
	if (have_vcpu_info_placement) {
		printk(KERN_INFO "Xen: using vcpu_info placement\n");

		pv_irq_ops.save_fl = xen_save_fl_direct;
		pv_irq_ops.restore_fl = xen_restore_fl_direct;
		pv_irq_ops.irq_disable = xen_irq_disable_direct;
		pv_irq_ops.irq_enable = xen_irq_enable_direct;
		pv_mmu_ops.read_cr2 = xen_read_cr2_direct;
		pv_cpu_ops.iret = xen_iret_direct;
	}
}

static unsigned xen_patch(u8 type, u16 clobbers, void *insnbuf,
			  unsigned long addr, unsigned len)
{
	char *start, *end, *reloc;
	unsigned ret;

	start = end = reloc = NULL;

#define SITE(op, x)							\
	case PARAVIRT_PATCH(op.x):					\
	if (have_vcpu_info_placement) {					\
		start = (char *)xen_##x##_direct;			\
		end = xen_##x##_direct_end;				\
		reloc = xen_##x##_direct_reloc;				\
	}								\
	goto patch_site

	switch (type) {
		SITE(pv_irq_ops, irq_enable);
		SITE(pv_irq_ops, irq_disable);
		SITE(pv_irq_ops, save_fl);
		SITE(pv_irq_ops, restore_fl);
#undef SITE

	patch_site:
		if (start == NULL || (end-start) > len)
			goto default_patch;

		ret = paravirt_patch_insns(insnbuf, len, start, end);

		/* Note: because reloc is assigned from something that
		   appears to be an array, gcc assumes it's non-null,
		   but doesn't know its relationship with start and
		   end. */
		if (reloc > start && reloc < end) {
			int reloc_off = reloc - start;
			long *relocp = (long *)(insnbuf + reloc_off);
			long delta = start - (char *)addr;

			*relocp += delta;
		}
		break;

	default_patch:
	default:
		ret = paravirt_patch_default(type, clobbers, insnbuf,
					     addr, len);
		break;
	}

	return ret;
}

static const struct pv_info xen_info __initdata = {
	.paravirt_enabled = 1,
	.shared_kernel_pmd = 0,

	.name = "Xen",
};

static const struct pv_init_ops xen_init_ops __initdata = {
	.patch = xen_patch,

	.banner = xen_banner,
	.memory_setup = xen_memory_setup,
	.arch_setup = xen_arch_setup,
	.post_allocator_init = xen_mark_init_mm_pinned,
};

static const struct pv_time_ops xen_time_ops __initdata = {
	.time_init = xen_time_init,

	.set_wallclock = xen_set_wallclock,
	.get_wallclock = xen_get_wallclock,
	.get_cpu_khz = xen_cpu_khz,
	.sched_clock = xen_sched_clock,
};

static const struct pv_cpu_ops xen_cpu_ops __initdata = {
	.cpuid = xen_cpuid,

	.set_debugreg = xen_set_debugreg,
	.get_debugreg = xen_get_debugreg,

	.clts = native_clts,

	.read_cr0 = native_read_cr0,
	.write_cr0 = native_write_cr0,

	.read_cr4 = native_read_cr4,
	.read_cr4_safe = native_read_cr4_safe,
	.write_cr4 = xen_write_cr4,

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

	.lazy_mode = {
		.enter = paravirt_enter_lazy_cpu,
		.leave = xen_leave_lazy,
	},
};

static const struct pv_irq_ops xen_irq_ops __initdata = {
	.init_IRQ = xen_init_IRQ,
	.save_fl = xen_save_fl,
	.restore_fl = xen_restore_fl,
	.irq_disable = xen_irq_disable,
	.irq_enable = xen_irq_enable,
	.safe_halt = xen_safe_halt,
	.halt = xen_halt,
};

static const struct pv_apic_ops xen_apic_ops __initdata = {
#ifdef CONFIG_X86_LOCAL_APIC
	.apic_write = xen_apic_write,
	.apic_write_atomic = xen_apic_write,
	.apic_read = xen_apic_read,
	.setup_boot_clock = paravirt_nop,
	.setup_secondary_clock = paravirt_nop,
	.startup_ipi_hook = paravirt_nop,
#endif
};

static const struct pv_mmu_ops xen_mmu_ops __initdata = {
	.pagetable_setup_start = xen_pagetable_setup_start,
	.pagetable_setup_done = xen_pagetable_setup_done,

	.read_cr2 = xen_read_cr2,
	.write_cr2 = xen_write_cr2,

	.read_cr3 = xen_read_cr3,
	.write_cr3 = xen_write_cr3,

	.flush_tlb_user = xen_flush_tlb,
	.flush_tlb_kernel = xen_flush_tlb,
	.flush_tlb_single = xen_flush_tlb_single,
	.flush_tlb_others = xen_flush_tlb_others,

	.pte_update = paravirt_nop,
	.pte_update_defer = paravirt_nop,

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

	.lazy_mode = {
		.enter = paravirt_enter_lazy_mmu,
		.leave = xen_leave_lazy,
	},
};

#ifdef CONFIG_SMP
static const struct smp_ops xen_smp_ops __initdata = {
	.smp_prepare_boot_cpu = xen_smp_prepare_boot_cpu,
	.smp_prepare_cpus = xen_smp_prepare_cpus,
	.cpu_up = xen_cpu_up,
	.smp_cpus_done = xen_smp_cpus_done,

	.smp_send_stop = xen_smp_send_stop,
	.smp_send_reschedule = xen_smp_send_reschedule,
	.smp_call_function_mask = xen_smp_call_function_mask,
};
#endif	/* CONFIG_SMP */

static void xen_reboot(int reason)
{
#ifdef CONFIG_SMP
	smp_send_stop();
#endif

	if (HYPERVISOR_sched_op(SCHEDOP_shutdown, reason))
		BUG();
}

static void xen_restart(char *msg)
{
	xen_reboot(SHUTDOWN_reboot);
}

static void xen_emergency_restart(void)
{
	xen_reboot(SHUTDOWN_reboot);
}

static void xen_machine_halt(void)
{
	xen_reboot(SHUTDOWN_poweroff);
}

static void xen_crash_shutdown(struct pt_regs *regs)
{
	xen_reboot(SHUTDOWN_crash);
}

static const struct machine_ops __initdata xen_machine_ops = {
	.restart = xen_restart,
	.halt = xen_machine_halt,
	.power_off = xen_machine_halt,
	.shutdown = xen_machine_halt,
	.crash_shutdown = xen_crash_shutdown,
	.emergency_restart = xen_emergency_restart,
};


static void __init xen_reserve_top(void)
{
	unsigned long top = HYPERVISOR_VIRT_START;
	struct xen_platform_parameters pp;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0)
		top = pp.virt_start;

	reserve_top_address(-top + 2 * PAGE_SIZE);
}

/* First C function to be called on Xen boot */
asmlinkage void __init xen_start_kernel(void)
{
	pgd_t *pgd;

	if (!xen_start_info)
		return;

	BUG_ON(memcmp(xen_start_info->magic, "xen-3.0", 7) != 0);

	/* Install Xen paravirt ops */
	pv_info = xen_info;
	pv_init_ops = xen_init_ops;
	pv_time_ops = xen_time_ops;
	pv_cpu_ops = xen_cpu_ops;
	pv_irq_ops = xen_irq_ops;
	pv_apic_ops = xen_apic_ops;
	pv_mmu_ops = xen_mmu_ops;

	machine_ops = xen_machine_ops;

#ifdef CONFIG_SMP
	smp_ops = xen_smp_ops;
#endif

	xen_setup_features();

	/* Get mfn list */
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		phys_to_machine_mapping = (unsigned long *)xen_start_info->mfn_list;

	pgd = (pgd_t *)xen_start_info->pt_base;

	init_pg_tables_end = __pa(pgd) + xen_start_info->nr_pt_frames*PAGE_SIZE;

	init_mm.pgd = pgd; /* use the Xen pagetables to start */

	/* keep using Xen gdt for now; no urgent need to change it */

	x86_write_percpu(xen_cr3, __pa(pgd));
	x86_write_percpu(xen_current_cr3, __pa(pgd));

#ifdef CONFIG_SMP
	/* Don't do the full vcpu_info placement stuff until we have a
	   possible map. */
	per_cpu(xen_vcpu, 0) = &HYPERVISOR_shared_info->vcpu_info[0];
#else
	/* May as well do it now, since there's no good time to call
	   it later on UP. */
	xen_setup_vcpu_info_placement();
#endif

	pv_info.kernel_rpl = 1;
	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		pv_info.kernel_rpl = 0;

	/* set the limit of our address space */
	xen_reserve_top();

	/* set up basic CPUID stuff */
	cpu_detect(&new_cpu_data);
	new_cpu_data.hard_math = 1;
	new_cpu_data.x86_capability[0] = cpuid_edx(1);

	/* Poke various useful things into boot_params */
	boot_params.hdr.type_of_loader = (9 << 4) | 0;
	boot_params.hdr.ramdisk_image = xen_start_info->mod_start
		? __pa(xen_start_info->mod_start) : 0;
	boot_params.hdr.ramdisk_size = xen_start_info->mod_len;

	/* Start the world */
	start_kernel();
}
