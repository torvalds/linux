/*
 * Nios2 KGDB support
 *
 * Copyright (C) 2015 Altera Corporation
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
#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/io.h>

static int wait_for_remote_debugger;

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] =
{
	{ "zero", GDB_SIZEOF_REG, -1 },
	{ "at", GDB_SIZEOF_REG, offsetof(struct pt_regs, r1) },
	{ "r2", GDB_SIZEOF_REG, offsetof(struct pt_regs, r2) },
	{ "r3", GDB_SIZEOF_REG, offsetof(struct pt_regs, r3) },
	{ "r4", GDB_SIZEOF_REG, offsetof(struct pt_regs, r4) },
	{ "r5", GDB_SIZEOF_REG, offsetof(struct pt_regs, r5) },
	{ "r6", GDB_SIZEOF_REG, offsetof(struct pt_regs, r6) },
	{ "r7", GDB_SIZEOF_REG, offsetof(struct pt_regs, r7) },
	{ "r8", GDB_SIZEOF_REG, offsetof(struct pt_regs, r8) },
	{ "r9", GDB_SIZEOF_REG, offsetof(struct pt_regs, r9) },
	{ "r10", GDB_SIZEOF_REG, offsetof(struct pt_regs, r10) },
	{ "r11", GDB_SIZEOF_REG, offsetof(struct pt_regs, r11) },
	{ "r12", GDB_SIZEOF_REG, offsetof(struct pt_regs, r12) },
	{ "r13", GDB_SIZEOF_REG, offsetof(struct pt_regs, r13) },
	{ "r14", GDB_SIZEOF_REG, offsetof(struct pt_regs, r14) },
	{ "r15", GDB_SIZEOF_REG, offsetof(struct pt_regs, r15) },
	{ "r16", GDB_SIZEOF_REG, -1 },
	{ "r17", GDB_SIZEOF_REG, -1 },
	{ "r18", GDB_SIZEOF_REG, -1 },
	{ "r19", GDB_SIZEOF_REG, -1 },
	{ "r20", GDB_SIZEOF_REG, -1 },
	{ "r21", GDB_SIZEOF_REG, -1 },
	{ "r22", GDB_SIZEOF_REG, -1 },
	{ "r23", GDB_SIZEOF_REG, -1 },
	{ "et", GDB_SIZEOF_REG, -1 },
	{ "bt", GDB_SIZEOF_REG, -1 },
	{ "gp", GDB_SIZEOF_REG, offsetof(struct pt_regs, gp) },
	{ "sp", GDB_SIZEOF_REG, offsetof(struct pt_regs, sp) },
	{ "fp", GDB_SIZEOF_REG, offsetof(struct pt_regs, fp) },
	{ "ea", GDB_SIZEOF_REG, -1 },
	{ "ba", GDB_SIZEOF_REG, -1 },
	{ "ra", GDB_SIZEOF_REG, offsetof(struct pt_regs, ra) },
	{ "pc", GDB_SIZEOF_REG, offsetof(struct pt_regs, ea) },
	{ "status", GDB_SIZEOF_REG, -1 },
	{ "estatus", GDB_SIZEOF_REG, offsetof(struct pt_regs, estatus) },
	{ "bstatus", GDB_SIZEOF_REG, -1 },
	{ "ienable", GDB_SIZEOF_REG, -1 },
	{ "ipending", GDB_SIZEOF_REG, -1},
	{ "cpuid", GDB_SIZEOF_REG, -1 },
	{ "ctl6", GDB_SIZEOF_REG, -1 },
	{ "exception", GDB_SIZEOF_REG, -1 },
	{ "pteaddr", GDB_SIZEOF_REG, -1 },
	{ "tlbacc", GDB_SIZEOF_REG, -1 },
	{ "tlbmisc", GDB_SIZEOF_REG, -1 },
	{ "eccinj", GDB_SIZEOF_REG, -1 },
	{ "badaddr", GDB_SIZEOF_REG, -1 },
	{ "config", GDB_SIZEOF_REG, -1 },
	{ "mpubase", GDB_SIZEOF_REG, -1 },
	{ "mpuacc", GDB_SIZEOF_REG, -1 },
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy(mem, (void *)regs + dbg_reg_def[regno].offset,
		       dbg_reg_def[regno].size);
	else
		memset(mem, 0, dbg_reg_def[regno].size);

	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	if (dbg_reg_def[regno].offset != -1)
		memcpy((void *)regs + dbg_reg_def[regno].offset, mem,
		       dbg_reg_def[regno].size);

	return 0;
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	memset((char *)gdb_regs, 0, NUMREGBYTES);
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

const struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: trap 30 */
	.gdb_bpt_instr = { 0xba, 0x6f, 0x3b, 0x00 },
};
