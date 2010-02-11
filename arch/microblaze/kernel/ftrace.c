/*
 * Ftrace support for Microblaze.
 *
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2009 PetaLogix
 *
 * Based on MIPS and PowerPC ftrace code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <asm/cacheflush.h>
#include <linux/ftrace.h>

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	unsigned long old;
	int faulted, err;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)
				&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	asm volatile("	1:	lwi	%0, %2, 0;		\
			2:	swi	%3, %2, 0;		\
				addik	%1, r0, 0;		\
			3:					\
				.section .fixup, \"ax\";	\
			4:	brid	3b;			\
				addik	%1, r0, 1;		\
				.previous;			\
				.section __ex_table,\"a\";	\
				.word	1b,4b;			\
				.word	2b,4b;			\
				.previous;"			\
			: "=&r" (old), "=r" (faulted)
			: "r" (parent), "r" (return_hooker)
	);

	if (unlikely(faulted)) {
		ftrace_graph_stop();
		WARN_ON(1);
		return;
	}

	err = ftrace_push_return_trace(old, self_addr, &trace.depth, 0);
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
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_DYNAMIC_FTRACE
/* save value to addr - it is save to do it in asm */
static int ftrace_modify_code(unsigned long addr, unsigned int value)
{
	int faulted = 0;

	__asm__ __volatile__("	1:	swi	%2, %1, 0;		\
					addik	%0, r0, 0;		\
				2:					\
					.section .fixup, \"ax\";	\
				3:	brid	2b;			\
					addik	%0, r0, 1;		\
					.previous;			\
					.section __ex_table,\"a\";	\
					.word	1b,3b;			\
					.previous;"			\
				: "=r" (faulted)
				: "r" (addr), "r" (value)
	);

	if (unlikely(faulted))
		return -EFAULT;

	return 0;
}

#define MICROBLAZE_NOP 0x80000000
#define MICROBLAZE_BRI 0xb800000C

static unsigned int recorded; /* if save was or not */
static unsigned int imm; /* saving whole imm instruction */

/* There are two approaches howto solve ftrace_make nop function - look below */
#undef USE_FTRACE_NOP

#ifdef USE_FTRACE_NOP
static unsigned int bralid; /* saving whole bralid instruction */
#endif

int ftrace_make_nop(struct module *mod,
			struct dyn_ftrace *rec, unsigned long addr)
{
	/* we have this part of code which we are working with
	 * b000c000        imm     -16384
	 * b9fc8e30        bralid  r15, -29136     // c0008e30 <_mcount>
	 * 80000000        or      r0, r0, r0
	 *
	 * The first solution (!USE_FTRACE_NOP-could be called branch solution)
	 * b000c000        bri	12 (0xC - jump to any other instruction)
	 * b9fc8e30        bralid  r15, -29136     // c0008e30 <_mcount>
	 * 80000000        or      r0, r0, r0
	 * any other instruction
	 *
	 * The second solution (USE_FTRACE_NOP) - no jump just nops
	 * 80000000        or      r0, r0, r0
	 * 80000000        or      r0, r0, r0
	 * 80000000        or      r0, r0, r0
	 */
	int ret = 0;

	if (recorded == 0) {
		recorded = 1;
		imm = *(unsigned int *)rec->ip;
		pr_debug("%s: imm:0x%x\n", __func__, imm);
#ifdef USE_FTRACE_NOP
		bralid = *(unsigned int *)(rec->ip + 4);
		pr_debug("%s: bralid 0x%x\n", __func__, bralid);
#endif /* USE_FTRACE_NOP */
	}

#ifdef USE_FTRACE_NOP
	ret = ftrace_modify_code(rec->ip, MICROBLAZE_NOP);
	ret += ftrace_modify_code(rec->ip + 4, MICROBLAZE_NOP);
#else /* USE_FTRACE_NOP */
	ret = ftrace_modify_code(rec->ip, MICROBLAZE_BRI);
#endif /* USE_FTRACE_NOP */
	return ret;
}

static int ret_addr; /* initialized as 0 by default */

/* I believe that first is called ftrace_make_nop before this function */
int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	int ret;
	ret_addr = addr; /* saving where the barrier jump is */
	pr_debug("%s: addr:0x%x, rec->ip: 0x%x, imm:0x%x\n",
		__func__, (unsigned int)addr, (unsigned int)rec->ip, imm);
	ret = ftrace_modify_code(rec->ip, imm);
#ifdef USE_FTRACE_NOP
	pr_debug("%s: bralid:0x%x\n", __func__, bralid);
	ret += ftrace_modify_code(rec->ip + 4, bralid);
#endif /* USE_FTRACE_NOP */
	return ret;
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* The return code is retured via data */
	*(unsigned long *)data = 0;

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned int upper = (unsigned int)func;
	unsigned int lower = (unsigned int)func;
	int ret = 0;

	/* create proper saving to ftrace_call poll */
	upper = 0xb0000000 + (upper >> 16); /* imm func_upper */
	lower = 0x32800000 + (lower & 0xFFFF); /* addik r20, r0, func_lower */

	pr_debug("%s: func=0x%x, ip=0x%x, upper=0x%x, lower=0x%x\n",
		__func__, (unsigned int)func, (unsigned int)ip, upper, lower);

	/* save upper and lower code */
	ret = ftrace_modify_code(ip, upper);
	ret += ftrace_modify_code(ip + 4, lower);

	/* We just need to remove the rtsd r15, 8 by NOP */
	BUG_ON(!ret_addr);
	if (ret_addr)
		ret += ftrace_modify_code(ret_addr, MICROBLAZE_NOP);
	else
		ret = 1; /* fault */

	/* All changes are done - lets do caches consistent */
	flush_icache();
	return ret;
}

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
unsigned int old_jump; /* saving place for jump instruction */

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned int ret;
	unsigned long ip = (unsigned long)(&ftrace_call_graph);

	old_jump = *(unsigned int *)ip; /* save jump over instruction */
	ret = ftrace_modify_code(ip, MICROBLAZE_NOP);
	flush_icache();

	pr_debug("%s: Replace instruction: 0x%x\n", __func__, old_jump);
	return ret;
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned int ret;
	unsigned long ip = (unsigned long)(&ftrace_call_graph);

	ret = ftrace_modify_code(ip, old_jump);
	flush_icache();

	pr_debug("%s\n", __func__);
	return ret;
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
#endif /* CONFIG_DYNAMIC_FTRACE */
