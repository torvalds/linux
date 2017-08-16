/*
 * H8/300 KGDB support
 *
 * Copyright (C) 2015 Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/ptrace.h>
#include <linux/kgdb.h>
#include <linux/kdebug.h>
#include <linux/io.h>

struct dbg_reg_def_t dbg_reg_def[DBG_MAX_REG_NUM] = {
	{ "er0", GDB_SIZEOF_REG, offsetof(struct pt_regs, er0) },
	{ "er1", GDB_SIZEOF_REG, offsetof(struct pt_regs, er1) },
	{ "er2", GDB_SIZEOF_REG, offsetof(struct pt_regs, er2) },
	{ "er3", GDB_SIZEOF_REG, offsetof(struct pt_regs, er3) },
	{ "er4", GDB_SIZEOF_REG, offsetof(struct pt_regs, er4) },
	{ "er5", GDB_SIZEOF_REG, offsetof(struct pt_regs, er5) },
	{ "er6", GDB_SIZEOF_REG, offsetof(struct pt_regs, er6) },
	{ "sp", GDB_SIZEOF_REG, offsetof(struct pt_regs, sp) },
	{ "ccr", GDB_SIZEOF_REG, offsetof(struct pt_regs, ccr) },
	{ "pc", GDB_SIZEOF_REG, offsetof(struct pt_regs, pc) },
	{ "cycles", GDB_SIZEOF_REG, -1 },
#if defined(CONFIG_CPU_H8S)
	{ "exr", GDB_SIZEOF_REG, offsetof(struct pt_regs, exr) },
#endif
	{ "tick", GDB_SIZEOF_REG, -1 },
	{ "inst", GDB_SIZEOF_REG, -1 },
};

char *dbg_get_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return NULL;

	switch (regno) {
	case GDB_CCR:
#if defined(CONFIG_CPU_H8S)
	case GDB_EXR:
#endif
		*(u32 *)mem = *(u16 *)((void *)regs +
				       dbg_reg_def[regno].offset);
		break;
	default:
		if (dbg_reg_def[regno].offset >= 0)
			memcpy(mem, (void *)regs + dbg_reg_def[regno].offset,
			       dbg_reg_def[regno].size);
		else
			memset(mem, 0, dbg_reg_def[regno].size);
		break;
	}
	return dbg_reg_def[regno].name;
}

int dbg_set_reg(int regno, void *mem, struct pt_regs *regs)
{
	if (regno >= DBG_MAX_REG_NUM || regno < 0)
		return -EINVAL;

	switch (regno) {
	case GDB_CCR:
#if defined(CONFIG_CPU_H8S)
	case GDB_EXR:
#endif
		*(u16 *)((void *)regs +
			 dbg_reg_def[regno].offset) = *(u32 *)mem;
		break;
	default:
		memcpy((void *)regs + dbg_reg_def[regno].offset, mem,
		       dbg_reg_def[regno].size);
	}
	return 0;
}

asmlinkage void h8300_kgdb_trap(struct pt_regs *regs)
{
	regs->pc &= 0x00ffffff;
	if (kgdb_handle_exception(10, SIGTRAP, 0, regs))
		return;
	if (*(u16 *)(regs->pc) == *(u16 *)&arch_kgdb_ops.gdb_bpt_instr)
		regs->pc += BREAK_INSTR_SIZE;
	regs->pc |= regs->ccr << 24;
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs, struct task_struct *p)
{
	memset((char *)gdb_regs, 0, NUMREGBYTES);
	gdb_regs[GDB_SP] = p->thread.ksp;
	gdb_regs[GDB_PC] = KSTK_EIP(p);
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long pc)
{
	regs->pc = pc;
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
			regs->pc = addr;

		return 0;
	}

	return -1; /* this means that we do not want to exit from the handler */
}

int kgdb_arch_init(void)
{
	return 0;
}

void kgdb_arch_exit(void)
{
	/* Nothing to do */
}

const struct kgdb_arch arch_kgdb_ops = {
	/* Breakpoint instruction: trapa #2 */
	.gdb_bpt_instr = { 0x57, 0x20 },
};
