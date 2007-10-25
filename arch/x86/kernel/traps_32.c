/*
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <linux/kallsyms.h>
#include <linux/ptrace.h>
#include <linux/utsname.h>
#include <linux/kprobes.h>
#include <linux/kexec.h>
#include <linux/unwind.h>
#include <linux/uaccess.h>
#include <linux/nmi.h>
#include <linux/bug.h>

#ifdef CONFIG_EISA
#include <linux/ioport.h>
#include <linux/eisa.h>
#endif

#ifdef CONFIG_MCA
#include <linux/mca.h>
#endif

#if defined(CONFIG_EDAC)
#include <linux/edac.h>
#endif

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/nmi.h>
#include <asm/unwind.h>
#include <asm/smp.h>
#include <asm/arch_hooks.h>
#include <linux/kdebug.h>
#include <asm/stacktrace.h>

#include <linux/module.h>

#include "mach_traps.h"

int panic_on_unrecovered_nmi;

DECLARE_BITMAP(used_vectors, NR_VECTORS);
EXPORT_SYMBOL_GPL(used_vectors);

asmlinkage int system_call(void);

/* Do we ignore FPU interrupts ? */
char ignore_fpu_irq = 0;

/*
 * The IDT has to be page-aligned to simplify the Pentium
 * F0 0F bug workaround.. We have a special link segment
 * for this.
 */
struct desc_struct idt_table[256] __attribute__((__section__(".data.idt"))) = { {0, 0}, };

asmlinkage void divide_error(void);
asmlinkage void debug(void);
asmlinkage void nmi(void);
asmlinkage void int3(void);
asmlinkage void overflow(void);
asmlinkage void bounds(void);
asmlinkage void invalid_op(void);
asmlinkage void device_not_available(void);
asmlinkage void coprocessor_segment_overrun(void);
asmlinkage void invalid_TSS(void);
asmlinkage void segment_not_present(void);
asmlinkage void stack_segment(void);
asmlinkage void general_protection(void);
asmlinkage void page_fault(void);
asmlinkage void coprocessor_error(void);
asmlinkage void simd_coprocessor_error(void);
asmlinkage void alignment_check(void);
asmlinkage void spurious_interrupt_bug(void);
asmlinkage void machine_check(void);

int kstack_depth_to_print = 24;
static unsigned int code_bytes = 64;

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p, unsigned size)
{
	return	p > (void *)tinfo &&
		p <= (void *)tinfo + THREAD_SIZE - size;
}

/* The form of the top of the frame on the stack */
struct stack_frame {
	struct stack_frame *next_frame;
	unsigned long return_address;
};

static inline unsigned long print_context_stack(struct thread_info *tinfo,
				unsigned long *stack, unsigned long ebp,
				const struct stacktrace_ops *ops, void *data)
{
#ifdef	CONFIG_FRAME_POINTER
	struct stack_frame *frame = (struct stack_frame *)ebp;
	while (valid_stack_ptr(tinfo, frame, sizeof(*frame))) {
		struct stack_frame *next;
		unsigned long addr;

		addr = frame->return_address;
		ops->address(data, addr);
		/*
		 * break out of recursive entries (such as
		 * end_of_stack_stop_unwind_function). Also,
		 * we can never allow a frame pointer to
		 * move downwards!
		 */
		next = frame->next_frame;
		if (next <= frame)
			break;
		frame = next;
	}
#else
	while (valid_stack_ptr(tinfo, stack, sizeof(*stack))) {
		unsigned long addr;

		addr = *stack++;
		if (__kernel_text_address(addr))
			ops->address(data, addr);
	}
#endif
	return ebp;
}

#define MSG(msg) ops->warning(data, msg)

void dump_trace(struct task_struct *task, struct pt_regs *regs,
	        unsigned long *stack,
		const struct stacktrace_ops *ops, void *data)
{
	unsigned long ebp = 0;

	if (!task)
		task = current;

	if (!stack) {
		unsigned long dummy;
		stack = &dummy;
		if (task != current)
			stack = (unsigned long *)task->thread.esp;
	}

#ifdef CONFIG_FRAME_POINTER
	if (!ebp) {
		if (task == current) {
			/* Grab ebp right from our regs */
			asm ("movl %%ebp, %0" : "=r" (ebp) : );
		} else {
			/* ebp is the last reg pushed by switch_to */
			ebp = *(unsigned long *) task->thread.esp;
		}
	}
#endif

	while (1) {
		struct thread_info *context;
		context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = print_context_stack(context, stack, ebp, ops, data);
		/* Should be after the line below, but somewhere
		   in early boot context comes out corrupted and we
		   can't reference it -AK */
		if (ops->stack(data, "IRQ") < 0)
			break;
		stack = (unsigned long*)context->previous_esp;
		if (!stack)
			break;
		touch_nmi_watchdog();
	}
}
EXPORT_SYMBOL(dump_trace);

