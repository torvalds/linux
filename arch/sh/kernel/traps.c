#include <linux/bug.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/kdebug.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/system.h>

#ifdef CONFIG_BUG
static void handle_BUG(struct pt_regs *regs)
{
	enum bug_trap_type tt;
	tt = report_bug(regs->pc, regs);
	if (tt == BUG_TRAP_TYPE_WARN) {
		regs->pc += instruction_size(regs->pc);
		return;
	}

	die("Kernel BUG", regs, TRAPA_BUG_OPCODE & 0xff);
}

int is_valid_bugaddr(unsigned long addr)
{
	unsigned short opcode;

	if (addr < PAGE_OFFSET)
		return 0;
	if (probe_kernel_address((u16 *)addr, opcode))
		return 0;

	return opcode == TRAPA_BUG_OPCODE;
}
#endif

/*
 * Generic trap handler.
 */
BUILD_TRAP_HANDLER(debug)
{
	TRAP_HANDLER_DECL;

	/* Rewind */
	regs->pc -= instruction_size(ctrl_inw(regs->pc - 4));

	if (notify_die(DIE_TRAP, "debug trap", regs, 0, vec & 0xff,
		       SIGTRAP) == NOTIFY_STOP)
		return;

	force_sig(SIGTRAP, current);
}

/*
 * Special handler for BUG() traps.
 */
BUILD_TRAP_HANDLER(bug)
{
	TRAP_HANDLER_DECL;

	/* Rewind */
	regs->pc -= instruction_size(ctrl_inw(regs->pc - 4));

	if (notify_die(DIE_TRAP, "bug trap", regs, 0, TRAPA_BUG_OPCODE & 0xff,
		       SIGTRAP) == NOTIFY_STOP)
		return;

#ifdef CONFIG_BUG
	if (__kernel_text_address(instruction_pointer(regs))) {
		opcode_t insn = *(opcode_t *)instruction_pointer(regs);
		if (insn == TRAPA_BUG_OPCODE)
			handle_BUG(regs);
	}
#endif

	force_sig(SIGTRAP, current);
}
