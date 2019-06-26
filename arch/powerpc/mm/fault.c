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
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
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

#include <asm/firmware.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu.h>
#include <asm/mmu_context.h>
#include <asm/siginfo.h>
#include <asm/debug.h>

static inline bool notify_page_fault(struct pt_regs *regs)
{
	bool ret = false;

#ifdef CONFIG_KPROBES
	/* kprobe_running() needs smp_processor_id() */
	if (!user_mode(regs)) {
		preempt_disable();
		if (kprobe_running() && kprobe_fault_handler(regs, 11))
			ret = true;
		preempt_enable();
	}
#endif /* CONFIG_KPROBES */

	if (unlikely(debugger_fault_handler(regs)))
		ret = true;

	return ret;
}

/*
 * Check whether the instruction inst is a store using
 * an update addressing form which will update r1.
 */
static bool store_updates_sp(unsigned int inst)
{
	/* check for 1 in the rA field */
	if (((inst >> 16) & 0x1f) != 1)
		return false;
	/* check major opcode */
	switch (inst >> 26) {
	case OP_STWU:
	case OP_STBU:
	case OP_STHU:
	case OP_STFSU:
	case OP_STFDU:
		return true;
	case OP_STD:	/* std or stdu */
		return (inst & 3) == 1;
	case OP_31:
		/* check minor opcode */
		switch ((inst >> 1) & 0x3ff) {
		case OP_31_XOP_STDUX:
		case OP_31_XOP_STWUX:
		case OP_31_XOP_STBUX:
		case OP_31_XOP_STHUX:
		case OP_31_XOP_STFSUX:
		case OP_31_XOP_STFDUX:
			return true;
		}
	}
	return false;
}
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

static int __bad_area(struct pt_regs *regs, unsigned long address, int si_code)
{
	struct mm_struct *mm = current->mm;

	/*
	 * Something tried to access memory that isn't in our memory map..
	 * Fix it, but check if it's kernel or user first..
	 */
	up_read(&mm->mmap_sem);

	return __bad_area_nosemaphore(regs, address, si_code);
}

static noinline int bad_area(struct pt_regs *regs, unsigned long address)
{
	return __bad_area(regs, address, SEGV_MAPERR);
}

static int bad_key_fault_exception(struct pt_regs *regs, unsigned long address,
				    int pkey)
{
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

static noinline int bad_access(struct pt_regs *regs, unsigned long address)
{
	return __bad_area(regs, address, SEGV_ACCERR);
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

		force_sig_mceerr(BUS_MCEERR_AR, (void __user *)address, lsb,
				 current);
		return 0;
	}

#endif
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)address, current);
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
static bool bad_kernel_fault(bool is_exec, unsigned long error_code,
			     unsigned long address)
{
	/* NX faults set DSISR_PROTFAULT on the 8xx, DSISR_NOEXEC_OR_G on others */
	if (is_exec && (error_code & (DSISR_NOEXEC_OR_G | DSISR_KEYFAULT |
				      DSISR_PROTFAULT))) {
		printk_ratelimited(KERN_CRIT "kernel tried to execute"
				   " exec-protected page (%lx) -"
				   "exploit attempt? (uid: %d)\n",
				   address, from_kuid(&init_user_ns,
						      current_uid()));
	}
	return is_exec || (address >= TASK_SIZE);
}

static bool bad_stack_expansion(struct pt_regs *regs, unsigned long address,
				struct vm_area_struct *vma, unsigned int flags,
				bool *must_retry)
{
	/*
	 * N.B. The POWER/Open ABI allows programs to access up to
	 * 288 bytes below the stack pointer.
	 * The kernel signal delivery code writes up to about 1.5kB
	 * below the stack pointer (r1) before decrementing it.
	 * The exec code can write slightly over 640kB to the stack
	 * before setting the user r1.  Thus we allow the stack to
	 * expand to 1MB without further checks.
	 */
	if (address + 0x100000 < vma->vm_end) {
		unsigned int __user *nip = (unsigned int __user *)regs->nip;
		/* get user regs even if this fault is in kernel mode */
		struct pt_regs *uregs = current->thread.regs;
		if (uregs == NULL)
			return true;

		/*
		 * A user-mode access to an address a long way below
		 * the stack pointer is only valid if the instruction
		 * is one which would update the stack pointer to the
		 * address accessed if the instruction completed,
		 * i.e. either stwu rs,n(r1) or stwux rs,r1,rb
		 * (or the byte, halfword, float or double forms).
		 *
		 * If we don't check this then any write to the area
		 * between the last mapped region and the stack will
		 * expand the stack rather than segfaulting.
		 */
		if (address + 2048 >= uregs->gpr[1])
			return false;

		if ((flags & FAULT_FLAG_WRITE) && (flags & FAULT_FLAG_USER) &&
		    access_ok(nip, sizeof(*nip))) {
			unsigned int inst;
			int res;

			pagefault_disable();
			res = __get_user_inatomic(inst, nip);
			pagefault_enable();
			if (!res)
				return !store_updates_sp(inst);
			*must_retry = true;
		}
		return true;
	}
	return false;
}

