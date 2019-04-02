// SPDX-License-Identifier: GPL-2.0
#include <linux/.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/kde.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/sched/de.h>
#include <linux/sched/task_stack.h>
#include <linux/uaccess.h>
#include <linux/hardirq.h>
#include <linux/kernel.h>
#include <linux/kexec.h>
#include <linux/sched/signal.h>

#include <linux/extable.h>
#include <linux/module.h>	/* print_modules */
#include <asm/unwinder.h>
#include <asm/traps.h>

static DEFINE_SPINLOCK(die_lock);

void die(const char *str, struct pt_regs *regs, long err)
{
	static int die_counter;

	oops_enter();

	spin_lock_irq(&die_lock);
	console_verbose();
	bust_spinlocks(1);

	printk("%s: %04lx [#%d]\n", str, err & 0xffff, ++die_counter);
	print_modules();
	show_regs(regs);

	printk("Process: %s (pid: %d, stack limit = %p)\n", current->comm,
			task_pid_nr(current), task_stack_page(current) + 1);

	if (!user_mode(regs) || in_interrupt())
		dump_mem("Stack: ", regs->regs[15], THREAD_SIZE +
			 (unsigned long)task_stack_page(current));

	notify_die(DIE_OOPS, str, regs, err, 255, SIGSEGV);

	bust_spinlocks(0);
	add_taint(TAINT_DIE, LOCKDEP_NOW_UNRELIABLE);
	spin_unlock_irq(&die_lock);
	oops_exit();

	if (kexec_should_crash(current))
		crash_kexec(regs);

	if (in_interrupt())
		panic("Fatal exception in interrupt");

	if (panic_on_oops)
		panic("Fatal exception");

	do_exit(SIGSEGV);
}

void die_if_kernel(const char *str, struct pt_regs *regs, long err)
{
	if (!user_mode(regs))
		die(str, regs, err);
}

/*
 * try and fix up kernelspace address errors
 * - userspace errors just cause EFAULT to be returned, resulting in SEGV
 * - kernel/userspace interfaces cause a jump to an appropriate handler
 * - other kernel errors are bad
 */
void die_if_no_fixup(const char *str, struct pt_regs *regs, long err)
{
	if (!user_mode(regs)) {
		const struct exception_table_entry *fixup;
		fixup = search_exception_tables(regs->pc);
		if (fixup) {
			regs->pc = fixup->fixup;
			return;
		}

		die(str, regs, err);
	}
}

#ifdef CONFIG_GENERIC_
static void handle_(struct pt_regs *regs)
{
	const struct _entry *;
	unsigned long addr = regs->pc;
	enum _trap_type tt;

	if (!is_valid_addr(addr))
		goto invalid;

	 = find_(addr);

	/* Switch unwinders when unwind_stack() is called */
	if (->flags & FLAG_UNWINDER)
		unwinder_faulted = 1;

	tt = report_(addr, regs);
	if (tt == _TRAP_TYPE_WARN) {
		regs->pc += instruction_size(addr);
		return;
	}

invalid:
	die("Kernel ", regs, TRAPA__OPCODE & 0xff);
}

int is_valid_addr(unsigned long addr)
{
	insn_size_t opcode;

	if (addr < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((insn_size_t *)addr, opcode))
		return 0;
	if (opcode == TRAPA__OPCODE)
		return 1;

	return 0;
}
#endif

/*
 * Generic trap handler.
 */
BUILD_TRAP_HANDLER(de)
{
	TRAP_HANDLER_DECL;

	/* Rewind */
	regs->pc -= instruction_size(__raw_readw(regs->pc - 4));

	if (notify_die(DIE_TRAP, "de trap", regs, 0, vec & 0xff,
		       SIGTRAP) == NOTIFY_STOP)
		return;

	force_sig(SIGTRAP, current);
}

/*
 * Special handler for () traps.
 */
BUILD_TRAP_HANDLER()
{
	TRAP_HANDLER_DECL;

	/* Rewind */
	regs->pc -= instruction_size(__raw_readw(regs->pc - 4));

	if (notify_die(DIE_TRAP, " trap", regs, 0, TRAPA__OPCODE & 0xff,
		       SIGTRAP) == NOTIFY_STOP)
		return;

#ifdef CONFIG_GENERIC_
	if (__kernel_text_address(instruction_pointer(regs))) {
		insn_size_t insn = *(insn_size_t *)instruction_pointer(regs);
		if (insn == TRAPA__OPCODE)
			handle_(regs);
		return;
	}
#endif

	force_sig(SIGTRAP, current);
}

BUILD_TRAP_HANDLER(nmi)
{
	unsigned int cpu = smp_processor_id();
	TRAP_HANDLER_DECL;

	nmi_enter();
	nmi_count(cpu)++;

	switch (notify_die(DIE_NMI, "NMI", regs, 0, vec & 0xff, SIGINT)) {
	case NOTIFY_OK:
	case NOTIFY_STOP:
		break;
	case NOTIFY_BAD:
		die("Fatal Non-Maskable Interrupt", regs, SIGINT);
	default:
		printk(KERN_ALERT "Got NMI, but nobody cared. Ignoring...\n");
		break;
	}

	nmi_exit();
}
