// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  PowerPC version
 *    Copyright (C) 1995-1996 Gary Thomas (gdt@linuxppc.org)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1991, 1992, 1993, 1994  Linus Torvalds
 *
 *  Modified by Cort Dougan and Paul Mackerras.
 *
 *  Modified for PPC64 by Dave Engebretsen (engebret@ibm.com)
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/string_choices.h>
#include <linux/types.h>
#include <linux/pagemap.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/extable.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>
#include <linux/perf_event.h>
#include <linux/ratelimit.h>
#include <linux/context_tracking.h>
#include <linux/hugetlb.h>
#include <linux/uaccess.h>
#include <linux/kfence.h>
#include <linux/pkeys.h>

#include <asm/firmware.h>
#include <asm/interrupt.h>
#include <asm/page.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/siginfo.h>
#include <asm/debug.h>
#include <asm/kup.h>
#include <asm/inst.h>


/*
 * do_page_fault error handling helpers
 */

static int
__bad_area_nosemaphore(struct pt_regs *regs, unsigned long address, int si_code)
{
	/*
	 * If we are in kernel mode, bail out with a SEGV, this will
	 * be caught by the assembly which will restore the non-volatile
	 * registers before calling bad_page_fault()
	 */
	if (!user_mode(regs))
		return SIGSEGV;

	_exception(SIGSEGV, regs, si_code, address);

	return 0;
}

static noinline int bad_area_nosemaphore(struct pt_regs *regs, unsigned long address)
{
	return __bad_area_nosemaphore(regs, address, SEGV_MAPERR);
}

static int __bad_area(struct pt_regs *regs, unsigned long address, int si_code,
		      struct mm_struct *mm, struct vm_area_struct *vma)
{

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
	if (mm)
		mmap_read_unlock(mm);
	else
		vma_end_read(vma);

	return __bad_area_nosemaphore(regs, address, si_code);
}

static noinline int bad_access_pkey(struct pt_regs *regs, unsigned long address,
				    struct mm_struct *mm,
				    struct vm_area_struct *vma)
{
	int pkey;

	/*
	 * We don't try to fetch the pkey from page table because reading
	 * page table without locking doesn't guarantee stable pte value.
	 * Hence the pkey value that we return to userspace can be different
	 * from the pkey that actually caused access error.
	 *
	 * It does *not* guarantee that the VMA we find here
	 * was the one that we faulted on.
	 *
	 * 1. T1   : mprotect_key(foo, PAGE_SIZE, pkey=4);
	 * 2. T1   : set AMR to deny access to pkey=4, touches, page
	 * 3. T1   : faults...
	 * 4.    T2: mprotect_key(foo, PAGE_SIZE, pkey=5);
	 * 5. T1   : enters fault handler, takes mmap_lock, etc...
	 * 6. T1   : reaches here, sees vma_pkey(vma)=5, when we really
	 *	     faulted on a pte with its pkey=4.
	 */
	pkey = vma_pkey(vma);

	if (mm)
		mmap_read_unlock(mm);
	else
		vma_end_read(vma);

	/*
	 * If we are in kernel mode, bail out with a SEGV, this will
	 * be caught by the assembly which will restore the non-volatile
	 * registers before calling bad_page_fault()
	 */
	if (!user_mode(regs))
		return SIGSEGV;

	_exception_pkey(regs, address, pkey);

	return 0;
}

static noinline int bad_access(struct pt_regs *regs, unsigned long address,
			       struct mm_struct *mm, struct vm_area_struct *vma)
{
	return __bad_area(regs, address, SEGV_ACCERR, mm, vma);
}

static int do_sigbus(struct pt_regs *regs, unsigned long address,
		     vm_fault_t fault)
{
	if (!user_mode(regs))
		return SIGBUS;

	current->thread.trap_nr = BUS_ADRERR;
#ifdef CONFIG_MEMORY_FAILURE
	if (fault & (VM_FAULT_HWPOISON|VM_FAULT_HWPOISON_LARGE)) {
		unsigned int lsb = 0; /* shutup gcc */

		pr_err("MCE: Killing %s:%d due to hardware memory corruption fault at %lx\n",
			current->comm, current->pid, address);

		if (fault & VM_FAULT_HWPOISON_LARGE)
			lsb = hstate_index_to_shift(VM_FAULT_GET_HINDEX(fault));
		if (fault & VM_FAULT_HWPOISON)
			lsb = PAGE_SHIFT;

		force_sig_mceerr(BUS_MCEERR_AR, (void __user *)address, lsb);
		return 0;
	}

#endif
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address);
	return 0;
}

