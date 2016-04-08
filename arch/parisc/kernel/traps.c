/*
 *  linux/arch/parisc/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *  Copyright (C) 1999, 2000  Philipp Rumpf <prumpf@tux.org>
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/bug.h>

#include <asm/assembly.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <asm/smp.h>
#include <asm/pdc.h>
#include <asm/pdc_chassis.h>
#include <asm/unwind.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

#include "../math-emu/math-emu.h"	/* for handle_fpe() */

#define PRINT_USER_FAULTS /* (turn this on if you want user faults to be */
			  /*  dumped to the console via printk)          */

#if defined(CONFIG_SMP) || defined(CONFIG_DEBUG_SPINLOCK)
DEFINE_SPINLOCK(pa_dbit_lock);
#endif

static void parisc_show_stack(struct task_struct *task, unsigned long *sp,
	struct pt_regs *regs);

static int printbinary(char *buf, unsigned long x, int nbits)
{
	unsigned long mask = 1UL << (nbits - 1);
	while (mask != 0) {
		*buf++ = (mask & x ? '1' : '0');
		mask >>= 1;
	}
	*buf = '\0';

	return nbits;
}

#ifdef CONFIG_64BIT
#define RFMT "%016lx"
#else
#define RFMT "%08lx"
#endif
#define FFMT "%016llx"	/* fpregs are 64-bit always */

#define PRINTREGS(lvl,r,f,fmt,x)	\
	printk("%s%s%02d-%02d  " fmt " " fmt " " fmt " " fmt "\n",	\
		lvl, f, (x), (x+3), (r)[(x)+0], (r)[(x)+1],		\
		(r)[(x)+2], (r)[(x)+3])

static void print_gr(char *level, struct pt_regs *regs)
{
	int i;
	char buf[64];

	printk("%s\n", level);
	printk("%s     YZrvWESTHLNXBCVMcbcbcbcbOGFRQPDI\n", level);
	printbinary(buf, regs->gr[0], 32);
	printk("%sPSW: %s %s\n", level, buf, print_tainted());

	for (i = 0; i < 32; i += 4)
		PRINTREGS(level, regs->gr, "r", RFMT, i);
}

static void print_fr(char *level, struct pt_regs *regs)
{
	int i;
	char buf[64];
	struct { u32 sw[2]; } s;

	/* FR are 64bit everywhere. Need to use asm to get the content
	 * of fpsr/fper1, and we assume that we won't have a FP Identify
	 * in our way, otherwise we're screwed.
	 * The fldd is used to restore the T-bit if there was one, as the
	 * store clears it anyway.
	 * PA2.0 book says "thou shall not use fstw on FPSR/FPERs" - T-Bone */
	asm volatile ("fstd %%fr0,0(%1)	\n\t"
		      "fldd 0(%1),%%fr0	\n\t"
		      : "=m" (s) : "r" (&s) : "r0");

	printk("%s\n", level);
	printk("%s      VZOUICununcqcqcqcqcqcrmunTDVZOUI\n", level);
	printbinary(buf, s.sw[0], 32);
	printk("%sFPSR: %s\n", level, buf);
	printk("%sFPER1: %08x\n", level, s.sw[1]);

	/* here we'll print fr0 again, tho it'll be meaningless */
	for (i = 0; i < 32; i += 4)
		PRINTREGS(level, regs->fr, "fr", FFMT, i);
}

