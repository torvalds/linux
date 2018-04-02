// SPDX-License-Identifier: GPL-2.0
/*
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2001, 2002 Andi Kleen, SuSE Labs.
 *  Copyright (C) 2008-2009, Red Hat Inc., Ingo Molnar
 */
#include <linux/sched.h>		/* test_thread_flag(), ...	*/
#include <linux/sched/task_stack.h>	/* task_stack_*(), ...		*/
#include <linux/kdebug.h>		/* oops_begin/end, ...		*/
#include <linux/extable.h>		/* search_exception_tables	*/
#include <linux/bootmem.h>		/* max_low_pfn			*/
#include <linux/kprobes.h>		/* NOKPROBE_SYMBOL, ...		*/
#include <linux/mmiotrace.h>		/* kmmio_handler, ...		*/
#include <linux/perf_event.h>		/* perf_sw_event		*/
#include <linux/hugetlb.h>		/* hstate_index_to_shift	*/
#include <linux/prefetch.h>		/* prefetchw			*/
#include <linux/context_tracking.h>	/* exception_enter(), ...	*/
#include <linux/uaccess.h>		/* faulthandler_disabled()	*/

#include <asm/cpufeature.h>		/* boot_cpu_has, ...		*/
#include <asm/traps.h>			/* dotraplinkage, ...		*/
#include <asm/pgalloc.h>		/* pgd_*(), ...			*/
#include <asm/fixmap.h>			/* VSYSCALL_ADDR		*/
#include <asm/vsyscall.h>		/* emulate_vsyscall		*/
#include <asm/vm86.h>			/* struct vm86			*/
#include <asm/mmu_context.h>		/* vma_pkey()			*/

#define CREATE_TRACE_POINTS
#include <asm/trace/exceptions.h>

/*
 * Returns 0 if mmiotrace is disabled, or if the fault is not
 * handled by mmiotrace:
 */
static nokprobe_inline int
kmmio_fault(struct pt_regs *regs, unsigned long addr)
{
	if (unlikely(is_kmmio_active()))
		if (kmmio_handler(regs, addr) == 1)
			return -1;
	return 0;
}

static nokprobe_inline int kprobes_fault(struct pt_regs *regs)
{
	int ret = 0;

	/* kprobe_running() needs smp_processor_id() */
	if (kprobes_built_in() && !user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 14))
			ret = 1;
		preempt_enable();
	}

	return ret;
}

/*
 * Prefetch quirks:
 *
 * 32-bit mode:
 *
 *   Sometimes AMD Athlon/Opteron CPUs report invalid exceptions on prefetch.
 *   Check that here and ignore it.
 *
 * 64-bit mode:
 *
 *   Sometimes the CPU reports invalid exceptions on prefetch.
 *   Check that here and ignore it.
 *
 * Opcode checker based on code by Richard Brunner.
 */
static inline int
check_prefetch_opcode(struct pt_regs *regs, unsigned char *instr,
		      unsigned char opcode, int *prefetch)
{
	unsigned char instr_hi = opcode & 0xf0;
	unsigned char instr_lo = opcode & 0x0f;

	switch (instr_hi) {
	case 0x20:
	case 0x30:
		/*
		 * Values 0x26,0x2E,0x36,0x3E are valid x86 prefixes.
		 * In X86_64 long mode, the CPU will signal invalid
		 * opcode if some of these prefixes are present so
		 * X86_64 will never get here anyway
		 */
		return ((instr_lo & 7) == 0x6);
#ifdef CONFIG_X86_64
	case 0x40:
		/*
		 * In AMD64 long mode 0x40..0x4F are valid REX prefixes
		 * Need to figure out under what instruction mode the
		 * instruction was issued. Could check the LDT for lm,
		 * but for now it's good enough to assume that long
		 * mode only uses well known segments or kernel.
		 */
		return (!user_mode(regs) || user_64bit_mode(regs));
#endif
	case 0x60:
		/* 0x64 thru 0x67 are valid prefixes in all modes. */
		return (instr_lo & 0xC) == 0x4;
	case 0xF0:
		/* 0xF0, 0xF2, 0xF3 are valid prefixes in all modes. */
		return !instr_lo || (instr_lo>>1) == 1;
	case 0x00:
		/* Prefetch instruction is 0x0F0D or 0x0F18 */
		if (probe_kernel_address(instr, opcode))
			return 0;

		*prefetch = (instr_lo == 0xF) &&
			(opcode == 0x0D || opcode == 0x18);
		return 0;
	default:
		return 0;
	}
}

static int
is_prefetch(struct pt_regs *regs, unsigned long error_code, unsigned long addr)
{
	unsigned char *max_instr;
	unsigned char *instr;
	int prefetch = 0;

	/*
	 * If it was a exec (instruction fetch) fault on NX page, then
	 * do not ignore the fault:
	 */
	if (error_code & X86_PF_INSTR)
		return 0;

	instr = (void *)convert_ip_to_linear(current, regs);
	max_instr = instr + 15;

	if (user_mode(regs) && instr >= (unsigned char *)TASK_SIZE_MAX)
		return 0;

	while (instr < max_instr) {
		unsigned char opcode;

		if (probe_kernel_address(instr, opcode))
			break;

		instr++;

		if (!check_prefetch_opcode(regs, instr, opcode, &prefetch))
			break;
	}
	return prefetch;
}

