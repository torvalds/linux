// SPDX-License-Identifier: GPL-2.0
/*
 * arch/alpha/kernel/traps.c
 *
 * (C) Copyright 1994 Linus Torvalds
 */

/*
 * This file initializes the trap entry points
 */

#include <linux/jiffies.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/tty.h>
#include <linux/delay.h>
#include <linux/extable.h>
#include <linux/kallsyms.h>
#include <linux/ratelimit.h>

#include <asm/gentrap.h>
#include <linux/uaccess.h>
#include <asm/unaligned.h>
#include <asm/sysinfo.h>
#include <asm/hwrpb.h>
#include <asm/mmu_context.h>
#include <asm/special_insns.h>

#include "proto.h"

/* Work-around for some SRMs which mishandle opDEC faults.  */

static int opDEC_fix;

static void
opDEC_check(void)
{
	__asm__ __volatile__ (
	/* Load the address of... */
	"	br	$16, 1f\n"
	/* A stub instruction fault handler.  Just add 4 to the
	   pc and continue.  */
	"	ldq	$16, 8($sp)\n"
	"	addq	$16, 4, $16\n"
	"	stq	$16, 8($sp)\n"
	"	call_pal %[rti]\n"
	/* Install the instruction fault handler.  */
	"1:	lda	$17, 3\n"
	"	call_pal %[wrent]\n"
	/* With that in place, the fault from the round-to-minf fp
	   insn will arrive either at the "lda 4" insn (bad) or one
	   past that (good).  This places the correct fixup in %0.  */
	"	lda %[fix], 0\n"
	"	cvttq/svm $f31,$f31\n"
	"	lda %[fix], 4"
	: [fix] "=r" (opDEC_fix)
	: [rti] "n" (PAL_rti), [wrent] "n" (PAL_wrent)
	: "$0", "$1", "$16", "$17", "$22", "$23", "$24", "$25");

	if (opDEC_fix)
		printk("opDEC fixup enabled.\n");
}

void
dik_show_regs(struct pt_regs *regs, unsigned long *r9_15)
{
	printk("pc = [<%016lx>]  ra = [<%016lx>]  ps = %04lx    %s\n",
	       regs->pc, regs->r26, regs->ps, print_tainted());
	printk("pc is at %pSR\n", (void *)regs->pc);
	printk("ra is at %pSR\n", (void *)regs->r26);
	printk("v0 = %016lx  t0 = %016lx  t1 = %016lx\n",
	       regs->r0, regs->r1, regs->r2);
	printk("t2 = %016lx  t3 = %016lx  t4 = %016lx\n",
 	       regs->r3, regs->r4, regs->r5);
	printk("t5 = %016lx  t6 = %016lx  t7 = %016lx\n",
	       regs->r6, regs->r7, regs->r8);

	if (r9_15) {
		printk("s0 = %016lx  s1 = %016lx  s2 = %016lx\n",
		       r9_15[9], r9_15[10], r9_15[11]);
		printk("s3 = %016lx  s4 = %016lx  s5 = %016lx\n",
		       r9_15[12], r9_15[13], r9_15[14]);
		printk("s6 = %016lx\n", r9_15[15]);
	}

	printk("a0 = %016lx  a1 = %016lx  a2 = %016lx\n",
	       regs->r16, regs->r17, regs->r18);
	printk("a3 = %016lx  a4 = %016lx  a5 = %016lx\n",
 	       regs->r19, regs->r20, regs->r21);
 	printk("t8 = %016lx  t9 = %016lx  t10= %016lx\n",
	       regs->r22, regs->r23, regs->r24);
	printk("t11= %016lx  pv = %016lx  at = %016lx\n",
	       regs->r25, regs->r27, regs->r28);
	printk("gp = %016lx  sp = %p\n", regs->gp, regs+1);
#if 0
__halt();
#endif
}

#if 0
static char * ireg_name[] = {"v0", "t0", "t1", "t2", "t3", "t4", "t5", "t6",
			   "t7", "s0", "s1", "s2", "s3", "s4", "s5", "s6",
			   "a0", "a1", "a2", "a3", "a4", "a5", "t8", "t9",
			   "t10", "t11", "ra", "pv", "at", "gp", "sp", "zero"};
