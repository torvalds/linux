// SPDX-License-Identifier: GPL-2.0-only
/*
 * kgdb support for ARC
 *
 * Copyright (C) 2012 Synopsys, Inc. (www.synopsys.com)
 */

#include <linux/kgdb.h>
#include <linux/sched.h>
#include <linux/sched/task_stack.h>
#include <asm/disasm.h>
#include <asm/cacheflush.h>

static void to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *kernel_regs,
			struct callee_regs *cregs)
{
	int regno;

	for (regno = 0; regno <= 26; regno++)
		gdb_regs[_R0 + regno] = get_reg(regno, kernel_regs, cregs);

	for (regno = 27; regno < GDB_MAX_REGS; regno++)
		gdb_regs[regno] = 0;

	gdb_regs[_FP]		= kernel_regs->fp;
	gdb_regs[__SP]		= kernel_regs->sp;
	gdb_regs[_BLINK]	= kernel_regs->blink;
	gdb_regs[_RET]		= kernel_regs->ret;
	gdb_regs[_STATUS32]	= kernel_regs->status32;
	gdb_regs[_LP_COUNT]	= kernel_regs->lp_count;
	gdb_regs[_LP_END]	= kernel_regs->lp_end;
	gdb_regs[_LP_START]	= kernel_regs->lp_start;
	gdb_regs[_BTA]		= kernel_regs->bta;
	gdb_regs[_STOP_PC]	= kernel_regs->ret;
}

static void from_gdb_regs(unsigned long *gdb_regs, struct pt_regs *kernel_regs,
			struct callee_regs *cregs)
{
	int regno;

	for (regno = 0; regno <= 26; regno++)
		set_reg(regno, gdb_regs[regno + _R0], kernel_regs, cregs);

	kernel_regs->fp		= gdb_regs[_FP];
	kernel_regs->sp		= gdb_regs[__SP];
	kernel_regs->blink	= gdb_regs[_BLINK];
	kernel_regs->ret	= gdb_regs[_RET];
	kernel_regs->status32	= gdb_regs[_STATUS32];
	kernel_regs->lp_count	= gdb_regs[_LP_COUNT];
	kernel_regs->lp_end	= gdb_regs[_LP_END];
	kernel_regs->lp_start	= gdb_regs[_LP_START];
	kernel_regs->bta	= gdb_regs[_BTA];
}


void pt_regs_to_gdb_regs(unsigned long *gdb_regs, struct pt_regs *kernel_regs)
{
	to_gdb_regs(gdb_regs, kernel_regs, (struct callee_regs *)
		current->thread.callee_reg);
}

void gdb_regs_to_pt_regs(unsigned long *gdb_regs, struct pt_regs *kernel_regs)
{
	from_gdb_regs(gdb_regs, kernel_regs, (struct callee_regs *)
		current->thread.callee_reg);
}

void sleeping_thread_to_gdb_regs(unsigned long *gdb_regs,
				 struct task_struct *task)
{
	if (task)
		to_gdb_regs(gdb_regs, task_pt_regs(task),
			(struct callee_regs *) task->thread.callee_reg);
}

struct single_step_data_t {
	uint16_t opcode[2];
	unsigned long address[2];
	int is_branch;
	int armed;
} single_step_data;

static void undo_single_step(struct pt_regs *regs)
{
	if (single_step_data.armed) {
		int i;

		for (i = 0; i < (single_step_data.is_branch ? 2 : 1); i++) {
			memcpy((void *) single_step_data.address[i],
				&single_step_data.opcode[i],
				BREAK_INSTR_SIZE);

			flush_icache_range(single_step_data.address[i],
				single_step_data.address[i] +
				BREAK_INSTR_SIZE);
		}
		single_step_data.armed = 0;
	}
}

static void place_trap(unsigned long address, void *save)
{
	memcpy(save, (void *) address, BREAK_INSTR_SIZE);
	memcpy((void *) address, &arch_kgdb_ops.gdb_bpt_instr,
		BREAK_INSTR_SIZE);
	flush_icache_range(address, address + BREAK_INSTR_SIZE);
}

static void do_single_step(struct pt_regs *regs)
{
	single_step_data.is_branch = disasm_next_pc((unsigned long)
		regs->ret, regs, (struct callee_regs *)
		current->thread.callee_reg,
		&single_step_data.address[0],
		&single_step_data.address[1]);

	place_trap(single_step_data.address[0], &single_step_data.opcode[0]);

	if (single_step_data.is_branch) {
		place_trap(single_step_data.address[1],
			&single_step_data.opcode[1]);
	}

	single_step_data.armed++;
}

int kgdb_arch_handle_exception(int e_vector, int signo, int err_code,
			       char *remcomInBuffer, char *remcomOutBuffer,
			       struct pt_regs *regs)
{
	unsigned long addr;
	char *ptr;

	undo_single_step(regs);

	switch (remcomInBuffer[0]) {
	case 's':
	case 'c':
		ptr = &remcomInBuffer[1];
		if (kgdb_hex2long(&ptr, &addr))
			regs->ret = addr;
		fallthrough;

	case 'D':
	case 'k':
		atomic_set(&kgdb_cpu_doing_single_step, -1);

		if (remcomInBuffer[0] == 's') {
			do_single_step(regs);
			atomic_set(&kgdb_cpu_doing_single_step,
				   smp_processor_id());
		}

		return 0;
	}
	return -1;
}

int kgdb_arch_init(void)
{
	single_step_data.armed = 0;
	return 0;
}

void kgdb_trap(struct pt_regs *regs)
{
	/* trap_s 3 is used for breakpoints that overwrite existing
	 * instructions, while trap_s 4 is used for compiled breakpoints.
	 *
	 * with trap_s 3 breakpoints the original instruction needs to be
	 * restored and continuation needs to start at the location of the
	 * breakpoint.
	 *
	 * with trap_s 4 (compiled) breakpoints, continuation needs to
	 * start after the breakpoint.
	 */
	if (regs->ecr.param == 3)
		instruction_pointer(regs) -= BREAK_INSTR_SIZE;

	kgdb_handle_exception(1, SIGTRAP, 0, regs);
}

void kgdb_arch_exit(void)
{
}

void kgdb_arch_set_pc(struct pt_regs *regs, unsigned long ip)
{
	instruction_pointer(regs) = ip;
}

void kgdb_call_nmi_hook(void *ignored)
{
	/* Default implementation passes get_irq_regs() but we don't */
	kgdb_nmicallback(raw_smp_processor_id(), NULL);
}

const struct kgdb_arch arch_kgdb_ops = {
	/* breakpoint instruction: TRAP_S 0x3 */
#ifdef CONFIG_CPU_BIG_ENDIAN
	.gdb_bpt_instr		= {0x78, 0x7e},
#else
	.gdb_bpt_instr		= {0x7e, 0x78},
#endif
};
