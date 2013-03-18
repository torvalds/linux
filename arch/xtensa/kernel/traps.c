/*
 * arch/xtensa/kernel/traps.c
 *
 * Exception handling.
 *
 * Derived from code with the following copyrights:
 * Copyright (C) 1994 - 1999 by Ralf Baechle
 * Modified for R3000 by Paul M. Antoine, 1995, 1996
 * Complete output from die() by Ulf Carlsson, 1998
 * Copyright (C) 1999 Silicon Graphics, Inc.
 *
 * Essentially rewritten for the Xtensa architecture port.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 *
 * Joe Taylor	<joe@tensilica.com, joetylr@yahoo.com>
 * Chris Zankel	<chris@zankel.net>
 * Marc Gauthier<marc@tensilica.com, marc@alumni.uwaterloo.ca>
 * Kevin Chea
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/kallsyms.h>
#include <linux/delay.h>
#include <linux/hardirq.h>

#include <asm/ptrace.h>
#include <asm/timex.h>
#include <asm/uaccess.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/traps.h>

#ifdef CONFIG_KGDB
extern int gdb_enter;
extern int return_from_debug_flag;
#endif

/*
 * Machine specific interrupt handlers
 */

extern void kernel_exception(void);
extern void user_exception(void);

extern void fast_syscall_kernel(void);
extern void fast_syscall_user(void);
extern void fast_alloca(void);
extern void fast_unaligned(void);
extern void fast_second_level_miss(void);
extern void fast_store_prohibited(void);
extern void fast_coprocessor(void);

extern void do_illegal_instruction (struct pt_regs*);
extern void do_interrupt (struct pt_regs*);
extern void do_unaligned_user (struct pt_regs*);
extern void do_multihit (struct pt_regs*, unsigned long);
extern void do_page_fault (struct pt_regs*, unsigned long);
extern void do_debug (struct pt_regs*);
extern void system_call (struct pt_regs*);

/*
 * The vector table must be preceded by a save area (which
 * implies it must be in RAM, unless one places RAM immediately
 * before a ROM and puts the vector at the start of the ROM (!))
 */

#define KRNL		0x01
#define USER		0x02