#endif

static void
dik_show_code(unsigned int *pc)
{
	long i;

	printk("Code:");
	for (i = -6; i < 2; i++) {
		unsigned int insn;
		if (__get_user(insn, (unsigned int __user *)pc + i))
			break;
		printk("%c%08x%c", i ? ' ' : '<', insn, i ? ' ' : '>');
	}
	printk("\n");
}

static void
dik_show_trace(unsigned long *sp, const char *loglvl)
{
	long i = 0;
	printk("%sTrace:\n", loglvl);
	while (0x1ff8 & (unsigned long) sp) {
		extern char _stext[], _etext[];
		unsigned long tmp = *sp;
		sp++;
		if (!is_kernel_text(tmp))
			continue;
		printk("%s[<%lx>] %pSR\n", loglvl, tmp, (void *)tmp);
		if (i > 40) {
			printk("%s ...", loglvl);
			break;
		}
	}
	printk("%s\n", loglvl);
}

static int kstack_depth_to_print = 24;

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	unsigned long *stack;
	int i;

	/*
	 * debugging aid: "show_stack(NULL, NULL, KERN_EMERG);" prints the
	 * back trace for this cpu.
	 */
	if(sp==NULL)
		sp=(unsigned long*)&sp;

	stack = sp;
	for(i=0; i < kstack_depth_to_print; i++) {
		if (((long) stack & (THREAD_SIZE-1)) == 0)
			break;
		if ((i % 4) == 0) {
			if (i)
				pr_cont("\n");
			printk("%s       ", loglvl);
		} else {
			pr_cont(" ");
		}
		pr_cont("%016lx", *stack++);
	}
	pr_cont("\n");
	dik_show_trace(sp, loglvl);
}

void
die_if_kernel(char * str, struct pt_regs *regs, long err, unsigned long *r9_15)
{
	if (regs->ps & 8)
		return;
#ifdef CONFIG_SMP
	printk("CPU %d ", hard_smp_processor_id());
#endif
	printk("%s(%d): %s %ld\n", current->comm, task_pid_nr(current), str, err);
	dik_show_regs(regs, r9_15);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	dik_show_trace((unsigned long *)(regs+1), KERN_DEFAULT);
	dik_show_code((unsigned int *)regs->pc);

	if (test_and_set_thread_flag (TIF_DIE_IF_KERNEL)) {
		printk("die_if_kernel recursion detected.\n");
		local_irq_enable();
		while (1);
	}
	do_exit(SIGSEGV);
}

#ifndef CONFIG_MATHEMU
static long dummy_emul(void) { return 0; }
long (*alpha_fp_emul_imprecise)(struct pt_regs *regs, unsigned long writemask)
  = (void *)dummy_emul;
EXPORT_SYMBOL_GPL(alpha_fp_emul_imprecise);
long (*alpha_fp_emul) (unsigned long pc)
  = (void *)dummy_emul;
EXPORT_SYMBOL_GPL(alpha_fp_emul);
#else
long alpha_fp_emul_imprecise(struct pt_regs *regs, unsigned long writemask);
long alpha_fp_emul (unsigned long pc);
#endif

asmlinkage void
do_entArith(unsigned long summary, unsigned long write_mask,
	    struct pt_regs *regs)
{
	long si_code = FPE_FLTINV;

	if (summary & 1) {
		/* Software-completion summary bit is set, so try to
		   emulate the instruction.  If the processor supports
		   precise exceptions, we don't have to search.  */
		if (!amask(AMASK_PRECISE_TRAP))
			si_code = alpha_fp_emul(regs->pc - 4);
		else
			si_code = alpha_fp_emul_imprecise(regs, write_mask);
		if (si_code == 0)
			return;
	}
	die_if_kernel("Arithmetic fault", regs, 0, NULL);

	send_sig_fault_trapno(SIGFPE, si_code, (void __user *) regs->pc, 0, current);
}

