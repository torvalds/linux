// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2012-2014 Andy Lutomirski <luto@amacapital.net>
 *
 * Based on the original implementation which is:
 *  Copyright (C) 2001 Andrea Arcangeli <andrea@suse.de> SuSE
 *  Copyright 2003 Andi Kleen, SuSE Labs.
 *
 *  Parts of the original code have been moved to arch/x86/vdso/vma.c
 *
 * This file implements vsyscall emulation.  vsyscalls are a legacy ABI:
 * Userspace can request certain kernel services by calling fixed
 * addresses.  This concept is problematic:
 *
 * - It interferes with ASLR.
 * - It's awkward to write code that lives in kernel addresses but is
 *   callable by userspace at fixed addresses.
 * - The whole concept is impossible for 32-bit compat userspace.
 * - UML cannot easily virtualize a vsyscall.
 *
 * As of mid-2014, I believe that there is no new userspace code that
 * will use a vsyscall if the vDSO is present.  I hope that there will
 * soon be no new userspace code that will ever use a vsyscall.
 *
 * The code in this file emulates vsyscalls when notified of a page
 * fault to a vsyscall address.
 */

#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/sched/signal.h>
#include <linux/mm_types.h>
#include <linux/syscalls.h>
#include <linux/ratelimit.h>

#include <asm/vsyscall.h>
#include <asm/unistd.h>
#include <asm/fixmap.h>
#include <asm/traps.h>
#include <asm/paravirt.h>

#define CREATE_TRACE_POINTS
#include "vsyscall_trace.h"

static enum { EMULATE, NONE } vsyscall_mode =
#ifdef CONFIG_LEGACY_VSYSCALL_NONE
	NONE;
#else
	EMULATE;
#endif

static int __init vsyscall_setup(char *str)
{
	if (str) {
		if (!strcmp("emulate", str))
			vsyscall_mode = EMULATE;
		else if (!strcmp("none", str))
			vsyscall_mode = NONE;
		else
			return -EINVAL;

		return 0;
	}

	return -EINVAL;
}
early_param("vsyscall", vsyscall_setup);

static void warn_bad_vsyscall(const char *level, struct pt_regs *regs,
			      const char *message)
{
	if (!show_unhandled_signals)
		return;

	printk_ratelimited("%s%s[%d] %s ip:%lx cs:%lx sp:%lx ax:%lx si:%lx di:%lx\n",
			   level, current->comm, task_pid_nr(current),
			   message, regs->ip, regs->cs,
			   regs->sp, regs->ax, regs->si, regs->di);
}

static int addr_to_vsyscall_nr(unsigned long addr)
{
	int nr;

	if ((addr & ~0xC00UL) != VSYSCALL_ADDR)
		return -EINVAL;

	nr = (addr & 0xC00UL) >> 10;
	if (nr >= 3)
		return -EINVAL;

	return nr;
}

static bool write_ok_or_segv(unsigned long ptr, size_t size)
{
	/*
	 * XXX: if access_ok, get_user, and put_user handled
	 * sig_on_uaccess_err, this could go away.
	 */

	if (!access_ok(VERIFY_WRITE, (void __user *)ptr, size)) {
		siginfo_t info;
		struct thread_struct *thread = &current->thread;

		thread->error_code	= 6;  /* user fault, no page, write */
		thread->cr2		= ptr;
		thread->trap_nr		= X86_TRAP_PF;

		clear_siginfo(&info);
		info.si_signo		= SIGSEGV;
		info.si_errno		= 0;
		info.si_code		= SEGV_MAPERR;
		info.si_addr		= (void __user *)ptr;

		force_sig_info(SIGSEGV, &info, current);
		return false;
	} else {
		return true;
	}
}