/*
 * A protection key fault means that the PKRU value did not allow
 * access to some PTE.  Userspace can figure out what PKRU was
 * from the XSAVE state, and this function fills out a field in
 * siginfo so userspace can discover which protection key was set
 * on the PTE.
 *
 * If we get here, we know that the hardware signaled a X86_PF_PK
 * fault and that there was a VMA once we got in the fault
 * handler.  It does *not* guarantee that the VMA we find here
 * was the one that we faulted on.
 *
 * 1. T1   : mprotect_key(foo, PAGE_SIZE, pkey=4);
 * 2. T1   : set PKRU to deny access to pkey=4, touches page
 * 3. T1   : faults...
 * 4.    T2: mprotect_key(foo, PAGE_SIZE, pkey=5);
 * 5. T1   : enters fault handler, takes mmap_sem, etc...
 * 6. T1   : reaches here, sees vma_pkey(vma)=5, when we really
 *	     faulted on a pte with its pkey=4.
 */
static void fill_sig_info_pkey(int si_signo, int si_code, siginfo_t *info,
		u32 *pkey)
{
	/* This is effectively an #ifdef */
	if (!boot_cpu_has(X86_FEATURE_OSPKE))
		return;

	/* Fault not from Protection Keys: nothing to do */
	if ((si_code != SEGV_PKUERR) || (si_signo != SIGSEGV))
		return;
	/*
	 * force_sig_info_fault() is called from a number of
	 * contexts, some of which have a VMA and some of which
	 * do not.  The X86_PF_PK handing happens after we have a
	 * valid VMA, so we should never reach this without a
	 * valid VMA.
	 */
	if (!pkey) {
		WARN_ONCE(1, "PKU fault with no VMA passed in");
		info->si_pkey = 0;
		return;
	}
	/*
	 * si_pkey should be thought of as a strong hint, but not
	 * absolutely guranteed to be 100% accurate because of
	 * the race explained above.
	 */
	info->si_pkey = *pkey;
}

static void
force_sig_info_fault(int si_signo, int si_code, unsigned long address,
		     struct task_struct *tsk, u32 *pkey, int fault)
{
	unsigned lsb = 0;
	siginfo_t info;

	info.si_signo	= si_signo;
	info.si_errno	= 0;
	info.si_code	= si_code;
	info.si_addr	= (void __user *)address;
	if (fault & VM_FAULT_HWPOISON_LARGE)
		lsb = hstate_index_to_shift(VM_FAULT_GET_HINDEX(fault)); 
	if (fault & VM_FAULT_HWPOISON)
		lsb = PAGE_SHIFT;
	info.si_addr_lsb = lsb;

	fill_sig_info_pkey(si_signo, si_code, &info, pkey);

	force_sig_info(si_signo, &info, tsk);
}

DEFINE_SPINLOCK(pgd_lock);
LIST_HEAD(pgd_list);

#ifdef CONFIG_X86_32
static inline pmd_t *vmalloc_sync_one(pgd_t *pgd, unsigned long address)
{
	unsigned index = pgd_index(address);
	pgd_t *pgd_k;
	p4d_t *p4d, *p4d_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	pgd += index;
	pgd_k = init_mm.pgd + index;

	if (!pgd_present(*pgd_k))
		return NULL;

	/*
	 * set_pgd(pgd, *pgd_k); here would be useless on PAE
	 * and redundant with the set_pmd() on non-PAE. As would
	 * set_p4d/set_pud.
	 */
	p4d = p4d_offset(pgd, address);
	p4d_k = p4d_offset(pgd_k, address);
	if (!p4d_present(*p4d_k))
		return NULL;

	pud = pud_offset(p4d, address);
	pud_k = pud_offset(p4d_k, address);
	if (!pud_present(*pud_k))
		return NULL;

	pmd = pmd_offset(pud, address);
	pmd_k = pmd_offset(pud_k, address);
	if (!pmd_present(*pmd_k))
		return NULL;

	if (!pmd_present(*pmd))
		set_pmd(pmd, *pmd_k);
	else
		BUG_ON(pmd_page(*pmd) != pmd_page(*pmd_k));

	return pmd_k;
}

void vmalloc_sync_all(void)
{
	unsigned long address;

	if (SHARED_KERNEL_PMD)
		return;

	for (address = VMALLOC_START & PMD_MASK;
	     address >= TASK_SIZE_MAX && address < FIXADDR_TOP;
	     address += PMD_SIZE) {
		struct page *page;

		spin_lock(&pgd_lock);
		list_for_each_entry(page, &pgd_list, lru) {
			spinlock_t *pgt_lock;
			pmd_t *ret;

			/* the pgt_lock only for Xen */
			pgt_lock = &pgd_page_get_mm(page)->page_table_lock;

			spin_lock(pgt_lock);
			ret = vmalloc_sync_one(page_address(page), address);
			spin_unlock(pgt_lock);

			if (!ret)
				break;
		}
		spin_unlock(&pgd_lock);
	}
}

/*
 * 32-bit:
 *
 *   Handle a fault on the vmalloc or module mapping area
 */
static noinline int vmalloc_fault(unsigned long address)
{
	unsigned long pgd_paddr;
	pmd_t *pmd_k;
	pte_t *pte_k;

	/* Make sure we are in vmalloc area: */
	if (!(address >= VMALLOC_START && address < VMALLOC_END))
		return -1;

	WARN_ON_ONCE(in_nmi());

	/*
	 * Synchronize this task's top level page-table
	 * with the 'reference' page table.
	 *
	 * Do _not_ use "current" here. We might be inside
	 * an interrupt in the middle of a task switch..
	 */
	pgd_paddr = read_cr3_pa();
	pmd_k = vmalloc_sync_one(__va(pgd_paddr), address);
	if (!pmd_k)
		return -1;

	if (pmd_large(*pmd_k))
		return 0;

	pte_k = pte_offset_kernel(pmd_k, address);
	if (!pte_present(*pte_k))
		return -1;

	return 0;
}
NOKPROBE_SYMBOL(vmalloc_fault);

/*
 * Did it hit the DOS screen memory VA from vm86 mode?
 */
static inline void
check_v8086_mode(struct pt_regs *regs, unsigned long address,
		 struct task_struct *tsk)
{
#ifdef CONFIG_VM86
	unsigned long bit;