asmlinkage void
do_entIF(unsigned long type, struct pt_regs *regs)
{
	int signo, code;

	if ((regs->ps & ~IPL_MAX) == 0) {
		if (type == 1) {
			const unsigned int *data
			  = (const unsigned int *) regs->pc;
			printk("Kernel bug at %s:%d\n",
			       (const char *)(data[1] | (long)data[2] << 32), 
			       data[0]);
		}
#ifdef CONFIG_ALPHA_WTINT
		if (type == 4) {
			/* If CALL_PAL WTINT is totally unsupported by the
			   PALcode, e.g. MILO, "emulate" it by overwriting
			   the insn.  */
			unsigned int *pinsn
			  = (unsigned int *) regs->pc - 1;
			if (*pinsn == PAL_wtint) {
				*pinsn = 0x47e01400; /* mov 0,$0 */
				imb();
				regs->r0 = 0;
				return;
			}
		}
#endif /* ALPHA_WTINT */
		die_if_kernel((type == 1 ? "Kernel Bug" : "Instruction fault"),
			      regs, type, NULL);
	}

	switch (type) {
	      case 0: /* breakpoint */
		if (ptrace_cancel_bpt(current)) {
			regs->pc -= 4;	/* make pc point to former bpt */
		}

		send_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *)regs->pc,
			       current);
		return;

	      case 1: /* bugcheck */
		send_sig_fault_trapno(SIGTRAP, TRAP_UNK,
				      (void __user *) regs->pc, 0, current);
		return;
		
	      case 2: /* gentrap */
		switch ((long) regs->r16) {
		case GEN_INTOVF:
			signo = SIGFPE;
			code = FPE_INTOVF;
			break;
		case GEN_INTDIV:
			signo = SIGFPE;
			code = FPE_INTDIV;
			break;
		case GEN_FLTOVF:
			signo = SIGFPE;
			code = FPE_FLTOVF;
			break;
		case GEN_FLTDIV:
			signo = SIGFPE;
			code = FPE_FLTDIV;
			break;
		case GEN_FLTUND:
			signo = SIGFPE;
			code = FPE_FLTUND;
			break;
		case GEN_FLTINV:
			signo = SIGFPE;
			code = FPE_FLTINV;
			break;
		case GEN_FLTINE:
			signo = SIGFPE;
			code = FPE_FLTRES;
			break;
		case GEN_ROPRAND:
			signo = SIGFPE;
			code = FPE_FLTUNK;
			break;

		case GEN_DECOVF:
		case GEN_DECDIV:
		case GEN_DECINV:
		case GEN_ASSERTERR:
		case GEN_NULPTRERR:
		case GEN_STKOVF:
		case GEN_STRLENERR:
		case GEN_SUBSTRERR:
		case GEN_RANGERR:
		case GEN_SUBRNG:
		case GEN_SUBRNG1:
		case GEN_SUBRNG2:
		case GEN_SUBRNG3:
		case GEN_SUBRNG4:
		case GEN_SUBRNG5:
		case GEN_SUBRNG6:
		case GEN_SUBRNG7:
		default:
			signo = SIGTRAP;
			code = TRAP_UNK;
			break;
		}

		send_sig_fault_trapno(signo, code, (void __user *) regs->pc,
				      regs->r16, current);
		return;

	      case 4: /* opDEC */
		if (implver() == IMPLVER_EV4) {
			long si_code;

			/* The some versions of SRM do not handle
			   the opDEC properly - they return the PC of the
			   opDEC fault, not the instruction after as the
			   Alpha architecture requires.  Here we fix it up.
			   We do this by intentionally causing an opDEC
			   fault during the boot sequence and testing if
			   we get the correct PC.  If not, we set a flag
			   to correct it every time through.  */
			regs->pc += opDEC_fix; 
			
			/* EV4 does not implement anything except normal
			   rounding.  Everything else will come here as
			   an illegal instruction.  Emulate them.  */
			si_code = alpha_fp_emul(regs->pc - 4);
			if (si_code == 0)
				return;
			if (si_code > 0) {
				send_sig_fault_trapno(SIGFPE, si_code,
						      (void __user *) regs->pc,
						      0, current);
				return;
			}
		}
		break;

	      case 3: /* FEN fault */
		/* Irritating users can call PAL_clrfen to disable the
		   FPU for the process.  The kernel will then trap in
		   do_switch_stack and undo_switch_stack when we try
		   to save and restore the FP registers.

		   Given that GCC by default generates code that uses the
		   FP registers, PAL_clrfen is not useful except for DoS
		   attacks.  So turn the bleeding FPU back on and be done
		   with it.  */
		current_thread_info()->pcb.flags |= 1;
		__reload_thread(&current_thread_info()->pcb);
		return;

	      case 5: /* illoc */
	      default: /* unexpected instruction-fault type */
		      ;
	}

	send_sig_fault(SIGILL, ILL_ILLOPC, (void __user *)regs->pc, current);
}