bool emulate_vsyscall(struct pt_regs *regs, unsigned long address)
{
	struct task_struct *tsk;
	unsigned long caller;
	int vsyscall_nr, syscall_nr, tmp;
	int prev_sig_on_uaccess_err;
	long ret;
	unsigned long orig_dx;

	/*
	 * No point in checking CS -- the only way to get here is a user mode
	 * trap to a high address, which means that we're in 64-bit user code.
	 */

	WARN_ON_ONCE(address != regs->ip);

	if (vsyscall_mode == NONE) {
		warn_bad_vsyscall(KERN_INFO, regs,
				  "vsyscall attempted with vsyscall=none");
		return false;
	}

	vsyscall_nr = addr_to_vsyscall_nr(address);

	trace_emulate_vsyscall(vsyscall_nr);

	if (vsyscall_nr < 0) {
		warn_bad_vsyscall(KERN_WARNING, regs,
				  "misaligned vsyscall (exploit attempt or buggy program) -- look up the vsyscall kernel parameter if you need a workaround");
		goto sigsegv;
	}

	if (get_user(caller, (unsigned long __user *)regs->sp) != 0) {
		warn_bad_vsyscall(KERN_WARNING, regs,
				  "vsyscall with bad stack (exploit attempt?)");
		goto sigsegv;
	}

	tsk = current;

	/*
	 * Check for access_ok violations and find the syscall nr.
	 *
	 * NULL is a valid user pointer (in the access_ok sense) on 32-bit and
	 * 64-bit, so we don't need to special-case it here.  For all the
	 * vsyscalls, NULL means "don't write anything" not "write it at
	 * address 0".
	 */
	switch (vsyscall_nr) {
	case 0:
		if (!write_ok_or_segv(regs->di, sizeof(struct timeval)) ||
		    !write_ok_or_segv(regs->si, sizeof(struct timezone))) {
			ret = -EFAULT;
			goto check_fault;
		}

		syscall_nr = __NR_gettimeofday;
		break;

	case 1:
		if (!write_ok_or_segv(regs->di, sizeof(time_t))) {
			ret = -EFAULT;
			goto check_fault;
		}

		syscall_nr = __NR_time;
		break;

	case 2:
		if (!write_ok_or_segv(regs->di, sizeof(unsigned)) ||
		    !write_ok_or_segv(regs->si, sizeof(unsigned))) {
			ret = -EFAULT;
			goto check_fault;
		}

		syscall_nr = __NR_getcpu;
		break;
	}

	/*
	 * Handle seccomp.  regs->ip must be the original value.
	 * See seccomp_send_sigsys and Documentation/userspace-api/seccomp_filter.rst.
	 *
	 * We could optimize the seccomp disabled case, but performance
	 * here doesn't matter.
	 */
	regs->orig_ax = syscall_nr;
	regs->ax = -ENOSYS;
	tmp = secure_computing(NULL);
	if ((!tmp && regs->orig_ax != syscall_nr) || regs->ip != address) {
		warn_bad_vsyscall(KERN_DEBUG, regs,
				  "seccomp tried to change syscall nr or ip");
		do_exit(SIGSYS);
	}
	regs->orig_ax = -1;
	if (tmp)
		goto do_ret;  /* skip requested */

	/*
	 * With a real vsyscall, page faults cause SIGSEGV.  We want to
	 * preserve that behavior to make writing exploits harder.
	 */
	prev_sig_on_uaccess_err = current->thread.sig_on_uaccess_err;
	current->thread.sig_on_uaccess_err = 1;

	ret = -EFAULT;
	switch (vsyscall_nr) {
	case 0:
		/* this decodes regs->di and regs->si on its own */
		ret = __x64_sys_gettimeofday(regs);
		break;

	case 1:
		/* this decodes regs->di on its own */
		ret = __x64_sys_time(regs);
		break;

	case 2:
		/* while we could clobber regs->dx, we didn't in the past... */
		orig_dx = regs->dx;
		regs->dx = 0;
		/* this decodes regs->di, regs->si and regs->dx on its own */
		ret = __x64_sys_getcpu(regs);
		regs->dx = orig_dx;
		break;
	}

	current->thread.sig_on_uaccess_err = prev_sig_on_uaccess_err;

check_fault:
	if (ret == -EFAULT) {
		/* Bad news -- userspace fed a bad pointer to a vsyscall. */
		warn_bad_vsyscall(KERN_INFO, regs,
				  "vsyscall fault (exploit attempt?)");

		/*
		 * If we failed to generate a signal for any reason,
		 * generate one here.  (This should be impossible.)
		 */
		if (WARN_ON_ONCE(!sigismember(&tsk->pending.signal, SIGBUS) &&
				 !sigismember(&tsk->pending.signal, SIGSEGV)))
			goto sigsegv;

		return true;  /* Don't emulate the ret. */
	}

	regs->ax = ret;

do_ret:
	/* Emulate a ret instruction. */
	regs->ip = caller;
	regs->sp += 8;
	return true;

sigsegv:
	force_sig(SIGSEGV, current);
	return true;
}