static void
print_trace_warning_symbol(void *data, char *msg, unsigned long symbol)
{
	printk(data);
	print_symbol(msg, symbol);
	printk("\n");
}

static void print_trace_warning(void *data, char *msg)
{
	printk("%s%s\n", (char *)data, msg);
}

static int print_trace_stack(void *data, char *name)
{
	return 0;
}

/*
 * Print one address/symbol entries per line.
 */
static void print_trace_address(void *data, unsigned long addr)
{
	printk("%s [<%08lx>] ", (char *)data, addr);
	print_symbol("%s\n", addr);
	touch_nmi_watchdog();
}

static const struct stacktrace_ops print_trace_ops = {
	.warning = print_trace_warning,
	.warning_symbol = print_trace_warning_symbol,
	.stack = print_trace_stack,
	.address = print_trace_address,
};

static void
show_trace_log_lvl(struct task_struct *task, struct pt_regs *regs,
		   unsigned long * stack, char *log_lvl)
{
	dump_trace(task, regs, stack, &print_trace_ops, log_lvl);
	printk("%s =======================\n", log_lvl);
}

void show_trace(struct task_struct *task, struct pt_regs *regs,
		unsigned long * stack)
{
	show_trace_log_lvl(task, regs, stack, "");
}

static void show_stack_log_lvl(struct task_struct *task, struct pt_regs *regs,
			       unsigned long *esp, char *log_lvl)
{
	unsigned long *stack;
	int i;

	if (esp == NULL) {
		if (task)
			esp = (unsigned long*)task->thread.esp;
		else
			esp = (unsigned long *)&esp;
	}

	stack = esp;
	for(i = 0; i < kstack_depth_to_print; i++) {
		if (kstack_end(stack))
			break;
		if (i && ((i % 8) == 0))
			printk("\n%s       ", log_lvl);
		printk("%08lx ", *stack++);
	}
	printk("\n%sCall Trace:\n", log_lvl);
	show_trace_log_lvl(task, regs, esp, log_lvl);
}

void show_stack(struct task_struct *task, unsigned long *esp)
{
	printk("       ");
	show_stack_log_lvl(task, NULL, esp, "");
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(current, NULL, &stack);
}

EXPORT_SYMBOL(dump_stack);

void show_registers(struct pt_regs *regs)
{
	int i;

	print_modules();
	__show_registers(regs, 0);
	printk(KERN_EMERG "Process %.*s (pid: %d, ti=%p task=%p task.ti=%p)",
		TASK_COMM_LEN, current->comm, task_pid_nr(current),
		current_thread_info(), current, task_thread_info(current));
	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (!user_mode_vm(regs)) {
		u8 *eip;
		unsigned int code_prologue = code_bytes * 43 / 64;
		unsigned int code_len = code_bytes;
		unsigned char c;

		printk("\n" KERN_EMERG "Stack: ");
		show_stack_log_lvl(NULL, regs, &regs->esp, KERN_EMERG);

		printk(KERN_EMERG "Code: ");

		eip = (u8 *)regs->eip - code_prologue;
		if (eip < (u8 *)PAGE_OFFSET ||
			probe_kernel_address(eip, c)) {
			/* try starting at EIP */
			eip = (u8 *)regs->eip;
			code_len = code_len - code_prologue + 1;
		}
		for (i = 0; i < code_len; i++, eip++) {
			if (eip < (u8 *)PAGE_OFFSET ||
				probe_kernel_address(eip, c)) {
				printk(" Bad EIP value.");
				break;
			}
			if (eip == (u8 *)regs->eip)
				printk("<%02x> ", c);
			else
				printk("%02x ", c);
		}
	}
	printk("\n");
}	

int is_valid_bugaddr(unsigned long eip)
{
	unsigned short ud2;

	if (eip < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((unsigned short *)eip, ud2))
		return 0;

	return ud2 == 0x0b0f;
}

/*
 * This is gone through when something in the kernel has done something bad and
 * is about to be terminated.
 */
