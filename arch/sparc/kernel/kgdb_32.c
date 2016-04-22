/* kgdb.c: KGDB support for 32-bit sparc.
 *
 * Copyright (C) 2008 David S. Miller <davem@davemloft.net>
 */

#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/sched.h>

#include <asm/kdebug.h>
#include <asm/ptrace.h>
#include <asm/irq.h>
#include <asm/cacheflush.h>

extern unsigned long trapbase;

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct reg_window32 *win;
	int i;

	gdb_regs[GDB_G0] = 0;
	for (i = 0; i < 15; i++)
		gdb_regs[GDB_G1 + i] = regs->u_regs[UREG_G1 + i];

	win = (struct reg_window32 *) regs->u_regs[UREG_FP];
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_L0 + i] = win->locals[i];
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_I0 + i] = win->ins[i];

	for (i = GDB_F0; i <= GDB_F31; i++)
		gdb_regs[i] = 0;

	gdb_regs[GDB_Y] = regs->y;
	gdb_regs[GDB_PSR] = regs->psr;
	gdb_regs[GDB_WIM] = 0;
	gdb_regs[GDB_TBR] = (unsigned long) &trapbase;
	gdb_regs[GDB_PC] = regs->pc;
	gdb_regs[GDB_NPC] = regs->npc;
	gdb_regs[GDB_FSR] = 0;
	gdb_regs[GDB_CSR] = 0;
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	struct thread_info *t = task_thread_info(p);
	struct reg_window32 *win;
	int i;

	for (i = GDB_G0; i < GDB_G6; i++)
		gdb_regs[i] = 0;
	gdb_regs[GDB_G6] = (unsigned long) t;
	gdb_regs[GDB_G7] = 0;
	for (i = GDB_O0; i < GDB_SP; i++)
		gdb_regs[i] = 0;
	gdb_regs[GDB_SP] = t->ksp;
	gdb_regs[GDB_O7] = 0;

	win = (struct reg_window32 *) t->ksp;
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_L0 + i] = win->locals[i];
	for (i = 0; i < 8; i++)
		gdb_regs[GDB_I0 + i] = win->ins[i];

	for (i = GDB_F0; i <= GDB_F31; i++)
		gdb_regs[i] = 0;

	gdb_regs[GDB_Y] = 0;

	gdb_regs[GDB_PSR] = t->kpsr;
	gdb_regs[GDB_WIM] = t->kwim;
	gdb_regs[GDB_TBR] = (unsigned long) &trapbase;
	gdb_regs[GDB_PC] = t->kpc;
	gdb_regs[GDB_NPC] = t->kpc + 4;
	gdb_regs[GDB_FSR] = 0;
	gdb_regs[GDB_CSR] = 0;
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	struct reg_window32 *win;
	int i;

	for (i = 0; i < 15; i++)
		regs->u_regs[UREG_G1 + i] = gdb_regs[GDB_G1 + i];

	/* If the PSR register is changing, we have to preserve
	 * the CWP field, otherwise window save/restore explodes.
	 */
	if (regs->psr != gdb_regs[GDB_PSR]) {
		unsigned long cwp = regs->psr & PSR_CWP;

		regs->psr = (gdb_regs[GDB_PSR] & ~PSR_CWP) | cwp;
	}

	regs->pc = gdb_regs[GDB_PC];
	regs->npc = gdb_regs[GDB_NPC];
	regs->y = gdb_regs[GDB_Y];

	win = (struct reg_window32 *) regs->u_regs[UREG_FP];
	for (i = 0; i < 8; i++)
		win->locals[i] = gdb_regs[GDB_L0 + i];
	for (i = 0; i < 8; i++)
		win->ins[i] = gdb_regs[GDB_I0 + i];
}

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
			linux_regs->pc = addr;
			linux_regs->npc = addr + 4;
		}
		/* fallthru */

	case 'D':
	case 'k':
		if (linux_regs->pc == (unsigned long) arch_kgdb_breakpoint) {
			linux_regs->pc = linux_regs->npc;
			linux_regs->npc += 4;
		}
		return 0;
	}
	return -1;
}

extern void do_hw_interrupt(struct pt_regs *regs, unsigned long type);

asmlinkage void kgdb_trap(unsigned long trap_level, struct pt_regs *regs)
{
	unsigned long flags;

	if (user_mode(regs)) {
		do_hw_interrupt(regs, trap_level);
		return;
	}

	flushw_all();

	local_irq_save(flags);
	kgdb_handle_exception(trap_level, SIGTRAP, 0, regs);
	local_irq_restore(flags);
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
	regs->pc = ip;
	regs->npc = regs->pc + 4;
}

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: ta 0x7d */
	.gdb_bpt_instr		= { 0x91, 0xd0, 0x20, 0x7d },
};