void show_regs(struct pt_regs *regs)
{
	int i, user;
	char *level;
	unsigned long cr30, cr31;

	user = user_mode(regs);
	level = user ? KERN_DEBUG : KERN_CRIT;

	show_regs_print_info(level);

	print_gr(level, regs);

	for (i = 0; i < 8; i += 4)
		PRINTREGS(level, regs->sr, "sr", RFMT, i);

	if (user)
		print_fr(level, regs);

	cr30 = mfctl(30);
	cr31 = mfctl(31);
	printk("%s\n", level);
	printk("%sIASQ: " RFMT " " RFMT " IAOQ: " RFMT " " RFMT "\n",
	       level, regs->iasq[0], regs->iasq[1], regs->iaoq[0], regs->iaoq[1]);
	printk("%s IIR: %08lx    ISR: " RFMT "  IOR: " RFMT "\n",
	       level, regs->iir, regs->isr, regs->ior);
	printk("%s CPU: %8d   CR30: " RFMT " CR31: " RFMT "\n",
	       level, current_thread_info()->cpu, cr30, cr31);
	printk("%s ORIG_R28: " RFMT "\n", level, regs->orig_r28);

	if (user) {
		printk("%s IAOQ[0]: " RFMT "\n", level, regs->iaoq[0]);
		printk("%s IAOQ[1]: " RFMT "\n", level, regs->iaoq[1]);
		printk("%s RP(r2): " RFMT "\n", level, regs->gr[2]);
	} else {
		printk("%s IAOQ[0]: %pS\n", level, (void *) regs->iaoq[0]);
		printk("%s IAOQ[1]: %pS\n", level, (void *) regs->iaoq[1]);
		printk("%s RP(r2): %pS\n", level, (void *) regs->gr[2]);

		parisc_show_stack(current, NULL, regs);
	}
}

static void do_show_stack(struct unwind_frame_info *info)
{
	int i = 1;

	printk(KERN_CRIT "Backtrace:\n");
	while (i <= 16) {
		if (unwind_once(info) < 0 || info->ip == 0)
			break;

		if (__kernel_text_address(info->ip)) {
			printk(KERN_CRIT " [<" RFMT ">] %pS\n",
				info->ip, (void *) info->ip);
			i++;
		}
	}
	printk(KERN_CRIT "\n");
}

static void parisc_show_stack(struct task_struct *task, unsigned long *sp,
	struct pt_regs *regs)
{
	struct unwind_frame_info info;
	struct task_struct *t;

	t = task ? task : current;
	if (regs) {
		unwind_frame_init(&info, t, regs);
		goto show_stack;
	}

	if (t == current) {
		unsigned long sp;

HERE:
		asm volatile ("copy %%r30, %0" : "=r"(sp));
		{
			struct pt_regs r;

			memset(&r, 0, sizeof(struct pt_regs));
			r.iaoq[0] = (unsigned long)&&HERE;
			r.gr[2] = (unsigned long)__builtin_return_address(0);
			r.gr[30] = sp;

			unwind_frame_init(&info, current, &r);
		}
	} else {
		unwind_frame_init_from_blocked_task(&info, t);
	}

show_stack:
	do_show_stack(&info);
}

void show_stack(struct task_struct *t, unsigned long *sp)
{
	return parisc_show_stack(t, sp, NULL);
}

int is_valid_bugaddr(unsigned long iaoq)
{
	return 1;
}