void die(const char * str, struct pt_regs * regs, long err)
{
	static struct {
		raw_spinlock_t lock;
		u32 lock_owner;
		int lock_owner_depth;
	} die = {
		.lock =			__RAW_SPIN_LOCK_UNLOCKED,
		.lock_owner =		-1,
		.lock_owner_depth =	0
	};
	static int die_counter;
	unsigned long flags;

	oops_enter();

	if (die.lock_owner != raw_smp_processor_id()) {
		console_verbose();
		__raw_spin_lock(&die.lock);
		raw_local_save_flags(flags);
		die.lock_owner = smp_processor_id();
		die.lock_owner_depth = 0;
		bust_spinlocks(1);
	}
	else
		raw_local_save_flags(flags);

	if (++die.lock_owner_depth < 3) {
		unsigned long esp;
		unsigned short ss;

		report_bug(regs->eip, regs);

		printk(KERN_EMERG "%s: %04lx [#%d] ", str, err & 0xffff,
		       ++die_counter);
#ifdef CONFIG_PREEMPT
		printk("PREEMPT ");
#endif
#ifdef CONFIG_SMP
		printk("SMP ");
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
		printk("DEBUG_PAGEALLOC");
#endif
		printk("\n");

		if (notify_die(DIE_OOPS, str, regs, err,
					current->thread.trap_no, SIGSEGV) !=
				NOTIFY_STOP) {
			show_registers(regs);
			/* Executive summary in case the oops scrolled away */
			esp = (unsigned long) (&regs->esp);
			savesegment(ss, ss);
			if (user_mode(regs)) {
				esp = regs->esp;
				ss = regs->xss & 0xffff;
			}
			printk(KERN_EMERG "EIP: [<%08lx>] ", regs->eip);
			print_symbol("%s", regs->eip);
			printk(" SS:ESP %04x:%08lx\n", ss, esp);
		}
		else
			regs = NULL;
  	} else
		printk(KERN_EMERG "Recursive die() failure, output suppressed\n");

	bust_spinlocks(0);
	die.lock_owner = -1;
	add_taint(TAINT_DIE);
	__raw_spin_unlock(&die.lock);
	raw_local_irq_restore(flags);

	if (!regs)
		return;

	if (kexec_should_crash(current))
		crash_kexec(regs);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	oops_exit();
	do_exit(SIGSEGV);
}

static inline void die_if_kernel(const char * str, struct pt_regs * regs, long err)
{
	if (!user_mode_vm(regs))
		die(str, regs, err);
}

static void __kprobes do_trap(int trapnr, int signr, char *str, int vm86,
			      struct pt_regs * regs, long error_code,
			      siginfo_t *info)
{
	struct task_struct *tsk = current;

	if (regs->eflags & VM_MASK) {
		if (vm86)
			goto vm86_trap;
		goto trap_signal;
	}

	if (!user_mode(regs))
		goto kernel_trap;

	trap_signal: {
		/*
		 * We want error_code and trap_no set for userspace faults and
		 * kernelspace faults which result in die(), but not
		 * kernelspace faults which are fixed up.  die() gives the
		 * process no chance to handle the signal and notice the
		 * kernel fault information, so that won't result in polluting
		 * the information about previously queued, but not yet
		 * delivered, faults.  See also do_general_protection below.
		 */
		tsk->thread.error_code = error_code;
		tsk->thread.trap_no = trapnr;

		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	}

	kernel_trap: {
		if (!fixup_exception(regs)) {
			tsk->thread.error_code = error_code;
			tsk->thread.trap_no = trapnr;
			die(str, regs, error_code);
		}
		return;
	}

	vm86_trap: {
		int ret = handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, trapnr);
		if (ret) goto trap_signal;
		return;
	}
}

#define DO_ERROR(trapnr, signr, str, name) \
fastcall void do_##name(struct pt_regs * regs, long error_code) \
{ \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
						== NOTIFY_STOP) \
		return; \
	do_trap(trapnr, signr, str, 0, regs, error_code, NULL); \
}

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr, irq) \
fastcall void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	if (irq) \
		local_irq_enable(); \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void __user *)siaddr; \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
						== NOTIFY_STOP) \
		return; \
	do_trap(trapnr, signr, str, 0, regs, error_code, &info); \
}

