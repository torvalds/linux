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
#include <linux/console.h>

#include <xen/interface/xen.h>
#include <xen/interface/version.h>
#include <xen/interface/physdev.h>
#include <xen/interface/vcpu.h>
#include <xen/features.h>
#include <xen/page.h>
#include <xen/hvc-console.h>

#include <asm/paravirt.h>
#include <asm/apic.h>
#include <asm/page.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/hypervisor.h>
#include <asm/fixmap.h>
#include <asm/processor.h>
#include <asm/msr-index.h>
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

enum xen_domain_type xen_domain_type = XEN_NATIVE;
EXPORT_SYMBOL_GPL(xen_domain_type);

/*
 * Identity map, in addition to plain kernel map.  This needs to be
 * large enough to allocate page table pages to allocate the rest.
 * Each page can map 2MB.
 */
static pte_t level1_ident_pgt[PTRS_PER_PTE * 4] __page_aligned_bss;

#ifdef CONFIG_X86_64
/* l3 pud for userspace vsyscall mapping */
static pud_t level3_user_vsyscall[PTRS_PER_PUD] __page_aligned_bss;
#endif /* CONFIG_X86_64 */

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

struct shared_info xen_dummy_shared_info;

/*
 * Point at some empty memory to start with. We map the real shared_info
 * page as soon as fixmap is up and running.
 */
struct shared_info *HYPERVISOR_shared_info = (void *)&xen_dummy_shared_info;

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
static int have_vcpu_info_placement =
#ifdef CONFIG_X86_32
	1
#else
	0
#endif
	;


