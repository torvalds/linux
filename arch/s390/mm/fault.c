// SPDX-License-Identifier: GPL-2.0
/*
 *  S390 version
 *    Copyright IBM Corp. 1999
 *    Author(s): Hartmut Penner (hp@de.ibm.com)
 *		 Ulrich Weigand (uweigand@de.ibm.com)
 *
 *  Derived from "arch/i386/mm/fault.c"
 *    Copyright (C) 1995  Linus Torvalds
 */

#include <linux/kernel_stat.h>
#include <linux/mmu_context.h>
#include <linux/perf_event.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/debug.h>
#include <linux/jump_label.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/compat.h>
#include <linux/smp.h>
#include <linux/kdebug.h>
#include <linux/init.h>
#include <linux/console.h>
#include <linux/extable.h>
#include <linux/hardirq.h>
#include <linux/kprobes.h>
#include <linux/uaccess.h>
#include <linux/hugetlb.h>
#include <linux/kfence.h>
#include <asm/asm-extable.h>
#include <asm/asm-offsets.h>
#include <asm/ptrace.h>
#include <asm/fault.h>
#include <asm/diag.h>
#include <asm/gmap.h>
#include <asm/irq.h>
#include <asm/facility.h>
#include <asm/uv.h>
#include "../kernel/entry.h"

enum fault_type {
	KERNEL_FAULT,
	USER_FAULT,
	GMAP_FAULT,
};

static DEFINE_STATIC_KEY_FALSE(have_store_indication);

static int __init fault_init(void)
{
	if (test_facility(75))
		static_branch_enable(&have_store_indication);
	return 0;
}
early_initcall(fault_init);

/*
 * Find out which address space caused the exception.
 */
static enum fault_type get_fault_type(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };
	struct gmap *gmap;

	if (likely(teid.as == PSW_BITS_AS_PRIMARY)) {
		if (user_mode(regs))
			return USER_FAULT;
		if (!IS_ENABLED(CONFIG_PGSTE))
			return KERNEL_FAULT;
		gmap = (struct gmap *)S390_lowcore.gmap;
		if (gmap && gmap->asce == regs->cr1)
			return GMAP_FAULT;
		return KERNEL_FAULT;
	}
	if (teid.as == PSW_BITS_AS_SECONDARY)
		return USER_FAULT;
	/* Access register mode, not used in the kernel */
	if (teid.as == PSW_BITS_AS_ACCREG)
		return USER_FAULT;
	/* Home space -> access via kernel ASCE */
	return KERNEL_FAULT;
}

static unsigned long get_fault_address(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };

	return teid.addr * PAGE_SIZE;
}

static __always_inline bool fault_is_write(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };

	if (static_branch_likely(&have_store_indication))
		return teid.fsi == TEID_FSI_STORE;
	return false;
}