static int mm_fault_error(struct pt_regs *regs, unsigned long addr,
				vm_fault_t fault)
{
	/*
	 * Kernel page fault interrupted by SIGKILL. We have no reason to
	 * continue processing.
	 */
	if (fatal_signal_pending(current) && !user_mode(regs))
		return SIGKILL;

	/* Out of memory */
	if (fault & VM_FAULT_OOM) {
		/*
		 * We ran out of memory, or some other thing happened to us that
		 * made us unable to handle the page fault gracefully.
		 */
		if (!user_mode(regs))
			return SIGSEGV;
		pagefault_out_of_memory();
	} else {
		if (fault & (VM_FAULT_SIGBUS|VM_FAULT_HWPOISON|
			     VM_FAULT_HWPOISON_LARGE))
			return do_sigbus(regs, addr, fault);
		else if (fault & VM_FAULT_SIGSEGV)
			return bad_area_nosemaphore(regs, addr);
		else
			BUG();
	}
	return 0;
}

/* Is this a bad kernel fault ? */
static bool bad_kernel_fault(struct pt_regs *regs, unsigned long error_code,
			     unsigned long address, bool is_write)
{
	int is_exec = TRAP(regs) == INTERRUPT_INST_STORAGE;

	if (is_exec) {
		pr_crit_ratelimited("kernel tried to execute %s page (%lx) - exploit attempt? (uid: %d)\n",
				    address >= TASK_SIZE ? "exec-protected" : "user",
				    address,
				    from_kuid(&init_user_ns, current_uid()));

		// Kernel exec fault is always bad
		return true;
	}

	// Kernel fault on kernel address is bad
	if (address >= TASK_SIZE)
		return true;

	// Read/write fault blocked by KUAP is bad, it can never succeed.
	if (bad_kuap_fault(regs, address, is_write)) {
		pr_crit_ratelimited("Kernel attempted to %s user page (%lx) - exploit attempt? (uid: %d)\n",
				    str_write_read(is_write), address,
				    from_kuid(&init_user_ns, current_uid()));

		// Fault on user outside of certain regions (eg. copy_tofrom_user()) is bad
		if (!search_exception_tables(regs->nip))
			return true;

		// Read/write fault in a valid region (the exception table search passed
		// above), but blocked by KUAP is bad, it can never succeed.
		return WARN(true, "Bug: %s fault blocked by KUAP!", is_write ? "Write" : "Read");
	}

	// What's left? Kernel fault on user and allowed by KUAP in the faulting context.
	return false;
}

static bool access_pkey_error(bool is_write, bool is_exec, bool is_pkey,
			      struct vm_area_struct *vma)
{
	/*
	 * Make sure to check the VMA so that we do not perform
	 * faults just to hit a pkey fault as soon as we fill in a
	 * page. Only called for current mm, hence foreign == 0
	 */
	if (!arch_vma_access_permitted(vma, is_write, is_exec, 0))
		return true;

	return false;
}

static bool access_error(bool is_write, bool is_exec, struct vm_area_struct *vma)
{
	/*
	 * Allow execution from readable areas if the MMU does not
	 * provide separate controls over reading and executing.
	 *
	 * Note: That code used to not be enabled for 4xx/BookE.
	 * It is now as I/D cache coherency for these is done at
	 * set_pte_at() time and I see no reason why the test
	 * below wouldn't be valid on those processors. This -may-
	 * break programs compiled with a really old ABI though.
	 */
	if (is_exec) {
		return !(vma->vm_flags & VM_EXEC) &&
			(cpu_has_feature(CPU_FTR_NOEXECUTE) ||
			 !(vma->vm_flags & (VM_READ | VM_WRITE)));
	}

	if (is_write) {
		if (unlikely(!(vma->vm_flags & VM_WRITE)))
			return true;
		return false;
	}

	/*
	 * VM_READ, VM_WRITE and VM_EXEC may imply read permissions, as
	 * defined in protection_map[].  In that case Read faults can only be
	 * caused by a PROT_NONE mapping. However a non exec access on a
	 * VM_EXEC only mapping is invalid anyway, so report it as such.
	 */
	if (unlikely(!vma_is_accessible(vma)))
		return true;

	if ((vma->vm_flags & VM_ACCESS_FLAGS) == VM_EXEC)
		return true;

	/*
	 * We should ideally do the vma pkey access check here. But in the
	 * fault path, handle_mm_fault() also does the same check. To avoid
	 * these multiple checks, we skip it here and handle access error due
	 * to pkeys later.
	 */
	return false;
}