static void xen_vcpu_setup(int cpu)
{
	struct vcpu_register_vcpu_info info;
	int err;
	struct vcpu_info *vcpup;

	BUG_ON(HYPERVISOR_shared_info == &xen_dummy_shared_info);
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

/*
 * On restore, set the vcpu placement up again.
 * If it fails, then we're in a bad state, since
 * we can't back out from using it...
 */
void xen_vcpu_restore(void)
{
	if (have_vcpu_info_placement) {
		int cpu;

		for_each_online_cpu(cpu) {
			bool other_cpu = (cpu != smp_processor_id());

			if (other_cpu &&
			    HYPERVISOR_vcpu_op(VCPUOP_down, cpu, NULL))
				BUG();

			xen_vcpu_setup(cpu);

			if (other_cpu &&
			    HYPERVISOR_vcpu_op(VCPUOP_up, cpu, NULL))
				BUG();
		}

		BUG_ON(!have_vcpu_info_placement);
	}
}

static void __init xen_banner(void)
{
	unsigned version = HYPERVISOR_xen_version(XENVER_version, NULL);
	struct xen_extraversion extra;
	HYPERVISOR_xen_version(XENVER_extraversion, &extra);

	printk(KERN_INFO "Booting paravirtualized kernel on %s\n",
	       pv_info.name);
	printk(KERN_INFO "Xen version: %d.%d%s%s\n",
	       version >> 16, version & 0xffff, extra.extraversion,
	       xen_feature(XENFEAT_mmu_pt_update_preserve_ad) ? " (preserve-AD)" : "");
}

static void xen_cpuid(unsigned int *ax, unsigned int *bx,
		      unsigned int *cx, unsigned int *dx)
{
	unsigned maskedx = ~0;

	/*
	 * Mask out inconvenient features, to try and disable as many
	 * unsupported kernel subsystems as possible.
	 */
	if (*ax == 1)
		maskedx = ~((1 << X86_FEATURE_APIC) |  /* disable APIC */
			    (1 << X86_FEATURE_ACPI) |  /* disable ACPI */
			    (1 << X86_FEATURE_MCE)  |  /* disable MCE */
			    (1 << X86_FEATURE_MCA)  |  /* disable MCA */
			    (1 << X86_FEATURE_ACC));   /* thermal monitoring */

	asm(XEN_EMULATE_PREFIX "cpuid"
		: "=a" (*ax),
		  "=b" (*bx),
		  "=c" (*cx),
		  "=d" (*dx)
		: "0" (*ax), "2" (*cx));
	*dx &= maskedx;
}

static void xen_set_debugreg(int reg, unsigned long val)
{
	HYPERVISOR_set_debugreg(reg, val);
}

static unsigned long xen_get_debugreg(int reg)
{
	return HYPERVISOR_get_debugreg(reg);
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

/*
 * Set the page permissions for a particular virtual address.  If the
 * address is a vmalloc mapping (or other non-linear mapping), then
 * find the linear mapping of the page and also set its protections to
 * match.
 */
static void set_aliased_prot(void *v, pgprot_t prot)
{
	int level;
	pte_t *ptep;
	pte_t pte;
	unsigned long pfn;
	struct page *page;

	ptep = lookup_address((unsigned long)v, &level);
	BUG_ON(ptep == NULL);

	pfn = pte_pfn(*ptep);
	page = pfn_to_page(pfn);

	pte = pfn_pte(pfn, prot);

	if (HYPERVISOR_update_va_mapping((unsigned long)v, pte, 0))
		BUG();

	if (!PageHighMem(page)) {
		void *av = __va(PFN_PHYS(pfn));

		if (av != v)
			if (HYPERVISOR_update_va_mapping((unsigned long)av, pte, 0))
				BUG();
	} else
		kmap_flush_unused();
}

static void xen_alloc_ldt(struct desc_struct *ldt, unsigned entries)
{
	const unsigned entries_per_page = PAGE_SIZE / LDT_ENTRY_SIZE;
	int i;

	for(i = 0; i < entries; i += entries_per_page)
		set_aliased_prot(ldt + i, PAGE_KERNEL_RO);
}

static void xen_free_ldt(struct desc_struct *ldt, unsigned entries)
{
	const unsigned entries_per_page = PAGE_SIZE / LDT_ENTRY_SIZE;
	int i;

	for(i = 0; i < entries; i += entries_per_page)
		set_aliased_prot(ldt + i, PAGE_KERNEL);
}

static void xen_set_ldt(const void *addr, unsigned entries)
{
	struct mmuext_op *op;
	struct multicall_space mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_SET_LDT;
	op->arg1.linear_addr = (unsigned long)addr;
	op->arg2.nr_ents = entries;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_load_gdt(const struct desc_ptr *dtr)
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
	/*
	 * XXX sleazy hack: If we're being called in a lazy-cpu zone,
	 * it means we're in a context switch, and %gs has just been
	 * saved.  This means we can zero it out to prevent faults on
	 * exit from the hypervisor if the next process has no %gs.
	 * Either way, it has been saved, and the new value will get
	 * loaded properly.  This will go away as soon as Xen has been
	 * modified to not save/restore %gs for normal hypercalls.
	 *
	 * On x86_64, this hack is not used for %gs, because gs points
	 * to KERNEL_GS_BASE (and uses it for PDA references), so we
	 * must not zero %gs on x86_64
	 *
	 * For x86_64, we need to zero %fs, otherwise we may get an
	 * exception between the new %fs descriptor being loaded and
	 * %fs being effectively cleared at __switch_to().
	 */
	if (paravirt_get_lazy_mode() == PARAVIRT_LAZY_CPU) {
#ifdef CONFIG_X86_32
		loadsegment(gs, 0);
#else
		loadsegment(fs, 0);
#endif
	}

	xen_mc_batch();

	load_TLS_descriptor(t, cpu, 0);
	load_TLS_descriptor(t, cpu, 1);
	load_TLS_descriptor(t, cpu, 2);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

#ifdef CONFIG_X86_64
static void xen_load_gs_index(unsigned int idx)
{
	if (HYPERVISOR_set_segment_base(SEGBASE_GS_USER_SEL, idx))
		BUG();
}
#endif

static void xen_write_ldt_entry(struct desc_struct *dt, int entrynum,
				const void *ptr)
{
	xmaddr_t mach_lp = arbitrary_virt_to_machine(&dt[entrynum]);
	u64 entry = *(u64 *)ptr;

	preempt_disable();

	xen_mc_flush();
	if (HYPERVISOR_update_descriptor(mach_lp.maddr, entry))
		BUG();

	preempt_enable();
}

static int cvt_gate_to_trap(int vector, const gate_desc *val,
			    struct trap_info *info)
{
	if (val->type != 0xf && val->type != 0xe)
		return 0;

	info->vector = vector;
	info->address = gate_offset(*val);
	info->cs = gate_segment(*val);
	info->flags = val->dpl;
	/* interrupt gates clear IF */
	if (val->type == 0xe)
		info->flags |= 4;

	return 1;
}

/* Locations of each CPU's IDT */
static DEFINE_PER_CPU(struct desc_ptr, idt_desc);

/* Set an IDT entry.  If the entry is part of the current IDT, then
   also update Xen. */
static void xen_write_idt_entry(gate_desc *dt, int entrynum, const gate_desc *g)
{
	unsigned long p = (unsigned long)&dt[entrynum];
	unsigned long start, end;

	preempt_disable();

	start = __get_cpu_var(idt_desc).address;
	end = start + __get_cpu_var(idt_desc).size + 1;

	xen_mc_flush();

	native_write_idt_entry(dt, entrynum, g);

	if (p >= start && (p + 8) <= end) {
		struct trap_info info[2];

		info[1].address = 0;

		if (cvt_gate_to_trap(entrynum, g, &info[0]))
			if (HYPERVISOR_set_trap_table(info))
				BUG();
	}

	preempt_enable();
}

static void xen_convert_trap_info(const struct desc_ptr *desc,
				  struct trap_info *traps)
{
	unsigned in, out, count;

	count = (desc->size+1) / sizeof(gate_desc);
	BUG_ON(count > 256);

	for (in = out = 0; in < count; in++) {
		gate_desc *entry = (gate_desc*)(desc->address) + in;

		if (cvt_gate_to_trap(in, entry, &traps[out]))
			out++;
	}
	traps[out].address = 0;
}

void xen_copy_trap_info(struct trap_info *traps)
{
	const struct desc_ptr *desc = &__get_cpu_var(idt_desc);

	xen_convert_trap_info(desc, traps);
}

/* Load a new IDT into Xen.  In principle this can be per-CPU, so we
   hold a spinlock to protect the static traps[] array (static because
   it avoids allocation, and saves stack space). */
static void xen_load_idt(const struct desc_ptr *desc)
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
				const void *desc, int type)
{
	preempt_disable();

	switch (type) {
	case DESC_LDT:
	case DESC_TSS:
		/* ignore */
		break;

	default: {
		xmaddr_t maddr = virt_to_machine(&dt[entry]);

		xen_mc_flush();
		if (HYPERVISOR_update_descriptor(maddr.maddr, *(u64 *)desc))
			BUG();
	}

	}

	preempt_enable();
}

static void xen_load_sp0(struct tss_struct *tss,
			 struct thread_struct *thread)
{
	struct multicall_space mcs = xen_mc_entry(0);
	MULTI_stack_switch(mcs.mc, __KERNEL_DS, thread->sp0);
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
static u32 xen_apic_read(u32 reg)
{
	return 0;
}

static void xen_apic_write(u32 reg, u32 val)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}

static u64 xen_apic_icr_read(void)
{
	return 0;
}

static void xen_apic_icr_write(u32 low, u32 id)
{
	/* Warn to see if there's any stray references */
	WARN_ON(1);
}

static void xen_apic_wait_icr_idle(void)
{
        return;
}

static u32 xen_safe_apic_wait_icr_idle(void)
{
        return 0;
}

static struct apic_ops xen_basic_apic_ops = {
	.read = xen_apic_read,
	.write = xen_apic_write,
	.icr_read = xen_apic_icr_read,
	.icr_write = xen_apic_icr_write,
	.wait_icr_idle = xen_apic_wait_icr_idle,
	.safe_wait_icr_idle = xen_safe_apic_wait_icr_idle,
};

#endif

static void xen_flush_tlb(void)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = MMUEXT_TLB_FLUSH_LOCAL;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
}

static void xen_flush_tlb_single(unsigned long addr)
{
	struct mmuext_op *op;
	struct multicall_space mcs;

	preempt_disable();

	mcs = xen_mc_entry(sizeof(*op));
	op = mcs.args;
	op->cmd = MMUEXT_INVLPG_LOCAL;
	op->arg1.linear_addr = addr & PAGE_MASK;
	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	xen_mc_issue(PARAVIRT_LAZY_MMU);

	preempt_enable();
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

static void xen_clts(void)
{
	struct multicall_space mcs;

	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_write_cr0(unsigned long cr0)
{
	struct multicall_space mcs;

	/* Only pay attention to cr0.TS; everything else is
	   ignored. */
	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, (cr0 & X86_CR0_TS) != 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
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
	cr4 &= ~X86_CR4_PGE;
	cr4 &= ~X86_CR4_PSE;

	native_write_cr4(cr4);
}

static unsigned long xen_read_cr3(void)
{
	return x86_read_percpu(xen_cr3);
}

static void set_current_cr3(void *v)
{
	x86_write_percpu(xen_current_cr3, (unsigned long)v);
}

static void __xen_write_cr3(bool kernel, unsigned long cr3)
{
	struct mmuext_op *op;
	struct multicall_space mcs;
	unsigned long mfn;

	if (cr3)
		mfn = pfn_to_mfn(PFN_DOWN(cr3));
	else
		mfn = 0;

	WARN_ON(mfn == 0 && kernel);

	mcs = __xen_mc_entry(sizeof(*op));

	op = mcs.args;
	op->cmd = kernel ? MMUEXT_NEW_BASEPTR : MMUEXT_NEW_USER_BASEPTR;
	op->arg1.mfn = mfn;

	MULTI_mmuext_op(mcs.mc, op, 1, NULL, DOMID_SELF);

	if (kernel) {
		x86_write_percpu(xen_cr3, cr3);

		/* Update xen_current_cr3 once the batch has actually
		   been submitted. */
		xen_mc_callback(set_current_cr3, (void *)cr3);
	}
}

static void xen_write_cr3(unsigned long cr3)
{
	BUG_ON(preemptible());

	xen_mc_batch();  /* disables interrupts */

	/* Update while interrupts are disabled, so its atomic with
	   respect to ipis */
	x86_write_percpu(xen_cr3, cr3);

	__xen_write_cr3(true, cr3);

#ifdef CONFIG_X86_64
	{
		pgd_t *user_pgd = xen_get_user_pgd(__va(cr3));
		if (user_pgd)
			__xen_write_cr3(false, __pa(user_pgd));
		else
			__xen_write_cr3(false, 0);
	}
#endif

	xen_mc_issue(PARAVIRT_LAZY_CPU);  /* interrupts restored */
}

static int xen_write_msr_safe(unsigned int msr, unsigned low, unsigned high)
{
	int ret;

	ret = 0;

	switch (msr) {
#ifdef CONFIG_X86_64
		unsigned which;
		u64 base;

	case MSR_FS_BASE:		which = SEGBASE_FS; goto set;
	case MSR_KERNEL_GS_BASE:	which = SEGBASE_GS_USER; goto set;
	case MSR_GS_BASE:		which = SEGBASE_GS_KERNEL; goto set;

	set:
		base = ((u64)high << 32) | low;
		if (HYPERVISOR_set_segment_base(which, base) != 0)
			ret = -EFAULT;
		break;
#endif

	case MSR_STAR:
	case MSR_CSTAR:
	case MSR_LSTAR:
	case MSR_SYSCALL_MASK:
	case MSR_IA32_SYSENTER_CS:
	case MSR_IA32_SYSENTER_ESP:
	case MSR_IA32_SYSENTER_EIP:
		/* Fast syscall setup is all done in hypercalls, so
		   these are all ignored.  Stub them out here to stop
		   Xen console noise. */
		break;

	default:
		ret = native_write_msr_safe(msr, low, high);
	}

	return ret;
}

/* Early in boot, while setting up the initial pagetable, assume
   everything is pinned. */
static __init void xen_alloc_pte_init(struct mm_struct *mm, unsigned long pfn)
{
#ifdef CONFIG_FLATMEM
	BUG_ON(mem_map);	/* should only be used early */
#endif
	make_lowmem_page_readonly(__va(PFN_PHYS(pfn)));
}

/* Early release_pte assumes that all pts are pinned, since there's
   only init_mm and anything attached to that is pinned. */
static void xen_release_pte_init(unsigned long pfn)
{
	make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
}

static void pin_pagetable_pfn(unsigned cmd, unsigned long pfn)
{
	struct mmuext_op op;
	op.cmd = cmd;
	op.arg1.mfn = pfn_to_mfn(pfn);
	if (HYPERVISOR_mmuext_op(&op, 1, NULL, DOMID_SELF))
		BUG();
}

/* This needs to make sure the new pte page is pinned iff its being
   attached to a pinned pagetable. */
static void xen_alloc_ptpage(struct mm_struct *mm, unsigned long pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(virt_to_page(mm->pgd))) {
		SetPagePinned(page);

		vm_unmap_aliases();
		if (!PageHighMem(page)) {
			make_lowmem_page_readonly(__va(PFN_PHYS((unsigned long)pfn)));
			if (level == PT_PTE && USE_SPLIT_PTLOCKS)
				pin_pagetable_pfn(MMUEXT_PIN_L1_TABLE, pfn);
		} else {
			/* make sure there are no stray mappings of
			   this page */
			kmap_flush_unused();
		}
	}
}

static void xen_alloc_pte(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PTE);
}

static void xen_alloc_pmd(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PMD);
}