	if (!v8086_mode(regs) || !tsk->thread.vm86)
		return;

	bit = (address - 0xA0000) >> PAGE_SHIFT;
	if (bit < 32)
		tsk->thread.vm86->screen_bitmap |= 1 << bit;
#endif
}

static bool low_pfn(unsigned long pfn)
{
	return pfn < max_low_pfn;
}

static void dump_pagetable(unsigned long address)
{
	pgd_t *base = __va(read_cr3_pa());
	pgd_t *pgd = &base[pgd_index(address)];
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

#ifdef CONFIG_X86_PAE
	pr_info("*pdpt = %016Lx ", pgd_val(*pgd));
	if (!low_pfn(pgd_val(*pgd) >> PAGE_SHIFT) || !pgd_present(*pgd))
		goto out;
#define pr_pde pr_cont
#else
#define pr_pde pr_info
#endif
	p4d = p4d_offset(pgd, address);
	pud = pud_offset(p4d, address);
	pmd = pmd_offset(pud, address);
	pr_pde("*pde = %0*Lx ", sizeof(*pmd) * 2, (u64)pmd_val(*pmd));
#undef pr_pde

	/*
	 * We must not directly access the pte in the highpte
	 * case if the page table is located in highmem.
	 * And let's rather not kmap-atomic the pte, just in case
	 * it's allocated already:
	 */
	if (!low_pfn(pmd_pfn(*pmd)) || !pmd_present(*pmd) || pmd_large(*pmd))
		goto out;

	pte = pte_offset_kernel(pmd, address);
	pr_cont("*pte = %0*Lx ", sizeof(*pte) * 2, (u64)pte_val(*pte));
out:
	pr_cont("\n");
}

#else /* CONFIG_X86_64: */

void vmalloc_sync_all(void)
{
	sync_global_pgds(VMALLOC_START & PGDIR_MASK, VMALLOC_END);
}

/*
 * 64-bit:
 *
 *   Handle a fault on the vmalloc area
 */
static noinline int vmalloc_fault(unsigned long address)
{
	pgd_t *pgd, *pgd_ref;
	p4d_t *p4d, *p4d_ref;
	pud_t *pud, *pud_ref;
	pmd_t *pmd, *pmd_ref;
	pte_t *pte, *pte_ref;

	/* Make sure we are in vmalloc area: */
	if (!(address >= VMALLOC_START && address < VMALLOC_END))
		return -1;

	WARN_ON_ONCE(in_nmi());

	/*
	 * Copy kernel mappings over when needed. This can also
	 * happen within a race in page table update. In the later
	 * case just flush:
	 */
	pgd = (pgd_t *)__va(read_cr3_pa()) + pgd_index(address);
	pgd_ref = pgd_offset_k(address);
	if (pgd_none(*pgd_ref))
		return -1;

	if (CONFIG_PGTABLE_LEVELS > 4) {
		if (pgd_none(*pgd)) {
			set_pgd(pgd, *pgd_ref);
			arch_flush_lazy_mmu_mode();
		} else {
			BUG_ON(pgd_page_vaddr(*pgd) != pgd_page_vaddr(*pgd_ref));
		}
	}

	/* With 4-level paging, copying happens on the p4d level. */
	p4d = p4d_offset(pgd, address);
	p4d_ref = p4d_offset(pgd_ref, address);
	if (p4d_none(*p4d_ref))
		return -1;

	if (p4d_none(*p4d) && CONFIG_PGTABLE_LEVELS == 4) {
		set_p4d(p4d, *p4d_ref);
		arch_flush_lazy_mmu_mode();
	} else {
		BUG_ON(p4d_pfn(*p4d) != p4d_pfn(*p4d_ref));
	}

	/*
	 * Below here mismatches are bugs because these lower tables
	 * are shared:
	 */
	BUILD_BUG_ON(CONFIG_PGTABLE_LEVELS < 4);

	pud = pud_offset(p4d, address);
	pud_ref = pud_offset(p4d_ref, address);
	if (pud_none(*pud_ref))
		return -1;

	if (pud_none(*pud) || pud_pfn(*pud) != pud_pfn(*pud_ref))
		BUG();

	if (pud_large(*pud))
		return 0;

	pmd = pmd_offset(pud, address);
	pmd_ref = pmd_offset(pud_ref, address);
	if (pmd_none(*pmd_ref))
		return -1;

	if (pmd_none(*pmd) || pmd_pfn(*pmd) != pmd_pfn(*pmd_ref))
		BUG();

	if (pmd_large(*pmd))
		return 0;

	pte_ref = pte_offset_kernel(pmd_ref, address);
	if (!pte_present(*pte_ref))
		return -1;

	pte = pte_offset_kernel(pmd, address);

	/*
	 * Don't use pte_page here, because the mappings can point
	 * outside mem_map, and the NUMA hash lookup cannot handle
	 * that:
	 */
	if (!pte_present(*pte) || pte_pfn(*pte) != pte_pfn(*pte_ref))
		BUG();

	return 0;
}
NOKPROBE_SYMBOL(vmalloc_fault);

#ifdef CONFIG_CPU_SUP_AMD
static const char errata93_warning[] =
KERN_ERR 
"******* Your BIOS seems to not contain a fix for K8 errata #93\n"
"******* Working around it, but it may cause SEGVs or burn power.\n"
"******* Please consider a BIOS update.\n"
"******* Disabling USB legacy in the BIOS may also help.\n";
#endif

/*
 * No vm86 mode in 64-bit mode:
 */
static inline void
check_v8086_mode(struct pt_regs *regs, unsigned long address,
		 struct task_struct *tsk)
{
}

static int bad_address(void *p)
{
	unsigned long dummy;

	return probe_kernel_address((unsigned long *)p, dummy);
}