static bool access_error(bool is_write, bool is_exec,
			 struct vm_area_struct *vma)
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

	if (unlikely(!(vma->vm_flags & (VM_READ | VM_EXEC | VM_WRITE))))
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

#ifdef CONFIG_PPC_BOOK3S
static void sanity_check_fault(bool is_write, bool is_user,
			       unsigned long error_code, unsigned long address)
{
	/*
	 * Userspace trying to access kernel address, we get PROTFAULT for that.
	 */
	if (is_user && address >= TASK_SIZE) {
		pr_crit_ratelimited("%s[%d]: User access of kernel address (%lx) - exploit attempt? (uid: %d)\n",
				   current->comm, current->pid, address,
				   from_kuid(&init_user_ns, current_uid()));
		return;
	}

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
#else
static void sanity_check_fault(bool is_write, bool is_user,
			       unsigned long error_code, unsigned long address) { }
#endif /* CONFIG_PPC_BOOK3S */

/*
 * Define the correct "is_write" bit in error_code based
 * on the processor family
 */
#if (defined(CONFIG_4xx) || defined(CONFIG_BOOKE))
#define page_fault_is_write(__err)	((__err) & ESR_DST)
#define page_fault_is_bad(__err)	(0)
#else
#define page_fault_is_write(__err)	((__err) & DSISR_ISSTORE)
#if defined(CONFIG_PPC_8xx)
#define page_fault_is_bad(__err)	((__err) & DSISR_NOEXEC_OR_G)
#elif defined(CONFIG_PPC64)
#define page_fault_is_bad(__err)	((__err) & DSISR_BAD_FAULT_64S)
#else
#define page_fault_is_bad(__err)	((__err) & DSISR_BAD_FAULT_32S)
#endif
#endif

/*
 * For 600- and 800-family processors, the error_code parameter is DSISR
 * for a data fault, SRR1 for an instruction fault. For 400-family processors
 * the error_code parameter is ESR for a data fault, 0 for an instruction
 * fault.
 * For 64-bit processors, the error_code parameter is
 *  - DSISR for a non-SLB data access fault,
 *  - SRR1 & 0x08000000 for a non-SLB instruction access fault
 *  - 0 any SLB fault.
 *
 * The return value is 0 if the fault was handled, or the signal
 * number if this is a kernel fault that can't be handled here.
 */
static int __do_page_fault(struct pt_regs *regs, unsigned long address,
			   unsigned long error_code)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned int flags = FAULT_FLAG_ALLOW_RETRY | FAULT_FLAG_KILLABLE;
 	int is_exec = TRAP(regs) == 0x400;
	int is_user = user_mode(regs);
	int is_write = page_fault_is_write(error_code);
	vm_fault_t fault, major = 0;
	bool must_retry = false;

	if (notify_page_fault(regs))
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
	 * take a page fault to a kernel address.
	 */
	if (unlikely(!is_user && bad_kernel_fault(is_exec, error_code, address)))
		return SIGSEGV;

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

	/* We restore the interrupt state now */
	if (!arch_irq_disabled_regs(regs))
		local_irq_enable();

	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);

	if (error_code & DSISR_KEYFAULT)
		return bad_key_fault_exception(regs, address,
					       get_mm_addr_key(mm, address));

	/*
	 * We want to do this outside mmap_sem, because reading code around nip
	 * can result in fault, which will cause a deadlock when called with
	 * mmap_sem held
	 */
	if (is_user)
		flags |= FAULT_FLAG_USER;
	if (is_write)
		flags |= FAULT_FLAG_WRITE;
	if (is_exec)
		flags |= FAULT_FLAG_INSTRUCTION;

	/* When running in the kernel we expect faults to occur only to
	 * addresses in user space.  All other faults represent errors in the
	 * kernel and should generate an OOPS.  Unfortunately, in the case of an
	 * erroneous fault occurring in a code path which already holds mmap_sem
	 * we will deadlock attempting to validate the fault against the
	 * address space.  Luckily the kernel only validly references user
	 * space from well defined areas of code, which are listed in the
	 * exceptions table.
	 *
	 * As the vast majority of faults will be valid we will only perform
	 * the source reference check when there is a possibility of a deadlock.
	 * Attempt to lock the address space, if we cannot we then validate the
	 * source.  If this is invalid we can skip the address space check,
	 * thus avoiding the deadlock.
	 */
	if (unlikely(!down_read_trylock(&mm->mmap_sem))) {
		if (!is_user && !search_exception_tables(regs->nip))
			return bad_area_nosemaphore(regs, address);

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
	if (unlikely(!vma))
		return bad_area(regs, address);
	if (likely(vma->vm_start <= address))
		goto good_area;
	if (unlikely(!(vma->vm_flags & VM_GROWSDOWN)))
		return bad_area(regs, address);

	/* The stack is being expanded, check if it's valid */
	if (unlikely(bad_stack_expansion(regs, address, vma, flags,
					 &must_retry))) {
		if (!must_retry)
			return bad_area(regs, address);

		up_read(&mm->mmap_sem);
		if (fault_in_pages_readable((const char __user *)regs->nip,
					    sizeof(unsigned int)))
			return bad_area_nosemaphore(regs, address);
		goto retry;
	}

	/* Try to expand it */
	if (unlikely(expand_stack(vma, address)))
		return bad_area(regs, address);

good_area:
	if (unlikely(access_error(is_write, is_exec, vma)))
		return bad_access(regs, address);

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(vma, address, flags);

#ifdef CONFIG_PPC_MEM_KEYS
	/*
	 * we skipped checking for access error due to key earlier.
	 * Check that using handle_mm_fault error return.
	 */
	if (unlikely(fault & VM_FAULT_SIGSEGV) &&
		!arch_vma_access_permitted(vma, is_write, is_exec, 0)) {

		int pkey = vma_pkey(vma);

		up_read(&mm->mmap_sem);
		return bad_key_fault_exception(regs, address, pkey);
	}
#endif /* CONFIG_PPC_MEM_KEYS */

	major |= fault & VM_FAULT_MAJOR;

	/*
	 * Handle the retry right now, the mmap_sem has been released in that
	 * case.
	 */
	if (unlikely(fault & VM_FAULT_RETRY)) {
		/* We retry only once */
		if (flags & FAULT_FLAG_ALLOW_RETRY) {
			/*
			 * Clear FAULT_FLAG_ALLOW_RETRY to avoid any risk
			 * of starvation.
			 */
			flags &= ~FAULT_FLAG_ALLOW_RETRY;
			flags |= FAULT_FLAG_TRIED;
			if (!fatal_signal_pending(current))
				goto retry;
		}

		/*
		 * User mode? Just return to handle the fatal exception otherwise
		 * return to bad_page_fault
		 */
		return is_user ? 0 : SIGBUS;
	}

	up_read(&current->mm->mmap_sem);

	if (unlikely(fault & VM_FAULT_ERROR))
		return mm_fault_error(regs, address, fault);

	/*
	 * Major/minor page fault accounting.
	 */
	if (major) {
		current->maj_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MAJ, 1, regs, address);
		cmo_account_page_fault();
	} else {
		current->min_flt++;
		perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS_MIN, 1, regs, address);
	}
	return 0;
}
NOKPROBE_SYMBOL(__do_page_fault);