static int xen_pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd = mm->pgd;
	int ret = 0;

	BUG_ON(PagePinned(virt_to_page(pgd)));

#ifdef CONFIG_X86_64
	{
		struct page *page = virt_to_page(pgd);
		pgd_t *user_pgd;

		BUG_ON(page->private != 0);

		ret = -ENOMEM;

		user_pgd = (pgd_t *)__get_free_page(GFP_KERNEL | __GFP_ZERO);
		page->private = (unsigned long)user_pgd;

		if (user_pgd != NULL) {
			user_pgd[pgd_index(VSYSCALL_START)] =
				__pgd(__pa(level3_user_vsyscall) | _PAGE_TABLE);
			ret = 0;
		}

		BUG_ON(PagePinned(virt_to_page(xen_get_user_pgd(pgd))));
	}
#endif

	return ret;
}

static void xen_pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#ifdef CONFIG_X86_64
	pgd_t *user_pgd = xen_get_user_pgd(pgd);

	if (user_pgd)
		free_page((unsigned long)user_pgd);
#endif
}

/* This should never happen until we're OK to use struct page */
static void xen_release_ptpage(unsigned long pfn, unsigned level)
{
	struct page *page = pfn_to_page(pfn);

	if (PagePinned(page)) {
		if (!PageHighMem(page)) {
			if (level == PT_PTE && USE_SPLIT_PTLOCKS)
				pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, pfn);
			make_lowmem_page_readwrite(__va(PFN_PHYS(pfn)));
		}
		ClearPagePinned(page);
	}
}