static void dump_pagetable(unsigned long address)
{
	pgd_t *base = __va(read_cr3_pa());
	pgd_t *pgd = base + pgd_index(address);
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;

	if (bad_address(pgd))
		goto bad;

	pr_info("PGD %lx ", pgd_val(*pgd));

	if (!pgd_present(*pgd))
		goto out;

	p4d = p4d_offset(pgd, address);
	if (bad_address(p4d))
		goto bad;

	pr_cont("P4D %lx ", p4d_val(*p4d));
	if (!p4d_present(*p4d) || p4d_large(*p4d))
		goto out;

	pud = pud_offset(p4d, address);
	if (bad_address(pud))
		goto bad;

	pr_cont("PUD %lx ", pud_val(*pud));
	if (!pud_present(*pud) || pud_large(*pud))
		goto out;

	pmd = pmd_offset(pud, address);
	if (bad_address(pmd))
		goto bad;

	pr_cont("PMD %lx ", pmd_val(*pmd));
	if (!pmd_present(*pmd) || pmd_large(*pmd))
		goto out;

	pte = pte_offset_kernel(pmd, address);
	if (bad_address(pte))
		goto bad;

	pr_cont("PTE %lx", pte_val(*pte));
out:
	pr_cont("\n");
	return;
bad:
	pr_info("BAD\n");
}

#endif /* CONFIG_X86_64 */

/*
 * Workaround for K8 erratum #93 & buggy BIOS.
 *
 * BIOS SMM functions are required to use a specific workaround
 * to avoid corruption of the 64bit RIP register on C stepping K8.
 *
 * A lot of BIOS that didn't get tested properly miss this.
 *
 * The OS sees this as a page fault with the upper 32bits of RIP cleared.
 * Try to work around it here.
 *
 * Note we only handle faults in kernel here.
 * Does nothing on 32-bit.
 */
static int is_errata93(struct pt_regs *regs, unsigned long address)
{
#if defined(CONFIG_X86_64) && defined(CONFIG_CPU_SUP_AMD)
	if (boot_cpu_data.x86_vendor != X86_VENDOR_AMD
	    || boot_cpu_data.x86 != 0xf)
		return 0;

	if (address != regs->ip)
		return 0;

	if ((address >> 32) != 0)
		return 0;

	address |= 0xffffffffUL << 32;
	if ((address >= (u64)_stext && address <= (u64)_etext) ||
	    (address >= MODULES_VADDR && address <= MODULES_END)) {
		printk_once(errata93_warning);
		regs->ip = address;
		return 1;
	}
#endif
	return 0;
}

/*
 * Work around K8 erratum #100 K8 in compat mode occasionally jumps
 * to illegal addresses >4GB.
 *
 * We catch this in the page fault handler because these addresses
 * are not reachable. Just detect this case and return.  Any code
 * segment in LDT is compatibility mode.
 */
static int is_errata100(struct pt_regs *regs, unsigned long address)
{
#ifdef CONFIG_X86_64
	if ((regs->cs == __USER32_CS || (regs->cs & (1<<2))) && (address >> 32))
		return 1;
#endif
	return 0;
}

static int is_f00f_bug(struct pt_regs *regs, unsigned long address)
{
#ifdef CONFIG_X86_F00F_BUG
	unsigned long nr;

	/*
	 * Pentium F0 0F C7 C8 bug workaround:
	 */
	if (boot_cpu_has_bug(X86_BUG_F00F)) {
		nr = (address - idt_descr.address) >> 3;

		if (nr == 6) {
			do_invalid_op(regs, 0);
			return 1;
		}
	}
#endif
	return 0;
}

static const char nx_warning[] = KERN_CRIT
"kernel tried to execute NX-protected page - exploit attempt? (uid: %d)\n";
static const char smep_warning[] = KERN_CRIT
"unable to execute userspace code (SMEP?) (uid: %d)\n";

static void
show_fault_oops(struct pt_regs *regs, unsigned long error_code,
		unsigned long address)
{
	if (!oops_may_print())
		return;

	if (error_code & X86_PF_INSTR) {
		unsigned int level;
		pgd_t *pgd;
		pte_t *pte;

		pgd = __va(read_cr3_pa());
		pgd += pgd_index(address);

		pte = lookup_address_in_pgd(pgd, address, &level);

		if (pte && pte_present(*pte) && !pte_exec(*pte))
			printk(nx_warning, from_kuid(&init_user_ns, current_uid()));
		if (pte && pte_present(*pte) && pte_exec(*pte) &&
				(pgd_flags(*pgd) & _PAGE_USER) &&
				(__read_cr4() & X86_CR4_SMEP))
			printk(smep_warning, from_kuid(&init_user_ns, current_uid()));
	}

	printk(KERN_ALERT "BUG: unable to handle kernel ");
	if (address < PAGE_SIZE)
		printk(KERN_CONT "NULL pointer dereference");
	else
		printk(KERN_CONT "paging request");

	printk(KERN_CONT " at %px\n", (void *) address);

	dump_pagetable(address);
}

static noinline void
pgtable_bad(struct pt_regs *regs, unsigned long error_code,
	    unsigned long address)
{
	struct task_struct *tsk;
	unsigned long flags;
	int sig;

	flags = oops_begin();
	tsk = current;
	sig = SIGKILL;

	printk(KERN_ALERT "%s: Corrupted page table at address %lx\n",
	       tsk->comm, address);
	dump_pagetable(address);

	tsk->thread.cr2		= address;
	tsk->thread.trap_nr	= X86_TRAP_PF;
	tsk->thread.error_code	= error_code;

	if (__die("Bad pagetable", regs, error_code))
		sig = 0;

	oops_end(flags, regs, sig);
}

