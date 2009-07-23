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
#include <linux/kprobes.h>
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
#include <asm/proto.h>
#include <asm/msr-index.h>
#include <asm/traps.h>
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

struct start_info *xen_start_info;
EXPORT_SYMBOL_GPL(xen_start_info);

struct shared_info xen_dummy_shared_info;

void *xen_initial_gdt;

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
static int have_vcpu_info_placement = 1;

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

	info.mfn = arbitrary_virt_to_mfn(vcpup);
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

static __read_mostly unsigned int cpuid_leaf1_edx_mask = ~0;
static __read_mostly unsigned int cpuid_leaf1_ecx_mask = ~0;

static void xen_cpuid(unsigned int *ax, unsigned int *bx,
		      unsigned int *cx, unsigned int *dx)
{
	unsigned maskecx = ~0;
	unsigned maskedx = ~0;

	/*
	 * Mask out inconvenient features, to try and disable as many
	 * unsupported kernel subsystems as possible.
	 */
	if (*ax == 1) {
		maskecx = cpuid_leaf1_ecx_mask;
		maskedx = cpuid_leaf1_edx_mask;
	}

	asm(XEN_EMULATE_PREFIX "cpuid"
		: "=a" (*ax),
		  "=b" (*bx),
		  "=c" (*cx),
		  "=d" (*dx)
		: "0" (*ax), "2" (*cx));

	*cx &= maskecx;
	*dx &= maskedx;
}

static __init void xen_init_cpuid_mask(void)
{
	unsigned int ax, bx, cx, dx;

	cpuid_leaf1_edx_mask =
		~((1 << X86_FEATURE_MCE)  |  /* disable MCE */
		  (1 << X86_FEATURE_MCA)  |  /* disable MCA */
		  (1 << X86_FEATURE_ACC));   /* thermal monitoring */

	if (!xen_initial_domain())
		cpuid_leaf1_edx_mask &=
			~((1 << X86_FEATURE_APIC) |  /* disable local APIC */
			  (1 << X86_FEATURE_ACPI));  /* disable ACPI */

	ax = 1;
	xen_cpuid(&ax, &bx, &cx, &dx);

	/* cpuid claims we support xsave; try enabling it to see what happens */
	if (cx & (1 << (X86_FEATURE_XSAVE % 32))) {
		unsigned long cr4;

		set_in_cr4(X86_CR4_OSXSAVE);
		
		cr4 = read_cr4();

		if ((cr4 & X86_CR4_OSXSAVE) == 0)
			cpuid_leaf1_ecx_mask &= ~(1 << (X86_FEATURE_XSAVE % 32));

		clear_in_cr4(X86_CR4_OSXSAVE);
	}
}

static void xen_set_debugreg(int reg, unsigned long val)
{
	HYPERVISOR_set_debugreg(reg, val);
}

static unsigned long xen_get_debugreg(int reg)
{
	return HYPERVISOR_get_debugreg(reg);
}

static void xen_end_context_switch(struct task_struct *next)
{
	xen_mc_flush();
	paravirt_end_context_switch(next);
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
	unsigned long va = dtr->address;
	unsigned int size = dtr->size + 1;
	unsigned pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	unsigned long frames[pages];
	int f;

	/* A GDT can be up to 64k in size, which corresponds to 8192
	   8-byte entries, or 16 4k pages.. */

	BUG_ON(size > 65536);
	BUG_ON(va & ~PAGE_MASK);

	for (f = 0; va < dtr->address + size; va += PAGE_SIZE, f++) {
		int level;
		pte_t *ptep = lookup_address(va, &level);
		unsigned long pfn, mfn;
		void *virt;

		BUG_ON(ptep == NULL);

		pfn = pte_pfn(*ptep);
		mfn = pfn_to_mfn(pfn);
		virt = __va(PFN_PHYS(pfn));

		frames[f] = mfn;

		make_lowmem_page_readonly((void *)va);
		make_lowmem_page_readonly(virt);
	}

	if (HYPERVISOR_set_gdt(frames, size / sizeof(struct desc_struct)))
		BUG();
}

static void load_TLS_descriptor(struct thread_struct *t,
				unsigned int cpu, unsigned int i)
{
	struct desc_struct *gdt = get_cpu_gdt_table(cpu);
	xmaddr_t maddr = arbitrary_virt_to_machine(&gdt[GDT_ENTRY_TLS_MIN+i]);
	struct multicall_space mc = __xen_mc_entry(0);

	MULTI_update_descriptor(mc.mc, maddr.maddr, t->tls_array[i]);
}