static void xen_release_pte(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PTE);
}

static void xen_release_pmd(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PMD);
}

#if PAGETABLE_LEVELS == 4
static void xen_alloc_pud(struct mm_struct *mm, unsigned long pfn)
{
	xen_alloc_ptpage(mm, pfn, PT_PUD);
}

static void xen_release_pud(unsigned long pfn)
{
	xen_release_ptpage(pfn, PT_PUD);
}
#endif

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

#ifdef CONFIG_X86_32
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
#endif

static __init void xen_pagetable_setup_start(pgd_t *base)
{
}

void xen_setup_shared_info(void)
{
	if (!xen_feature(XENFEAT_auto_translated_physmap)) {
		set_fixmap(FIX_PARAVIRT_BOOTMAP,
			   xen_start_info->shared_info);

		HYPERVISOR_shared_info =
			(struct shared_info *)fix_to_virt(FIX_PARAVIRT_BOOTMAP);
	} else
		HYPERVISOR_shared_info =
			(struct shared_info *)__va(xen_start_info->shared_info);

#ifndef CONFIG_SMP
	/* In UP this is as good a place as any to set up shared info */
	xen_setup_vcpu_info_placement();
#endif

	xen_setup_mfn_list_list();
}

static __init void xen_pagetable_setup_done(pgd_t *base)
{
	xen_setup_shared_info();
}

