// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <linux/stop_machine.h>
#include <asm/cacheflush.h>

#ifdef CONFIG_DYNAMIC_FTRACE

#define NOP		0x4000
#define NOP32_HI	0xc400
#define NOP32_LO	0x4820
#define PUSH_LR		0x14d0
#define MOVIH_LINK	0xea3a
#define ORI_LINK	0xef5a
#define JSR_LINK	0xe8fa
#define BSR_LINK	0xe000

/*
 * Gcc-csky with -pg will insert stub in function prologue:
 *	push	lr
 *	jbsr	_mcount
 *	nop32
 *	nop32
 *
 * If the (callee - current_pc) is less then 64MB, we'll use bsr:
 *	push	lr
 *	bsr	_mcount
 *	nop32
 *	nop32
 * else we'll use (movih + ori + jsr):
 *	push	lr
 *	movih	r26, ...
 *	ori	r26, ...
 *	jsr	r26
 *
 * (r26 is our reserved link-reg)
 *
 */
static inline void make_jbsr(unsigned long callee, unsigned long pc,
			     uint16_t *call, bool nolr)
{
	long offset;

	call[0]	= nolr ? NOP : PUSH_LR;

	offset = (long) callee - (long) pc;

	if (unlikely(offset < -67108864 || offset > 67108864)) {
		call[1] = MOVIH_LINK;
		call[2] = callee >> 16;
		call[3] = ORI_LINK;
		call[4] = callee & 0xffff;
		call[5] = JSR_LINK;
		call[6] = 0;
	} else {
		offset = offset >> 1;

		call[1] = BSR_LINK |
			 ((uint16_t)((unsigned long) offset >> 16) & 0x3ff);
		call[2] = (uint16_t)((unsigned long) offset & 0xffff);
		call[3] = call[5] = NOP32_HI;
		call[4] = call[6] = NOP32_LO;
	}
}

static uint16_t nops[7] = {NOP, NOP32_HI, NOP32_LO, NOP32_HI, NOP32_LO,
				NOP32_HI, NOP32_LO};
static int ftrace_check_current_nop(unsigned long hook)
{
	uint16_t olds[7];
	unsigned long hook_pos = hook - 2;

	if (probe_kernel_read((void *)olds, (void *)hook_pos, sizeof(nops)))
		return -EFAULT;

	if (memcmp((void *)nops, (void *)olds, sizeof(nops))) {
		pr_err("%p: nop but get (%04x %04x %04x %04x %04x %04x %04x)\n",
			(void *)hook_pos,
			olds[0], olds[1], olds[2], olds[3], olds[4], olds[5],
			olds[6]);

		return -EINVAL;
	}

	return 0;
}

static int ftrace_modify_code(unsigned long hook, unsigned long target,
			      bool enable, bool nolr)
{
	uint16_t call[7];

	unsigned long hook_pos = hook - 2;
	int ret = 0;

	make_jbsr(target, hook, call, nolr);

	ret = probe_kernel_write((void *)hook_pos, enable ? call : nops,
				 sizeof(nops));
	if (ret)
		return -EPERM;

	flush_icache_range(hook_pos, hook_pos + MCOUNT_INSN_SIZE);

	return 0;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	int ret = ftrace_check_current_nop(rec->ip);

	if (ret)
		return ret;

	return ftrace_modify_code(rec->ip, addr, true, false);
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	return ftrace_modify_code(rec->ip, addr, false, false);
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	int ret = ftrace_modify_code((unsigned long)&ftrace_call,
				(unsigned long)func, true, true);
	if (!ret)
		ret = ftrace_modify_code((unsigned long)&ftrace_regs_call,
				(unsigned long)func, true, true);
	return ret;
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_DYNAMIC_FTRACE_WITH_REGS
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
		       unsigned long addr)
{
	return ftrace_modify_code(rec->ip, addr, true, true);
}
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter(old, self_addr,
			*(unsigned long *)frame_pointer, parent)) {
		/*
		 * For csky-gcc function has sub-call:
		 * subi	sp,	sp, 8
		 * stw	r8,	(sp, 0)
		 * mov	r8,	sp
		 * st.w r15,	(sp, 0x4)
		 * push	r15
		 * jl	_mcount
		 * We only need set *parent for resume
		 *
		 * For csky-gcc function has no sub-call:
		 * subi	sp,	sp, 4
		 * stw	r8,	(sp, 0)
		 * mov	r8,	sp
		 * push	r15
		 * jl	_mcount
		 * We need set *parent and *(frame_pointer + 4) for resume,
		 * because lr is resumed twice.
		 */
		*parent = return_hooker;
		frame_pointer += 4;
		if (*(unsigned long *)frame_pointer == old)
			*(unsigned long *)frame_pointer = return_hooker;
	}
}

#ifdef CONFIG_DYNAMIC_FTRACE
int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_code((unsigned long)&ftrace_graph_call,
			(unsigned long)&ftrace_graph_caller, true, true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_code((unsigned long)&ftrace_graph_call,
			(unsigned long)&ftrace_graph_caller, false, true);
}
#endif /* CONFIG_DYNAMIC_FTRACE */
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifndef CONFIG_CPU_HAS_ICACHE_INS
struct ftrace_modify_param {
	int command;
	atomic_t cpu_count;
};

static int __ftrace_modify_code(void *data)
{
	struct ftrace_modify_param *param = data;

	if (atomic_inc_return(&param->cpu_count) == 1) {
		ftrace_modify_all_code(param->command);
		atomic_inc(&param->cpu_count);
	} else {
		while (atomic_read(&param->cpu_count) <= num_online_cpus())
			cpu_relax();
		local_icache_inv_all(NULL);
	}

	return 0;
}

void arch_ftrace_update_code(int command)
{
	struct ftrace_modify_param param = { command, ATOMIC_INIT(0) };

	stop_machine(__ftrace_modify_code, &param, cpu_online_mask);
}
#endif

/* _mcount is defined in abi's mcount.S */
EXPORT_SYMBOL(_mcount);