/* There is an ifdef in the PALcode in MILO that enables a 
   "kernel debugging entry point" as an unprivileged call_pal.

   We don't want to have anything to do with it, but unfortunately
   several versions of MILO included in distributions have it enabled,
   and if we don't put something on the entry point we'll oops.  */

asmlinkage void
do_entDbg(struct pt_regs *regs)
{
	die_if_kernel("Instruction fault", regs, 0, NULL);

	force_sig_fault(SIGILL, ILL_ILLOPC, (void __user *)regs->pc);
}


/*
 * entUna has a different register layout to be reasonably simple. It
 * needs access to all the integer registers (the kernel doesn't use
 * fp-regs), and it needs to have them in order for simpler access.
 *
 * Due to the non-standard register layout (and because we don't want
 * to handle floating-point regs), user-mode unaligned accesses are
 * handled separately by do_entUnaUser below.
 *
 * Oh, btw, we don't handle the "gp" register correctly, but if we fault
 * on a gp-register unaligned load/store, something is _very_ wrong
 * in the kernel anyway..
 */
struct allregs {
	unsigned long regs[32];
	unsigned long ps, pc, gp, a0, a1, a2;
};

struct unaligned_stat {
	unsigned long count, va, pc;
} unaligned[2];


/* Macro for exception fixup code to access integer registers.  */
#define una_reg(r)  (_regs[(r) >= 16 && (r) <= 18 ? (r)+19 : (r)])


asmlinkage void
do_entUna(void * va, unsigned long opcode, unsigned long reg,
	  struct allregs *regs)
{
	long error, tmp1, tmp2, tmp3, tmp4;
	unsigned long pc = regs->pc - 4;
	unsigned long *_regs = regs->regs;
	const struct exception_table_entry *fixup;

	unaligned[0].count++;
	unaligned[0].va = (unsigned long) va;
	unaligned[0].pc = pc;

	/* We don't want to use the generic get/put unaligned macros as
	   we want to trap exceptions.  Only if we actually get an
	   exception will we decide whether we should have caught it.  */

