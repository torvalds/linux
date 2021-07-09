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
 * Copyright (C) 2001 - 2013 Tensilica Inc.
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
#include <linux/sched/signal.h>
#include <linux/sched/debug.h>
#include <linux/sched/task_stack.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/stringify.h>
#include <linux/kallsyms.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/ratelimit.h>
#include <linux/pgtable.h>

#include <asm/stacktrace.h>
#include <asm/ptrace.h>
#include <asm/timex.h>
#include <linux/uaccess.h>
#include <asm/processor.h>
#include <asm/traps.h>
#include <asm/hw_breakpoint.h>

/*
 * Machine specific interrupt handlers
 */

extern void kernel_exception(void);
extern void user_exception(void);

extern void fast_illegal_instruction_user(void);
extern void fast_syscall_user(void);
extern void fast_alloca(void);
extern void fast_unaligned(void);
extern void fast_second_level_miss(void);
extern void fast_store_prohibited(void);
extern void fast_coprocessor(void);

extern void do_illegal_instruction (struct pt_regs*);
extern void do_interrupt (struct pt_regs*);
extern void do_nmi(struct pt_regs *);
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

#ifdef CONFIG_USER_ABI_CALL0_PROBE
{ EXCCAUSE_ILLEGAL_INSTRUCTION,	USER,	   fast_illegal_instruction_user },
#endif
{ EXCCAUSE_ILLEGAL_INSTRUCTION,	0,	   do_illegal_instruction},
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
#endif
{ EXCCAUSE_UNALIGNED,		0,	   do_unaligned_user },
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
#if XTENSA_FAKE_NMI
{ EXCCAUSE_MAPPED_NMI,			0,		do_nmi },
#endif
{ EXCCAUSE_MAPPED_DEBUG,		0,		do_debug },
{ -1, -1, 0 }

};

/* The exception table <exc_table> serves two functions:
 * 1. it contains three dispatch tables (fast_user, fast_kernel, default-c)
 * 2. it is a temporary memory buffer for the exception handlers.
 */

DEFINE_PER_CPU(struct exc_table, exc_table);
DEFINE_PER_CPU(struct debug_table, debug_table);

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
	pr_info_ratelimited("Caught unhandled exception in '%s' "
			    "(pid = %d, pc = %#010lx) - should not happen\n"
			    "\tEXCCAUSE is %ld\n",
			    current->comm, task_pid_nr(current), regs->pc,
			    exccause);
	force_sig(SIGILL);
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
 */

extern void do_IRQ(int, struct pt_regs *);

#if XTENSA_FAKE_NMI

#define IS_POW2(v) (((v) & ((v) - 1)) == 0)

#if !(PROFILING_INTLEVEL == XCHAL_EXCM_LEVEL && \
      IS_POW2(XTENSA_INTLEVEL_MASK(PROFILING_INTLEVEL)))
#warning "Fake NMI is requested for PMM, but there are other IRQs at or above its level."
#warning "Fake NMI will be used, but there will be a bugcheck if one of those IRQs fire."

static inline void check_valid_nmi(void)
{
	unsigned intread = xtensa_get_sr(interrupt);
	unsigned intenable = xtensa_get_sr(intenable);

	BUG_ON(intread & intenable &
	       ~(XTENSA_INTLEVEL_ANDBELOW_MASK(PROFILING_INTLEVEL) ^
		 XTENSA_INTLEVEL_MASK(PROFILING_INTLEVEL) ^
		 BIT(XCHAL_PROFILING_INTERRUPT)));
}

#else

static inline void check_valid_nmi(void)
{
}

#endif

irqreturn_t xtensa_pmu_irq_handler(int irq, void *dev_id);

DEFINE_PER_CPU(unsigned long, nmi_count);

