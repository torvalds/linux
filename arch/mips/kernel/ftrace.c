// SPDX-License-Identifier: GPL-2.0
/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2009, 2010 DSLab, Lanzhou University, China
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 *
 * Thanks goes to Steven Rostedt for writing the original x86 version.
 */

#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/ftrace.h>
#include <linux/syscalls.h>

#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>
#include <asm/uasm.h>
#include <asm/unistd.h>

#include <asm-generic/sections.h>

#if defined(KBUILD_MCOUNT_RA_ADDRESS) && defined(CONFIG_32BIT)
#define MCOUNT_OFFSET_INSNS 5
#else
#define MCOUNT_OFFSET_INSNS 4
#endif

#ifdef CONFIG_DYNAMIC_FTRACE

/* Arch override because MIPS doesn't need to run this from stop_machine() */
void arch_ftrace_update_code(int command)
{
	ftrace_modify_all_code(command);
}

#define JAL 0x0c000000		/* jump & link: ip --> ra, jump to target */
#define ADDR_MASK 0x03ffffff	/*  op_code|addr : 31...26|25 ....0 */
#define JUMP_RANGE_MASK ((1UL << 28) - 1)

#define INSN_NOP 0x00000000	/* nop */
#define INSN_JAL(addr)	\
	((unsigned int)(JAL | (((addr) >> 2) & ADDR_MASK)))

static unsigned int insn_jal_ftrace_caller __read_mostly;
static unsigned int insn_la_mcount[2] __read_mostly;
static unsigned int insn_j_ftrace_graph_caller __maybe_unused __read_mostly;

static inline void ftrace_dyn_arch_init_insns(void)
{
	u32 *buf;
	unsigned int v1;

	/* la v1, _mcount */
	v1 = 3;
	buf = (u32 *)&insn_la_mcount[0];
	UASM_i_LA(&buf, v1, MCOUNT_ADDR);

	/* jal (ftrace_caller + 8), jump over the first two instruction */
	buf = (u32 *)&insn_jal_ftrace_caller;
	uasm_i_jal(&buf, (FTRACE_ADDR + 8) & JUMP_RANGE_MASK);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* j ftrace_graph_caller */
	buf = (u32 *)&insn_j_ftrace_graph_caller;
	uasm_i_j(&buf, (unsigned long)ftrace_graph_caller & JUMP_RANGE_MASK);
#endif
}

static int ftrace_modify_code(unsigned long ip, unsigned int new_code)
{
	int faulted;

	/* *(unsigned int *)ip = new_code; */
	safe_store_code(new_code, ip, faulted);

	if (unlikely(faulted))
		return -EFAULT;

	flush_icache_range(ip, ip + 8);

	return 0;
}

#ifndef CONFIG_64BIT
static int ftrace_modify_code_2(unsigned long ip, unsigned int new_code1,
				unsigned int new_code2)
{
	int faulted;

	safe_store_code(new_code1, ip, faulted);
	if (unlikely(faulted))
		return -EFAULT;

	ip += 4;
	safe_store_code(new_code2, ip, faulted);
	if (unlikely(faulted))
		return -EFAULT;

	ip -= 4;
	flush_icache_range(ip, ip + 8);

	return 0;
}

static int ftrace_modify_code_2r(unsigned long ip, unsigned int new_code1,
				 unsigned int new_code2)
{
	int faulted;

	ip += 4;
	safe_store_code(new_code2, ip, faulted);
	if (unlikely(faulted))
		return -EFAULT;

	ip -= 4;
	safe_store_code(new_code1, ip, faulted);
	if (unlikely(faulted))
		return -EFAULT;

	flush_icache_range(ip, ip + 8);

	return 0;
}
#endif