static __init void xen_post_allocator_init(void)
{
	pv_mmu_ops.set_pte = xen_set_pte;
	pv_mmu_ops.set_pmd = xen_set_pmd;
	pv_mmu_ops.set_pud = xen_set_pud;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.set_pgd = xen_set_pgd;
#endif

	/* This will work as long as patching hasn't happened yet
	   (which it hasn't) */
	pv_mmu_ops.alloc_pte = xen_alloc_pte;
	pv_mmu_ops.alloc_pmd = xen_alloc_pmd;
	pv_mmu_ops.release_pte = xen_release_pte;
	pv_mmu_ops.release_pmd = xen_release_pmd;
#if PAGETABLE_LEVELS == 4
	pv_mmu_ops.alloc_pud = xen_alloc_pud;
	pv_mmu_ops.release_pud = xen_release_pud;
#endif

#ifdef CONFIG_X86_64
	SetPagePinned(virt_to_page(level3_user_vsyscall));
#endif
	xen_mark_init_mm_pinned();
}

/* This is called once we have the cpu_possible_map */
void xen_setup_vcpu_info_placement(void)
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

static void xen_set_fixmap(unsigned idx, unsigned long phys, pgprot_t prot)
{
	pte_t pte;

	phys >>= PAGE_SHIFT;

	switch (idx) {
	case FIX_BTMAP_END ... FIX_BTMAP_BEGIN:
#ifdef CONFIG_X86_F00F_BUG
	case FIX_F00F_IDT:
#endif
#ifdef CONFIG_X86_32
	case FIX_WP_TEST:
	case FIX_VDSO:
# ifdef CONFIG_HIGHMEM
	case FIX_KMAP_BEGIN ... FIX_KMAP_END:
# endif
#else
	case VSYSCALL_LAST_PAGE ... VSYSCALL_FIRST_PAGE:
#endif
#ifdef CONFIG_X86_LOCAL_APIC
	case FIX_APIC_BASE:	/* maps dummy local APIC */
#endif
		pte = pfn_pte(phys, prot);
		break;

	default:
		pte = mfn_pte(phys, prot);
		break;
	}

	__native_set_fixmap(idx, pte);

#ifdef CONFIG_X86_64
	/* Replicate changes to map the vsyscall page into the user
	   pagetable vsyscall mapping. */
	if (idx >= VSYSCALL_LAST_PAGE && idx <= VSYSCALL_FIRST_PAGE) {
		unsigned long vaddr = __fix_to_virt(idx);
		set_pte_vaddr_pud(level3_user_vsyscall, vaddr, pte);
	}
#endif
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
	.post_allocator_init = xen_post_allocator_init,
};

static const struct pv_time_ops xen_time_ops __initdata = {
	.time_init = xen_time_init,

	.set_wallclock = xen_set_wallclock,
	.get_wallclock = xen_get_wallclock,
	.get_tsc_khz = xen_tsc_khz,
	.sched_clock = xen_sched_clock,
};

static const struct pv_cpu_ops xen_cpu_ops __initdata = {
	.cpuid = xen_cpuid,

	.set_debugreg = xen_set_debugreg,
	.get_debugreg = xen_get_debugreg,

	.clts = xen_clts,

	.read_cr0 = native_read_cr0,
	.write_cr0 = xen_write_cr0,

	.read_cr4 = native_read_cr4,
	.read_cr4_safe = native_read_cr4_safe,
	.write_cr4 = xen_write_cr4,

	.wbinvd = native_wbinvd,

	.read_msr = native_read_msr_safe,
	.write_msr = xen_write_msr_safe,
	.read_tsc = native_read_tsc,
	.read_pmc = native_read_pmc,

	.iret = xen_iret,
	.irq_enable_sysexit = xen_sysexit,
#ifdef CONFIG_X86_64
	.usergs_sysret32 = xen_sysret32,
	.usergs_sysret64 = xen_sysret64,
#endif

	.load_tr_desc = paravirt_nop,
	.set_ldt = xen_set_ldt,
	.load_gdt = xen_load_gdt,
	.load_idt = xen_load_idt,
	.load_tls = xen_load_tls,
#ifdef CONFIG_X86_64
	.load_gs_index = xen_load_gs_index,
#endif

	.alloc_ldt = xen_alloc_ldt,
	.free_ldt = xen_free_ldt,

	.store_gdt = native_store_gdt,
	.store_idt = native_store_idt,
	.store_tr = xen_store_tr,

	.write_ldt_entry = xen_write_ldt_entry,
	.write_gdt_entry = xen_write_gdt_entry,
	.write_idt_entry = xen_write_idt_entry,
	.load_sp0 = xen_load_sp0,

	.set_iopl_mask = xen_set_iopl_mask,
	.io_delay = xen_io_delay,

	/* Xen takes care of %gs when switching to usermode for us */
	.swapgs = paravirt_nop,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_cpu,
		.leave = xen_leave_lazy,
	},
};