void do_nmi(struct pt_regs *regs)
{
	struct pt_regs *old_regs;

	if ((regs->ps & PS_INTLEVEL_MASK) < LOCKLEVEL)
		trace_hardirqs_off();

	old_regs = set_irq_regs(regs);
	nmi_enter();
	++*this_cpu_ptr(&nmi_count);
	check_valid_nmi();
	xtensa_pmu_irq_handler(0, NULL);
	nmi_exit();
	set_irq_regs(old_regs);
}
#endif

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
	struct pt_regs *old_regs;
	unsigned unhandled = ~0u;

	trace_hardirqs_off();

	old_regs = set_irq_regs(regs);
	irq_enter();

	for (;;) {
		unsigned intread = xtensa_get_sr(interrupt);
		unsigned intenable = xtensa_get_sr(intenable);
		unsigned int_at_level = intread & intenable;
		unsigned level;

		for (level = LOCKLEVEL; level > 0; --level) {
			if (int_at_level & int_level_mask[level]) {
				int_at_level &= int_level_mask[level];
				if (int_at_level & unhandled)
					int_at_level &= unhandled;
				else
					unhandled |= int_level_mask[level];
				break;
			}
		}

		if (level == 0)
			break;

		/* clear lowest pending irq in the unhandled mask */
		unhandled ^= (int_at_level & -int_at_level);
		do_IRQ(__ffs(int_at_level), regs);
	}

	irq_exit();
	set_irq_regs(old_regs);
}

/*
 * Illegal instruction. Fatal if in kernel space.
 */

void
do_illegal_instruction(struct pt_regs *regs)
{
	__die_if_kernel("Illegal instruction in kernel", regs, SIGKILL);

	/* If in user mode, send SIGILL signal to current process. */

	pr_info_ratelimited("Illegal Instruction in '%s' (pid = %d, pc = %#010lx)\n",
			    current->comm, task_pid_nr(current), regs->pc);
	force_sig(SIGILL);
}


/*
 * Handle unaligned memory accesses from user space. Kill task.
 *
 * If CONFIG_UNALIGNED_USER is not set, we don't allow unaligned memory
 * accesses causes from user space.
 */

#if XCHAL_UNALIGNED_LOAD_EXCEPTION || XCHAL_UNALIGNED_STORE_EXCEPTION
void
do_unaligned_user (struct pt_regs *regs)
{
	__die_if_kernel("Unhandled unaligned exception in kernel",
			regs, SIGKILL);

	current->thread.bad_vaddr = regs->excvaddr;
	current->thread.error_code = -3;
	pr_info_ratelimited("Unaligned memory access to %08lx in '%s' "
			    "(pid = %d, pc = %#010lx)\n",
			    regs->excvaddr, current->comm,
			    task_pid_nr(current), regs->pc);
	force_sig_fault(SIGBUS, BUS_ADRALN, (void *) regs->excvaddr);
}
#endif

/* Handle debug events.
 * When CONFIG_HAVE_HW_BREAKPOINT is on this handler is called with
 * preemption disabled to avoid rescheduling and keep mapping of hardware
 * breakpoint structures to debug registers intact, so that
 * DEBUGCAUSE.DBNUM could be used in case of data breakpoint hit.
 */
void
do_debug(struct pt_regs *regs)
{
#ifdef CONFIG_HAVE_HW_BREAKPOINT
	int ret = check_hw_breakpoint(regs);

	preempt_enable();
	if (ret == 0)
		return;
#endif
	__die_if_kernel("Breakpoint in kernel", regs, SIGKILL);

	/* If in user mode, send SIGTRAP signal to current process */

	force_sig(SIGTRAP);
}


#define set_handler(type, cause, handler)				\
	do {								\
		unsigned int cpu;					\
									\
		for_each_possible_cpu(cpu)				\
			per_cpu(exc_table, cpu).type[cause] = (handler);\
	} while (0)

/* Set exception C handler - for temporary use when probing exceptions */

void * __init trap_set_handler(int cause, void *handler)
{
	void *previous = per_cpu(exc_table, 0).default_handler[cause];

	set_handler(default_handler, cause, handler);
	return previous;
}


static void trap_init_excsave(void)
{
	unsigned long excsave1 = (unsigned long)this_cpu_ptr(&exc_table);
	__asm__ __volatile__("wsr  %0, excsave1\n" : : "a" (excsave1));
}