void die_if_kernel(char *str, struct pt_regs *regs, long err)
{
	if (user_mode(regs)) {
		if (err == 0)
			return; /* STFU */

		printk(KERN_CRIT "%s (pid %d): %s (code %ld) at " RFMT "\n",
			current->comm, task_pid_nr(current), str, err, regs->iaoq[0]);
#ifdef PRINT_USER_FAULTS
		/* XXX for debugging only */
		show_regs(regs);
#endif
		return;
	}

	oops_in_progress = 1;

	oops_enter();

	/* Amuse the user in a SPARC fashion */
	if (err) printk(KERN_CRIT
			"      _______________________________ \n"
			"     < Your System ate a SPARC! Gah! >\n"
			"      ------------------------------- \n"
			"             \\   ^__^\n"
			"                 (__)\\       )\\/\\\n"
			"                  U  ||----w |\n"
			"                     ||     ||\n");
	
	/* unlock the pdc lock if necessary */
	pdc_emergency_unlock();

	/* maybe the kernel hasn't booted very far yet and hasn't been able 
	 * to initialize the serial or STI console. In that case we should 
	 * re-enable the pdc console, so that the user will be able to 
	 * identify the problem. */
	if (!console_drivers)
		pdc_console_restart();
	
	if (err)
		printk(KERN_CRIT "%s (pid %d): %s (code %ld)\n",
			current->comm, task_pid_nr(current), str, err);

	/* Wot's wrong wif bein' racy? */
	if (current->thread.flags & PARISC_KERNEL_DEATH) {
		printk(KERN_CRIT "%s() recursion detected.\n", __func__);
		local_irq_enable();
		while (1);
	}
	current->thread.flags |= PARISC_KERNEL_DEATH;

	show_regs(regs);
	dump_stack();
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops) {
		printk(KERN_EMERG "Fatal exception: panic in 5 seconds\n");
		ssleep(5);
		panic("Fatal exception");
	}

	oops_exit();
	do_exit(SIGSEGV);
}

int syscall_ipi(int (*syscall) (struct pt_regs *), struct pt_regs *regs)
{
	return syscall(regs);
}

/* gdb uses break 4,8 */
#define GDB_BREAK_INSN 0x10004
static void handle_gdb_break(struct pt_regs *regs, int wot)
{
	struct siginfo si;

	si.si_signo = SIGTRAP;
	si.si_errno = 0;
	si.si_code = wot;
	si.si_addr = (void __user *) (regs->iaoq[0] & ~3);
	force_sig_info(SIGTRAP, &si, current);
}

static void handle_break(struct pt_regs *regs)
{
	unsigned iir = regs->iir;

	if (unlikely(iir == PARISC_BUG_BREAK_INSN && !user_mode(regs))) {
		/* check if a BUG() or WARN() trapped here.  */
		enum bug_trap_type tt;
		tt = report_bug(regs->iaoq[0] & ~3, regs);
		if (tt == BUG_TRAP_TYPE_WARN) {
			regs->iaoq[0] += 4;
			regs->iaoq[1] += 4;
			return; /* return to next instruction when WARN_ON().  */
		}
		die_if_kernel("Unknown kernel breakpoint", regs,
			(tt == BUG_TRAP_TYPE_NONE) ? 9 : 0);
	}

#ifdef PRINT_USER_FAULTS
	if (unlikely(iir != GDB_BREAK_INSN)) {
		printk(KERN_DEBUG "break %d,%d: pid=%d command='%s'\n",
			iir & 31, (iir>>13) & ((1<<13)-1),
			task_pid_nr(current), current->comm);
		show_regs(regs);
	}
#endif

	/* send standard GDB signal */
	handle_gdb_break(regs, TRAP_BRKPT);
}

static void default_trap(int code, struct pt_regs *regs)
{
	printk(KERN_ERR "Trap %d on CPU %d\n", code, smp_processor_id());
	show_regs(regs);
}

void (*cpu_lpmc) (int code, struct pt_regs *regs) __read_mostly = default_trap;