#define COPROCESSOR(x)							\
{ EXCCAUSE_COPROCESSOR ## x ## _DISABLED, USER, fast_coprocessor }

typedef struct {
	int cause;
	int fast;
	void* handler;
} dispatch_init_table_t;

static dispatch_init_table_t __initdata dispatch_init_table[] = {

{ EXCCAUSE_ILLEGAL_INSTRUCTION,	0,	   do_illegal_instruction},
{ EXCCAUSE_SYSTEM_CALL,		KRNL,	   fast_syscall_kernel },
{ EXCCAUSE_SYSTEM_CALL,		USER,	   fast_syscall_user },
{ EXCCAUSE_SYSTEM_CALL,		0,	   system_call },
/* EXCCAUSE_INSTRUCTION_FETCH unhandled */
/* EXCCAUSE_LOAD_STORE_ERROR unhandled*/
{ EXCCAUSE_LEVEL1_INTERRUPT,	0,	   do_interrupt },
{ EXCCAUSE_ALLOCA,		USER|KRNL, fast_alloca },
/* EXCCAUSE_INTEGER_DIVIDE_BY_ZERO unhandled */
/* EXCCAUSE_PRIVILEGED unhandled */
#if XCHAL_UNALIGNED_LOAD_EXCEPTION || XCHAL_UNALIGNED_STORE_EXCEPTION
#ifdef CONFIG_XTENSA_UNALIGNED_USER
{ EXCCAUSE_UNALIGNED,		USER,	   fast_unaligned },
#else
{ EXCCAUSE_UNALIGNED,		0,	   do_unaligned_user },
#endif
{ EXCCAUSE_UNALIGNED,		KRNL,	   fast_unaligned },
#endif
#ifdef CONFIG_MMU
{ EXCCAUSE_ITLB_MISS,		0,	   do_page_fault },
{ EXCCAUSE_ITLB_MISS,		USER|KRNL, fast_second_level_miss},
{ EXCCAUSE_ITLB_MULTIHIT,		0,	   do_multihit },
{ EXCCAUSE_ITLB_PRIVILEGE,	0,	   do_page_fault },
/* EXCCAUSE_SIZE_RESTRICTION unhandled */
{ EXCCAUSE_FETCH_CACHE_ATTRIBUTE,	0,	   do_page_fault },
{ EXCCAUSE_DTLB_MISS,		USER|KRNL, fast_second_level_miss},
{ EXCCAUSE_DTLB_MISS,		0,	   do_page_fault },
{ EXCCAUSE_DTLB_MULTIHIT,		0,	   do_multihit },
{ EXCCAUSE_DTLB_PRIVILEGE,	0,	   do_page_fault },
/* EXCCAUSE_DTLB_SIZE_RESTRICTION unhandled */
{ EXCCAUSE_STORE_CACHE_ATTRIBUTE,	USER|KRNL, fast_store_prohibited },
{ EXCCAUSE_STORE_CACHE_ATTRIBUTE,	0,	   do_page_fault },
{ EXCCAUSE_LOAD_CACHE_ATTRIBUTE,	0,	   do_page_fault },
#endif /* CONFIG_MMU */
/* XCCHAL_EXCCAUSE_FLOATING_POINT unhandled */
#if XTENSA_HAVE_COPROCESSOR(0)
COPROCESSOR(0),
#endif
#if XTENSA_HAVE_COPROCESSOR(1)
COPROCESSOR(1),
#endif
#if XTENSA_HAVE_COPROCESSOR(2)
COPROCESSOR(2),
#endif
#if XTENSA_HAVE_COPROCESSOR(3)
COPROCESSOR(3),
#endif
#if XTENSA_HAVE_COPROCESSOR(4)
COPROCESSOR(4),
#endif
#if XTENSA_HAVE_COPROCESSOR(5)
COPROCESSOR(5),
#endif
#if XTENSA_HAVE_COPROCESSOR(6)
COPROCESSOR(6),
#endif
#if XTENSA_HAVE_COPROCESSOR(7)
COPROCESSOR(7),
#endif
{ EXCCAUSE_MAPPED_DEBUG,		0,		do_debug },
{ -1, -1, 0 }

};

/* The exception table <exc_table> serves two functions:
 * 1. it contains three dispatch tables (fast_user, fast_kernel, default-c)
 * 2. it is a temporary memory buffer for the exception handlers.
 */

unsigned long exc_table[EXC_TABLE_SIZE/4];

void die(const char*, struct pt_regs*, long);

static inline void
__die_if_kernel(const char *str, struct pt_regs *regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

/*
 * Unhandled Exceptions. Kill user task or panic if in kernel space.
 */

void do_unhandled(struct pt_regs *regs, unsigned long exccause)
{
	__die_if_kernel("Caught unhandled exception - should not happen",
	    		regs, SIGKILL);

	/* If in user mode, send SIGILL signal to current process */
	printk("Caught unhandled exception in '%s' "
	       "(pid = %d, pc = %#010lx) - should not happen\n"
	       "\tEXCCAUSE is %ld\n",
	       current->comm, task_pid_nr(current), regs->pc, exccause);
	force_sig(SIGILL, current);
}

/*
 * Multi-hit exception. This if fatal!
 */

void do_multihit(struct pt_regs *regs, unsigned long exccause)
{
	die("Caught multihit exception", regs, SIGKILL);
}

/*
 * IRQ handler.
 * PS.INTLEVEL is the current IRQ priority level.
 */

extern void do_IRQ(int, struct pt_regs *);

void do_interrupt(struct pt_regs *regs)
{
	static const unsigned int_level_mask[] = {
		0,
		XCHAL_INTLEVEL1_MASK,
		XCHAL_INTLEVEL2_MASK,
		XCHAL_INTLEVEL3_MASK,
		XCHAL_INTLEVEL4_MASK,
		XCHAL_INTLEVEL5_MASK,
		XCHAL_INTLEVEL6_MASK,
		XCHAL_INTLEVEL7_MASK,
	};
	unsigned level = get_sr(ps) & PS_INTLEVEL_MASK;

	if (WARN_ON_ONCE(level >= ARRAY_SIZE(int_level_mask)))
		return;

	for (;;) {
		unsigned intread = get_sr(interrupt);
		unsigned intenable = get_sr(intenable);
		unsigned int_at_level = intread & intenable &
			int_level_mask[level];

		if (!int_at_level)
			return;

		/*
		 * Clear the interrupt before processing, in case it's
		 *  edge-triggered or software-generated
		 */
		while (int_at_level) {
			unsigned i = __ffs(int_at_level);
			unsigned mask = 1 << i;

			int_at_level ^= mask;
			set_sr(mask, intclear);
			do_IRQ(i, regs);
		}
	}
}

/*
 * Illegal instruction. Fatal if in kernel space.
 */

void
do_illegal_instruction(struct pt_regs *regs)
{
	__die_if_kernel("Illegal instruction in kernel", regs, SIGKILL);

	/* If in user mode, send SIGILL signal to current process. */

	printk("Illegal Instruction in '%s' (pid = %d, pc = %#010lx)\n",
	    current->comm, task_pid_nr(current), regs->pc);
	force_sig(SIGILL, current);
}


/*
 * Handle unaligned memory accesses from user space. Kill task.
 *
 * If CONFIG_UNALIGNED_USER is not set, we don't allow unaligned memory
 * accesses causes from user space.
 */

#if XCHAL_UNALIGNED_LOAD_EXCEPTION || XCHAL_UNALIGNED_STORE_EXCEPTION
#ifndef CONFIG_XTENSA_UNALIGNED_USER
void
do_unaligned_user (struct pt_regs *regs)
{
	siginfo_t info;

	__die_if_kernel("Unhandled unaligned exception in kernel",
	    		regs, SIGKILL);

	current->thread.bad_vaddr = regs->excvaddr;
	current->thread.error_code = -3;
	printk("Unaligned memory access to %08lx in '%s' "
	       "(pid = %d, pc = %#010lx)\n",
	       regs->excvaddr, current->comm, task_pid_nr(current), regs->pc);
	info.si_signo = SIGBUS;
	info.si_errno = 0;
	info.si_code = BUS_ADRALN;
	info.si_addr = (void *) regs->excvaddr;
	force_sig_info(SIGSEGV, &info, current);

}
#endif
#endif

void
do_debug(struct pt_regs *regs)
{
#ifdef CONFIG_KGDB
	/* If remote debugging is configured AND enabled, we give control to
	 * kgdb.  Otherwise, we fall through, perhaps giving control to the
	 * native debugger.
	 */

	if (gdb_enter) {
		extern void gdb_handle_exception(struct pt_regs *);
		gdb_handle_exception(regs);
		return_from_debug_flag = 1;
		return;
	}
#endif

	__die_if_kernel("Breakpoint in kernel", regs, SIGKILL);

	/* If in user mode, send SIGTRAP signal to current process */

	force_sig(SIGTRAP, current);
}


/* Set exception C handler - for temporary use when probing exceptions */

void * __init trap_set_handler(int cause, void *handler)
{
	unsigned long *entry = &exc_table[EXC_TABLE_DEFAULT / 4 + cause];
	void *previous = (void *)*entry;
	*entry = (unsigned long)handler;
	return previous;
}


/*
 * Initialize dispatch tables.
 *
 * The exception vectors are stored compressed the __init section in the
 * dispatch_init_table. This function initializes the following three tables
 * from that compressed table:
 * - fast user		first dispatch table for user exceptions
 * - fast kernel	first dispatch table for kernel exceptions
 * - default C-handler	C-handler called by the default fast handler.
 *
 * See vectors.S for more details.
 */

#define set_handler(idx,handler) (exc_table[idx] = (unsigned long) (handler))

void __init trap_init(void)
{
	int i;

	/* Setup default vectors. */

	for(i = 0; i < 64; i++) {
		set_handler(EXC_TABLE_FAST_USER/4   + i, user_exception);
		set_handler(EXC_TABLE_FAST_KERNEL/4 + i, kernel_exception);
		set_handler(EXC_TABLE_DEFAULT/4 + i, do_unhandled);
	}

	/* Setup specific handlers. */

	for(i = 0; dispatch_init_table[i].cause >= 0; i++) {

		int fast = dispatch_init_table[i].fast;
		int cause = dispatch_init_table[i].cause;
		void *handler = dispatch_init_table[i].handler;

		if (fast == 0)
			set_handler (EXC_TABLE_DEFAULT/4 + cause, handler);
		if (fast && fast & USER)
			set_handler (EXC_TABLE_FAST_USER/4 + cause, handler);
		if (fast && fast & KRNL)
			set_handler (EXC_TABLE_FAST_KERNEL/4 + cause, handler);
	}

	/* Initialize EXCSAVE_1 to hold the address of the exception table. */

	i = (unsigned long)exc_table;
	__asm__ __volatile__("wsr  %0, excsave1\n" : : "a" (i));
}

/*
 * This function dumps the current valid window frame and other base registers.
 */

void show_regs(struct pt_regs * regs)
{
	int i, wmask;

	wmask = regs->wmask & ~1;

	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			printk(KERN_INFO "a%02d:", i);
		printk(KERN_CONT " %08lx", regs->areg[i]);
	}
	printk(KERN_CONT "\n");

	printk("pc: %08lx, ps: %08lx, depc: %08lx, excvaddr: %08lx\n",
	       regs->pc, regs->ps, regs->depc, regs->excvaddr);
	printk("lbeg: %08lx, lend: %08lx lcount: %08lx, sar: %08lx\n",
	       regs->lbeg, regs->lend, regs->lcount, regs->sar);
	if (user_mode(regs))
		printk("wb: %08lx, ws: %08lx, wmask: %08lx, syscall: %ld\n",
		       regs->windowbase, regs->windowstart, regs->wmask,
		       regs->syscall);
}

static __always_inline unsigned long *stack_pointer(struct task_struct *task)
{
	unsigned long *sp;

	if (!task || task == current)
		__asm__ __volatile__ ("mov %0, a1\n" : "=a"(sp));
	else
		sp = (unsigned long *)task->thread.sp;

	return sp;
}

void show_trace(struct task_struct *task, unsigned long *sp)
{
	unsigned long a0, a1, pc;
	unsigned long sp_start, sp_end;

	if (sp)
		a1 = (unsigned long)sp;
	else
		a1 = (unsigned long)stack_pointer(task);

	sp_start = a1 & ~(THREAD_SIZE-1);
	sp_end = sp_start + THREAD_SIZE;

	printk("Call Trace:");
#ifdef CONFIG_KALLSYMS
	printk("\n");
#endif
	spill_registers();

	while (a1 > sp_start && a1 < sp_end) {
		sp = (unsigned long*)a1;

		a0 = *(sp - 4);
		a1 = *(sp - 3);

		if (a1 <= (unsigned long) sp)
			break;

		pc = MAKE_PC_FROM_RA(a0, a1);

		if (kernel_text_address(pc)) {
			printk(" [<%08lx>] ", pc);
			print_symbol("%s\n", pc);
		}
	}
	printk("\n");
}

/*
 * This routine abuses get_user()/put_user() to reference pointers
 * with at least a bit of error checking ...
 */

static int kstack_depth_to_print = 24;

void show_stack(struct task_struct *task, unsigned long *sp)
{
	int i = 0;
	unsigned long *stack;

	if (!sp)
		sp = stack_pointer(task);
	stack = sp;

	printk("\nStack: ");

	for (i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(sp))
			break;
		if (i && ((i % 8) == 0))
			printk("\n       ");
		printk("%08lx ", *sp++);
	}
	printk("\n");
	show_trace(task, stack);
}

void dump_stack(void)
{
	show_stack(current, NULL);
}

EXPORT_SYMBOL(dump_stack);


void show_code(unsigned int *pc)
{
	long i;

	printk("\nCode:");

	for(i = -3 ; i < 6 ; i++) {
		unsigned long insn;
		if (__get_user(insn, pc + i)) {
			printk(" (Bad address in pc)\n");
			break;
		}
		printk("%c%08lx%c",(i?' ':'<'),insn,(i?' ':'>'));
	}
}

DEFINE_SPINLOCK(die_lock);

void die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;
	int nl = 0;

	console_verbose();
	spin_lock_irq(&die_lock);

	printk("%s: sig: %ld [#%d]\n", str, err, ++die_counter);
#ifdef CONFIG_PREEMPT
	printk("PREEMPT ");
	nl = 1;
#endif
	if (nl)
		printk("\n");
	show_regs(regs);
	if (!user_mode(regs))
		show_stack(NULL, (unsigned long*)regs->areg[1]);

	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	do_exit(err);
}