static const struct pv_apic_ops xen_apic_ops __initdata = {
#ifdef CONFIG_X86_LOCAL_APIC
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

	.pgd_alloc = xen_pgd_alloc,
	.pgd_free = xen_pgd_free,

	.alloc_pte = xen_alloc_pte_init,
	.release_pte = xen_release_pte_init,
	.alloc_pmd = xen_alloc_pte_init,
	.alloc_pmd_clone = paravirt_nop,
	.release_pmd = xen_release_pte_init,

#ifdef CONFIG_HIGHPTE
	.kmap_atomic_pte = xen_kmap_atomic_pte,
#endif

#ifdef CONFIG_X86_64
	.set_pte = xen_set_pte,
#else
	.set_pte = xen_set_pte_init,
#endif
	.set_pte_at = xen_set_pte_at,
	.set_pmd = xen_set_pmd_hyper,

	.ptep_modify_prot_start = __ptep_modify_prot_start,
	.ptep_modify_prot_commit = __ptep_modify_prot_commit,

	.pte_val = xen_pte_val,
	.pte_flags = native_pte_flags,
	.pgd_val = xen_pgd_val,

	.make_pte = xen_make_pte,
	.make_pgd = xen_make_pgd,

#ifdef CONFIG_X86_PAE
	.set_pte_atomic = xen_set_pte_atomic,
	.set_pte_present = xen_set_pte_at,
	.pte_clear = xen_pte_clear,
	.pmd_clear = xen_pmd_clear,
#endif	/* CONFIG_X86_PAE */
	.set_pud = xen_set_pud_hyper,

	.make_pmd = xen_make_pmd,
	.pmd_val = xen_pmd_val,

#if PAGETABLE_LEVELS == 4
	.pud_val = xen_pud_val,
	.make_pud = xen_make_pud,
	.set_pgd = xen_set_pgd_hyper,

	.alloc_pud = xen_alloc_pte_init,
	.release_pud = xen_release_pte_init,
#endif	/* PAGETABLE_LEVELS == 4 */

	.activate_mm = xen_activate_mm,
	.dup_mmap = xen_dup_mmap,
	.exit_mmap = xen_exit_mmap,

	.lazy_mode = {
		.enter = paravirt_enter_lazy_mmu,
		.leave = xen_leave_lazy,
	},

	.set_fixmap = xen_set_fixmap,
};

static void xen_reboot(int reason)
{
	struct sched_shutdown r = { .reason = reason };

#ifdef CONFIG_SMP
	smp_send_stop();
#endif

	if (HYPERVISOR_sched_op(SCHEDOP_shutdown, &r))
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
#ifdef CONFIG_X86_32
	unsigned long top = HYPERVISOR_VIRT_START;
	struct xen_platform_parameters pp;

	if (HYPERVISOR_xen_version(XENVER_platform_parameters, &pp) == 0)
		top = pp.virt_start;

	reserve_top_address(-top);
#endif	/* CONFIG_X86_32 */
}

/*
 * Like __va(), but returns address in the kernel mapping (which is
 * all we have until the physical memory mapping has been set up.
 */
static void *__ka(phys_addr_t paddr)
{
#ifdef CONFIG_X86_64
	return (void *)(paddr + __START_KERNEL_map);
#else
	return __va(paddr);
#endif
}

/* Convert a machine address to physical address */
static unsigned long m2p(phys_addr_t maddr)
{
	phys_addr_t paddr;

	maddr &= PTE_PFN_MASK;
	paddr = mfn_to_pfn(maddr >> PAGE_SHIFT) << PAGE_SHIFT;

	return paddr;
}

/* Convert a machine address to kernel virtual */
static void *m2v(phys_addr_t maddr)
{
	return __ka(m2p(maddr));
}

static void set_page_prot(void *addr, pgprot_t prot)
{
	unsigned long pfn = __pa(addr) >> PAGE_SHIFT;
	pte_t pte = pfn_pte(pfn, prot);

	if (HYPERVISOR_update_va_mapping((unsigned long)addr, pte, 0))
		BUG();
}