void transfer_pim_to_trap_frame(struct pt_regs *regs)
{
    register int i;
    extern unsigned int hpmc_pim_data[];
    struct pdc_hpmc_pim_11 *pim_narrow;
    struct pdc_hpmc_pim_20 *pim_wide;

    if (boot_cpu_data.cpu_type >= pcxu) {

	pim_wide = (struct pdc_hpmc_pim_20 *)hpmc_pim_data;

	/*
	 * Note: The following code will probably generate a
	 * bunch of truncation error warnings from the compiler.
	 * Could be handled with an ifdef, but perhaps there
	 * is a better way.
	 */

	regs->gr[0] = pim_wide->cr[22];

	for (i = 1; i < 32; i++)
	    regs->gr[i] = pim_wide->gr[i];

	for (i = 0; i < 32; i++)
	    regs->fr[i] = pim_wide->fr[i];

	for (i = 0; i < 8; i++)
	    regs->sr[i] = pim_wide->sr[i];

	regs->iasq[0] = pim_wide->cr[17];
	regs->iasq[1] = pim_wide->iasq_back;
	regs->iaoq[0] = pim_wide->cr[18];
	regs->iaoq[1] = pim_wide->iaoq_back;

	regs->sar  = pim_wide->cr[11];
	regs->iir  = pim_wide->cr[19];
	regs->isr  = pim_wide->cr[20];
	regs->ior  = pim_wide->cr[21];
    }
    else {
	pim_narrow = (struct pdc_hpmc_pim_11 *)hpmc_pim_data;

	regs->gr[0] = pim_narrow->cr[22];

	for (i = 1; i < 32; i++)
	    regs->gr[i] = pim_narrow->gr[i];

	for (i = 0; i < 32; i++)
	    regs->fr[i] = pim_narrow->fr[i];

	for (i = 0; i < 8; i++)
	    regs->sr[i] = pim_narrow->sr[i];

	regs->iasq[0] = pim_narrow->cr[17];
	regs->iasq[1] = pim_narrow->iasq_back;
	regs->iaoq[0] = pim_narrow->cr[18];
	regs->iaoq[1] = pim_narrow->iaoq_back;

	regs->sar  = pim_narrow->cr[11];
	regs->iir  = pim_narrow->cr[19];
	regs->isr  = pim_narrow->cr[20];
	regs->ior  = pim_narrow->cr[21];
    }

    /*
     * The following fields only have meaning if we came through
     * another path. So just zero them here.
     */

    regs->ksp = 0;
    regs->kpc = 0;
    regs->orig_r28 = 0;
}


/*
 * This routine is called as a last resort when everything else
 * has gone clearly wrong. We get called for faults in kernel space,
 * and HPMC's.
 */
void parisc_terminate(char *msg, struct pt_regs *regs, int code, unsigned long offset)
{
	static DEFINE_SPINLOCK(terminate_lock);

	oops_in_progress = 1;

	set_eiem(0);
	local_irq_disable();
	spin_lock(&terminate_lock);

	/* unlock the pdc lock if necessary */
	pdc_emergency_unlock();

	/* restart pdc console if necessary */
	if (!console_drivers)
		pdc_console_restart();

	/* Not all paths will gutter the processor... */
	switch(code){

	case 1:
		transfer_pim_to_trap_frame(regs);
		break;

	default:
		/* Fall through */
		break;

	}
	    
	{
		/* show_stack(NULL, (unsigned long *)regs->gr[30]); */
		struct unwind_frame_info info;
		unwind_frame_init(&info, current, regs);
		do_show_stack(&info);
	}

	printk("\n");
	printk(KERN_CRIT "%s: Code=%d regs=%p (Addr=" RFMT ")\n",
			msg, code, regs, offset);
	show_regs(regs);

	spin_unlock(&terminate_lock);

	/* put soft power button back under hardware control;
	 * if the user had pressed it once at any time, the 
	 * system will shut down immediately right here. */
	pdc_soft_power_button(0);
	
	/* Call kernel panic() so reboot timeouts work properly 
	 * FIXME: This function should be on the list of
	 * panic notifiers, and we should call panic
	 * directly from the location that we wish. 
	 * e.g. We should not call panic from
	 * parisc_terminate, but rather the oter way around.
	 * This hack works, prints the panic message twice,
	 * and it enables reboot timers!
	 */
	panic(msg);
}