static noinline void
no_context(struct pt_regs *regs, unsigned long error_code,
	   unsigned long address, int signal, int si_code)
{
	struct task_struct *tsk = current;
	unsigned long flags;
	int sig;

	/* Are we prepared to handle this kernel fault? */
	if (fixup_exception(regs, X86_TRAP_PF)) {
		/*
		 * Any interrupt that takes a fault gets the fixup. This makes
		 * the below recursive fault logic only apply to a faults from
		 * task context.
		 */
		if (in_interrupt())
			return;

		/*
		 * Per the above we're !in_interrupt(), aka. task context.
		 *
		 * In this case we need to make sure we're not recursively
		 * faulting through the emulate_vsyscall() logic.
		 */
		if (current->thread.sig_on_uaccess_err && signal) {
			tsk->thread.trap_nr = X86_TRAP_PF;
			tsk->thread.error_code = error_code | X86_PF_USER;
			tsk->thread.cr2 = address;

			/* XXX: hwpoison faults will set the wrong code. */
			force_sig_info_fault(signal, si_code, address,
					     tsk, NULL, 0);
		}

		/*
		 * Barring that, we can do the fixup and be happy.
		 */
		return;
	}

#ifdef CONFIG_VMAP_STACK
	/*
	 * Stack overflow?  During boot, we can fault near the initial
	 * stack in the direct map, but that's not an overflow -- check
	 * that we're in vmalloc space to avoid this.
	 */
	if (is_vmalloc_addr((void *)address) &&
	    (((unsigned long)tsk->stack - 1 - address < PAGE_SIZE) ||
	     address - ((unsigned long)tsk->stack + THREAD_SIZE) < PAGE_SIZE)) {
		unsigned long stack = this_cpu_read(orig_ist.ist[DOUBLEFAULT_STACK]) - sizeof(void *);
		/*
		 * We're likely to be running with very little stack space
		 * left.  It's plausible that we'd hit this condition but
		 * double-fault even before we get this far, in which case
		 * we're fine: the double-fault handler will deal with it.
		 *
		 * We don't want to make it all the way into the oops code
		 * and then double-fault, though, because we're likely to
		 * break the console driver and lose most of the stack dump.
		 */
		asm volatile ("movq %[stack], %%rsp\n\t"
			      "call handle_stack_overflow\n\t"
			      "1: jmp 1b"
			      : ASM_CALL_CONSTRAINT
			      : "D" ("kernel stack overflow (page fault)"),
				"S" (regs), "d" (address),
				[stack] "rm" (stack));
		unreachable();
	}
#endif

	/*
	 * 32-bit:
	 *
	 *   Valid to do another page fault here, because if this fault
	 *   had been triggered by is_prefetch fixup_exception would have
	 *   handled it.
	 *
	 * 64-bit:
	 *
	 *   Hall of shame of CPU/BIOS bugs.
	 */
	if (is_prefetch(regs, error_code, address))
		return;

	if (is_errata93(regs, address))
		return;

	/*
	 * Oops. The kernel tried to access some bad page. We'll have to
	 * terminate things with extreme prejudice:
	 */
	flags = oops_begin();

	show_fault_oops(regs, error_code, address);

	if (task_stack_end_corrupted(tsk))
		printk(KERN_EMERG "Thread overran stack, or stack corrupted\n");

	tsk->thread.cr2		= address;
	tsk->thread.trap_nr	= X86_TRAP_PF;
	tsk->thread.error_code	= error_code;

	sig = SIGKILL;
	if (__die("Oops", regs, error_code))
		sig = 0;

	/* Executive summary in case the body of the oops scrolled away */
	printk(KERN_DEFAULT "CR2: %016lx\n", address);

	oops_end(flags, regs, sig);
}

/*
 * Print out info about fatal segfaults, if the show_unhandled_signals
 * sysctl is set:
 */
static inline void
show_signal_msg(struct pt_regs *regs, unsigned long error_code,
		unsigned long address, struct task_struct *tsk)
{
	if (!unhandled_signal(tsk, SIGSEGV))
		return;

	if (!printk_ratelimit())
		return;

	printk("%s%s[%d]: segfault at %lx ip %px sp %px error %lx",
		task_pid_nr(tsk) > 1 ? KERN_INFO : KERN_EMERG,
		tsk->comm, task_pid_nr(tsk), address,
		(void *)regs->ip, (void *)regs->sp, error_code);

	print_vma_addr(KERN_CONT " in ", regs->ip);

	printk(KERN_CONT "\n");
}

static void
__bad_area_nosemaphore(struct pt_regs *regs, unsigned long error_code,
		       unsigned long address, u32 *pkey, int si_code)
{
	struct task_struct *tsk = current;

	/* User mode accesses just cause a SIGSEGV */
	if (error_code & X86_PF_USER) {
		/*
		 * It's possible to have interrupts off here:
		 */
		local_irq_enable();

		/*
		 * Valid to do another page fault here because this one came
		 * from user space:
		 */
		if (is_prefetch(regs, error_code, address))
			return;

		if (is_errata100(regs, address))
			return;

#ifdef CONFIG_X86_64
		/*
		 * Instruction fetch faults in the vsyscall page might need
		 * emulation.
		 */
		if (unlikely((error_code & X86_PF_INSTR) &&
			     ((address & ~0xfff) == VSYSCALL_ADDR))) {
			if (emulate_vsyscall(regs, address))
				return;
		}
#endif

		/*
		 * To avoid leaking information about the kernel page table
		 * layout, pretend that user-mode accesses to kernel addresses
		 * are always protection faults.
		 */
		if (address >= TASK_SIZE_MAX)
			error_code |= X86_PF_PROT;

		if (likely(show_unhandled_signals))
			show_signal_msg(regs, error_code, address, tsk);

		tsk->thread.cr2		= address;
		tsk->thread.error_code	= error_code;
		tsk->thread.trap_nr	= X86_TRAP_PF;

		force_sig_info_fault(SIGSEGV, si_code, address, tsk, pkey, 0);

		return;
	}

	if (is_f00f_bug(regs, address))
		return;

	no_context(regs, error_code, address, SIGSEGV, si_code);
}