/*
 * The details about the calling site of mcount on MIPS
 *
 * 1. For kernel:
 *
 * move at, ra
 * jal _mcount		--> nop
 *  sub sp, sp, 8	--> nop  (CONFIG_32BIT)
 *
 * 2. For modules:
 *
 * 2.1 For KBUILD_MCOUNT_RA_ADDRESS and CONFIG_32BIT
 *
 * lui v1, hi_16bit_of_mcount	     --> b 1f (0x10000005)
 * addiu v1, v1, low_16bit_of_mcount --> nop  (CONFIG_32BIT)
 * move at, ra
 * move $12, ra_address
 * jalr v1
 *  sub sp, sp, 8
 *				    1: offset = 5 instructions
 * 2.2 For the Other situations
 *
 * lui v1, hi_16bit_of_mcount	     --> b 1f (0x10000004)
 * addiu v1, v1, low_16bit_of_mcount --> nop  (CONFIG_32BIT)
 * move at, ra
 * jalr v1
 *  nop | move $12, ra_address | sub sp, sp, 8
 *				    1: offset = 4 instructions
 */

#define INSN_B_1F (0x10000000 | MCOUNT_OFFSET_INSNS)

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int new;
	unsigned long ip = rec->ip;

	/*
	 * If ip is in kernel space, no long call, otherwise, long call is
	 * needed.
	 */
	new = core_kernel_text(ip) ? INSN_NOP : INSN_B_1F;
#ifdef CONFIG_64BIT
	return ftrace_modify_code(ip, new);
#else
	/*
	 * On 32 bit MIPS platforms, gcc adds a stack adjust
	 * instruction in the delay slot after the branch to
	 * mcount and expects mcount to restore the sp on return.
	 * This is based on a legacy API and does nothing but
	 * waste instructions so it's being removed at runtime.
	 */
	return ftrace_modify_code_2(ip, new, INSN_NOP);
#endif
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int new;
	unsigned long ip = rec->ip;

	new = core_kernel_text(ip) ? insn_jal_ftrace_caller : insn_la_mcount[0];

#ifdef CONFIG_64BIT
	return ftrace_modify_code(ip, new);
#else
	return ftrace_modify_code_2r(ip, new, core_kernel_text(ip) ?
						INSN_NOP : insn_la_mcount[1]);
#endif
}

#define FTRACE_CALL_IP ((unsigned long)(&ftrace_call))

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned int new;

	new = INSN_JAL((unsigned long)func);

	return ftrace_modify_code(FTRACE_CALL_IP, new);
}

int __init ftrace_dyn_arch_init(void)
{
	/* Encode the instructions when booting */
	ftrace_dyn_arch_init_insns();

	/* Remove "b ftrace_stub" to ensure ftrace_caller() is executed */
	ftrace_modify_code(MCOUNT_ADDR, INSN_NOP);

	return 0;
}
#endif	/* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE

extern void ftrace_graph_call(void);
#define FTRACE_GRAPH_CALL_IP	((unsigned long)(&ftrace_graph_call))

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_code(FTRACE_GRAPH_CALL_IP,
			insn_j_ftrace_graph_caller);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_code(FTRACE_GRAPH_CALL_IP, INSN_NOP);
}

#endif	/* CONFIG_DYNAMIC_FTRACE */

#ifndef KBUILD_MCOUNT_RA_ADDRESS

#define S_RA_SP (0xafbf << 16)	/* s{d,w} ra, offset(sp) */
#define S_R_SP	(0xafb0 << 16)	/* s{d,w} R, offset(sp) */
#define OFFSET_MASK	0xffff	/* stack offset range: 0 ~ PT_SIZE */

unsigned long ftrace_get_parent_ra_addr(unsigned long self_ra, unsigned long
		old_parent_ra, unsigned long parent_ra_addr, unsigned long fp)
{
	unsigned long sp, ip, tmp;
	unsigned int code;
	int faulted;

	/*
	 * For module, move the ip from the return address after the
	 * instruction "lui v1, hi_16bit_of_mcount"(offset is 24), but for
	 * kernel, move after the instruction "move ra, at"(offset is 16)
	 */
	ip = self_ra - (core_kernel_text(self_ra) ? 16 : 24);

	/*
	 * search the text until finding the non-store instruction or "s{d,w}
	 * ra, offset(sp)" instruction
	 */
	do {
		/* get the code at "ip": code = *(unsigned int *)ip; */
		safe_load_code(code, ip, faulted);

		if (unlikely(faulted))
			return 0;
		/*
		 * If we hit the non-store instruction before finding where the
		 * ra is stored, then this is a leaf function and it does not
		 * store the ra on the stack
		 */
		if ((code & S_R_SP) != S_R_SP)
			return parent_ra_addr;

		/* Move to the next instruction */
		ip -= 4;
	} while ((code & S_RA_SP) != S_RA_SP);

	sp = fp + (code & OFFSET_MASK);

	/* tmp = *(unsigned long *)sp; */
	safe_load_stack(tmp, sp, faulted);
	if (unlikely(faulted))
		return 0;

	if (tmp == old_parent_ra)
		return sp;
	return 0;
}

