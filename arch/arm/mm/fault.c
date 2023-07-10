// SPDX-License-Identifier: GPL-2.0-only
/*
 *  linux/arch/arm/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Modifications for ARM processor (c) 1995-2004 Russell King
 */
#include <linux/extable.h>
#include <linux/signal.h>
#include <linux/mm.h>
#include <linux/hardirq.h>
#include <linux/init.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/page-flags.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/highmem.h>
#include <linux/perf_event.h>
#include <linux/kfence.h>

#include <asm/system_misc.h>
#include <asm/system_info.h>
#include <asm/tlbflush.h>

#include "fault.h"

#ifdef CONFIG_MMU

/*
 * This is useful to dump out the page tables associated with
 * 'addr' in mm 'mm'.
 */
void show_pte(const char *lvl, struct mm_struct *mm, unsigned long addr)
{
	pgd_t *pgd;

	if (!mm)
		mm = &init_mm;

	pgd = pgd_offset(mm, addr);
	printk("%s[%08lx] *pgd=%08llx", lvl, addr, (long long)pgd_val(*pgd));

	do {
		p4d_t *p4d;
		pud_t *pud;
		pmd_t *pmd;
		pte_t *pte;

		p4d = p4d_offset(pgd, addr);
		if (p4d_none(*p4d))
			break;

		if (p4d_bad(*p4d)) {
			pr_cont("(bad)");
			break;
		}

		pud = pud_offset(p4d, addr);
		if (PTRS_PER_PUD != 1)
			pr_cont(", *pud=%08llx", (long long)pud_val(*pud));

		if (pud_none(*pud))
			break;

		if (pud_bad(*pud)) {
			pr_cont("(bad)");
			break;
		}

		pmd = pmd_offset(pud, addr);
		if (PTRS_PER_PMD != 1)
			pr_cont(", *pmd=%08llx", (long long)pmd_val(*pmd));

		if (pmd_none(*pmd))
			break;

		if (pmd_bad(*pmd)) {
			pr_cont("(bad)");
			break;
		}

		/* We must not map this if we have highmem enabled */
		if (PageHighMem(pfn_to_page(pmd_val(*pmd) >> PAGE_SHIFT)))
			break;

		pte = pte_offset_map(pmd, addr);
		if (!pte)
			break;

		pr_cont(", *pte=%08llx", (long long)pte_val(*pte));
#ifndef CONFIG_ARM_LPAE
		pr_cont(", *ppte=%08llx",
		       (long long)pte_val(pte[PTE_HWTABLE_PTRS]));
#endif
		pte_unmap(pte);
	} while(0);

	pr_cont("\n");
}
#else					/* CONFIG_MMU */
void show_pte(const char *lvl, struct mm_struct *mm, unsigned long addr)
{ }
#endif					/* CONFIG_MMU */

static inline bool is_write_fault(unsigned int fsr)
{
	return (fsr & FSR_WRITE) && !(fsr & FSR_CM);
}

static inline bool is_translation_fault(unsigned int fsr)
{
	int fs = fsr_fs(fsr);
#ifdef CONFIG_ARM_LPAE
	if ((fs & FS_MMU_NOLL_MASK) == FS_TRANS_NOLL)
		return true;
#else
	if (fs == FS_L1_TRANS || fs == FS_L2_TRANS)
		return true;
#endif
	return false;
}

static void die_kernel_fault(const char *msg, struct mm_struct *mm,
			     unsigned long addr, unsigned int fsr,
			     struct pt_regs *regs)
{
	bust_spinlocks(1);
	pr_alert("8<--- cut here ---\n");
	pr_alert("Unable to handle kernel %s at virtual address %08lx when %s\n",
		 msg, addr, fsr & FSR_LNX_PF ? "execute" :
		 fsr & FSR_WRITE ? "write" : "read");

	show_pte(KERN_ALERT, mm, addr);
	die("Oops", regs, fsr);
	bust_spinlocks(0);
	make_task_dead(SIGKILL);
}

/*
 * Oops.  The kernel tried to access some page that wasn't present.
 */
static void
__do_kernel_fault(struct mm_struct *mm, unsigned long addr, unsigned int fsr,
		  struct pt_regs *regs)
{
	const char *msg;
	/*
	 * Are we prepared to handle this kernel fault?
	 */
	if (fixup_exception(regs))
		return;