static noinline void
bad_area_nosemaphore(struct pt_regs *regs, unsigned long error_code,
		     unsigned long address, u32 *pkey)
{
	__bad_area_nosemaphore(regs, error_code, address, pkey, SEGV_MAPERR);
}

static void
__bad_area(struct pt_regs *regs, unsigned long error_code,
	   unsigned long address,  struct vm_area_struct *vma, int si_code)
{
	struct mm_struct *mm = current->mm;
	u32 pkey;

	if (vma)
		pkey = vma_pkey(vma);

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
	up_read(&mm->mmap_sem);

	__bad_area_nosemaphore(regs, error_code, address,
			       (vma) ? &pkey : NULL, si_code);
}

static noinline void
bad_area(struct pt_regs *regs, unsigned long error_code, unsigned long address)
{
	__bad_area(regs, error_code, address, NULL, SEGV_MAPERR);
}

static inline bool bad_area_access_from_pkeys(unsigned long error_code,
		struct vm_area_struct *vma)
{
	/* This code is always called on the current mm */
	bool foreign = false;

	if (!boot_cpu_has(X86_FEATURE_OSPKE))
		return false;
	if (error_code & X86_PF_PK)
		return true;
	/* this checks permission keys on the VMA: */
	if (!arch_vma_access_permitted(vma, (error_code & X86_PF_WRITE),
				       (error_code & X86_PF_INSTR), foreign))
		return true;
	return false;
}

static noinline void
bad_area_access_error(struct pt_regs *regs, unsigned long error_code,
		      unsigned long address, struct vm_area_struct *vma)
{
	/*
	 * This OSPKE check is not strictly necessary at runtime.
	 * But, doing it this way allows compiler optimizations
	 * if pkeys are compiled out.
	 */
	if (bad_area_access_from_pkeys(error_code, vma))
		__bad_area(regs, error_code, address, vma, SEGV_PKUERR);
	else
		__bad_area(regs, error_code, address, vma, SEGV_ACCERR);
}

static void
do_sigbus(struct pt_regs *regs, unsigned long error_code, unsigned long address,
	  u32 *pkey, unsigned int fault)
{
	struct task_struct *tsk = current;
	int code = BUS_ADRERR;

	/* Kernel mode? Handle exceptions or die: */
	if (!(error_code & X86_PF_USER)) {
		no_context(regs, error_code, address, SIGBUS, BUS_ADRERR);
		return;
	}

	/* User-space => ok to do another page fault: */
	if (is_prefetch(regs, error_code, address))
		return;

	tsk->thread.cr2		= address;
	tsk->thread.error_code	= error_code;
	tsk->thread.trap_nr	= X86_TRAP_PF;

#ifdef CONFIG_MEMORY_FAILURE
	if (fault & (VM_FAULT_HWPOISON|VM_FAULT_HWPOISON_LARGE)) {
		printk(KERN_ERR
	"MCE: Killing %s:%d due to hardware memory corruption fault at %lx\n",
			tsk->comm, tsk->pid, address);
		code = BUS_MCEERR_AR;
	}
#endif
	force_sig_info_fault(SIGBUS, code, address, tsk, pkey, fault);
}

static noinline void
mm_fault_error(struct pt_regs *regs, unsigned long error_code,
	       unsigned long address, u32 *pkey, unsigned int fault)
{
	if (fatal_signal_pending(current) && !(error_code & X86_PF_USER)) {
		no_context(regs, error_code, address, 0, 0);
		return;
	}

	if (fault & VM_FAULT_OOM) {
		/* Kernel mode? Handle exceptions or die: */
		if (!(error_code & X86_PF_USER)) {
			no_context(regs, error_code, address,
				   SIGSEGV, SEGV_MAPERR);
			return;
		}

		/*
		 * We ran out of memory, call the OOM killer, and return the
		 * userspace (which will retry the fault, or kill us if we got
		 * oom-killed):
		 */
		pagefault_out_of_memory();
	} else {
		if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|
			     VM_FAULT_HWPOISON_LARGE))
			do_sigbus(regs, error_code, address, pkey, fault);
		else if (fault & VM_FAULT_SIGSEGV)
			bad_area_nosemaphore(regs, error_code, address, pkey);
		else
			BUG();
	}
}

static int spurious_fault_check(unsigned long error_code, pte_t *pte)
{
	if ((error_code & X86_PF_WRITE) && !pte_write(*pte))
		return 0;

	if ((error_code & X86_PF_INSTR) && !pte_exec(*pte))
		return 0;
	/*
	 * Note: We do not do lazy flushing on protection key
	 * changes, so no spurious fault will ever set X86_PF_PK.
	 */
	if ((error_code & X86_PF_PK))
		return 1;

	return 1;
}

/*
 * Handle a spurious fault caused by a stale TLB entry.
 *
 * This allows us to lazily refresh the TLB when increasing the
 * permissions of a kernel page (RO -> RW or NX -> X).  Doing it
 * eagerly is very expensive since that implies doing a full
 * cross-processor TLB flush, even if no stale TLB entries exist
 * on other processors.
 *
 * Spurious faults may only occur if the TLB contains an entry with
 * fewer permission than the page table entry.  Non-present (P = 0)
 * and reserved bit (R = 1) faults are never spurious.
 *
 * There are no security implications to leaving a stale TLB when
 * increasing the permissions on a page.
 *
 * Returns non-zero if a spurious fault was handled, zero otherwise.
 *
 * See Intel Developer's Manual Vol 3 Section 4.10.4.3, bullet 3
 * (Optional Invalidation).
 */
