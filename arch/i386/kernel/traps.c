/*
 *  linux/arch/i386/traps.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Pentium III FXSR, SSE support
 *	Gareth Hughes <gareth@valinux.com>, May 2000
 */

/*
 * 'Traps.c' handles hardware traps and faults after we have saved some
 * state in 'asm.s'.
 */
#include <linux/config.h>
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

#ifdef CONFIG_EISA
#include <linux/ioport.h>
#include <linux/eisa.h>
#endif

#ifdef CONFIG_MCA
#include <linux/mca.h>
#endif

#include <asm/processor.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/debugreg.h>
#include <asm/desc.h>
#include <asm/i387.h>
#include <asm/nmi.h>

#include <asm/smp.h>
#include <asm/arch_hooks.h>
#include <asm/kdebug.h>

#include <linux/module.h>

#include "mach_traps.h"

asmlinkage int system_call(void);

struct desc_struct default_ldt[] = { { 0, 0 }, { 0, 0 }, { 0, 0 },
		{ 0, 0 }, { 0, 0 } };

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

static int kstack_depth_to_print = 24;
ATOMIC_NOTIFIER_HEAD(i386die_chain);

int register_die_notifier(struct notifier_block *nb)
{
	vmalloc_sync_all();
	return atomic_notifier_chain_register(&i386die_chain, nb);
}
EXPORT_SYMBOL(register_die_notifier);

int unregister_die_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_unregister(&i386die_chain, nb);
}
EXPORT_SYMBOL(unregister_die_notifier);

static inline int valid_stack_ptr(struct thread_info *tinfo, void *p)
{
	return	p > (void *)tinfo &&
		p < (void *)tinfo + THREAD_SIZE - 3;
}

/*
 * Print CONFIG_STACK_BACKTRACE_COLS address/symbol entries per line.
 */
static inline int print_addr_and_symbol(unsigned long addr, char *log_lvl,
					int printed)
{
	if (!printed)
		printk(log_lvl);

#if CONFIG_STACK_BACKTRACE_COLS == 1
	printk(" [<%08lx>] ", addr);
#else
	printk(" <%08lx> ", addr);
#endif
	print_symbol("%s", addr);

	printed = (printed + 1) % CONFIG_STACK_BACKTRACE_COLS;
	if (printed)
		printk(" ");
	else
		printk("\n");

	return printed;
}

static inline unsigned long print_context_stack(struct thread_info *tinfo,
				unsigned long *stack, unsigned long ebp,
				char *log_lvl)
{
	unsigned long addr;
	int printed = 0; /* nr of entries already printed on current line */

#ifdef	CONFIG_FRAME_POINTER
	while (valid_stack_ptr(tinfo, (void *)ebp)) {
		addr = *(unsigned long *)(ebp + 4);
		printed = print_addr_and_symbol(addr, log_lvl, printed);
		/*
		 * break out of recursive entries (such as
		 * end_of_stack_stop_unwind_function):
	 	 */
		if (ebp == *(unsigned long *)ebp)
			break;
		ebp = *(unsigned long *)ebp;
	}
#else
	while (valid_stack_ptr(tinfo, stack)) {
		addr = *stack++;
		if (__kernel_text_address(addr))
			printed = print_addr_and_symbol(addr, log_lvl, printed);
	}
#endif
	if (printed)
		printk("\n");

	return ebp;
}

static void show_trace_log_lvl(struct task_struct *task,
			       unsigned long *stack, char *log_lvl)
{
	unsigned long ebp;

	if (!task)
		task = current;

	if (task == current) {
		/* Grab ebp right from our regs */
		asm ("movl %%ebp, %0" : "=r" (ebp) : );
	} else {
		/* ebp is the last reg pushed by switch_to */
		ebp = *(unsigned long *) task->thread.esp;
	}

	while (1) {
		struct thread_info *context;
		context = (struct thread_info *)
			((unsigned long)stack & (~(THREAD_SIZE - 1)));
		ebp = print_context_stack(context, stack, ebp, log_lvl);
		stack = (unsigned long*)context->previous_esp;
		if (!stack)
			break;
		printk("%s =======================\n", log_lvl);
	}
}