	/*
	 * No handler, we'll have to terminate things with extreme prejudice.
	 */
	if (addr < PAGE_SIZE) {
		msg = "NULL pointer dereference";
	} else {
		if (is_translation_fault(fsr) &&
		    kfence_handle_page_fault(addr, is_write_fault(fsr), regs))
			return;

		msg = "paging request";
	}

	die_kernel_fault(msg, mm, addr, fsr, regs);
}

/*
 * Something tried to access memory that isn't in our memory map..
 * User mode accesses just cause a SIGSEGV
 */
static void
__do_user_fault(unsigned long addr, unsigned int fsr, unsigned int sig,
		int code, struct pt_regs *regs)
{
	struct task_struct *tsk = current;

	if (addr > TASK_SIZE)
		harden_branch_predictor();

#ifdef CONFIG_DEBUG_USER
	if (((user_debug & UDBG_SEGV) && (sig == SIGSEGV)) ||
	    ((user_debug & UDBG_BUS)  && (sig == SIGBUS))) {
		pr_err("8<--- cut here ---\n");
		pr_err("%s: unhandled page fault (%d) at 0x%08lx, code 0x%03x\n",
		       tsk->comm, sig, addr, fsr);
		show_pte(KERN_ERR, tsk->mm, addr);
		show_regs(regs);
	}
#endif
#ifndef CONFIG_KUSER_HELPERS
	if ((sig == SIGSEGV) && ((addr & PAGE_MASK) == 0xffff0000))
		printk_ratelimited(KERN_DEBUG
				   "%s: CONFIG_KUSER_HELPERS disabled at 0x%08lx\n",
				   tsk->comm, addr);
#endif

	tsk->thread.address = addr;
	tsk->thread.error_code = fsr;
	tsk->thread.trap_no = 14;
	force_sig_fault(sig, code, (void __user *)addr);
}

void do_bad_area(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct task_struct *tsk = current;
	struct mm_struct *mm = tsk->active_mm;

	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (user_mode(regs))
		__do_user_fault(addr, fsr, SIGSEGV, SEGV_MAPERR, regs);
	else
		__do_kernel_fault(mm, addr, fsr, regs);
}

#ifdef CONFIG_MMU
#define VM_FAULT_BADMAP		((__force vm_fault_t)0x010000)
#define VM_FAULT_BADACCESS	((__force vm_fault_t)0x020000)

static inline bool is_permission_fault(unsigned int fsr)
{
	int fs = fsr_fs(fsr);
#ifdef CONFIG_ARM_LPAE
	if ((fs & FS_MMU_NOLL_MASK) == FS_PERM_NOLL)
		return true;
#else
	if (fs == FS_L1_PERM || fs == FS_L2_PERM)
		return true;
#endif
	return false;
}

static int __kprobes
do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	int sig, code;
	vm_fault_t fault;
	unsigned int flags = FAULT_FLAG_DEFAULT;
	unsigned long vm_flags = VM_ACCESS_FLAGS;

	if (kprobe_page_fault(regs, fsr))
		return 0;


	/* Enable interrupts if they were enabled in the parent context. */
	if (interrupts_enabled(regs))
		local_irq_enable();

	/*
	 * If we're in an interrupt or have no user
	 * context, we must not take the fault..
	 */
	if (faulthandler_disabled() || !mm)
		goto no_context;

	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;

	if (is_write_fault(fsr)) {
		flags |= FAULT_FLAG_WRITE;
		vm_flags = VM_WRITE;
	}

	if (fsr & FSR_LNX_PF) {
		vm_flags = VM_EXEC;

		if (is_permission_fault(fsr) && !user_mode(regs))
			die_kernel_fault("execution of memory",
					 mm, addr, fsr, regs);
	}

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, addr);

retry:
	vma = lock_mm_and_find_vma(mm, addr, regs);
	if (unlikely(!vma)) {
		fault = VM_FAULT_BADMAP;
		goto bad_area;
	}

	/*
	 * ok, we have a good vm_area for this memory access, check the
	 * permissions on the VMA allow for the fault which occurred.
	 */
	if (!(vma->vm_flags & vm_flags))
		fault = VM_FAULT_BADACCESS;
	else
		fault = handle_mm_fault(vma, addr & PAGE_MASK, flags, regs);

	/* If we need to retry but a fatal signal is pending, handle the
	 * signal first. We do not need to release the mmap_lock because
	 * it would already be released in __lock_page_or_retry in
	 * mm/filemap.c. */
	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			goto no_context;
		return 0;
	}

	/* The fault is fully completed (including releasing mmap lock) */
	if (fault & VM_FAULT_COMPLETED)
		return 0;

	if (!(fault & VM_FAULT_ERROR)) {
		if (fault & VM_FAULT_RETRY) {
			flags |= FAULT_FLAG_TRIED;
			goto retry;
		}
	}

	mmap_read_unlock(mm);

	/*
	 * Handle the "normal" case first - VM_FAULT_MAJOR
	 */
	if (likely(!(fault & (VM_FAULT_ERROR | VM_FAULT_BADMAP | VM_FAULT_BADACCESS))))
		return 0;