static void dump_pagetable(unsigned long asce, unsigned long address)
{
	unsigned long entry, *table = __va(asce & _ASCE_ORIGIN);

	pr_alert("AS:%016lx ", asce);
	switch (asce & _ASCE_TYPE_MASK) {
	case _ASCE_TYPE_REGION1:
		table += (address & _REGION1_INDEX) >> _REGION1_SHIFT;
		if (get_kernel_nofault(entry, table))
			goto bad;
		pr_cont("R1:%016lx ", entry);
		if (entry & _REGION_ENTRY_INVALID)
			goto out;
		table = __va(entry & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_REGION2:
		table += (address & _REGION2_INDEX) >> _REGION2_SHIFT;
		if (get_kernel_nofault(entry, table))
			goto bad;
		pr_cont("R2:%016lx ", entry);
		if (entry & _REGION_ENTRY_INVALID)
			goto out;
		table = __va(entry & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_REGION3:
		table += (address & _REGION3_INDEX) >> _REGION3_SHIFT;
		if (get_kernel_nofault(entry, table))
			goto bad;
		pr_cont("R3:%016lx ", entry);
		if (entry & (_REGION_ENTRY_INVALID | _REGION3_ENTRY_LARGE))
			goto out;
		table = __va(entry & _REGION_ENTRY_ORIGIN);
		fallthrough;
	case _ASCE_TYPE_SEGMENT:
		table += (address & _SEGMENT_INDEX) >> _SEGMENT_SHIFT;
		if (get_kernel_nofault(entry, table))
			goto bad;
		pr_cont("S:%016lx ", entry);
		if (entry & (_SEGMENT_ENTRY_INVALID | _SEGMENT_ENTRY_LARGE))
			goto out;
		table = __va(entry & _SEGMENT_ENTRY_ORIGIN);
	}
	table += (address & _PAGE_INDEX) >> _PAGE_SHIFT;
	if (get_kernel_nofault(entry, table))
		goto bad;
	pr_cont("P:%016lx ", entry);
out:
	pr_cont("\n");
	return;
bad:
	pr_cont("BAD\n");
}

static void dump_fault_info(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };
	unsigned long asce;

	pr_alert("Failing address: %016lx TEID: %016lx\n",
		 get_fault_address(regs), teid.val);
	pr_alert("Fault in ");
	switch (teid.as) {
	case PSW_BITS_AS_HOME:
		pr_cont("home space ");
		break;
	case PSW_BITS_AS_SECONDARY:
		pr_cont("secondary space ");
		break;
	case PSW_BITS_AS_ACCREG:
		pr_cont("access register ");
		break;
	case PSW_BITS_AS_PRIMARY:
		pr_cont("primary space ");
		break;
	}
	pr_cont("mode while using ");
	switch (get_fault_type(regs)) {
	case USER_FAULT:
		asce = S390_lowcore.user_asce.val;
		pr_cont("user ");
		break;
	case GMAP_FAULT:
		asce = ((struct gmap *)S390_lowcore.gmap)->asce;
		pr_cont("gmap ");
		break;
	case KERNEL_FAULT:
		asce = S390_lowcore.kernel_asce.val;
		pr_cont("kernel ");
		break;
	default:
		unreachable();
	}
	pr_cont("ASCE.\n");
	dump_pagetable(asce, get_fault_address(regs));
}

int show_unhandled_signals = 1;

void report_user_fault(struct pt_regs *regs, long signr, int is_mm_fault)
{
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL, DEFAULT_RATELIMIT_BURST);

	if ((task_pid_nr(current) > 1) && !show_unhandled_signals)
		return;
	if (!unhandled_signal(current, signr))
		return;
	if (!__ratelimit(&rs))
		return;
	pr_alert("User process fault: interruption code %04x ilc:%d ",
		 regs->int_code & 0xffff, regs->int_code >> 17);
	print_vma_addr(KERN_CONT "in ", regs->psw.addr);
	pr_cont("\n");
	if (is_mm_fault)
		dump_fault_info(regs);
	show_regs(regs);
}

static void do_sigsegv(struct pt_regs *regs, int si_code)
{
	report_user_fault(regs, SIGSEGV, 1);
	force_sig_fault(SIGSEGV, si_code, (void __user *)get_fault_address(regs));
}

static void handle_fault_error_nolock(struct pt_regs *regs, int si_code)
{
	enum fault_type fault_type;
	unsigned long address;
	bool is_write;

	if (user_mode(regs)) {
		if (WARN_ON_ONCE(!si_code))
			si_code = SEGV_MAPERR;
		return do_sigsegv(regs, si_code);
	}
	if (fixup_exception(regs))
		return;
	fault_type = get_fault_type(regs);
	if (fault_type == KERNEL_FAULT) {
		address = get_fault_address(regs);
		is_write = fault_is_write(regs);
		if (kfence_handle_page_fault(address, is_write, regs))
			return;
	}
	if (fault_type == KERNEL_FAULT)
		pr_alert("Unable to handle kernel pointer dereference in virtual kernel address space\n");
	else
		pr_alert("Unable to handle kernel paging request in virtual user address space\n");
	dump_fault_info(regs);
	die(regs, "Oops");
}