#endif	/* !KBUILD_MCOUNT_RA_ADDRESS */

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent_ra_addr, unsigned long self_ra,
			   unsigned long fp)
{
	unsigned long old_parent_ra;
	unsigned long return_hooker = (unsigned long)
	    &return_to_handler;
	int faulted, insns;

	if (unlikely(ftrace_graph_is_dead()))
		return;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * "parent_ra_addr" is the stack address where the return address of
	 * the caller of _mcount is saved.
	 *
	 * If gcc < 4.5, a leaf function does not save the return address
	 * in the stack address, so we "emulate" one in _mcount's stack space,
	 * and hijack it directly.
	 * For a non-leaf function, it does save the return address to its own
	 * stack space, so we can not hijack it directly, but need to find the
	 * real stack address, which is done by ftrace_get_parent_addr().
	 *
	 * If gcc >= 4.5, with the new -mmcount-ra-address option, for a
	 * non-leaf function, the location of the return address will be saved
	 * to $12 for us.
	 * For a leaf function, it just puts a zero into $12, so we handle
	 * it in ftrace_graph_caller() of mcount.S.
	 */

	/* old_parent_ra = *parent_ra_addr; */
	safe_load_stack(old_parent_ra, parent_ra_addr, faulted);
	if (unlikely(faulted))
		goto out;
#ifndef KBUILD_MCOUNT_RA_ADDRESS
	parent_ra_addr = (unsigned long *)ftrace_get_parent_ra_addr(self_ra,
			old_parent_ra, (unsigned long)parent_ra_addr, fp);
	/*
	 * If fails when getting the stack address of the non-leaf function's
	 * ra, stop function graph tracer and return
	 */
	if (parent_ra_addr == NULL)
		goto out;
#endif
	/* *parent_ra_addr = return_hooker; */
	safe_store_stack(return_hooker, parent_ra_addr, faulted);
	if (unlikely(faulted))
		goto out;

	/*
	 * Get the recorded ip of the current mcount calling site in the
	 * __mcount_loc section, which will be used to filter the function
	 * entries configured through the tracing/set_graph_function interface.
	 */

	insns = core_kernel_text(self_ra) ? 2 : MCOUNT_OFFSET_INSNS + 1;
	self_ra -= (MCOUNT_INSN_SIZE * insns);

	if (function_graph_enter(old_parent_ra, self_ra, fp, NULL))
		*parent_ra_addr = old_parent_ra;
	return;
out:
	ftrace_graph_stop();
	WARN_ON(1);
}
#endif	/* CONFIG_FUNCTION_GRAPH_TRACER */

#ifdef CONFIG_FTRACE_SYSCALLS

#ifdef CONFIG_32BIT
unsigned long __init arch_syscall_addr(int nr)
{
	return (unsigned long)sys_call_table[nr - __NR_O32_Linux];
}
#endif

#ifdef CONFIG_64BIT

unsigned long __init arch_syscall_addr(int nr)
{
#ifdef CONFIG_MIPS32_N32
	if (nr >= __NR_N32_Linux && nr < __NR_N32_Linux + __NR_N32_Linux_syscalls)
		return (unsigned long)sysn32_call_table[nr - __NR_N32_Linux];
#endif
	if (nr >= __NR_64_Linux  && nr < __NR_64_Linux + __NR_64_Linux_syscalls)
		return (unsigned long)sys_call_table[nr - __NR_64_Linux];
#ifdef CONFIG_MIPS32_O32
	if (nr >= __NR_O32_Linux && nr < __NR_O32_Linux + __NR_O32_Linux_syscalls)
		return (unsigned long)sys32_call_table[nr - __NR_O32_Linux];
#endif

	return (unsigned long) &sys_ni_syscall;
}
#endif

#endif /* CONFIG_FTRACE_SYSCALLS */