void show_trace(struct task_struct *task, unsigned long * stack)
{
	show_trace_log_lvl(task, stack, "");
}

static void show_stack_log_lvl(struct task_struct *task, unsigned long *esp,
			       char *log_lvl)
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
	show_trace_log_lvl(task, esp, log_lvl);
}

void show_stack(struct task_struct *task, unsigned long *esp)
{
	printk("       ");
	show_stack_log_lvl(task, esp, "");
}

/*
 * The architecture-independent dump_stack generator
 */
void dump_stack(void)
{
	unsigned long stack;

	show_trace(current, &stack);
}

EXPORT_SYMBOL(dump_stack);

void show_registers(struct pt_regs *regs)
{
	int i;
	int in_kernel = 1;
	unsigned long esp;
	unsigned short ss;

	esp = (unsigned long) (&regs->esp);
	savesegment(ss, ss);
	if (user_mode_vm(regs)) {
		in_kernel = 0;
		esp = regs->esp;
		ss = regs->xss & 0xffff;
	}
	print_modules();
	printk(KERN_EMERG "CPU:    %d\nEIP:    %04x:[<%08lx>]    %s VLI\n"
			"EFLAGS: %08lx   (%s %.*s) \n",
		smp_processor_id(), 0xffff & regs->xcs, regs->eip,
		print_tainted(), regs->eflags, system_utsname.release,
		(int)strcspn(system_utsname.version, " "),
		system_utsname.version);
	print_symbol(KERN_EMERG "EIP is at %s\n", regs->eip);
	printk(KERN_EMERG "eax: %08lx   ebx: %08lx   ecx: %08lx   edx: %08lx\n",
		regs->eax, regs->ebx, regs->ecx, regs->edx);
	printk(KERN_EMERG "esi: %08lx   edi: %08lx   ebp: %08lx   esp: %08lx\n",
		regs->esi, regs->edi, regs->ebp, esp);
	printk(KERN_EMERG "ds: %04x   es: %04x   ss: %04x\n",
		regs->xds & 0xffff, regs->xes & 0xffff, ss);
	printk(KERN_EMERG "Process %.*s (pid: %d, ti=%p task=%p task.ti=%p)",
		TASK_COMM_LEN, current->comm, current->pid,
		current_thread_info(), current, current->thread_info);
	/*
	 * When in-kernel, we also print out the stack and code at the
	 * time of the fault..
	 */
	if (in_kernel) {
		u8 __user *eip;

		printk("\n" KERN_EMERG "Stack: ");
		show_stack_log_lvl(NULL, (unsigned long *)esp, KERN_EMERG);

		printk(KERN_EMERG "Code: ");

		eip = (u8 __user *)regs->eip - 43;
		for (i = 0; i < 64; i++, eip++) {
			unsigned char c;

			if (eip < (u8 __user *)PAGE_OFFSET || __get_user(c, eip)) {
				printk(" Bad EIP value.");
				break;
			}
			if (eip == (u8 __user *)regs->eip)
				printk("<%02x> ", c);
			else
				printk("%02x ", c);
		}
	}
	printk("\n");
}	

static void handle_BUG(struct pt_regs *regs)
{
	unsigned short ud2;
	unsigned short line;
	char *file;
	char c;
	unsigned long eip;

	eip = regs->eip;

	if (eip < PAGE_OFFSET)
		goto no_bug;
	if (__get_user(ud2, (unsigned short __user *)eip))
		goto no_bug;
	if (ud2 != 0x0b0f)
		goto no_bug;
	if (__get_user(line, (unsigned short __user *)(eip + 2)))
		goto bug;
	if (__get_user(file, (char * __user *)(eip + 4)) ||
		(unsigned long)file < PAGE_OFFSET || __get_user(c, file))
		file = "<bad filename>";

	printk(KERN_EMERG "------------[ cut here ]------------\n");
	printk(KERN_EMERG "kernel BUG at %s:%d!\n", file, line);

no_bug:
	return;

	/* Here we know it was a BUG but file-n-line is unavailable */
bug:
	printk(KERN_EMERG "Kernel BUG\n");
}