static __init void xen_map_identity_early(pmd_t *pmd, unsigned long max_pfn)
{
	unsigned pmdidx, pteidx;
	unsigned ident_pte;
	unsigned long pfn;

	ident_pte = 0;
	pfn = 0;
	for (pmdidx = 0; pmdidx < PTRS_PER_PMD && pfn < max_pfn; pmdidx++) {
		pte_t *pte_page;

		/* Reuse or allocate a page of ptes */
		if (pmd_present(pmd[pmdidx]))
			pte_page = m2v(pmd[pmdidx].pmd);
		else {
			/* Check for free pte pages */
			if (ident_pte == ARRAY_SIZE(level1_ident_pgt))
				break;

			pte_page = &level1_ident_pgt[ident_pte];
			ident_pte += PTRS_PER_PTE;

			pmd[pmdidx] = __pmd(__pa(pte_page) | _PAGE_TABLE);
		}

		/* Install mappings */
		for (pteidx = 0; pteidx < PTRS_PER_PTE; pteidx++, pfn++) {
			pte_t pte;

			if (pfn > max_pfn_mapped)
				max_pfn_mapped = pfn;

			if (!pte_none(pte_page[pteidx]))
				continue;

			pte = pfn_pte(pfn, PAGE_KERNEL_EXEC);
			pte_page[pteidx] = pte;
		}
	}

	for (pteidx = 0; pteidx < ident_pte; pteidx += PTRS_PER_PTE)
		set_page_prot(&level1_ident_pgt[pteidx], PAGE_KERNEL_RO);

	set_page_prot(pmd, PAGE_KERNEL_RO);
}

#ifdef CONFIG_X86_64
static void convert_pfn_mfn(void *v)
{
	pte_t *pte = v;
	int i;

	/* All levels are converted the same way, so just treat them
	   as ptes. */
	for (i = 0; i < PTRS_PER_PTE; i++)
		pte[i] = xen_make_pte(pte[i].pte);
}

/*
 * Set up the inital kernel pagetable.
 *
 * We can construct this by grafting the Xen provided pagetable into
 * head_64.S's preconstructed pagetables.  We copy the Xen L2's into
 * level2_ident_pgt, level2_kernel_pgt and level2_fixmap_pgt.  This
 * means that only the kernel has a physical mapping to start with -
 * but that's enough to get __va working.  We need to fill in the rest
 * of the physical mapping once some sort of allocator has been set
 * up.
 */
static __init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd,
						unsigned long max_pfn)
{
	pud_t *l3;
	pmd_t *l2;

	/* Zap identity mapping */
	init_level4_pgt[0] = __pgd(0);

	/* Pre-constructed entries are in pfn, so convert to mfn */
	convert_pfn_mfn(init_level4_pgt);
	convert_pfn_mfn(level3_ident_pgt);
	convert_pfn_mfn(level3_kernel_pgt);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map)].pud);

	memcpy(level2_ident_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);
	memcpy(level2_kernel_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	l3 = m2v(pgd[pgd_index(__START_KERNEL_map + PMD_SIZE)].pgd);
	l2 = m2v(l3[pud_index(__START_KERNEL_map + PMD_SIZE)].pud);
	memcpy(level2_fixmap_pgt, l2, sizeof(pmd_t) * PTRS_PER_PMD);

	/* Set up identity map */
	xen_map_identity_early(level2_ident_pgt, max_pfn);

	/* Make pagetable pieces RO */
	set_page_prot(init_level4_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_ident_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level3_user_vsyscall, PAGE_KERNEL_RO);
	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(level2_fixmap_pgt, PAGE_KERNEL_RO);

	/* Pin down new L4 */
	pin_pagetable_pfn(MMUEXT_PIN_L4_TABLE,
			  PFN_DOWN(__pa_symbol(init_level4_pgt)));

	/* Unpin Xen-provided one */
	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	/* Switch over */
	pgd = init_level4_pgt;

	/*
	 * At this stage there can be no user pgd, and no page
	 * structure to attach it to, so make sure we just set kernel
	 * pgd.
	 */
	xen_mc_batch();
	__xen_write_cr3(true, __pa(pgd));
	xen_mc_issue(PARAVIRT_LAZY_CPU);

	reserve_early(__pa(xen_start_info->pt_base),
		      __pa(xen_start_info->pt_base +
			   xen_start_info->nr_pt_frames * PAGE_SIZE),
		      "XEN PAGETABLES");

	return pgd;
}
#else	/* !CONFIG_X86_64 */
static pmd_t level2_kernel_pgt[PTRS_PER_PMD] __page_aligned_bss;