static void trap_init_debug(void)
{
	unsigned long debugsave = (unsigned long)this_cpu_ptr(&debug_table);

	this_cpu_ptr(&debug_table)->debug_exception = debug_exception;
	__asm__ __volatile__("wsr %0, excsave" __stringify(XCHAL_DEBUGLEVEL)
			     :: "a"(debugsave));
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

void __init trap_init(void)
{
	int i;

	/* Setup default vectors. */

	for (i = 0; i < EXCCAUSE_N; i++) {
		set_handler(fast_user_handler, i, user_exception);
		set_handler(fast_kernel_handler, i, kernel_exception);
		set_handler(default_handler, i, do_unhandled);
	}

	/* Setup specific handlers. */

	for(i = 0; dispatch_init_table[i].cause >= 0; i++) {
		int fast = dispatch_init_table[i].fast;
		int cause = dispatch_init_table[i].cause;
		void *handler = dispatch_init_table[i].handler;

		if (fast == 0)
			set_handler(default_handler, cause, handler);
		if ((fast & USER) != 0)
			set_handler(fast_user_handler, cause, handler);
		if ((fast & KRNL) != 0)
			set_handler(fast_kernel_handler, cause, handler);
	}

	/* Initialize EXCSAVE_1 to hold the address of the exception table. */
	trap_init_excsave();
	trap_init_debug();
}

#ifdef CONFIG_SMP
void secondary_trap_init(void)
{
	trap_init_excsave();
	trap_init_debug();
}
#endif

/*
 * This function dumps the current valid window frame and other base registers.
 */

void show_regs(struct pt_regs * regs)
{
	int i, wmask;

	show_regs_print_info(KERN_DEFAULT);

	wmask = regs->wmask & ~1;

	for (i = 0; i < 16; i++) {
		if ((i % 8) == 0)
			pr_info("a%02d:", i);
		pr_cont(" %08lx", regs->areg[i]);
	}
	pr_cont("\n");
	pr_info("pc: %08lx, ps: %08lx, depc: %08lx, excvaddr: %08lx\n",
		regs->pc, regs->ps, regs->depc, regs->excvaddr);
	pr_info("lbeg: %08lx, lend: %08lx lcount: %08lx, sar: %08lx\n",
		regs->lbeg, regs->lend, regs->lcount, regs->sar);
	if (user_mode(regs))
		pr_cont("wb: %08lx, ws: %08lx, wmask: %08lx, syscall: %ld\n",
			regs->windowbase, regs->windowstart, regs->wmask,
			regs->syscall);
}

static int show_trace_cb(struct stackframe *frame, void *data)
{
	const char *loglvl = data;

	if (kernel_text_address(frame->pc))
		printk("%s [<%08lx>] %pB\n",
			loglvl, frame->pc, (void *)frame->pc);
	return 0;
}

static void show_trace(struct task_struct *task, unsigned long *sp,
		       const char *loglvl)
{
	if (!sp)
		sp = stack_pointer(task);

	printk("%sCall Trace:\n", loglvl);
	walk_stackframe(sp, show_trace_cb, (void *)loglvl);
}

#define STACK_DUMP_ENTRY_SIZE 4
#define STACK_DUMP_LINE_SIZE 32
static size_t kstack_depth_to_print = CONFIG_PRINT_STACK_DEPTH;

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	size_t len;

	if (!sp)
		sp = stack_pointer(task);

	len = min((-(size_t)sp) & (THREAD_SIZE - STACK_DUMP_ENTRY_SIZE),
		  kstack_depth_to_print * STACK_DUMP_ENTRY_SIZE);

	printk("%sStack:\n", loglvl);
	print_hex_dump(loglvl, " ", DUMP_PREFIX_NONE,
		       STACK_DUMP_LINE_SIZE, STACK_DUMP_ENTRY_SIZE,
		       sp, len, false);
	show_trace(task, sp, loglvl);
}

DEFINE_SPINLOCK(die_lock);

void die(const char * str, struct pt_regs * regs, long err)
{
	static int die_counter;
	const char *pr = "";

	if (IS_ENABLED(CONFIG_PREEMPTION))
		pr = IS_ENABLED(CONFIG_PREEMPT_RT) ? " PREEMPT_RT" : " PREEMPT";

	console_verbose();
	spin_lock_irq(&die_lock);

	pr_info("%s: sig: %ld [#%d]%s\n", str, err, ++die_counter, pr);
	show_regs(regs);
	if (!user_mode(regs))
		show_stack(NULL, (unsigned long *)regs->areg[1], KERN_INFO);

	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	do_exit(err);
}