#define DO_VM86_ERROR(trapnr, signr, str, name) \
fastcall void do_##name(struct pt_regs * regs, long error_code) \
{ \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
						== NOTIFY_STOP) \
		return; \
	do_trap(trapnr, signr, str, 1, regs, error_code, NULL); \
}

#define DO_VM86_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
fastcall void do_##name(struct pt_regs * regs, long error_code) \
{ \
	siginfo_t info; \
	info.si_signo = signr; \
	info.si_errno = 0; \
	info.si_code = sicode; \
	info.si_addr = (void __user *)siaddr; \
	if (notify_die(DIE_TRAP, str, regs, error_code, trapnr, signr) \
						== NOTIFY_STOP) \
		return; \
	do_trap(trapnr, signr, str, 1, regs, error_code, &info); \
}

DO_VM86_ERROR_INFO( 0, SIGFPE,  "divide error", divide_error, FPE_INTDIV, regs->eip)
#ifndef CONFIG_KPROBES
DO_VM86_ERROR( 3, SIGTRAP, "int3", int3)
#endif
DO_VM86_ERROR( 4, SIGSEGV, "overflow", overflow)
DO_VM86_ERROR( 5, SIGSEGV, "bounds", bounds)
DO_ERROR_INFO( 6, SIGILL,  "invalid opcode", invalid_op, ILL_ILLOPN, regs->eip, 0)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, 0, 0)
DO_ERROR_INFO(32, SIGSEGV, "iret exception", iret_error, ILL_BADSTK, 0, 1)

fastcall void __kprobes do_general_protection(struct pt_regs * regs,
					      long error_code)
{
	int cpu = get_cpu();
	struct tss_struct *tss = &per_cpu(init_tss, cpu);
	struct thread_struct *thread = &current->thread;

	/*
	 * Perform the lazy TSS's I/O bitmap copy. If the TSS has an
	 * invalid offset set (the LAZY one) and the faulting thread has
	 * a valid I/O bitmap pointer, we copy the I/O bitmap in the TSS
	 * and we set the offset field correctly. Then we let the CPU to
	 * restart the faulting instruction.
	 */
	if (tss->x86_tss.io_bitmap_base == INVALID_IO_BITMAP_OFFSET_LAZY &&
	    thread->io_bitmap_ptr) {
		memcpy(tss->io_bitmap, thread->io_bitmap_ptr,
		       thread->io_bitmap_max);
		/*
		 * If the previously set map was extending to higher ports
		 * than the current one, pad extra space with 0xff (no access).
		 */
		if (thread->io_bitmap_max < tss->io_bitmap_max)
			memset((char *) tss->io_bitmap +
				thread->io_bitmap_max, 0xff,
				tss->io_bitmap_max - thread->io_bitmap_max);
		tss->io_bitmap_max = thread->io_bitmap_max;
		tss->x86_tss.io_bitmap_base = IO_BITMAP_OFFSET;
		tss->io_bitmap_owner = thread;
		put_cpu();
		return;
	}
	put_cpu();

	if (regs->eflags & VM_MASK)
		goto gp_in_vm86;

	if (!user_mode(regs))
		goto gp_in_kernel;

	current->thread.error_code = error_code;
	current->thread.trap_no = 13;
	if (show_unhandled_signals && unhandled_signal(current, SIGSEGV) &&
	    printk_ratelimit())
		printk(KERN_INFO
		    "%s[%d] general protection eip:%lx esp:%lx error:%lx\n",
		    current->comm, task_pid_nr(current),
		    regs->eip, regs->esp, error_code);

	force_sig(SIGSEGV, current);
	return;

gp_in_vm86:
	local_irq_enable();
	handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
	return;

gp_in_kernel:
	if (!fixup_exception(regs)) {
		current->thread.error_code = error_code;
		current->thread.trap_no = 13;
		if (notify_die(DIE_GPF, "general protection fault", regs,
				error_code, 13, SIGSEGV) == NOTIFY_STOP)
			return;
		die("general protection fault", regs, error_code);
	}
}

static __kprobes void
mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x on "
		"CPU %d.\n", reason, smp_processor_id());
	printk(KERN_EMERG "You have some hardware problem, likely on the PCI bus.\n");

#if defined(CONFIG_EDAC)
	if(edac_handler_set()) {
		edac_atomic_assert_error();
		return;
	}
#endif

	if (panic_on_unrecovered_nmi)
                panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");

	/* Clear and disable the memory parity error line. */
	clear_mem_error(reason);
}

