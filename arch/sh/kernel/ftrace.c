/*
 * Copyright (C) 2008 Matt Fleming <matt@console-pimps.org>
 * Copyright (C) 2008 Paul Mundt <lethal@linux-sh.org>
 *
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes to Ingo Molnar, for suggesting the idea.
 * Mathieu Desnoyers, for suggesting postponing the modifications.
 * Arjan van de Ven, for keeping me straight, and explaining to me
 * the dangers of modifying code on the run.
 */
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/io.h>
#include <asm/ftrace.h>
#include <asm/cacheflush.h>

static unsigned char ftrace_replaced_code[MCOUNT_INSN_SIZE];

static unsigned char ftrace_nop[4];
/*
 * If we're trying to nop out a call to a function, we instead
 * place a call to the address after the memory table.
 *
 * 8c011060 <a>:
 * 8c011060:       02 d1           mov.l   8c01106c <a+0xc>,r1
 * 8c011062:       22 4f           sts.l   pr,@-r15
 * 8c011064:       02 c7           mova    8c011070 <a+0x10>,r0
 * 8c011066:       2b 41           jmp     @r1
 * 8c011068:       2a 40           lds     r0,pr
 * 8c01106a:       09 00           nop
 * 8c01106c:       68 24           .word 0x2468     <--- ip
 * 8c01106e:       1d 8c           .word 0x8c1d
 * 8c011070:       26 4f           lds.l   @r15+,pr <--- ip + MCOUNT_INSN_SIZE
 *
 * We write 0x8c011070 to 0x8c01106c so that on entry to a() we branch
 * past the _mcount call and continue executing code like normal.
 */
static unsigned char *ftrace_nop_replace(unsigned long ip)
{
	__raw_writel(ip + MCOUNT_INSN_SIZE, ftrace_nop);
	return ftrace_nop;
}

static unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	/* Place the address in the memory table. */
	__raw_writel(addr, ftrace_replaced_code);

	/*
	 * No locking needed, this must be called via kstop_machine
	 * which in essence is like running on a uniprocessor machine.
	 */
	return ftrace_replaced_code;
}

static int ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		       unsigned char *new_code)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing. We do this by using the
	 *  probe_kernel_* functions.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine, or before SMP starts.
	 */

	/* read the text we want to modify */
	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (memcmp(replaced, old_code, MCOUNT_INSN_SIZE) != 0)
		return -EINVAL;

	/* replace the text with the new text */
	if (probe_kernel_write((void *)ip, new_code, MCOUNT_INSN_SIZE))
		return -EPERM;

	flush_icache_range(ip, ip + MCOUNT_INSN_SIZE);

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call) + MCOUNT_INSN_OFFSET;
	unsigned char old[MCOUNT_INSN_SIZE], *new;

	memcpy(old, (unsigned char *)ip, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, (unsigned long)func);

	return ftrace_modify_code(ip, old, new);
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, addr);
	new = ftrace_nop_replace(ip);

	return ftrace_modify_code(rec->ip, old, new);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace(ip);
	new = ftrace_call_replace(ip, addr);

	return ftrace_modify_code(rec->ip, old, new);
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* The return code is retured via data */
	__raw_writel(0, (unsigned long)data);

	return 0;
}