bad_area:
	/*
	 * If we are in kernel mode at this point, we
	 * have no context to handle this fault with.
	 */
	if (!user_mode(regs))
		goto no_context;

	if (fault & VM_FAULT_OOM) {
		/*
		 * We ran out of memory, call the OOM killer, and return to
		 * userspace (which will retry the fault, or kill us if we
		 * got oom-killed)
		 */
		pagefault_out_of_memory();
		return 0;
	}

	if (fault & VM_FAULT_SIGBUS) {
		/*
		 * We had some memory, but were unable to
		 * successfully fix up this page fault.
		 */
		sig = SIGBUS;
		code = BUS_ADRERR;
	} else {
		/*
		 * Something tried to access memory that
		 * isn't in our memory map..
		 */
		sig = SIGSEGV;
		code = fault == VM_FAULT_BADACCESS ?
			SEGV_ACCERR : SEGV_MAPERR;
	}

	__do_user_fault(addr, fsr, sig, code, regs);
	return 0;

no_context:
	__do_kernel_fault(mm, addr, fsr, regs);
	return 0;
}
#else					/* CONFIG_MMU */
static int
do_page_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	return 0;
}
#endif					/* CONFIG_MMU */

/*
 * First Level Translation Fault Handler
 *
 * We enter here because the first level page table doesn't contain
 * a valid entry for the address.
 *
 * If the address is in kernel space (>= TASK_SIZE), then we are
 * probably faulting in the vmalloc() area.
 *
 * If the init_task's first level page tables contains the relevant
 * entry, we copy the it to this task.  If not, we send the process
 * a signal, fixup the exception, or oops the kernel.
 *
 * NOTE! We MUST NOT take any locks for this case. We may be in an
 * interrupt or a critical region, and should only copy the information
 * from the master page table, nothing more.
 */
#ifdef CONFIG_MMU
static int __kprobes
do_translation_fault(unsigned long addr, unsigned int fsr,
		     struct pt_regs *regs)
{
	unsigned int index;
	pgd_t *pgd, *pgd_k;
	p4d_t *p4d, *p4d_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;

	if (addr < TASK_SIZE)
		return do_page_fault(addr, fsr, regs);

	if (user_mode(regs))
		goto bad_area;

	index = pgd_index(addr);

	pgd = cpu_get_pgd() + index;
	pgd_k = init_mm.pgd + index;

	p4d = p4d_offset(pgd, addr);
	p4d_k = p4d_offset(pgd_k, addr);

	if (p4d_none(*p4d_k))
		goto bad_area;
	if (!p4d_present(*p4d))
		set_p4d(p4d, *p4d_k);

	pud = pud_offset(p4d, addr);
	pud_k = pud_offset(p4d_k, addr);

	if (pud_none(*pud_k))
		goto bad_area;
	if (!pud_present(*pud))
		set_pud(pud, *pud_k);

	pmd = pmd_offset(pud, addr);
	pmd_k = pmd_offset(pud_k, addr);

#ifdef CONFIG_ARM_LPAE
	/*
	 * Only one hardware entry per PMD with LPAE.
	 */
	index = 0;
#else
	/*
	 * On ARM one Linux PGD entry contains two hardware entries (see page
	 * tables layout in pgtable.h). We normally guarantee that we always
	 * fill both L1 entries. But create_mapping() doesn't follow the rule.
	 * It can create inidividual L1 entries, so here we have to call
	 * pmd_none() check for the entry really corresponded to address, not
	 * for the first of pair.
	 */
	index = (addr >> SECTION_SHIFT) & 1;
#endif
	if (pmd_none(pmd_k[index]))
		goto bad_area;

	copy_pmd(pmd, pmd_k);
	return 0;

bad_area:
	do_bad_area(addr, fsr, regs);
	return 0;
}
#else					/* CONFIG_MMU */
static int
do_translation_fault(unsigned long addr, unsigned int fsr,
		     struct pt_regs *regs)
{
	return 0;
}
#endif					/* CONFIG_MMU */