void notrace handle_interruption(int code, struct pt_regs *regs)
{
	unsigned long fault_address = 0;
	unsigned long fault_space = 0;
	struct siginfo si;

	if (code == 1)
	    pdc_console_restart();  /* switch back to pdc if HPMC */
	else
	    local_irq_enable();

	/* Security check:
	 * If the priority level is still user, and the
	 * faulting space is not equal to the active space
	 * then the user is attempting something in a space
	 * that does not belong to them. Kill the process.
	 *
	 * This is normally the situation when the user
	 * attempts to jump into the kernel space at the
	 * wrong offset, be it at the gateway page or a
	 * random location.
	 *
	 * We cannot normally signal the process because it
	 * could *be* on the gateway page, and processes
	 * executing on the gateway page can't have signals
	 * delivered.
	 * 
	 * We merely readjust the address into the users
	 * space, at a destination address of zero, and
	 * allow processing to continue.
	 */
	if (((unsigned long)regs->iaoq[0] & 3) &&
	    ((unsigned long)regs->iasq[0] != (unsigned long)regs->sr[7])) { 
		/* Kill the user process later */
		regs->iaoq[0] = 0 | 3;
		regs->iaoq[1] = regs->iaoq[0] + 4;
		regs->iasq[0] = regs->iasq[1] = regs->sr[7];
		regs->gr[0] &= ~PSW_B;
		return;
	}
	
#if 0
	printk(KERN_CRIT "Interruption # %d\n", code);
#endif

	switch(code) {

	case  1:
		/* High-priority machine check (HPMC) */
		
		/* set up a new led state on systems shipped with a LED State panel */
		pdc_chassis_send_status(PDC_CHASSIS_DIRECT_HPMC);

		parisc_terminate("High Priority Machine Check (HPMC)",
				regs, code, 0);
		/* NOT REACHED */
		
	case  2:
		/* Power failure interrupt */
		printk(KERN_CRIT "Power failure interrupt !\n");
		return;

	case  3:
		/* Recovery counter trap */
		regs->gr[0] &= ~PSW_R;
		if (user_space(regs))
			handle_gdb_break(regs, TRAP_TRACE);
		/* else this must be the start of a syscall - just let it run */
		return;

	case  5:
		/* Low-priority machine check */
		pdc_chassis_send_status(PDC_CHASSIS_DIRECT_LPMC);
		
		flush_cache_all();
		flush_tlb_all();
		cpu_lpmc(5, regs);
		return;

	case  6:
		/* Instruction TLB miss fault/Instruction page fault */
		fault_address = regs->iaoq[0];
		fault_space   = regs->iasq[0];
		break;

	case  8:
		/* Illegal instruction trap */
		die_if_kernel("Illegal instruction", regs, code);
		si.si_code = ILL_ILLOPC;
		goto give_sigill;

	case  9:
		/* Break instruction trap */
		handle_break(regs);
		return;

	case 10:
		/* Privileged operation trap */
		die_if_kernel("Privileged operation", regs, code);
		si.si_code = ILL_PRVOPC;
		goto give_sigill;

	case 11:
		/* Privileged register trap */
		if ((regs->iir & 0xffdfffe0) == 0x034008a0) {

			/* This is a MFCTL cr26/cr27 to gr instruction.
			 * PCXS traps on this, so we need to emulate it.
			 */

			if (regs->iir & 0x00200000)
				regs->gr[regs->iir & 0x1f] = mfctl(27);
			else
				regs->gr[regs->iir & 0x1f] = mfctl(26);

			regs->iaoq[0] = regs->iaoq[1];
			regs->iaoq[1] += 4;
			regs->iasq[0] = regs->iasq[1];
			return;
		}

		die_if_kernel("Privileged register usage", regs, code);
		si.si_code = ILL_PRVREG;
	give_sigill:
		si.si_signo = SIGILL;
		si.si_errno = 0;
		si.si_addr = (void __user *) regs->iaoq[0];
		force_sig_info(SIGILL, &si, current);
		return;

	case 12:
		/* Overflow Trap, let the userland signal handler do the cleanup */
		si.si_signo = SIGFPE;
		si.si_code = FPE_INTOVF;
		si.si_addr = (void __user *) regs->iaoq[0];
		force_sig_info(SIGFPE, &si, current);
		return;
		
	case 13:
		/* Conditional Trap
		   The condition succeeds in an instruction which traps
		   on condition  */
		if(user_mode(regs)){
			si.si_signo = SIGFPE;
			/* Set to zero, and let the userspace app figure it out from
			   the insn pointed to by si_addr */
			si.si_code = 0;
			si.si_addr = (void __user *) regs->iaoq[0];
			force_sig_info(SIGFPE, &si, current);
			return;
		} 
		/* The kernel doesn't want to handle condition codes */
		break;
		
	case 14:
		/* Assist Exception Trap, i.e. floating point exception. */
		die_if_kernel("Floating point exception", regs, 0); /* quiet */
		__inc_irq_stat(irq_fpassist_count);
		handle_fpe(regs);
		return;

	case 15:
		/* Data TLB miss fault/Data page fault */
		/* Fall through */
	case 16:
		/* Non-access instruction TLB miss fault */
		/* The instruction TLB entry needed for the target address of the FIC
		   is absent, and hardware can't find it, so we get to cleanup */
		/* Fall through */
	case 17:
		/* Non-access data TLB miss fault/Non-access data page fault */
		/* FIXME: 
			 Still need to add slow path emulation code here!
			 If the insn used a non-shadow register, then the tlb
			 handlers could not have their side-effect (e.g. probe
			 writing to a target register) emulated since rfir would
			 erase the changes to said register. Instead we have to
			 setup everything, call this function we are in, and emulate
			 by hand. Technically we need to emulate:
			 fdc,fdce,pdc,"fic,4f",prober,probeir,probew, probeiw
		*/
		fault_address = regs->ior;
		fault_space = regs->isr;
		break;

	case 18:
		/* PCXS only -- later cpu's split this into types 26,27 & 28 */
		/* Check for unaligned access */
		if (check_unaligned(regs)) {
			handle_unaligned(regs);
			return;
		}
		/* Fall Through */
	case 26: 
		/* PCXL: Data memory access rights trap */
		fault_address = regs->ior;
		fault_space   = regs->isr;
		break;

	case 19:
		/* Data memory break trap */
		regs->gr[0] |= PSW_X; /* So we can single-step over the trap */
		/* fall thru */
	case 21:
		/* Page reference trap */
		handle_gdb_break(regs, TRAP_HWBKPT);
		return;

	case 25:
		/* Taken branch trap */
		regs->gr[0] &= ~PSW_T;
		if (user_space(regs))
			handle_gdb_break(regs, TRAP_BRANCH);
		/* else this must be the start of a syscall - just let it
		 * run.
		 */
		return;

	case  7:  
		/* Instruction access rights */
		/* PCXL: Instruction memory protection trap */

		/*
		 * This could be caused by either: 1) a process attempting
		 * to execute within a vma that does not have execute
		 * permission, or 2) an access rights violation caused by a
		 * flush only translation set up by ptep_get_and_clear().
		 * So we check the vma permissions to differentiate the two.
		 * If the vma indicates we have execute permission, then
		 * the cause is the latter one. In this case, we need to
		 * call do_page_fault() to fix the problem.
		 */

		if (user_mode(regs)) {
			struct vm_area_struct *vma;

			down_read(&current->mm->mmap_sem);
			vma = find_vma(current->mm,regs->iaoq[0]);
			if (vma && (regs->iaoq[0] >= vma->vm_start)
				&& (vma->vm_flags & VM_EXEC)) {

				fault_address = regs->iaoq[0];
				fault_space = regs->iasq[0];

				up_read(&current->mm->mmap_sem);
				break; /* call do_page_fault() */
			}
			up_read(&current->mm->mmap_sem);
		}
		/* Fall Through */
	case 27: 
		/* Data memory protection ID trap */
		if (code == 27 && !user_mode(regs) &&
			fixup_exception(regs))
			return;

		die_if_kernel("Protection id trap", regs, code);
		si.si_code = SEGV_MAPERR;
		si.si_signo = SIGSEGV;
		si.si_errno = 0;
		if (code == 7)
		    si.si_addr = (void __user *) regs->iaoq[0];
		else
		    si.si_addr = (void __user *) regs->ior;
		force_sig_info(SIGSEGV, &si, current);
		return;

	case 28: 
		/* Unaligned data reference trap */
		handle_unaligned(regs);
		return;

	default:
		if (user_mode(regs)) {
#ifdef PRINT_USER_FAULTS
			printk(KERN_DEBUG "\nhandle_interruption() pid=%d command='%s'\n",
			    task_pid_nr(current), current->comm);
			show_regs(regs);
#endif
			/* SIGBUS, for lack of a better one. */
			si.si_signo = SIGBUS;
			si.si_code = BUS_OBJERR;
			si.si_errno = 0;
			si.si_addr = (void __user *) regs->ior;
			force_sig_info(SIGBUS, &si, current);
			return;
		}
		pdc_chassis_send_status(PDC_CHASSIS_DIRECT_PANIC);
		
		parisc_terminate("Unexpected interruption", regs, code, 0);
		/* NOT REACHED */
	}

	if (user_mode(regs)) {
	    if ((fault_space >> SPACEID_SHIFT) != (regs->sr[7] >> SPACEID_SHIFT)) {
#ifdef PRINT_USER_FAULTS
		if (fault_space == 0)
			printk(KERN_DEBUG "User Fault on Kernel Space ");
		else
			printk(KERN_DEBUG "User Fault (long pointer) (fault %d) ",
			       code);
		printk(KERN_CONT "pid=%d command='%s'\n",
		       task_pid_nr(current), current->comm);
		show_regs(regs);
#endif
		si.si_signo = SIGSEGV;
		si.si_errno = 0;
		si.si_code = SEGV_MAPERR;
		si.si_addr = (void __user *) regs->ior;
		force_sig_info(SIGSEGV, &si, current);
		return;
	    }
	}
	else {

	    /*
	     * The kernel should never fault on its own address space,
	     * unless pagefault_disable() was called before.
	     */

	    if (fault_space == 0 && !in_atomic())
	    {
		/* Clean up and return if in exception table. */
		if (fixup_exception(regs))
			return;
		pdc_chassis_send_status(PDC_CHASSIS_DIRECT_PANIC);
		parisc_terminate("Kernel Fault", regs, code, fault_address);
	    }
	}

	do_page_fault(regs, code, fault_address);
}


