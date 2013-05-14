/*
 * Nios2 KGDB support
 *
 * Copyright (C) 2011 Tobias Klauser <tklauser@distanz.ch>
 *
 * Based on the code posted by Kazuyasu on the Altera Forum at:
 * http://www.alteraforum.com/forum/showpost.php?p=77003&postcount=20
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/io.h>

static int wait_for_remote_debugger;

void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	gdb_regs[GDB_R0] = 0;
	gdb_regs[GDB_AT] = regs->r1;
	gdb_regs[GDB_R2] = regs->r2;
	gdb_regs[GDB_R3] = regs->r3;
	gdb_regs[GDB_R4] = regs->r4;
	gdb_regs[GDB_R5] = regs->r5;
	gdb_regs[GDB_R6] = regs->r6;
	gdb_regs[GDB_R7] = regs->r7;
	gdb_regs[GDB_R8] = regs->r8;
	gdb_regs[GDB_R9] = regs->r9;
	gdb_regs[GDB_R10] = regs->r10;
	gdb_regs[GDB_R11] = regs->r11;
	gdb_regs[GDB_R12] = regs->r12;
	gdb_regs[GDB_R13] = regs->r13;
	gdb_regs[GDB_R14] = regs->r14;
	gdb_regs[GDB_R15] = regs->r15;

	gdb_regs[GDB_RA] = regs->ra;
	gdb_regs[GDB_FP] = regs->fp;
	gdb_regs[GDB_SP] = regs->sp;
	gdb_regs[GDB_GP] = regs->gp;
	gdb_regs[GDB_ESTATUS] = regs->estatus;
	gdb_regs[GDB_PC] = regs->ea;
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *regs)
{
	regs->r1 = gdb_regs[GDB_AT];
	regs->r2 = gdb_regs[GDB_R2];
	regs->r3 = gdb_regs[GDB_R3];
	regs->r4 = gdb_regs[GDB_R4];
	regs->r5 = gdb_regs[GDB_R5];
	regs->r6 = gdb_regs[GDB_R6];
	regs->r7 = gdb_regs[GDB_R7];
	regs->r8 = gdb_regs[GDB_R8];
	regs->r9 = gdb_regs[GDB_R9];
	regs->r10 = gdb_regs[GDB_R10];
	regs->r11 = gdb_regs[GDB_R11];
	regs->r12 = gdb_regs[GDB_R12];
	regs->r13 = gdb_regs[GDB_R13];
	regs->r14 = gdb_regs[GDB_R14];
	regs->r15 = gdb_regs[GDB_R15];

	regs->ra = gdb_regs[GDB_RA];
	regs->fp = gdb_regs[GDB_FP];
	regs->sp = gdb_regs[GDB_SP];
	regs->gp = gdb_regs[GDB_GP];
	regs->estatus = gdb_regs[GDB_ESTATUS];
	regs->ea = gdb_regs[GDB_PC];
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	gdb_regs[GDB_SP] = p->thread.kregs->sp;
	gdb_regs[GDB_PC] = p->thread.kregs->ea;
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->ea = pc;
}

int kgdb_arch_handle_exception(int vector, int signo, int err_code,
				char *remcom_in_buffer, char *remcom_out_buffer,
				struct pt_regs *regs)
{
	char *ptr;
	unsigned long addr;

	switch (remcom_in_buffer[0]) {
	case 's':
	case 'c':
		/* handle the optional parameters */
		ptr = &remcom_in_buffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			regs->ea = addr;

		return 0;
	}

	return -1; /* this means that we do not want to exit from the handler */
}

asmlinkage void kgdb_breakpoint_c(struct pt_regs *regs)
{
	/*
	 * The breakpoint entry code has moved the PC on by 4 bytes, so we must
	 * move it back.  This could be done on the host but we do it here
	 */
	if (!wait_for_remote_debugger)
		regs->ea -= 4;
	else	/* pass the first trap 30 code */
		wait_for_remote_debugger = 0;

	kgdb_handle_exception(30, SIGTRAP, 0, regs);
}

int kgdb_arch_init(void)
{
	wait_for_remote_debugger = 1;
	return 0;
}

void kgdb_arch_exit(void)
{
	/* Nothing to do */
}

struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: trap 30 */
	.gdb_bpt_instr = { 0xba, 0x6f, 0x3b, 0x00 },
};