#ifdef CONFIG_PPC_SMLPAR
static inline void cmo_account_page_fault(void)
{
	if (firmware_has_feature(FW_FEATURE_CMO)) {
		u32 page_ins;

		preempt_disable();
		page_ins = be32_to_cpu(get_lppaca()->page_ins);
		page_ins += 1 << PAGE_FACTOR;
		get_lppaca()->page_ins = cpu_to_be32(page_ins);
		preempt_enable();
	}
}
#else
static inline void cmo_account_page_fault(void) { }
#endif /* CONFIG_PPC_SMLPAR */

static void sanity_check_fault(bool is_write, bool is_user,
			       unsigned long error_code, unsigned long address)
{
	/*
	 * Userspace trying to access kernel address, we get PROTFAULT for that.
	 */
	if (is_user && address >= TASK_SIZE) {
		if ((long)address == -1)
			return;

		pr_crit_ratelimited("%s[%d]: User access of kernel address (%lx) - exploit attempt? (uid: %d)\n",
				   current->comm, current->pid, address,
				   from_kuid(&init_user_ns, current_uid()));
		return;
	}

	if (!IS_ENABLED(CONFIG_PPC_BOOK3S))
		return;

	/*
	 * For hash translation mode, we should never get a
	 * PROTFAULT. Any update to pte to reduce access will result in us
	 * removing the hash page table entry, thus resulting in a DSISR_NOHPTE
	 * fault instead of DSISR_PROTFAULT.
	 *
	 * A pte update to relax the access will not result in a hash page table
	 * entry invalidate and hence can result in DSISR_PROTFAULT.
	 * ptep_set_access_flags() doesn't do a hpte flush. This is why we have
	 * the special !is_write in the below conditional.
	 *
	 * For platforms that doesn't supports coherent icache and do support
	 * per page noexec bit, we do setup things such that we do the
	 * sync between D/I cache via fault. But that is handled via low level
	 * hash fault code (hash_page_do_lazy_icache()) and we should not reach
	 * here in such case.
	 *
	 * For wrong access that can result in PROTFAULT, the above vma->vm_flags
	 * check should handle those and hence we should fall to the bad_area
	 * handling correctly.
	 *
	 * For embedded with per page exec support that doesn't support coherent
	 * icache we do get PROTFAULT and we handle that D/I cache sync in
	 * set_pte_at while taking the noexec/prot fault. Hence this is WARN_ON
	 * is conditional for server MMU.
	 *
	 * For radix, we can get prot fault for autonuma case, because radix
	 * page table will have them marked noaccess for user.
	 */
	if (radix_enabled() || is_write)
		return;

	WARN_ON_ONCE(error_code & DSISR_PROTFAULT);
}

/*
 * Define the correct "is_write" bit in error_code based
 * on the processor family
 */
#ifdef CONFIG_BOOKE
#define page_fault_is_write(__err)	((__err) & ESR_DST)
#else
#define page_fault_is_write(__err)	((__err) & DSISR_ISSTORE)
#endif

#ifdef CONFIG_BOOKE
#define page_fault_is_bad(__err)	(0)
#elif defined(CONFIG_PPC_8xx)
#define page_fault_is_bad(__err)	((__err) & DSISR_NOEXEC_OR_G)
#elif defined(CONFIG_PPC64)
static int page_fault_is_bad(unsigned long err)
{
	unsigned long flag = DSISR_BAD_FAULT_64S;

	/*
	 * PAPR+ v2.11 § 14.15.3.4.1 (unreleased)
	 * If byte 0, bit 3 of pi-attribute-specifier-type in
	 * ibm,pi-features property is defined, ignore the DSI error
	 * which is caused by the paste instruction on the
	 * suspended NX window.
	 */
	if (mmu_has_feature(MMU_FTR_NX_DSI))
		flag &= ~DSISR_BAD_COPYPASTE;

	return err & flag;
}
#else
#define page_fault_is_bad(__err)	((__err) & DSISR_BAD_FAULT_32S)
#endif