int __init check_ivt(void *iva)
{
	extern u32 os_hpmc_size;
	extern const u32 os_hpmc[];

	int i;
	u32 check = 0;
	u32 *ivap;
	u32 *hpmcp;
	u32 length;

	if (strcmp((char *)iva, "cows can fly"))
		return -1;

	ivap = (u32 *)iva;

	for (i = 0; i < 8; i++)
	    *ivap++ = 0;

	/* Compute Checksum for HPMC handler */
	length = os_hpmc_size;
	ivap[7] = length;

	hpmcp = (u32 *)os_hpmc;

	for (i=0; i<length/4; i++)
	    check += *hpmcp++;

	for (i=0; i<8; i++)
	    check += ivap[i];

	ivap[5] = -check;

	return 0;
}
	
#ifndef CONFIG_64BIT
extern const void fault_vector_11;
#endif
extern const void fault_vector_20;

void __init trap_init(void)
{
	void *iva;

	if (boot_cpu_data.cpu_type >= pcxu)
		iva = (void *) &fault_vector_20;
	else
#ifdef CONFIG_64BIT
		panic("Can't boot 64-bit OS on PA1.1 processor!");
#else
		iva = (void *) &fault_vector_11;
#endif

	if (check_ivt(iva))
		panic("IVT invalid");
}