	switch (opcode) {
	case 0x0c: /* ldwu */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,1(%3)\n"
		"	extwl %1,%3,%1\n"
		"	extwh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;

	case 0x28: /* ldl */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = (int)(tmp1|tmp2);
		return;

	case 0x29: /* ldq */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto got_exception;
		una_reg(reg) = tmp1|tmp2;
		return;

	/* Note that the store sequences do not indicate that they change
	   memory because it _should_ be affecting nothing in this context.
	   (Otherwise we have other, much larger, problems.)  */
	case 0x0d: /* stw */
		__asm__ __volatile__(
		"1:	ldq_u %2,1(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inswh %6,%5,%4\n"
		"	inswl %6,%5,%3\n"
		"	mskwh %2,%5,%2\n"
		"	mskwl %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,1(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;

	case 0x2c: /* stl */
		__asm__ __volatile__(
		"1:	ldq_u %2,3(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inslh %6,%5,%4\n"
		"	insll %6,%5,%3\n"
		"	msklh %2,%5,%2\n"
		"	mskll %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,3(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;

	case 0x2d: /* stq */
		__asm__ __volatile__(
		"1:	ldq_u %2,7(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	insqh %6,%5,%4\n"
		"	insql %6,%5,%3\n"
		"	mskqh %2,%5,%2\n"
		"	mskql %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,7(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(una_reg(reg)), "0"(0));
		if (error)
			goto got_exception;
		return;
	}

	printk("Bad unaligned kernel access at %016lx: %p %lx %lu\n",
		pc, va, opcode, reg);
	do_exit(SIGSEGV);

got_exception:
	/* Ok, we caught the exception, but we don't want it.  Is there
	   someone to pass it along to?  */
	if ((fixup = search_exception_tables(pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(una_reg, fixup, pc);

		printk("Forwarding unaligned exception at %lx (%lx)\n",
		       pc, newpc);

		regs->pc = newpc;
		return;
	}

	/*
	 * Yikes!  No one to forward the exception to.
	 * Since the registers are in a weird format, dump them ourselves.
 	 */

	printk("%s(%d): unhandled unaligned exception\n",
	       current->comm, task_pid_nr(current));

	printk("pc = [<%016lx>]  ra = [<%016lx>]  ps = %04lx\n",
	       pc, una_reg(26), regs->ps);
	printk("r0 = %016lx  r1 = %016lx  r2 = %016lx\n",
	       una_reg(0), una_reg(1), una_reg(2));
	printk("r3 = %016lx  r4 = %016lx  r5 = %016lx\n",
 	       una_reg(3), una_reg(4), una_reg(5));
	printk("r6 = %016lx  r7 = %016lx  r8 = %016lx\n",
	       una_reg(6), una_reg(7), una_reg(8));
	printk("r9 = %016lx  r10= %016lx  r11= %016lx\n",
	       una_reg(9), una_reg(10), una_reg(11));
	printk("r12= %016lx  r13= %016lx  r14= %016lx\n",
	       una_reg(12), una_reg(13), una_reg(14));
	printk("r15= %016lx\n", una_reg(15));
	printk("r16= %016lx  r17= %016lx  r18= %016lx\n",
	       una_reg(16), una_reg(17), una_reg(18));
	printk("r19= %016lx  r20= %016lx  r21= %016lx\n",
 	       una_reg(19), una_reg(20), una_reg(21));
 	printk("r22= %016lx  r23= %016lx  r24= %016lx\n",
	       una_reg(22), una_reg(23), una_reg(24));
	printk("r25= %016lx  r27= %016lx  r28= %016lx\n",
	       una_reg(25), una_reg(27), una_reg(28));
	printk("gp = %016lx  sp = %p\n", regs->gp, regs+1);

	dik_show_code((unsigned int *)pc);
	dik_show_trace((unsigned long *)(regs+1), KERN_DEFAULT);

	if (test_and_set_thread_flag (TIF_DIE_IF_KERNEL)) {
		printk("die_if_kernel recursion detected.\n");
		local_irq_enable();
		while (1);
	}
	do_exit(SIGSEGV);
}

/*
 * Convert an s-floating point value in memory format to the
 * corresponding value in register format.  The exponent
 * needs to be remapped to preserve non-finite values
 * (infinities, not-a-numbers, denormals).
 */
static inline unsigned long
s_mem_to_reg (unsigned long s_mem)
{
	unsigned long frac    = (s_mem >>  0) & 0x7fffff;
	unsigned long sign    = (s_mem >> 31) & 0x1;
	unsigned long exp_msb = (s_mem >> 30) & 0x1;
	unsigned long exp_low = (s_mem >> 23) & 0x7f;
	unsigned long exp;

	exp = (exp_msb << 10) | exp_low;	/* common case */
	if (exp_msb) {
		if (exp_low == 0x7f) {
			exp = 0x7ff;
		}
	} else {
		if (exp_low == 0x00) {
			exp = 0x000;
		} else {
			exp |= (0x7 << 7);
		}
	}
	return (sign << 63) | (exp << 52) | (frac << 29);
}

/*
 * Convert an s-floating point value in register format to the
 * corresponding value in memory format.
 */
static inline unsigned long
s_reg_to_mem (unsigned long s_reg)
{
	return ((s_reg >> 62) << 30) | ((s_reg << 5) >> 34);
}

/*
 * Handle user-level unaligned fault.  Handling user-level unaligned
 * faults is *extremely* slow and produces nasty messages.  A user
 * program *should* fix unaligned faults ASAP.
 *
 * Notice that we have (almost) the regular kernel stack layout here,
 * so finding the appropriate registers is a little more difficult
 * than in the kernel case.
 *
 * Finally, we handle regular integer load/stores only.  In
 * particular, load-linked/store-conditionally and floating point
 * load/stores are not supported.  The former make no sense with
 * unaligned faults (they are guaranteed to fail) and I don't think
 * the latter will occur in any decent program.
 *
 * Sigh. We *do* have to handle some FP operations, because GCC will
 * uses them as temporary storage for integer memory to memory copies.
 * However, we need to deal with stt/ldt and sts/lds only.
 */

#define OP_INT_MASK	( 1L << 0x28 | 1L << 0x2c   /* ldl stl */	\
			| 1L << 0x29 | 1L << 0x2d   /* ldq stq */	\
			| 1L << 0x0c | 1L << 0x0d   /* ldwu stw */	\
			| 1L << 0x0a | 1L << 0x0e ) /* ldbu stb */

#define OP_WRITE_MASK	( 1L << 0x26 | 1L << 0x27   /* sts stt */	\
			| 1L << 0x2c | 1L << 0x2d   /* stl stq */	\
			| 1L << 0x0d | 1L << 0x0e ) /* stw stb */

#define R(x)	((size_t) &((struct pt_regs *)0)->x)

static int unauser_reg_offsets[32] = {
	R(r0), R(r1), R(r2), R(r3), R(r4), R(r5), R(r6), R(r7), R(r8),
	/* r9 ... r15 are stored in front of regs.  */
	-56, -48, -40, -32, -24, -16, -8,
	R(r16), R(r17), R(r18),
	R(r19), R(r20), R(r21), R(r22), R(r23), R(r24), R(r25), R(r26),
	R(r27), R(r28), R(gp),
	0, 0
};

#undef R

asmlinkage void
do_entUnaUser(void __user * va, unsigned long opcode,
	      unsigned long reg, struct pt_regs *regs)
{
	static DEFINE_RATELIMIT_STATE(ratelimit, 5 * HZ, 5);

	unsigned long tmp1, tmp2, tmp3, tmp4;
	unsigned long fake_reg, *reg_addr = &fake_reg;
	int si_code;
	long error;

	/* Check the UAC bits to decide what the user wants us to do
	   with the unaligned access.  */

	if (!(current_thread_info()->status & TS_UAC_NOPRINT)) {
		if (__ratelimit(&ratelimit)) {
			printk("%s(%d): unaligned trap at %016lx: %p %lx %ld\n",
			       current->comm, task_pid_nr(current),
			       regs->pc - 4, va, opcode, reg);
		}
	}
	if ((current_thread_info()->status & TS_UAC_SIGBUS))
		goto give_sigbus;
	/* Not sure why you'd want to use this, but... */
	if ((current_thread_info()->status & TS_UAC_NOFIX))
		return;

	/* Don't bother reading ds in the access check since we already
	   know that this came from the user.  Also rely on the fact that
	   the page at TASK_SIZE is unmapped and so can't be touched anyway. */
	if ((unsigned long)va >= TASK_SIZE)
		goto give_sigsegv;

	++unaligned[1].count;
	unaligned[1].va = (unsigned long)va;
	unaligned[1].pc = regs->pc - 4;

	if ((1L << opcode) & OP_INT_MASK) {
		/* it's an integer load/store */
		if (reg < 30) {
			reg_addr = (unsigned long *)
			  ((char *)regs + unauser_reg_offsets[reg]);
		} else if (reg == 30) {
			/* usp in PAL regs */
			fake_reg = rdusp();
		} else {
			/* zero "register" */
			fake_reg = 0;
		}
	}

	/* We don't want to use the generic get/put unaligned macros as
	   we want to trap exceptions.  Only if we actually get an
	   exception will we decide whether we should have caught it.  */

	switch (opcode) {
	case 0x0c: /* ldwu */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,1(%3)\n"
		"	extwl %1,%3,%1\n"
		"	extwh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = tmp1|tmp2;
		break;

	case 0x22: /* lds */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		alpha_write_fp_reg(reg, s_mem_to_reg((int)(tmp1|tmp2)));
		return;

	case 0x23: /* ldt */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		alpha_write_fp_reg(reg, tmp1|tmp2);
		return;

	case 0x28: /* ldl */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,3(%3)\n"
		"	extll %1,%3,%1\n"
		"	extlh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = (int)(tmp1|tmp2);
		break;

	case 0x29: /* ldq */
		__asm__ __volatile__(
		"1:	ldq_u %1,0(%3)\n"
		"2:	ldq_u %2,7(%3)\n"
		"	extql %1,%3,%1\n"
		"	extqh %2,%3,%2\n"
		"3:\n"
		EXC(1b,3b,%1,%0)
		EXC(2b,3b,%2,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2)
			: "r"(va), "0"(0));
		if (error)
			goto give_sigsegv;
		*reg_addr = tmp1|tmp2;
		break;

	/* Note that the store sequences do not indicate that they change
	   memory because it _should_ be affecting nothing in this context.
	   (Otherwise we have other, much larger, problems.)  */
	case 0x0d: /* stw */
		__asm__ __volatile__(
		"1:	ldq_u %2,1(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inswh %6,%5,%4\n"
		"	inswl %6,%5,%3\n"
		"	mskwh %2,%5,%2\n"
		"	mskwl %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,1(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	case 0x26: /* sts */
		fake_reg = s_reg_to_mem(alpha_read_fp_reg(reg));
		fallthrough;

	case 0x2c: /* stl */
		__asm__ __volatile__(
		"1:	ldq_u %2,3(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	inslh %6,%5,%4\n"
		"	insll %6,%5,%3\n"
		"	msklh %2,%5,%2\n"
		"	mskll %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,3(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	case 0x27: /* stt */
		fake_reg = alpha_read_fp_reg(reg);
		fallthrough;

	case 0x2d: /* stq */
		__asm__ __volatile__(
		"1:	ldq_u %2,7(%5)\n"
		"2:	ldq_u %1,0(%5)\n"
		"	insqh %6,%5,%4\n"
		"	insql %6,%5,%3\n"
		"	mskqh %2,%5,%2\n"
		"	mskql %1,%5,%1\n"
		"	or %2,%4,%2\n"
		"	or %1,%3,%1\n"
		"3:	stq_u %2,7(%5)\n"
		"4:	stq_u %1,0(%5)\n"
		"5:\n"
		EXC(1b,5b,%2,%0)
		EXC(2b,5b,%1,%0)
		EXC(3b,5b,$31,%0)
		EXC(4b,5b,$31,%0)
			: "=r"(error), "=&r"(tmp1), "=&r"(tmp2),
			  "=&r"(tmp3), "=&r"(tmp4)
			: "r"(va), "r"(*reg_addr), "0"(0));
		if (error)
			goto give_sigsegv;
		return;

	default:
		/* What instruction were you trying to use, exactly?  */
		goto give_sigbus;
	}

	/* Only integer loads should get here; everyone else returns early. */
	if (reg == 30)
		wrusp(fake_reg);
	return;

give_sigsegv:
	regs->pc -= 4;  /* make pc point to faulting insn */

	/* We need to replicate some of the logic in mm/fault.c,
	   since we don't have access to the fault code in the
	   exception handling return path.  */
	if ((unsigned long)va >= TASK_SIZE)
		si_code = SEGV_ACCERR;
	else {
		struct mm_struct *mm = current->mm;
		mmap_read_lock(mm);
		if (find_vma(mm, (unsigned long)va))
			si_code = SEGV_ACCERR;
		else
			si_code = SEGV_MAPERR;
		mmap_read_unlock(mm);
	}
	send_sig_fault(SIGSEGV, si_code, va, current);
	return;

give_sigbus:
	regs->pc -= 4;
	send_sig_fault(SIGBUS, BUS_ADRALN, va, current);
	return;
}

void
trap_init(void)
{
	/* Tell PAL-code what global pointer we want in the kernel.  */
	register unsigned long gptr __asm__("$29");
	wrkgp(gptr);

	/* Hack for Multia (UDB) and JENSEN: some of their SRMs have
	   a bug in the handling of the opDEC fault.  Fix it up if so.  */
	if (implver() == IMPLVER_EV4)
		opDEC_check();

	wrent(entArith, 1);
	wrent(entMM, 2);
	wrent(entIF, 3);
	wrent(entUna, 4);
	wrent(entSys, 5);
	wrent(entDbg, 6);
}