/* This is gone through when something in the kernel
 * has done something bad and is about to be terminated.
*/
void die(const char * str, struct pt_regs * regs, long err)
{
	static struct {
		spinlock_t lock;
		u32 lock_owner;
		int lock_owner_depth;
	} die = {
		.lock =			SPIN_LOCK_UNLOCKED,
		.lock_owner =		-1,
		.lock_owner_depth =	0
	};
	static int die_counter;
	unsigned long flags;

	oops_enter();

	if (die.lock_owner != raw_smp_processor_id()) {
		console_verbose();
		spin_lock_irqsave(&die.lock, flags);
		die.lock_owner = smp_processor_id();
		die.lock_owner_depth = 0;
		bust_spinlocks(1);
	}
	else
		local_save_flags(flags);

	if (++die.lock_owner_depth < 3) {
		int nl = 0;
		unsigned long esp;
		unsigned short ss;

		handle_BUG(regs);
		printk(KERN_EMERG "%s: %04lx [#%d]\n", str, err & 0xffff, ++die_counter);
#ifdef CONFIG_PREEMPT
		printk(KERN_EMERG "PREEMPT ");
		nl = 1;
#endif
#ifdef CONFIG_SMP
		if (!nl)
			printk(KERN_EMERG);
		printk("SMP ");
		nl = 1;
#endif
#ifdef CONFIG_DEBUG_PAGEALLOC
		if (!nl)
			printk(KERN_EMERG);
		printk("DEBUG_PAGEALLOC");
		nl = 1;
#endif
		if (nl)
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
	spin_unlock_irqrestore(&die.lock, flags);

	if (!regs)
		return;

	if (kexec_should_crash(current))
		crash_kexec(regs);

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
	tsk->thread.error_code = error_code;
	tsk->thread.trap_no = trapnr;

	if (regs->eflags & VM_MASK) {
		if (vm86)
			goto vm86_trap;
		goto trap_signal;
	}

	if (!user_mode(regs))
		goto kernel_trap;

	trap_signal: {
		if (info)
			force_sig_info(signr, info, tsk);
		else
			force_sig(signr, tsk);
		return;
	}

	kernel_trap: {
		if (!fixup_exception(regs))
			die(str, regs, error_code);
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

#define DO_ERROR_INFO(trapnr, signr, str, name, sicode, siaddr) \
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
DO_ERROR_INFO( 6, SIGILL,  "invalid opcode", invalid_op, ILL_ILLOPN, regs->eip)
DO_ERROR( 9, SIGFPE,  "coprocessor segment overrun", coprocessor_segment_overrun)
DO_ERROR(10, SIGSEGV, "invalid TSS", invalid_TSS)
DO_ERROR(11, SIGBUS,  "segment not present", segment_not_present)
DO_ERROR(12, SIGBUS,  "stack segment", stack_segment)
DO_ERROR_INFO(17, SIGBUS, "alignment check", alignment_check, BUS_ADRALN, 0)
DO_ERROR_INFO(32, SIGSEGV, "iret exception", iret_error, ILL_BADSTK, 0)

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
	if (tss->io_bitmap_base == INVALID_IO_BITMAP_OFFSET_LAZY &&
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
		tss->io_bitmap_base = IO_BITMAP_OFFSET;
		tss->io_bitmap_owner = thread;
		put_cpu();
		return;
	}
	put_cpu();

	current->thread.error_code = error_code;
	current->thread.trap_no = 13;

	if (regs->eflags & VM_MASK)
		goto gp_in_vm86;

	if (!user_mode(regs))
		goto gp_in_kernel;

	current->thread.error_code = error_code;
	current->thread.trap_no = 13;
	force_sig(SIGSEGV, current);
	return;

gp_in_vm86:
	local_irq_enable();
	handle_vm86_fault((struct kernel_vm86_regs *) regs, error_code);
	return;

gp_in_kernel:
	if (!fixup_exception(regs)) {
		if (notify_die(DIE_GPF, "general protection fault", regs,
				error_code, 13, SIGSEGV) == NOTIFY_STOP)
			return;
		die("general protection fault", regs, error_code);
	}
}

static void mem_parity_error(unsigned char reason, struct pt_regs * regs)
{
	printk(KERN_EMERG "Uhhuh. NMI received. Dazed and confused, but trying "
			"to continue\n");
	printk(KERN_EMERG "You probably have a hardware problem with your RAM "
			"chips\n");

	/* Clear and disable the memory parity error line. */
	clear_mem_error(reason);
}

static void io_check_error(unsigned char reason, struct pt_regs * regs)
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

static void unknown_nmi_error(unsigned char reason, struct pt_regs * regs)
{
#ifdef CONFIG_MCA
	/* Might actually be able to figure out what the guilty party
	* is. */
	if( MCA_bus ) {
		mca_handle_nmi();
		return;
	}
#endif
	printk("Uhhuh. NMI received for unknown reason %02x on CPU %d.\n",
		reason, smp_processor_id());
	printk("Dazed and confused, but trying to continue\n");
	printk("Do you have a strange power saving mode enabled?\n");
}

static DEFINE_SPINLOCK(nmi_print_lock);

void die_nmi (struct pt_regs *regs, const char *msg)
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
	printk(KERN_EMERG "console shuts up ...\n");
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

static void default_do_nmi(struct pt_regs * regs)
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
		if (nmi_watchdog) {
			nmi_watchdog_tick(regs);
			return;
		}
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

static int dummy_nmi_callback(struct pt_regs * regs, int cpu)
{
	return 0;
}
 
static nmi_callback_t nmi_callback = dummy_nmi_callback;
 
fastcall void do_nmi(struct pt_regs * regs, long error_code)
{
	int cpu;

	nmi_enter();

	cpu = smp_processor_id();

	++nmi_count(cpu);

	if (!rcu_dereference(nmi_callback)(regs, cpu))
		default_do_nmi(regs);

	nmi_exit();
}

void set_nmi_callback(nmi_callback_t callback)
{
	vmalloc_sync_all();
	rcu_assign_pointer(nmi_callback, callback);
}
EXPORT_SYMBOL_GPL(set_nmi_callback);

void unset_nmi_callback(void)
{
	nmi_callback = dummy_nmi_callback;
}
EXPORT_SYMBOL_GPL(unset_nmi_callback);

#ifdef CONFIG_KPROBES
fastcall void __kprobes do_int3(struct pt_regs *regs, long error_code)
{
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

fastcall void setup_x86_bogus_stack(unsigned char * stk)
{
	unsigned long *switch16_ptr, *switch32_ptr;
	struct pt_regs *regs;
	unsigned long stack_top, stack_bot;
	unsigned short iret_frame16_off;
	int cpu = smp_processor_id();
	/* reserve the space on 32bit stack for the magic switch16 pointer */
	memmove(stk, stk + 8, sizeof(struct pt_regs));
	switch16_ptr = (unsigned long *)(stk + sizeof(struct pt_regs));
	regs = (struct pt_regs *)stk;
	/* now the switch32 on 16bit stack */
	stack_bot = (unsigned long)&per_cpu(cpu_16bit_stack, cpu);
	stack_top = stack_bot +	CPU_16BIT_STACK_SIZE;
	switch32_ptr = (unsigned long *)(stack_top - 8);
	iret_frame16_off = CPU_16BIT_STACK_SIZE - 8 - 20;
	/* copy iret frame on 16bit stack */
	memcpy((void *)(stack_bot + iret_frame16_off), &regs->eip, 20);
	/* fill in the switch pointers */
	switch16_ptr[0] = (regs->esp & 0xffff0000) | iret_frame16_off;
	switch16_ptr[1] = __ESPFIX_SS;
	switch32_ptr[0] = (unsigned long)stk + sizeof(struct pt_regs) +
		8 - CPU_16BIT_STACK_SIZE;
	switch32_ptr[1] = __KERNEL_DS;
}

fastcall unsigned char * fixup_x86_bogus_stack(unsigned short sp)
{
	unsigned long *switch32_ptr;
	unsigned char *stack16, *stack32;
	unsigned long stack_top, stack_bot;
	int len;
	int cpu = smp_processor_id();
	stack_bot = (unsigned long)&per_cpu(cpu_16bit_stack, cpu);
	stack_top = stack_bot +	CPU_16BIT_STACK_SIZE;
	switch32_ptr = (unsigned long *)(stack_top - 8);
	/* copy the data from 16bit stack to 32bit stack */
	len = CPU_16BIT_STACK_SIZE - 8 - sp;
	stack16 = (unsigned char *)(stack_bot + sp);
	stack32 = (unsigned char *)
		(switch32_ptr[0] + CPU_16BIT_STACK_SIZE - 8 - len);
	memcpy(stack32, stack16, len);
	return stack32;
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
asmlinkage void math_state_restore(struct pt_regs regs)
{
	struct thread_info *thread = current_thread_info();
	struct task_struct *tsk = thread->task;

	clts();		/* Allow maths ops (or we recurse) */
	if (!tsk_used_math(tsk))
		init_fpu(tsk);
	restore_fpu(tsk);
	thread->status |= TS_USEDFPU;	/* So we fnsave on switch_to() */
}

#ifndef CONFIG_MATH_EMULATION

asmlinkage void math_emulate(long arg)
{
	printk(KERN_EMERG "math-emulation not enabled and no coprocessor found.\n");
	printk(KERN_EMERG "killing %s.\n",current->comm);
	force_sig(SIGFPE,current);
	schedule();
}

#endif /* CONFIG_MATH_EMULATION */

#ifdef CONFIG_X86_F00F_BUG
void __init trap_init_f00f_bug(void)
{
	__set_fixmap(FIX_F00F_IDT, __pa(&idt_table), PAGE_KERNEL_RO);

	/*
	 * Update the IDT descriptor and reload the IDT so that
	 * it uses the read-only mapped virtual address.
	 */
	idt_descr.address = fix_to_virt(FIX_F00F_IDT);
	load_idt(&idt_descr);
}
#endif

#define _set_gate(gate_addr,type,dpl,addr,seg) \
do { \
  int __d0, __d1; \
  __asm__ __volatile__ ("movw %%dx,%%ax\n\t" \
	"movw %4,%%dx\n\t" \
	"movl %%eax,%0\n\t" \
	"movl %%edx,%1" \
	:"=m" (*((long *) (gate_addr))), \
	 "=m" (*(1+(long *) (gate_addr))), "=&a" (__d0), "=&d" (__d1) \
	:"i" ((short) (0x8000+(dpl<<13)+(type<<8))), \
	 "3" ((char *) (addr)),"2" ((seg) << 16)); \
} while (0)


/*
 * This needs to use 'idt_table' rather than 'idt', and
 * thus use the _nonmapped_ version of the IDT, as the
 * Pentium F0 0F bugfix can have resulted in the mapped
 * IDT being write-protected.
 */
void set_intr_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,14,0,addr,__KERNEL_CS);
}

/*
 * This routine sets up an interrupt gate at directory privilege level 3.
 */
static inline void set_system_intr_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n, 14, 3, addr, __KERNEL_CS);
}

static void __init set_trap_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,15,0,addr,__KERNEL_CS);
}

static void __init set_system_gate(unsigned int n, void *addr)
{
	_set_gate(idt_table+n,15,3,addr,__KERNEL_CS);
}

static void __init set_task_gate(unsigned int n, unsigned int gdt_entry)
{
	_set_gate(idt_table+n,5,0,0,(gdt_entry<<3));
}


void __init trap_init(void)
{
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