static __kprobes void
io_check_error(unsigned char reason, struct pt_regs * regs)
{
	unsigned long i;

	printk(KERN_EMERG "NMI: IOCK error (debug interrupt?)\n");
	show_registers(regs);

	/* Re-enable the IOCK line, wait for a few seconds */
	reason = (reason & 0xf) | 8;
	outb(reason, 0x61);
	i = 2000;
	while (--i) udelay(1000);
	reason &= ~8;
	outb(reason, 0x61);
}

static __kprobes void
unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
#ifdef CONFIG_MCA
	/* Might actually be able to figure out what the guilty party
	* is. */
	if( MCA_bus ) {
		mca_handle_nmi();
		return;
	}
#endif
	printk(KERN_EMERG "Uhhuh. NMI received for unknown reason %02x on "
		"CPU %d.\n", reason, smp_processor_id());
	printk(KERN_EMERG "Do you have a strange power saving mode enabled?\n");
	if (panic_on_unrecovered_nmi)
                panic("NMI: Not continuing");

	printk(KERN_EMERG "Dazed and confused, but trying to continue\n");
}

static DEFINE_SPINLOCK(nmi_print_lock);

void __kprobes die_nmi(struct pt_regs *regs, const char *msg)
{
	if (notify_die(DIE_NMIWATCHDOG, msg, regs, 0, 2, SIGINT) ==
	    NOTIFY_STOP)
		return;

	spin_lock(&nmi_print_lock);
	/*
	* We are in trouble anyway, lets at least try
	* to get a message out.
	*/
	bust_spinlocks(1);
	printk(KERN_EMERG "%s", msg);
	printk(" on CPU%d, eip %08lx, registers:\n",
		smp_processor_id(), regs->eip);
	show_registers(regs);
	console_silent();
	spin_unlock(&nmi_print_lock);
	bust_spinlocks(0);

	/* If we are in kernel we are probably nested up pretty bad
	 * and might aswell get out now while we still can.
	*/
	if (!user_mode_vm(regs)) {
		current->thread.trap_no = 2;
		crash_kexec(regs);
	}

	do_exit(SIGSEGV);
}

static __kprobes void default_do_nmi(struct pt_regs * regs)
{
	unsigned char reason = 0;

	/* Only the BSP gets external NMIs from the system.  */
	if (!smp_processor_id())
		reason = get_nmi_reason();
 
	if (!(reason & 0xc0)) {
		if (notify_die(DIE_NMI_IPI, "nmi_ipi", regs, reason, 2, SIGINT)
							== NOTIFY_STOP)
			return;
#ifdef CONFIG_X86_LOCAL_APIC
		/*
		 * Ok, so this is none of the documented NMI sources,
		 * so it must be the NMI watchdog.
		 */
		if (nmi_watchdog_tick(regs, reason))
			return;
		if (!do_nmi_callback(regs, smp_processor_id()))
#endif
			unknown_nmi_error(reason, regs);

		return;
	}
	if (notify_die(DIE_NMI, "nmi", regs, reason, 2, SIGINT) == NOTIFY_STOP)
		return;
	if (reason & 0x80)
		mem_parity_error(reason, regs);
	if (reason & 0x40)
		io_check_error(reason, regs);
	/*
	 * Reassert NMI in case it became active meanwhile
	 * as it's edge-triggered.
	 */
	reassert_nmi();
}

static int ignore_nmis;

fastcall __kprobes void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu;

	nmi_enter();

	cpu = smp_processor_id();

	++nmi_count(cpu);

	if (!ignore_nmis)
		default_do_nmi(regs);

	nmi_exit();
}

void stop_nmi(void)
{
	acpi_nmi_disable();
	ignore_nmis++;
}

void restart_nmi(void)
{
	ignore_nmis--;
	acpi_nmi_enable();
}

#ifdef CONFIG_KPROBES
fastcall void __kprobes do_int3(struct pt_regs *regs, long error_code)
{
	trace_hardirqs_fixup();

	if (notify_die(DIE_INT3, "int3", regs, error_code, 3, SIGTRAP)
			== NOTIFY_STOP)
		return;
	/* This is an interrupt gate, because kprobes wants interrupts
	disabled.  Normal trap handlers don't. */
	restore_interrupts(regs);
	do_trap(3, SIGTRAP, "int3", 1, regs, error_code, NULL);
}
#endif