static void xen_load_tls(struct thread_struct *t, unsigned int cpu)
{
	/*
	 * XXX sleazy hack: If we're being called in a lazy-cpu zone
	 * and lazy gs handling is enabled, it means we're in a
	 * context switch, and %gs has just been saved.  This means we
	 * can zero it out to prevent faults on exit from the
	 * hypervisor if the next process has no %gs.  Either way, it
	 * has been saved, and the new value will get loaded properly.
	 * This will go away as soon as Xen has been modified to not
	 * save/restore %gs for normal hypercalls.
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
		lazy_load_gs(0);
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
	unsigned long addr;

	if (val->type != GATE_TRAP && val->type != GATE_INTERRUPT)
		return 0;

	info->vector = vector;

	addr = gate_offset(*val);
#ifdef CONFIG_X86_64
	/*
	 * Look for known traps using IST, and substitute them
	 * appropriately.  The debugger ones are the only ones we care
	 * about.  Xen will handle faults like double_fault and
	 * machine_check, so we should never see them.  Warn if
	 * there's an unexpected IST-using fault handler.
	 */
	if (addr == (unsigned long)debug)
		addr = (unsigned long)xen_debug;
	else if (addr == (unsigned long)int3)
		addr = (unsigned long)xen_int3;
	else if (addr == (unsigned long)stack_segment)
		addr = (unsigned long)xen_stack_segment;
	else if (addr == (unsigned long)double_fault ||
		 addr == (unsigned long)nmi) {
		/* Don't need to handle these */
		return 0;
#ifdef CONFIG_X86_MCE
	} else if (addr == (unsigned long)machine_check) {
		return 0;
#endif
	} else {
		/* Some other trap using IST? */
		if (WARN_ON(val->ist != 0))
			return 0;
	}
#endif	/* CONFIG_X86_64 */
	info->address = addr;

	info->cs = gate_segment(*val);
	info->flags = val->dpl;
	/* interrupt gates clear IF */
	if (val->type == GATE_INTERRUPT)
		info->flags |= 1 << 2;

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
		xmaddr_t maddr = arbitrary_virt_to_machine(&dt[entry]);

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

static void set_xen_basic_apic_ops(void)
{
	apic->read = xen_apic_read;
	apic->write = xen_apic_write;
	apic->icr_read = xen_apic_icr_read;
	apic->icr_write = xen_apic_icr_write;
	apic->wait_icr_idle = xen_apic_wait_icr_idle;
	apic->safe_wait_icr_idle = xen_safe_apic_wait_icr_idle;
}

#endif


static void xen_clts(void)
{
	struct multicall_space mcs;

	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static DEFINE_PER_CPU(unsigned long, xen_cr0_value);

static unsigned long xen_read_cr0(void)
{
	unsigned long cr0 = percpu_read(xen_cr0_value);

	if (unlikely(cr0 == 0)) {
		cr0 = native_read_cr0();
		percpu_write(xen_cr0_value, cr0);
	}

	return cr0;
}

static void xen_write_cr0(unsigned long cr0)
{
	struct multicall_space mcs;

	percpu_write(xen_cr0_value, cr0);

	/* Only pay attention to cr0.TS; everything else is
	   ignored. */
	mcs = xen_mc_entry(0);

	MULTI_fpu_taskswitch(mcs.mc, (cr0 & X86_CR0_TS) != 0);

	xen_mc_issue(PARAVIRT_LAZY_CPU);
}

static void xen_write_cr4(unsigned long cr4)
{
	cr4 &= ~X86_CR4_PGE;
	cr4 &= ~X86_CR4_PSE;

	native_write_cr4(cr4);
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

		pv_irq_ops.save_fl = __PV_IS_CALLEE_SAVE(xen_save_fl_direct);
		pv_irq_ops.restore_fl = __PV_IS_CALLEE_SAVE(xen_restore_fl_direct);
		pv_irq_ops.irq_disable = __PV_IS_CALLEE_SAVE(xen_irq_disable_direct);
		pv_irq_ops.irq_enable = __PV_IS_CALLEE_SAVE(xen_irq_enable_direct);
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

	.read_cr0 = xen_read_cr0,
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

	.start_context_switch = paravirt_start_context_switch,
	.end_context_switch = xen_end_context_switch,
};

static const struct pv_apic_ops xen_apic_ops __initdata = {
#ifdef CONFIG_X86_LOCAL_APIC
	.setup_boot_clock = paravirt_nop,
	.setup_secondary_clock = paravirt_nop,
	.startup_ipi_hook = paravirt_nop,
#endif
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

	xen_init_cpuid_mask();

#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * set up the basic apic ops.
	 */
	set_xen_basic_apic_ops();
#endif

	if (xen_feature(XENFEAT_mmu_pt_update_preserve_ad)) {
		pv_mmu_ops.ptep_modify_prot_start = xen_ptep_modify_prot_start;
		pv_mmu_ops.ptep_modify_prot_commit = xen_ptep_modify_prot_commit;
	}

	machine_ops = xen_machine_ops;

#ifdef CONFIG_X86_64
	/*
	 * Setup percpu state.  We only need to do this for 64-bit
	 * because 32-bit already has %fs set properly.
	 */
	load_percpu_segment(0);
#endif
	/*
	 * The only reliable way to retain the initial address of the
	 * percpu gdt_page is to remember it here, so we can go and
	 * mark it RW later, when the initial percpu area is freed.
	 */
	xen_initial_gdt = &per_cpu(gdt_page, 0);

	xen_smp_init();

	/* Get mfn list */
	if (!xen_feature(XENFEAT_auto_translated_physmap))
		xen_build_dynamic_phys_to_machine();

	pgd = (pgd_t *)xen_start_info->pt_base;

	/* Prevent unwanted bits from being set in PTEs. */
	__supported_pte_mask &= ~_PAGE_GLOBAL;
	if (!xen_initial_domain())
		__supported_pte_mask &= ~(_PAGE_PWT | _PAGE_PCD);

#ifdef CONFIG_X86_64
	/* Work out if we support NX */
	check_efer();
#endif

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