/*
 * For 600- and 800-family processors, the error_code parameter is DSISR
 * for a data fault, SRR1 for an instruction fault.
 * For 400-family processors the error_code parameter is ESR for a data fault,
 * 0 for an instruction fault.
 * For 64-bit processors, the error_code parameter is DSISR for a data access
 * fault, SRR1 & 0x08000000 for an instruction access fault.
 *
 * The return value is 0 if the fault was handled, or the signal
 * number if this is a kernel fault that can't be handled here.
 */
static int ___do_page_fault(struct pt_regs *regs, unsigned long address,
			   unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned int flags = FAULT_FLAG_DEFAULT;
	int is_exec = TRAP(regs) == INTERRUPT_INST_STORAGE;
	int is_user = user_mode(regs);
	int is_write = page_fault_is_write(error_code);
	vm_fault_t fault, major = 0;
	bool kprobe_fault = kprobe_page_fault(regs, 11);

	if (unlikely(debugger_fault_handler(regs) || kprobe_fault))
		return 0;

	if (unlikely(page_fault_is_bad(error_code))) {
		if (is_user) {
			_exception(SIGBUS, regs, BUS_OBJERR, address);
			return 0;
		}
		return SIGBUS;
	}

	/* Additional sanity check(s) */
	sanity_check_fault(is_write, is_user, error_code, address);

	/*
	 * The kernel should never take an execute fault nor should it
	 * take a page fault to a kernel address or a page fault to a user
	 * address outside of dedicated places.
	 *
	 * Rather than kfence directly reporting false negatives, search whether
	 * the NIP belongs to the fixup table for cases where fault could come
	 * from functions like copy_from_kernel_nofault().
	 */
	if (unlikely(!is_user && bad_kernel_fault(regs, error_code, address, is_write))) {
		if (is_kfence_address((void *)address) &&
		    !search_exception_tables(instruction_pointer(regs)) &&
		    kfence_handle_page_fault(address, is_write, regs))
			return 0;

		return SIGSEGV;
	}

	/*
	 * If we're in an interrupt, have no user context or are running
	 * in a region with pagefaults disabled then we must not take the fault
	 */
	if (unlikely(faulthandler_disabled() || !mm)) {
		if (is_user)
			printk_ratelimited(KERN_ERR "Page fault in user mode"
					   " with faulthandler_disabled()=%d"
					   " mm=%p\n",
					   faulthandler_disabled(), mm);
		return bad_area_nosemaphore(regs, address);
	}

	interrupt_cond_local_irq_enable(regs);

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	/*
	 * We want to do this outside mmap_lock, because reading code around nip
	 * can result in fault, which will cause a deadlock when called with
	 * mmap_lock held
	 */
	if (is_user)
		flags |= FAULT_FLAG_USER;
	if (is_write)
		flags |= FAULT_FLAG_WRITE;
	if (is_exec)
		flags |= FAULT_FLAG_INSTRUCTION;

	if (!(flags & FAULT_FLAG_USER))
		goto lock_mmap;

	vma = lock_vma_under_rcu(mm, address);
	if (!vma)
		goto lock_mmap;

	if (unlikely(access_pkey_error(is_write, is_exec,
				       (error_code & DSISR_KEYFAULT), vma))) {
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		return bad_access_pkey(regs, address, NULL, vma);
	}

	if (unlikely(access_error(is_write, is_exec, vma))) {
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		return bad_access(regs, address, NULL, vma);
	}

	fault = handle_mm_fault(vma, address, flags | FAULT_FLAG_VMA_LOCK, regs);
	if (!(fault & (VM_FAULT_RETRY | VM_FAULT_COMPLETED)))
		vma_end_read(vma);

	if (!(fault & VM_FAULT_RETRY)) {
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		goto done;
	}
	count_vm_vma_lock_event(VMA_LOCK_RETRY);
	if (fault & VM_FAULT_MAJOR)
		flags |= FAULT_FLAG_TRIED;

	if (fault_signal_pending(fault, regs))
		return user_mode(regs) ? 0 : SIGBUS;

lock_mmap:

	/* When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunately, in the case of an
	 * erroneous fault occurring in a code path which already holds mmap_lock
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table. lock_mm_and_find_vma() handles that logic.
	 */
retry:
	vma = lock_mm_and_find_vma(mm, address, regs);
	if (unlikely(!vma))
		return bad_area_nosemaphore(regs, address);

	if (unlikely(access_pkey_error(is_write, is_exec,
				       (error_code & DSISR_KEYFAULT), vma)))
		return bad_access_pkey(regs, address, mm, vma);

	if (unlikely(access_error(is_write, is_exec, vma)))
		return bad_access(regs, address, mm, vma);

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags, regs);

	major |= fault & VM_FAULT_MAJOR;

	if (fault_signal_pending(fault, regs))
		return user_mode(regs) ? 0 : SIGBUS;

	/* The fault is fully completed (including releasing mmap lock) */
	if (fault & VM_FAULT_COMPLETED)
		goto out;

	/*
	 * Handle the retry right now, the mmap_lock has been released in that
	 * case.
	 */
	if (unlikely(fault & VM_FAULT_RETRY)) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}

	mmap_read_unlock(current->mm);