static noinline int
spurious_fault(unsigned long error_code, unsigned long address)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int ret;

	/*
	 * Only writes to RO or instruction fetches from NX may cause
	 * spurious faults.
	 *
	 * These could be from user or supervisor accesses but the TLB
	 * is only lazily flushed after a kernel mapping protection
	 * change, so user accesses are not expected to cause spurious
	 * faults.
	 */
	if (error_code != (X86_PF_WRITE | X86_PF_PROT) &&
	    error_code != (X86_PF_INSTR | X86_PF_PROT))
		return 0;

	pgd = init_mm.pgd + pgd_index(address);
	if (!pgd_present(*pgd))
		return 0;

	p4d = p4d_offset(pgd, address);
	if (!p4d_present(*p4d))
		return 0;

	if (p4d_large(*p4d))
		return spurious_fault_check(error_code, (pte_t *) p4d);

	pud = pud_offset(p4d, address);
	if (!pud_present(*pud))
		return 0;

	if (pud_large(*pud))
		return spurious_fault_check(error_code, (pte_t *) pud);

	pmd = pmd_offset(pud, address);
	if (!pmd_present(*pmd))
		return 0;

	if (pmd_large(*pmd))
		return spurious_fault_check(error_code, (pte_t *) pmd);

	pte = pte_offset_kernel(pmd, address);
	if (!pte_present(*pte))
		return 0;

	ret = spurious_fault_check(error_code, pte);
	if (!ret)
		return 0;

	/*
	 * Make sure we have permissions in PMD.
	 * If not, then there's a bug in the page tables:
	 */
	ret = spurious_fault_check(error_code, (pte_t *) pmd);
	WARN_ONCE(!ret, "PMD has incorrect permission bits\n");

	return ret;
}
NOKPROBE_SYMBOL(spurious_fault);

int show_unhandled_signals = 1;

static inline int
access_error(unsigned long error_code, struct vm_area_struct *vma)
{
	/* This is only called for the current mm, so: */
	bool foreign = false;

	/*
	 * Read or write was blocked by protection keys.  This is
	 * always an unconditional error and can never result in
	 * a follow-up action to resolve the fault, like a COW.
	 */
	if (error_code & X86_PF_PK)
		return 1;

	/*
	 * Make sure to check the VMA so that we do not perform
	 * faults just to hit a X86_PF_PK as soon as we fill in a
	 * page.
	 */
	if (!arch_vma_access_permitted(vma, (error_code & X86_PF_WRITE),
				       (error_code & X86_PF_INSTR), foreign))
		return 1;

	if (error_code & X86_PF_WRITE) {
		/* write, present and write, not present: */
		if (unlikely(!(vma->vm_flags & VM_WRITE)))
			return 1;
		return 0;
	}

	/* read, present: */
	if (unlikely(error_code & X86_PF_PROT))
		return 1;

	/* read, not present: */
	if (unlikely(!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE))))
		return 1;

	return 0;
}

static int fault_in_kernel_space(unsigned long address)
{
	return address >= TASK_SIZE_MAX;
}

static inline bool smap_violation(int error_code, struct pt_regs *regs)
{
	if (!IS_ENABLED(CONFIG_X86_SMAP))
		return false;

	if (!static_cpu_has(X86_FEATURE_SMAP))
		return false;

	if (error_code & X86_PF_USER)
		return false;

	if (!user_mode(regs) && (regs->flags & X86_EFLAGS_AC))
		return false;

	return true;
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 */
static noinline void
__do_page_fault(struct pt_regs *regs, unsigned long error_code,
		unsigned long address)
{
	struct vm_area_struct *vma;
	struct task_struct *tsk;
	struct mm_struct *mm;
	int fault, major = 0;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
	u32 pkey;

	tsk = current;
	mm = tsk->mm;

	prefetchw(&mm->mmap_sem);

	if (unlikely(kmmio_fault(regs, address)))
		return;

	/*
	 * We fault-in kernel-space virtual memory on-demand. The
	 * 'reference' page table is init_mm.pgd.
	 *
	 * NOTE! We MUST NOT take any locks for this case. We may
	 * be in an interrupt or a critical region, and should
	 * only copy the information from the master page table,
	 * nothing more.
	 *
	 * This verifies that the fault happens in kernel space
	 * (error_code & 4) == 0, and that the fault was not a
	 * protection error (error_code & 9) == 0.
	 */
	if (unlikely(fault_in_kernel_space(address))) {
		if (!(error_code & (X86_PF_RSVD | X86_PF_USER | X86_PF_PROT))) {
			if (vmalloc_fault(address) >= 0)
				return;
		}

		/* Can handle a stale RO->RW TLB: */
		if (spurious_fault(error_code, address))
			return;

		/* kprobes don't want to hook the spurious faults: */
		if (kprobes_fault(regs))
			return;
		/*
		 * Don't take the mm semaphore here. If we fixup a prefetch
		 * fault we could otherwise deadlock:
		 */
		bad_area_nosemaphore(regs, error_code, address, NULL);

		return;
	}

	/* kprobes don't want to hook the spurious faults: */
	if (unlikely(kprobes_fault(regs)))
		return;

	if (unlikely(error_code & X86_PF_RSVD))
		pgtable_bad(regs, error_code, address);

	if (unlikely(smap_violation(error_code, regs))) {
		bad_area_nosemaphore(regs, error_code, address, NULL);
		return;
	}

	/*
	 * If we're in an interrupt, have no user context or are running
	 * in a region with pagefaults disabled then we must not take the fault
	 */
	if (unlikely(faulthandler_disabled() || !mm)) {
		bad_area_nosemaphore(regs, error_code, address, NULL);
		return;
	}

	/*
	 * It's safe to allow irq's after cr2 has been saved and the
	 * vmalloc fault has been handled.
	 *
	 * User-mode registers count as a user access even for any
	 * potential system fault or CPU buglet:
	 */
	if (user_mode(regs)) {
		local_irq_enable();
		error_code |= X86_PF_USER;
		flags |= FAULT_FLAG_USER;
	} else {
		if (regs->flags & X86_EFLAGS_IF)
			local_irq_enable();
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	if (error_code & X86_PF_WRITE)
		flags |= FAULT_FLAG_WRITE;
	if (error_code & X86_PF_INSTR)
		flags |= FAULT_FLAG_INSTRUCTION;

	/*
	 * When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in
	 * the kernel and should generate an OOPS.  Unfortunately, in the
	 * case of an erroneous fault occurring in a code path which already
	 * holds mmap_sem we will deadlock attempting to validate the fault
	 * against the address space.  Luckily the kernel only validly
	 * references user space from well defined areas of code, which are
	 * listed in the exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibility of a
	 * deadlock. Attempt to lock the address space, if we cannot we then
	 * validate the source. If this is invalid we can skip the address
	 * space check, thus avoiding the deadlock:
	 */
	if (unlikely(!down_read_trylock(&mm->mmap_sem))) {
		if (!(error_code & X86_PF_USER) &&
		    !search_exception_tables(regs->ip)) {
			bad_area_nosemaphore(regs, error_code, address, NULL);
			return;
		}
retry:
		down_read(&mm->mmap_sem);
	} else {
		/*
		 * The above down_read_trylock() might have succeeded in
		 * which case we'll have missed the might_sleep() from
		 * down_read():
		 */
		might_sleep();
	}

	vma = find_vma(mm, address);
	if (unlikely(!vma)) {
		bad_area(regs, error_code, address);
		return;
	}
	if (likely(vma->vm_start <= address))
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN))) {
		bad_area(regs, error_code, address);
		return;
	}
	if (error_code & X86_PF_USER) {
		/*
		 * Accessing the stack below %sp is always a bug.
		 * The large cushion allows instructions like enter
		 * and pusha to work. ("enter $65535, $31" pushes
		 * 32 pointers and then decrements %sp by 65535.)
		 */
		if (unlikely(address + 65536 + 32 * sizeof(unsigned long) < regs->sp)) {
			bad_area(regs, error_code, address);
			return;
		}
	}
	if (unlikely(expand_stack(vma, address))) {
		bad_area(regs, error_code, address);
		return;
	}

	/*
	 * Ok, we have a good vm_area for this memory access, so
	 * we can handle it..
	 */