/*
 * Some section permission faults need to be handled gracefully.
 * They can happen due to a __{get,put}_user during an oops.
 */
#ifndef CONFIG_ARM_LPAE
static int
do_sect_fault(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	do_bad_area(addr, fsr, regs);
	return 0;
}
#endif /* CONFIG_ARM_LPAE */

/*
 * This abort handler always returns "fault".
 */
static int
do_bad(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	return 1;
}

struct fsr_info {
	int	(*fn)(unsigned long addr, unsigned int fsr, struct pt_regs *regs);
	int	sig;
	int	code;
	const char *name;
};

/* FSR definition */
#ifdef CONFIG_ARM_LPAE
#include "fsr-3level.c"
#else
#include "fsr-2level.c"
#endif

void __init
hook_fault_code(int nr, int (*fn)(unsigned long, unsigned int, struct pt_regs *),
		int sig, int code, const char *name)
{
	if (nr < 0 || nr >= ARRAY_SIZE(fsr_info))
		BUG();

	fsr_info[nr].fn   = fn;
	fsr_info[nr].sig  = sig;
	fsr_info[nr].code = code;
	fsr_info[nr].name = name;
}

/*
 * Dispatch a data abort to the relevant handler.
 */
asmlinkage void
do_DataAbort(unsigned long addr, unsigned int fsr, struct pt_regs *regs)
{
	const struct fsr_info *inf = fsr_info + fsr_fs(fsr);

	if (!inf->fn(addr, fsr & ~FSR_LNX_PF, regs))
		return;

	pr_alert("8<--- cut here ---\n");
	pr_alert("Unhandled fault: %s (0x%03x) at 0x%08lx\n",
		inf->name, fsr, addr);
	show_pte(KERN_ALERT, current->mm, addr);

	arm_notify_die("", regs, inf->sig, inf->code, (void __user *)addr,
		       fsr, 0);
}

void __init
hook_ifault_code(int nr, int (*fn)(unsigned long, unsigned int, struct pt_regs *),
		 int sig, int code, const char *name)
{
	if (nr < 0 || nr >= ARRAY_SIZE(ifsr_info))
		BUG();

	ifsr_info[nr].fn   = fn;
	ifsr_info[nr].sig  = sig;
	ifsr_info[nr].code = code;
	ifsr_info[nr].name = name;
}

asmlinkage void
do_PrefetchAbort(unsigned long addr, unsigned int ifsr, struct pt_regs *regs)
{
	const struct fsr_info *inf = ifsr_info + fsr_fs(ifsr);

	if (!inf->fn(addr, ifsr | FSR_LNX_PF, regs))
		return;

	pr_alert("Unhandled prefetch abort: %s (0x%03x) at 0x%08lx\n",
		inf->name, ifsr, addr);

	arm_notify_die("", regs, inf->sig, inf->code, (void __user *)addr,
		       ifsr, 0);
}

/*
 * Abort handler to be used only during first unmasking of asynchronous aborts
 * on the boot CPU. This makes sure that the machine will not die if the
 * firmware/bootloader left an imprecise abort pending for us to trip over.
 */
static int __init early_abort_handler(unsigned long addr, unsigned int fsr,
				      struct pt_regs *regs)
{
	pr_warn("Hit pending asynchronous external abort (FSR=0x%08x) during "
		"first unmask, this is most likely caused by a "
		"firmware/bootloader bug.\n", fsr);

	return 0;
}

void __init early_abt_enable(void)
{
	fsr_info[FSR_FS_AEA].fn = early_abort_handler;
	local_abt_enable();
	fsr_info[FSR_FS_AEA].fn = do_bad;
}

#ifndef CONFIG_ARM_LPAE
static int __init exceptions_init(void)
{
	if (cpu_architecture() >= CPU_ARCH_ARMv6) {
		hook_fault_code(4, do_translation_fault, SIGSEGV, SEGV_MAPERR,
				"I-cache maintenance fault");
	}

	if (cpu_architecture() >= CPU_ARCH_ARMv7) {
		/*
		 * TODO: Access flag faults introduced in ARMv6K.
		 * Runtime check for 'K' extension is needed
		 */
		hook_fault_code(3, do_bad, SIGSEGV, SEGV_MAPERR,
				"section access flag fault");
		hook_fault_code(6, do_bad, SIGSEGV, SEGV_MAPERR,
				"section access flag fault");
	}

	return 0;
}

arch_initcall(exceptions_init);
#endif