done:
	if (unlikely(fault & VM_FAULT_ERROR))
		return mm_fault_error(regs, address, fault);

out:
	/*
	 * Major/minor page fault accounting.
	 */
	if (major)
		cmo_account_page_fault();

	return 0;
}
NOKPROBE_SYMBOL(___do_page_fault);

static __always_inline void __do_page_fault(struct pt_regs *regs)
{
	long err;

	err = ___do_page_fault(regs, regs->dar, regs->dsisr);
	if (unlikely(err))
		bad_page_fault(regs, err);
}

DEFINE_INTERRUPT_HANDLER(do_page_fault)
{
	__do_page_fault(regs);
}

#ifdef CONFIG_PPC_BOOK3S_64
/* Same as do_page_fault but interrupt entry has already run in do_hash_fault */
void hash__do_page_fault(struct pt_regs *regs)
{
	__do_page_fault(regs);
}
NOKPROBE_SYMBOL(hash__do_page_fault);
#endif

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from the DSI and ISI handlers in head.S and from some
 * of the procedures in traps.c.
 */
static void __bad_page_fault(struct pt_regs *regs, int sig)
{
	int is_write = page_fault_is_write(regs->dsisr);
	const char *msg;

	/* kernel has accessed a bad area */

	if (regs->dar < PAGE_SIZE)
		msg = "Kernel NULL pointer dereference";
	else
		msg = "Unable to handle kernel data access";

	switch (TRAP(regs)) {
	case INTERRUPT_DATA_STORAGE:
	case INTERRUPT_H_DATA_STORAGE:
		pr_alert("BUG: %s on %s at 0x%08lx\n", msg,
			 str_write_read(is_write), regs->dar);
		break;
	case INTERRUPT_DATA_SEGMENT:
		pr_alert("BUG: %s at 0x%08lx\n", msg, regs->dar);
		break;
	case INTERRUPT_INST_STORAGE:
	case INTERRUPT_INST_SEGMENT:
		pr_alert("BUG: Unable to handle kernel instruction fetch%s",
			 regs->nip < PAGE_SIZE ? " (NULL pointer?)\n" : "\n");
		break;
	case INTERRUPT_ALIGNMENT:
		pr_alert("BUG: Unable to handle kernel unaligned access at 0x%08lx\n",
			 regs->dar);
		break;
	default:
		pr_alert("BUG: Unable to handle unknown paging fault at 0x%08lx\n",
			 regs->dar);
		break;
	}
	printk(KERN_ALERT "Faulting instruction address: 0x%08lx\n",
		regs->nip);

	if (task_stack_end_corrupted(current))
		printk(KERN_ALERT "Thread overran stack, or stack corrupted\n");

	die("Kernel access of bad area", regs, sig);
}

void bad_page_fault(struct pt_regs *regs, int sig)
{
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault?  */
	entry = search_exception_tables(instruction_pointer(regs));
	if (entry)
		instruction_pointer_set(regs, extable_fixup(entry));
	else
		__bad_page_fault(regs, sig);
}

#ifdef CONFIG_PPC_BOOK3S_64
DEFINE_INTERRUPT_HANDLER(do_bad_page_fault_segv)
{
	bad_page_fault(regs, SIGSEGV);
}

/*
 * In radix, segment interrupts indicate the EA is not addressable by the
 * page table geometry, so they are always sent here.
 *
 * In hash, this is called if do_slb_fault returns error. Typically it is
 * because the EA was outside the region allowed by software.
 */
DEFINE_INTERRUPT_HANDLER(do_bad_segment_interrupt)
{
	int err = regs->result;

	if (err == -EFAULT) {
		if (user_mode(regs))
			_exception(SIGSEGV, regs, SEGV_BNDERR, regs->dar);
		else
			bad_page_fault(regs, SIGSEGV);
	} else if (err == -EINVAL) {
		unrecoverable_exception(regs);
	} else {
		BUG();
	}
}
#endif