/*
 * Our handling of the processor debug registers is non-trivial.
 * We do not clear them on entry and exit from the kernel. Therefore
 * it is possible to get a watchpoint trap here from inside the kernel.
 * However, the code in ./ptrace.c has ensured that the user can
 * only set watchpoints on userspace addresses. Therefore the in-kernel
 * watchpoint trap can only occur in code which is reading/writing
 * from user space. Such code must not hold kernel locks (since it
 * can equally take a page fault), therefore it is safe to call
 * force_sig_info even though that claims and releases locks.
 * 
 * Code in ./signal.c ensures that the debug control register
 * is restored before we deliver any signal, and therefore that
 * user code runs with the correct debug control register even though
 * we clear it here.
 *
 * Being careful here means that we don't have to be as careful in a
 * lot of more complicated places (task switching can be a bit lazy
 * about restoring all the debug state, and ptrace doesn't have to
 * find every occurrence of the TF bit that could be saved away even
 * by user code)
 */
fastcall void __kprobes do_debug(struct pt_regs * regs, long error_code)
{
	unsigned int condition;
	struct task_struct *tsk = current;

	get_debugreg(condition, 6);

	if (notify_die(DIE_DEBUG, "debug", regs, condition, error_code,
					SIGTRAP) == NOTIFY_STOP)
		return;
	/* It's safe to allow irq's after DR6 has been saved */
	if (regs->eflags & X86_EFLAGS_IF)
		local_irq_enable();

	/* Mask out spurious debug traps due to lazy DR7 setting */
	if (condition & (DR_TRAP0|DR_TRAP1|DR_TRAP2|DR_TRAP3)) {
		if (!tsk->thread.debugreg[7])
			goto clear_dr7;
	}

	if (regs->eflags & VM_MASK)
		goto debug_vm86;

	/* Save debug status register where ptrace can see it */
	tsk->thread.debugreg[6] = condition;

	/*
	 * Single-stepping through TF: make sure we ignore any events in
	 * kernel space (but re-enable TF when returning to user mode).
	 */
	if (condition & DR_STEP) {
		/*
		 * We already checked v86 mode above, so we can
		 * check for kernel mode by just checking the CPL
		 * of CS.
		 */
		if (!user_mode(regs))
			goto clear_TF_reenable;
	}

	/* Ok, finally something we can handle */
	send_sigtrap(tsk, regs, error_code);

	/* Disable additional traps. They'll be re-enabled when
	 * the signal is delivered.
	 */
clear_dr7:
	set_debugreg(0, 7);
	return;

debug_vm86:
	handle_vm86_trap((struct kernel_vm86_regs *) regs, error_code, 1);
	return;

clear_TF_reenable:
	set_tsk_thread_flag(tsk, TIF_SINGLESTEP);
	regs->eflags &= ~TF_MASK;
	return;
}

/*
 * Note that we play around with the 'TS' bit in an attempt to get
 * the correct behaviour even in the presence of the asynchronous
 * IRQ13 behaviour
 */