int do_page_fault(struct pt_regs *regs, unsigned long address,
		  unsigned long error_code)
{
	enum ctx_state prev_state = exception_enter();
	int rc = __do_page_fault(regs, address, error_code);
	exception_exit(prev_state);
	return rc;
}
NOKPROBE_SYMBOL(do_page_fault);

/*
 * bad_page_fault is called when we have a bad access from the kernel.
 * It is called from the DSI and ISI handlers in head.S and from some
 * of the procedures in traps.c.
 */
void bad_page_fault(struct pt_regs *regs, unsigned long address, int sig)
{
	const struct exception_table_entry *entry;

	/* Are we prepared to handle this fault?  */
	if ((entry = search_exception_tables(regs->nip)) != NULL) {
		regs->nip = extable_fixup(entry);
		return;
	}

	/* kernel has accessed a bad area */

	switch (TRAP(regs)) {
	case 0x300:
	case 0x380:
	case 0xe00:
		pr_alert("BUG: %s at 0x%08lx\n",
			 regs->dar < PAGE_SIZE ? "Kernel NULL pointer dereference" :
			 "Unable to handle kernel data access", regs->dar);
		break;
	case 0x400:
	case 0x480:
		pr_alert("BUG: Unable to handle kernel instruction fetch%s",
			 regs->nip < PAGE_SIZE ? " (NULL pointer?)\n" : "\n");
		break;
	case 0x600:
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
