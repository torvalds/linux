/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 * Copyright (C) 2009 DSLab, Lanzhou University, China
 * Author: Wu Zhangjin <wuzj@lemote.com>
 *
 * Thanks goes to Steven Rostedt for writing the original x86 version.
 */

#include <linux/uaccess.h>
#include <linux/init.h>
#include <linux/ftrace.h>

#include <asm/cacheflush.h>

#ifdef CONFIG_DYNAMIC_FTRACE

#define JAL 0x0c000000		/* jump & link: ip --> ra, jump to target */
#define ADDR_MASK 0x03ffffff	/*  op_code|addr : 31...26|25 ....0 */
#define jump_insn_encode(op_code, addr) \
	((unsigned int)((op_code) | (((addr) >> 2) & ADDR_MASK)))

static unsigned int ftrace_nop = 0x00000000;

static int ftrace_modify_code(unsigned long ip, unsigned int new_code)
{
	*(unsigned int *)ip = new_code;

	flush_icache_range(ip, ip + 8);

	return 0;
}

static int lui_v1;
static int jal_mcount;

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int new;
	unsigned long ip = rec->ip;

	/* We have compiled module with -mlong-calls, but compiled the kernel
	 * without it, we need to cope with them respectively. */
	if (ip & 0x40000000) {
		/* record it for ftrace_make_call */
		if (lui_v1 == 0)
			lui_v1 = *(unsigned int *)ip;

		/* lui v1, hi_16bit_of_mcount        --> b 1f (0x10000004)
		 * addiu v1, v1, low_16bit_of_mcount
		 * move at, ra
		 * jalr v1
		 * nop
		 * 				     1f: (ip + 12)
		 */
		new = 0x10000004;
	} else {
		/* record/calculate it for ftrace_make_call */
		if (jal_mcount == 0) {
			/* We can record it directly like this:
			 *     jal_mcount = *(unsigned int *)ip;
			 * Herein, jump over the first two nop instructions */
			jal_mcount = jump_insn_encode(JAL, (MCOUNT_ADDR + 8));
		}

		/* move at, ra
		 * jalr v1		--> nop
		 */
		new = ftrace_nop;
	}
	return ftrace_modify_code(ip, new);
}

static int modified;	/* initialized as 0 by default */

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned int new;
	unsigned long ip = rec->ip;

	/* We just need to remove the "b ftrace_stub" at the fist time! */
	if (modified == 0) {
		modified = 1;
		ftrace_modify_code(addr, ftrace_nop);
	}
	/* ip, module: 0xc0000000, kernel: 0x80000000 */
	new = (ip & 0x40000000) ? lui_v1 : jal_mcount;

	return ftrace_modify_code(ip, new);
}

#define FTRACE_CALL_IP ((unsigned long)(&ftrace_call))

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned int new;

	new = jump_insn_encode(JAL, (unsigned long)func);

	return ftrace_modify_code(FTRACE_CALL_IP, new);
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* The return code is retured via data */
	*(unsigned long *)data = 0;

	return 0;
}
#endif				/* CONFIG_DYNAMIC_FTRACE */