static void handle_fault_error(struct pt_regs *regs, int si_code)
{
	struct mm_struct *mm = current->mm;

	mmap_read_unlock(mm);
	handle_fault_error_nolock(regs, si_code);
}

static void do_sigbus(struct pt_regs *regs)
{
	force_sig_fault(SIGBUS, BUS_ADRERR, (void __user *)get_fault_address(regs));
}

/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to one of the appropriate
 * routines.
 *
 * interruption code (int_code):
 *   04       Protection	   ->  Write-Protection  (suppression)
 *   10       Segment translation  ->  Not present	 (nullification)
 *   11       Page translation	   ->  Not present	 (nullification)
 *   3b       Region third trans.  ->  Not present	 (nullification)
 */
static void do_exception(struct pt_regs *regs, int access)
{
	struct vm_area_struct *vma;
	unsigned long address;
	struct mm_struct *mm;
	enum fault_type type;
	unsigned int flags;
	struct gmap *gmap;
	vm_fault_t fault;
	bool is_write;

	/*
	 * The instruction that caused the program check has
	 * been nullified. Don't signal single step via SIGTRAP.
	 */
	clear_thread_flag(TIF_PER_TRAP);
	if (kprobe_page_fault(regs, 14))
		return;
	mm = current->mm;
	address = get_fault_address(regs);
	is_write = fault_is_write(regs);
	type = get_fault_type(regs);
	switch (type) {
	case KERNEL_FAULT:
		return handle_fault_error_nolock(regs, 0);
	case USER_FAULT:
	case GMAP_FAULT:
		if (faulthandler_disabled() || !mm)
			return handle_fault_error_nolock(regs, 0);
		break;
	}
	perf_sw_event(PERF_COUNT_SW_PAGE_FAULTS, 1, regs, address);
	flags = FAULT_FLAG_DEFAULT;
	if (user_mode(regs))
		flags |= FAULT_FLAG_USER;
	if (is_write)
		access = VM_WRITE;
	if (access == VM_WRITE)
		flags |= FAULT_FLAG_WRITE;
	if (!(flags & FAULT_FLAG_USER))
		goto lock_mmap;
	vma = lock_vma_under_rcu(mm, address);
	if (!vma)
		goto lock_mmap;
	if (!(vma->vm_flags & access)) {
		vma_end_read(vma);
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		return handle_fault_error_nolock(regs, SEGV_ACCERR);
	}
	fault = handle_mm_fault(vma, address, flags | FAULT_FLAG_VMA_LOCK, regs);
	if (!(fault & (VM_FAULT_RETRY | VM_FAULT_COMPLETED)))
		vma_end_read(vma);
	if (!(fault & VM_FAULT_RETRY)) {
		count_vm_vma_lock_event(VMA_LOCK_SUCCESS);
		if (unlikely(fault & VM_FAULT_ERROR))
			goto error;
		return;
	}
	count_vm_vma_lock_event(VMA_LOCK_RETRY);
	if (fault & VM_FAULT_MAJOR)
		flags |= FAULT_FLAG_TRIED;

	/* Quick path to respond to signals */
	if (fault_signal_pending(fault, regs)) {
		if (!user_mode(regs))
			handle_fault_error_nolock(regs, 0);
		return;
	}
lock_mmap:
	mmap_read_lock(mm);
	gmap = NULL;
	if (IS_ENABLED(CONFIG_PGSTE) && type == GMAP_FAULT) {
		gmap = (struct gmap *)S390_lowcore.gmap;
		current->thread.gmap_addr = address;
		current->thread.gmap_write_flag = !!(flags & FAULT_FLAG_WRITE);
		current->thread.gmap_int_code = regs->int_code & 0xffff;
		address = __gmap_translate(gmap, address);
		if (address == -EFAULT)
			return handle_fault_error(regs, SEGV_MAPERR);
		if (gmap->pfault_enabled)
			flags |= FAULT_FLAG_RETRY_NOWAIT;
	}
retry:
	vma = find_vma(mm, address);
	if (!vma)
		return handle_fault_error(regs, SEGV_MAPERR);
	if (unlikely(vma->vm_start > address)) {
		if (!(vma->vm_flags & VM_GROWSDOWN))
			return handle_fault_error(regs, SEGV_MAPERR);
		vma = expand_stack(mm, address);
		if (!vma)
			return handle_fault_error_nolock(regs, SEGV_MAPERR);
	}
	if (unlikely(!(vma->vm_flags & access)))
		return handle_fault_error(regs, SEGV_ACCERR);
	fault = handle_mm_fault(vma, address, flags, regs);
	if (fault_signal_pending(fault, regs)) {
		if (flags & FAULT_FLAG_RETRY_NOWAIT)
			mmap_read_unlock(mm);
		if (!user_mode(regs))
			handle_fault_error_nolock(regs, 0);
		return;
	}
	/* The fault is fully completed (including releasing mmap lock) */
	if (fault & VM_FAULT_COMPLETED) {
		if (gmap) {
			mmap_read_lock(mm);
			goto gmap;
		}
		return;
	}
	if (unlikely(fault & VM_FAULT_ERROR)) {
		mmap_read_unlock(mm);
		goto error;
	}
	if (fault & VM_FAULT_RETRY) {
		if (IS_ENABLED(CONFIG_PGSTE) && gmap &&	(flags & FAULT_FLAG_RETRY_NOWAIT)) {
			/*
			 * FAULT_FLAG_RETRY_NOWAIT has been set,
			 * mmap_lock has not been released
			 */
			current->thread.gmap_pfault = 1;
			return handle_fault_error(regs, 0);
		}
		flags &= ~FAULT_FLAG_RETRY_NOWAIT;
		flags |= FAULT_FLAG_TRIED;
		mmap_read_lock(mm);
		goto retry;
	}
gmap:
	if (IS_ENABLED(CONFIG_PGSTE) && gmap) {
		address =  __gmap_link(gmap, current->thread.gmap_addr,
				       address);
		if (address == -EFAULT)
			return handle_fault_error(regs, SEGV_MAPERR);
		if (address == -ENOMEM) {
			fault = VM_FAULT_OOM;
			mmap_read_unlock(mm);
			goto error;
		}
	}
	mmap_read_unlock(mm);
	return;
error:
	if (fault & VM_FAULT_OOM) {
		if (!user_mode(regs))
			handle_fault_error_nolock(regs, 0);
		else
			pagefault_out_of_memory();
	} else if (fault & VM_FAULT_SIGSEGV) {
		if (!user_mode(regs))
			handle_fault_error_nolock(regs, 0);
		else
			do_sigsegv(regs, SEGV_MAPERR);
	} else if (fault & VM_FAULT_SIGBUS) {
		if (!user_mode(regs))
			handle_fault_error_nolock(regs, 0);
		else
			do_sigbus(regs);
	} else {
		BUG();
	}
}