void math_error(void __user *eip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short cwd, swd;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 16;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = eip;
	/*
	 * (~cwd & swd) will mask out exceptions that are not set to unmasked
	 * status.  0x3f is the exception bits in these regs, 0x200 is the
	 * C1 reg you need in case of a stack fault, 0x040 is the stack
	 * fault bit.  We should only be taking one exception at a time,
	 * so if this combination doesn't produce any single exception,
	 * then we have a bad program that isn't syncronizing its FPU usage
	 * and it will suffer the consequences since we won't be able to
	 * fully reproduce the context of the exception
	 */
	cwd = get_fpu_cwd(task);
	swd = get_fpu_swd(task);
	switch (swd & ~cwd & 0x3f) {
		case 0x000: /* No unmasked exception */
			return;
		default:    /* Multiple exceptions */
			break;
		case 0x001: /* Invalid Op */
			/*
			 * swd & 0x240 == 0x040: Stack Underflow
			 * swd & 0x240 == 0x240: Stack Overflow
			 * User must clear the SF bit (0x40) if set
			 */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

fastcall void do_coprocessor_error(struct pt_regs * regs, long error_code)
{
	ignore_fpu_irq = 1;
	math_error((void __user *)regs->eip);
}

static void simd_math_error(void __user *eip)
{
	struct task_struct * task;
	siginfo_t info;
	unsigned short mxcsr;

	/*
	 * Save the info for the exception handler and clear the error.
	 */
	task = current;
	save_init_fpu(task);
	task->thread.trap_no = 19;
	task->thread.error_code = 0;
	info.si_signo = SIGFPE;
	info.si_errno = 0;
	info.si_code = __SI_FAULT;
	info.si_addr = eip;
	/*
	 * The SIMD FPU exceptions are handled a little differently, as there
	 * is only a single status/control register.  Thus, to determine which
	 * unmasked exception was caught we must mask the exception mask bits
	 * at 0x1f80, and then use these to mask the exception bits at 0x3f.
	 */
	mxcsr = get_fpu_mxcsr(task);
	switch (~((mxcsr & 0x1f80) >> 7) & (mxcsr & 0x3f)) {
		case 0x000:
		default:
			break;
		case 0x001: /* Invalid Op */
			info.si_code = FPE_FLTINV;
			break;
		case 0x002: /* Denormalize */
		case 0x010: /* Underflow */
			info.si_code = FPE_FLTUND;
			break;
		case 0x004: /* Zero Divide */
			info.si_code = FPE_FLTDIV;
			break;
		case 0x008: /* Overflow */
			info.si_code = FPE_FLTOVF;
			break;
		case 0x020: /* Precision */
			info.si_code = FPE_FLTRES;
			break;
	}
	force_sig_info(SIGFPE, &info, task);
}

fastcall void do_simd_coprocessor_error(struct pt_regs * regs,
					  long error_code)
{
	if (cpu_has_xmm) {
		/* Handle SIMD FPU exceptions on PIII+ processors. */
		ignore_fpu_irq = 1;
		simd_math_error((void __user *)regs->eip);
	} else {
		/*
		 * Handle strange cache flush from user space exception
		 * in all other cases.  This is undocumented behaviour.
		 */
		if (regs->eflags & VM_MASK) {
			handle_vm86_fault((struct kernel_vm86_regs *)regs,
					  error_code);
			return;
		}
		current->thread.trap_no = 19;
		current->thread.error_code = error_code;
		die_if_kernel("cache flush denied", regs, error_code);
		force_sig(SIGSEGV, current);
	}
}

fastcall void do_spurious_interrupt_bug(struct pt_regs * regs,
					  long error_code)
{
#if 0
	/* No need to warn about this any longer. */
	printk("Ignoring P6 Local APIC Spurious Interrupt Bug...\n");
#endif
}

fastcall unsigned long patch_espfix_desc(unsigned long uesp,
					  unsigned long kesp)
{
	struct desc_struct *gdt = __get_cpu_var(gdt_page).gdt;
	unsigned long base = (kesp - uesp) & -THREAD_SIZE;
	unsigned long new_kesp = kesp - base;
	unsigned long lim_pages = (new_kesp | (THREAD_SIZE - 1)) >> PAGE_SHIFT;
	__u64 desc = *(__u64 *)&gdt[GDT_ENTRY_ESPFIX_SS];
	/* Set up base for espfix segment */
 	desc &= 0x00f0ff0000000000ULL;
 	desc |=	((((__u64)base) << 16) & 0x000000ffffff0000ULL) |
		((((__u64)base) << 32) & 0xff00000000000000ULL) |
		((((__u64)lim_pages) << 32) & 0x000f000000000000ULL) |
		(lim_pages & 0xffff);
	*(__u64 *)&gdt[GDT_ENTRY_ESPFIX_SS] = desc;
	return new_kesp;
}

/*
 *  'math_state_restore()' saves the current math information in the
 * old math state array, and gets the new ones from the current task
 *
 * Careful.. There are problems with IBM-designed IRQ13 behaviour.
 * Don't touch unless you *really* know how it works.
 *
 * Must be called with kernel preemption disabled (in this case,
 * local interrupts are disabled at the call-site in entry.S).
 */
asmlinkage void math_state_restore(void)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = thread->task;

	clts();		/* Allow maths ops (or we recurse) */
	if (!tsk_used_math(tsk))
		init_fpu(tsk);
	restore_fpu(tsk);
	thread->status |= TS_USEDFPU;	/* So we fnsave on switch_to() */
	tsk->fpu_counter++;
}
EXPORT_SYMBOL_GPL(math_state_restore);

#ifndef CONFIG_MATH_EMULATION

asmlinkage void math_emulate(long arg)
{
	printk(KERN_EMERG "math-emulation not enabled and no coprocessor found.\n");
	printk(KERN_EMERG "killing %s.\n",current->comm);
	force_sig(SIGFPE,current);
	schedule();
}

#endif /* CONFIG_MATH_EMULATION */

/*
 * This needs to use 'idt_table' rather than 'idt', and
 * thus use the _nonmapped_ version of the IDT, as the
 * Pentium F0 0F bugfix can have resulted in the mapped
 * IDT being write-protected.
 */
void set_intr_gate(unsigned int n, void *addr)
{
	_set_gate(n, DESCTYPE_INT, addr, __KERNEL_CS);
}

/*
 * This routine sets up an interrupt gate at directory privilege level 3.
 */
static inline void set_system_intr_gate(unsigned int n, void *addr)
{
	_set_gate(n, DESCTYPE_INT | DESCTYPE_DPL3, addr, __KERNEL_CS);
}

static void __init set_trap_gate(unsigned int n, void *addr)
{
	_set_gate(n, DESCTYPE_TRAP, addr, __KERNEL_CS);
}

static void __init set_system_gate(unsigned int n, void *addr)
{
	_set_gate(n, DESCTYPE_TRAP | DESCTYPE_DPL3, addr, __KERNEL_CS);
}

static void __init set_task_gate(unsigned int n, unsigned int gdt_entry)
{
	_set_gate(n, DESCTYPE_TASK, (void *)0, (gdt_entry<<3));
}


void __init trap_init(void)
{
	int i;

#ifdef CONFIG_EISA
	void __iomem *p = ioremap(0x0FFFD9, 4);
	if (readl(p) == 'E'+('I'<<8)+('S'<<16)+('A'<<24)) {
		EISA_bus = 1;
	}
	iounmap(p);
#endif

#ifdef CONFIG_X86_LOCAL_APIC
	init_apic_mappings();
#endif

	set_trap_gate(0,&divide_error);
	set_intr_gate(1,&debug);
	set_intr_gate(2,&nmi);
	set_system_intr_gate(3, &int3); /* int3/4 can be called from all */
	set_system_gate(4,&overflow);
	set_trap_gate(5,&bounds);
	set_trap_gate(6,&invalid_op);
	set_trap_gate(7,&device_not_available);
	set_task_gate(8,GDT_ENTRY_DOUBLEFAULT_TSS);
	set_trap_gate(9,&coprocessor_segment_overrun);
	set_trap_gate(10,&invalid_TSS);
	set_trap_gate(11,&segment_not_present);
	set_trap_gate(12,&stack_segment);
	set_trap_gate(13,&general_protection);
	set_intr_gate(14,&page_fault);
	set_trap_gate(15,&spurious_interrupt_bug);
	set_trap_gate(16,&coprocessor_error);
	set_trap_gate(17,&alignment_check);
#ifdef CONFIG_X86_MCE
	set_trap_gate(18,&machine_check);
#endif
	set_trap_gate(19,&simd_coprocessor_error);

	if (cpu_has_fxsr) {
		/*
		 * Verify that the FXSAVE/FXRSTOR data will be 16-byte aligned.
		 * Generates a compile-time "error: zero width for bit-field" if
		 * the alignment is wrong.
		 */
		struct fxsrAlignAssert {
			int _:!(offsetof(struct task_struct,
					thread.i387.fxsave) & 15);
		};

		printk(KERN_INFO "Enabling fast FPU save and restore... ");
		set_in_cr4(X86_CR4_OSFXSR);
		printk("done.\n");
	}
	if (cpu_has_xmm) {
		printk(KERN_INFO "Enabling unmasked SIMD FPU exception "
				"support... ");
		set_in_cr4(X86_CR4_OSXMMEXCPT);
		printk("done.\n");
	}

	set_system_gate(SYSCALL_VECTOR,&system_call);

	/* Reserve all the builtin and the syscall vector. */
	for (i = 0; i < FIRST_EXTERNAL_VECTOR; i++)
		set_bit(i, used_vectors);
	set_bit(SYSCALL_VECTOR, used_vectors);

	/*
	 * Should be a barrier for any external CPU state.
	 */
	cpu_init();

	trap_init_hook();
}

static int __init kstack_setup(char *s)
{
	kstack_depth_to_print = simple_strtoul(s, NULL, 0);
	return 1;
}
__setup("kstack=", kstack_setup);

static int __init code_bytes_setup(char *s)
{
	code_bytes = simple_strtoul(s, NULL, 0);
	if (code_bytes > 8192)
		code_bytes = 8192;

	return 1;
}
__setup("code_bytes=", code_bytes_setup);
