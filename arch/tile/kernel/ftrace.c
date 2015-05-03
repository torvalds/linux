/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 *
 * TILE-Gx specific ftrace support
 */

#include <linux/ftrace.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/ftrace.h>
#include <asm/sections.h>

#include <arch/opcode.h>

#ifdef CONFIG_DYNAMIC_FTRACE

static inline tilegx_bundle_bits NOP(void)
{
	return create_UnaryOpcodeExtension_X0(FNOP_UNARY_OPCODE_X0) |
		create_RRROpcodeExtension_X0(UNARY_RRR_0_OPCODE_X0) |
		create_Opcode_X0(RRR_0_OPCODE_X0) |
		create_UnaryOpcodeExtension_X1(NOP_UNARY_OPCODE_X1) |
		create_RRROpcodeExtension_X1(UNARY_RRR_0_OPCODE_X1) |
		create_Opcode_X1(RRR_0_OPCODE_X1);
}

static int machine_stopped __read_mostly;

int ftrace_arch_code_modify_prepare(void)
{
	machine_stopped = 1;
	return 0;
}

int ftrace_arch_code_modify_post_process(void)
{
	flush_icache_range(0, CHIP_L1I_CACHE_SIZE());
	machine_stopped = 0;
	return 0;
}

/*
 * Put { move r10, lr; jal ftrace_caller } in a bundle, this lets dynamic
 * tracer just add one cycle overhead to every kernel function when disabled.
 */
static unsigned long ftrace_gen_branch(unsigned long pc, unsigned long addr,
				       bool link)
{
	tilegx_bundle_bits opcode_x0, opcode_x1;
	long pcrel_by_instr = (addr - pc) >> TILEGX_LOG2_BUNDLE_SIZE_IN_BYTES;

	if (link) {
		/* opcode: jal addr */
		opcode_x1 =
			create_Opcode_X1(JUMP_OPCODE_X1) |
			create_JumpOpcodeExtension_X1(JAL_JUMP_OPCODE_X1) |
			create_JumpOff_X1(pcrel_by_instr);
	} else {
		/* opcode: j addr */
		opcode_x1 =
			create_Opcode_X1(JUMP_OPCODE_X1) |
			create_JumpOpcodeExtension_X1(J_JUMP_OPCODE_X1) |
			create_JumpOff_X1(pcrel_by_instr);
	}

	/*
	 * Also put { move r10, lr; jal ftrace_stub } in a bundle, which
	 * is used to replace the instruction in address ftrace_call.
	 */
	if (addr == FTRACE_ADDR || addr == (unsigned long)ftrace_stub) {
		/* opcode: or r10, lr, zero */
		opcode_x0 =
			create_Dest_X0(10) |
			create_SrcA_X0(TREG_LR) |
			create_SrcB_X0(TREG_ZERO) |
			create_RRROpcodeExtension_X0(OR_RRR_0_OPCODE_X0) |
			create_Opcode_X0(RRR_0_OPCODE_X0);
	} else {
		/* opcode: fnop */
		opcode_x0 =
			create_UnaryOpcodeExtension_X0(FNOP_UNARY_OPCODE_X0) |
			create_RRROpcodeExtension_X0(UNARY_RRR_0_OPCODE_X0) |
			create_Opcode_X0(RRR_0_OPCODE_X0);
	}

	return opcode_x1 | opcode_x0;
}

static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return NOP();
}

static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	return ftrace_gen_branch(pc, addr, true);
}

static int ftrace_modify_code(unsigned long pc, unsigned long old,
			      unsigned long new)
{
	unsigned long pc_wr;

	/* Check if the address is in kernel text space and module space. */
	if (!kernel_text_address(pc))
		return -EINVAL;

	/* Operate on writable kernel text mapping. */
	pc_wr = pc - MEM_SV_START + PAGE_OFFSET;

	if (probe_kernel_write((void *)pc_wr, &new, MCOUNT_INSN_SIZE))
		return -EPERM;

	smp_wmb();

	if (!machine_stopped && num_online_cpus() > 1)
		flush_icache_range(pc, pc + MCOUNT_INSN_SIZE);

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long pc, old;
	unsigned long new;
	int ret;

	pc = (unsigned long)&ftrace_call;
	memcpy(&old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(pc, (unsigned long)func);

	ret = ftrace_modify_code(pc, old, new);

	return ret;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace(rec);
	new = ftrace_call_replace(ip, addr);

	return ftrace_modify_code(rec->ip, old, new);
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned long old;
	unsigned long new;
	int ret;

	old = ftrace_call_replace(ip, addr);
	new = ftrace_nop_replace(rec);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long) &return_to_handler;
	struct ftrace_graph_ent trace;
	unsigned long old;
	int err;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;
	*parent = return_hooker;

	err = ftrace_push_return_trace(old, self_addr, &trace.depth,
				       frame_pointer);
	if (err == -EBUSY) {
		*parent = old;
		return;
	}

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		*parent = old;
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_graph_call;

static int __ftrace_modify_caller(unsigned long *callsite,
				  void (*func) (void), bool enable)
{
	unsigned long caller_fn = (unsigned long) func;
	unsigned long pc = (unsigned long) callsite;
	unsigned long branch = ftrace_gen_branch(pc, caller_fn, false);
	unsigned long nop = NOP();
	unsigned long old = enable ? nop : branch;
	unsigned long new = enable ? branch : nop;

	return ftrace_modify_code(pc, old, new);
}

static int ftrace_modify_graph_caller(bool enable)
{
	int ret;

	ret = __ftrace_modify_caller(&ftrace_graph_call,
				     ftrace_graph_caller,
				     enable);

	return ret;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(false);
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