static __init pgd_t *xen_setup_kernel_pagetable(pgd_t *pgd,
						unsigned long max_pfn)
{
	pmd_t *kernel_pmd;

	init_pg_tables_start = __pa(pgd);
	init_pg_tables_end = __pa(pgd) + xen_start_info->nr_pt_frames*PAGE_SIZE;
	max_pfn_mapped = PFN_DOWN(init_pg_tables_end + 512*1024);

	kernel_pmd = m2v(pgd[KERNEL_PGD_BOUNDARY].pgd);
	memcpy(level2_kernel_pgt, kernel_pmd, sizeof(pmd_t) * PTRS_PER_PMD);

	xen_map_identity_early(level2_kernel_pgt, max_pfn);

	memcpy(swapper_pg_dir, pgd, sizeof(pgd_t) * PTRS_PER_PGD);
	set_pgd(&swapper_pg_dir[KERNEL_PGD_BOUNDARY],
			__pgd(__pa(level2_kernel_pgt) | _PAGE_PRESENT));

	set_page_prot(level2_kernel_pgt, PAGE_KERNEL_RO);
	set_page_prot(swapper_pg_dir, PAGE_KERNEL_RO);
	set_page_prot(empty_zero_page, PAGE_KERNEL_RO);

	pin_pagetable_pfn(MMUEXT_UNPIN_TABLE, PFN_DOWN(__pa(pgd)));

	xen_write_cr3(__pa(swapper_pg_dir));

	pin_pagetable_pfn(MMUEXT_PIN_L3_TABLE, PFN_DOWN(__pa(swapper_pg_dir)));

	return swapper_pg_dir;
}
#endif	/* CONFIG_X86_64 */

/* First C function to be called on Xen boot */
asmlinkage void __init xen_start_kernel(void)
{
	pgd_t *pgd;

	if (!xen_start_info)
		return;

	xen_domain_type = XEN_PV_DOMAIN;

	BUG_ON(memcmp(xen_start_info->magic, "xen-3", 5) != 0);

	xen_setup_features();

	/* Install Xen paravirt ops */
	pv_info = xen_info;
	pv_init_ops = xen_init_ops;
	pv_time_ops = xen_time_ops;
	pv_cpu_ops = xen_cpu_ops;
	pv_apic_ops = xen_apic_ops;
	pv_mmu_ops = xen_mmu_ops;

	xen_init_irq_ops();

#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * set up the basic apic ops.
	 */
	apic_ops = &xen_basic_apic_ops;
#endif

	if (xen_feature(XENFEAT_mmu_pt_update_preserve_ad)) {
		pv_mmu_ops.ptep_modify_prot_start = xen_ptep_modify_prot_start;
		pv_mmu_ops.ptep_modify_prot_commit = xen_ptep_modify_prot_commit;
	}

	machine_ops = xen_machine_ops;

#ifdef CONFIG_X86_64
	/* Disable until direct per-cpu data access. */
	have_vcpu_info_placement = 0;
	x86_64_init_pda();
#endif

	xen_smp_init();

	/* Get mfn list */
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		xen_build_dynamic_phys_to_machine();

	pgd = (pgd_t *)xen_start_info->pt_base;

	/* Prevent unwanted bits from being set in PTEs. */
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (!xen_initial_domain())
		__supported_pte_mask &= ~(_PAGE_PWT | _PAGE_PCD);

	/* Don't do the full vcpu_info placement stuff until we have a
	   possible map and a non-dummy shared_info. */
	per_cpu(xen_vcpu, 0) = &HYPERVISOR_shared_info->vcpu_info[0];

	local_irq_disable();
	early_boot_irqs_off();

	xen_raw_console_write("mapping kernel into physical memory\n");
	pgd = xen_setup_kernel_pagetable(pgd, xen_start_info->nr_pages);

	init_mm.pgd = pgd;

	/* keep using Xen gdt for now; no urgent need to change it */

	pv_info.kernel_rpl = 1;
	if (xen_feature(XENFEAT_supervisor_mode_kernel))
		pv_info.kernel_rpl = 0;

	/* set the limit of our address space */
	xen_reserve_top();

#ifdef CONFIG_X86_32
	/* set up basic CPUID stuff */
	cpu_detect(&new_cpu_data);
	new_cpu_data.hard_math = 1;
	new_cpu_data.x86_capability[0] = cpuid_edx(1);
#endif

	/* Poke various useful things into boot_params */
	boot_params.hdr.type_of_loader = (9 << 4) | 0;
	boot_params.hdr.ramdisk_image = xen_start_info->mod_start
		? __pa(xen_start_info->mod_start) : 0;
	boot_params.hdr.ramdisk_size = xen_start_info->mod_len;
	boot_params.hdr.cmd_line_ptr = __pa(xen_start_info->cmd_line);

	if (!xen_initial_domain()) {
		add_preferred_console("xenboot", 0, NULL);
		add_preferred_console("tty", 0, NULL);
		add_preferred_console("hvc", 0, NULL);
	}

	xen_raw_console_write("about to get started...\n");

	/* Start the world */
#ifdef CONFIG_X86_32
	i386_start_kernel();
#else
	x86_64_start_reservations((char *)__pa_symbol(&boot_params));
#endif
}