void do_protection_exception(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };

	/*
	 * Protection exceptions are suppressing, decrement psw address.
	 * The exception to this rule are aborted transactions, for these
	 * the PSW already points to the correct location.
	 */
	if (!(regs->int_code & 0x200))
		regs->psw.addr = __rewind_psw(regs->psw, regs->int_code >> 16);
	/*
	 * Check for low-address protection.  This needs to be treated
	 * as a special case because the translation exception code
	 * field is not guaranteed to contain valid data in this case.
	 */
	if (unlikely(!teid.b61)) {
		if (user_mode(regs)) {
			/* Low-address protection in user mode: cannot happen */
			die(regs, "Low-address protection");
		}
		/*
		 * Low-address protection in kernel mode means
		 * NULL pointer write access in kernel mode.
		 */
		return handle_fault_error_nolock(regs, 0);
	}
	if (unlikely(MACHINE_HAS_NX && teid.b56)) {
		regs->int_parm_long = (teid.addr * PAGE_SIZE) | (regs->psw.addr & PAGE_MASK);
		return handle_fault_error_nolock(regs, SEGV_ACCERR);
	}
	do_exception(regs, VM_WRITE);
}
NOKPROBE_SYMBOL(do_protection_exception);

void do_dat_exception(struct pt_regs *regs)
{
	do_exception(regs, VM_ACCESS_FLAGS);
}
NOKPROBE_SYMBOL(do_dat_exception);