good_area:
	if (unlikely(access_error(error_code, vma))) {
		bad_area_access_error(regs, error_code, address, vma);
		return;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.  Since we never set FAULT_FLAG_RETRY_NOWAIT, if
	 * we get VM_FAULT_RETRY back, the mmap_sem has been unlocked.
	 *
	 * Note that handle_userfault() may also release and reacquire mmap_sem
	 * (and not return with VM_FAULT_RETRY), when returning to userland to
	 * repeat the page fault later with a VM_FAULT_NOPAGE retval
	 * (potentially after handling any pending signal during the return to
	 * userland). The return to userland is identified whenever
	 * FAULT_FLAG_USER|FAULT_FLAG_KILLABLE are both set in flags.
	 * Thus we have to be careful about not touching vma after handling the
	 * fault, so we read the pkey beforehand.
	 */
	pkey = vma_pkey(vma);
	fault = handle_mm_fault(vma, address, flags);
	major |= fault & VM_FAULT_MAJOR;

	/*
	 * If we need to retry the mmap_sem has already been released,
	 * and if there is a fatal signal pending there is no guarantee
	 * that we made any progress. Handle this case first.
	 */
	if (unlikely(fault & VM_FAULT_RETRY)) {
		/* Retry at most once */
		if (flags & FAULT_FLAG_ALLOW_RETRY) {
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;
			if (!fatal_signal_pending(tsk))
				goto retry;
		}

		/* User mode? Just return to handle the fatal exception */
		if (flags & FAULT_FLAG_USER)
			return;

		/* Not returning to user mode? Handle exceptions or die: */
		no_context(regs, error_code, address, SIGBUS, BUS_ADRERR);
		return;
	}

	up_read(&mm->mmap_sem);
	if (unlikely(fault & VM_FAULT_ERROR)) {
		mm_fault_error(regs, error_code, address, &pkey, fault);
		return;
	}

	/*
	 * Major/minor page fault accounting. If any of the events
	 * returned VM_FAULT_MAJOR, we account it as a major fault.
	 */
	if (major) {
		tsk->maj_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, regs, address);
	} else {
		tsk->min_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, regs, address);
	}

	check_v8086_mode(regs, address, tsk);
}
NOKPROBE_SYMBOL(__do_page_fault);

static nokprobe_inline void
trace_page_fault_entries(unsigned long address, struct pt_regs *regs,
			 unsigned long error_code)
{
	if (user_mode(regs))
		trace_page_fault_user(address, regs, error_code);
	else
		trace_page_fault_kernel(address, regs, error_code);
}

/*
 * We must have this function blacklisted from kprobes, tagged with notrace
 * and call read_cr2() before calling anything else. To avoid calling any
 * kind of tracing machinery before we've observed the CR2 value.
 *
 * exception_{enter,exit}() contains all sorts of tracepoints.
 */
dotraplinkage void notrace
do_page_fault(struct pt_regs *regs, unsigned long error_code)
{
	unsigned long address = read_cr2(); /* Get the faulting address */
	enum ctx_state prev_state;

	prev_state = exception_enter();
	if (trace_pagefault_enabled())
		trace_page_fault_entries(address, regs, error_code);

	__do_page_fault(regs, error_code, address);
	exception_exit(prev_state);
}
NOKPROBE_SYMBOL(do_page_fault);
