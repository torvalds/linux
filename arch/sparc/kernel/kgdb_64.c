// SPDX-License-Identifier: GPL-2.0
/* kgdb.c: KGDB support for 64-bit sparc.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/ftrace.h>
#include <linux/context_tracking.h>

#include <asm/cacheflush.h>
#include <asm/kdebug.h>
#include <asm/ptrace.h>
#include <asm/irq.h>

#include "kernel.h"

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct reg_window *win;
	int i;

	gdb_regs[GDB_G0] = 0;
	for (i = 0; i < 15; i++)
		gdb_regs[GDB_G1 + i] = regs->u_regs[UREG_G1 + i];

	win = (struct reg_window *) (regs->u_regs[UREG_FP] + STACK_BIAS);
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_L0 + i] = win->locals[i];
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_I0 + i] = win->ins[i];

	for (i = GDB_F0; i <= GDB_F62; i++)
		gdb_regs[i] = 0;

	gdb_regs[GDB_PC] = regs->tpc;
	gdb_regs[GDB_NPC] = regs->tnpc;
	gdb_regs[GDB_STATE] = regs->tstate;
	gdb_regs[GDB_FSR] = 0;
	gdb_regs[GDB_FPRS] = 0;
	gdb_regs[GDB_Y] = regs->y;
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	struct thread_info *t = task_thread_info(p);
	extern unsigned int switch_to_pc;
	extern unsigned int ret_from_fork;
	struct reg_window *win;
	unsigned long pc, cwp;
	int i;

	for (i = GDB_G0; i < GDB_G6; i++)
		gdb_regs[i] = 0;
	gdb_regs[GDB_G6] = (unsigned long) t;
	gdb_regs[GDB_G7] = (unsigned long) p;
	for (i = GDB_O0; i < GDB_SP; i++)
		gdb_regs[i] = 0;
	gdb_regs[GDB_SP] = t->ksp;
	gdb_regs[GDB_O7] = 0;

	win = (struct reg_window *) (t->ksp + STACK_BIAS);
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_L0 + i] = win->locals[i];
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_I0 + i] = win->ins[i];

	for (i = GDB_F0; i <= GDB_F62; i++)
		gdb_regs[i] = 0;

	if (t->new_child)
		pc = (unsigned long) &ret_from_fork;
	else
		pc = (unsigned long) &switch_to_pc;

	gdb_regs[GDB_PC] = pc;
	gdb_regs[GDB_NPC] = pc + 4;

	cwp = __thread_flag_byte_ptr(t)[TI_FLAG_BYTE_CWP];

	gdb_regs[GDB_STATE] = (TSTATE_PRIV | TSTATE_IE | cwp);
	gdb_regs[GDB_FSR] = 0;
	gdb_regs[GDB_FPRS] = 0;
	gdb_regs[GDB_Y] = 0;
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct reg_window *win;
	int i;

	for (i = 0; i < 15; i++)
		regs->u_regs[UREG_G1 + i] = gdb_regs[GDB_G1 + i];

	/* If the TSTATE register is changing, we have to preserve
	 * the CWP field, otherwise window save/restore explodes.
	 */
	if (regs->tstate != gdb_regs[GDB_STATE]) {
		unsigned long cwp = regs->tstate & TSTATE_CWP;

		regs->tstate = (gdb_regs[GDB_STATE] & ~TSTATE_CWP) | cwp;
	}

	regs->tpc = gdb_regs[GDB_PC];
	regs->tnpc = gdb_regs[GDB_NPC];
	regs->y = gdb_regs[GDB_Y];

	win = (struct reg_window *) (regs->u_regs[UREG_FP] + STACK_BIAS);
	for (i = 0; i < 8; i++)
		win->locals[i] = gdb_regs[GDB_L0 + i];
	for (i = 0; i < 8; i++)
		win->ins[i] = gdb_regs[GDB_I0 + i];
}

#ifdef CONFIG_SMP
void __irq_entry smp_kgdb_capture_client(int irq, struct pt_regs *regs)
{
	unsigned long flags;

	__asm__ __volatile__("rdpr      %%pstate, %0\n\t"
			     "wrpr      %0, %1, %%pstate"
			     : "=r" (flags)
			     : "i" (PSTATE_IE));

	flushw_all();

	if (atomic_read(&kgdb_active) != -1)
		kgdb_nmicallback(raw_smp_processor_id(), regs);

	__asm__ __volatile__("wrpr	%0, 0, %%pstate"
			     : : "r" (flags));
}
#endif

int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *linux_regs)
{
	unsigned long addr;
	char *ptr;

	switch (remcomInBuffer[0]) {
	case 'c':
		/* try to read optional parameter, pc unchanged if no parm */
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr)) {
			linux_regs->tpc = addr;
			linux_regs->tnpc = addr + 4;
		}
		/* fall through */

	case 'D':
	case 'k':
		if (linux_regs->tpc == (unsigned long) arch_kgdb_breakpoint) {
			linux_regs->tpc = linux_regs->tnpc;
			linux_regs->tnpc += 4;
		}
		return 0;
	}
	return -1;
}

asmlinkage void kgdb_trap(unsigned long trap_level, struct pt_regs *regs)
{
	enum ctx_state prev_state = exception_enter();
	unsigned long flags;

	if (user_mode(regs)) {
		bad_trap(regs, trap_level);
		goto out;
	}

	flushw_all();

	local_irq_save(flags);
	kgdb_handle_exception(0x172, SIGTRAP, 0, regs);
	local_irq_restore(flags);
out:
	exception_exit(prev_state);
}

int kgdb_arch_init(void)
{
	return 0;
}

void kgdb_arch_exit(void)
{
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	regs->tpc = ip;
	regs->tnpc = regs->tpc + 4;
}

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: ta 0x72 */
	.gdb_bpt_instr		= { 0x91, 0xd0, 0x20, 0x72 },
};