/*
 * A pseudo VMA to allow ptrace access for the vsyscall page.  This only
 * covers the 64bit vsyscall page now. 32bit has a real VMA now and does
 * not need special handling anymore:
 */
static const char *gate_vma_name(struct vm_area_struct *vma)
{
	return "[vsyscall]";
}
static const struct vm_operations_struct gate_vma_ops = {
	.name = gate_vma_name,
};
static struct vm_area_struct gate_vma = {
	.vm_start	= VSYSCALL_ADDR,
	.vm_end		= VSYSCALL_ADDR + PAGE_SIZE,
	.vm_page_prot	= PAGE_READONLY_EXEC,
	.vm_flags	= VM_READ | VM_EXEC,
	.vm_ops		= &gate_vma_ops,
};

struct vm_area_struct *get_gate_vma(struct mm_struct *mm)
{
#ifdef CONFIG_COMPAT
	if (!mm || mm->context.ia32_compat)
		return NULL;
#endif
	if (vsyscall_mode == NONE)
		return NULL;
	return &gate_vma;
}

int in_gate_area(struct mm_struct *mm, unsigned long addr)
{
	struct vm_area_struct *vma = get_gate_vma(mm);

	if (!vma)
		return 0;

	return (addr >= vma->vm_start) && (addr < vma->vm_end);
}

/*
 * Use this when you have no reliable mm, typically from interrupt
 * context. It is less reliable than using a task's mm and may give
 * false positives.
 */
int in_gate_area_no_mm(unsigned long addr)
{
	return vsyscall_mode != NONE && (addr & PAGE_MASK) == VSYSCALL_ADDR;
}

/*
 * The VSYSCALL page is the only user-accessible page in the kernel address
 * range.  Normally, the kernel page tables can have _PAGE_USER clear, but
 * the tables covering VSYSCALL_ADDR need _PAGE_USER set if vsyscalls
 * are enabled.
 *
 * Some day we may create a "minimal" vsyscall mode in which we emulate
 * vsyscalls but leave the page not present.  If so, we skip calling
 * this.
 */
void __init set_vsyscall_pgtable_user_bits(pgd_t *root)
{
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	pgd = pgd_offset_pgd(root, VSYSCALL_ADDR);
	set_pgd(pgd, __pgd(pgd_val(*pgd) | _PAGE_USER));
	p4d = p4d_offset(pgd, VSYSCALL_ADDR);
#if CONFIG_PGTABLE_LEVELS >= 5
	set_p4d(p4d, __p4d(p4d_val(*p4d) | _PAGE_USER));
#endif
	pud = pud_offset(p4d, VSYSCALL_ADDR);
	set_pud(pud, __pud(pud_val(*pud) | _PAGE_USER));
	pmd = pmd_offset(pud, VSYSCALL_ADDR);
	set_pmd(pmd, __pmd(pmd_val(*pmd) | _PAGE_USER));
}

void __init map_vsyscall(void)
{
	extern char __vsyscall_page;
	unsigned long physaddr_vsyscall = __pa_symbol(&__vsyscall_page);

	if (vsyscall_mode != NONE) {
		__set_fixmap(VSYSCALL_PAGE, physaddr_vsyscall,
			     PAGE_KERNEL_VVAR);
		set_vsyscall_pgtable_user_bits(swapper_pg_dir);
	}

	BUILD_BUG_ON((unsigned long)__fix_to_virt(VSYSCALL_PAGE) !=
		     (unsigned long)VSYSCALL_ADDR);
}