#if IS_ENABLED(CONFIG_PGSTE)

void do_secure_storage_access(struct pt_regs *regs)
{
	union teid teid = { .val = regs->int_parm_long };
	unsigned long addr = get_fault_address(regs);
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	struct page *page;
	struct gmap *gmap;
	int rc;

	/*
	 * Bit 61 indicates if the address is valid, if it is not the
	 * kernel should be stopped or SIGSEGV should be sent to the
	 * process. Bit 61 is not reliable without the misc UV feature,
	 * therefore this needs to be checked too.
	 */
	if (uv_has_feature(BIT_UV_FEAT_MISC) && !teid.b61) {
		/*
		 * When this happens, userspace did something that it
		 * was not supposed to do, e.g. branching into secure
		 * memory. Trigger a segmentation fault.
		 */
		if (user_mode(regs)) {
			send_sig(SIGSEGV, current, 0);
			return;
		}
		/*
		 * The kernel should never run into this case and
		 * there is no way out of this situation.
		 */
		panic("Unexpected PGM 0x3d with TEID bit 61=0");
	}
	switch (get_fault_type(regs)) {
	case GMAP_FAULT:
		mm = current->mm;
		gmap = (struct gmap *)S390_lowcore.gmap;
		mmap_read_lock(mm);
		addr = __gmap_translate(gmap, addr);
		mmap_read_unlock(mm);
		if (IS_ERR_VALUE(addr))
			return handle_fault_error_nolock(regs, SEGV_MAPERR);
		fallthrough;
	case USER_FAULT:
		mm = current->mm;
		mmap_read_lock(mm);
		vma = find_vma(mm, addr);
		if (!vma)
			return handle_fault_error(regs, SEGV_MAPERR);
		page = follow_page(vma, addr, FOLL_WRITE | FOLL_GET);
		if (IS_ERR_OR_NULL(page)) {
			mmap_read_unlock(mm);
			break;
		}
		if (arch_make_page_accessible(page))
			send_sig(SIGSEGV, current, 0);
		put_page(page);
		mmap_read_unlock(mm);
		break;
	case KERNEL_FAULT:
		page = phys_to_page(addr);
		if (unlikely(!try_get_page(page)))
			break;
		rc = arch_make_page_accessible(page);
		put_page(page);
		if (rc)
			BUG();
		break;
	default:
		unreachable();
	}
}
NOKPROBE_SYMBOL(do_secure_storage_access);

void do_non_secure_storage_access(struct pt_regs *regs)
{
	struct gmap *gmap = (struct gmap *)S390_lowcore.gmap;
	unsigned long gaddr = get_fault_address(regs);

	if (WARN_ON_ONCE(get_fault_type(regs) != GMAP_FAULT))
		return handle_fault_error_nolock(regs, SEGV_MAPERR);
	if (gmap_convert_to_secure(gmap, gaddr) == -EINVAL)
		send_sig(SIGSEGV, current, 0);
}
NOKPROBE_SYMBOL(do_non_secure_storage_access);

void do_secure_storage_violation(struct pt_regs *regs)
{
	struct gmap *gmap = (struct gmap *)S390_lowcore.gmap;
	unsigned long gaddr = get_fault_address(regs);

	/*
	 * If the VM has been rebooted, its address space might still contain
	 * secure pages from the previous boot.
	 * Clear the page so it can be reused.
	 */
	if (!gmap_destroy_page(gmap, gaddr))
		return;
	/*
	 * Either KVM messed up the secure guest mapping or the same
	 * page is mapped into multiple secure guests.
	 *
	 * This exception is only triggered when a guest 2 is running
	 * and can therefore never occur in kernel context.
	 */
	pr_warn_ratelimited("Secure storage violation in task: %s, pid %d\n",
			    current->comm, current->pid);
	send_sig(SIGSEGV, current, 0);
}

#endif /* CONFIG_PGSTE */
