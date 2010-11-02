/*
 * Dynamic function tracing support.
 *
 * Copyright (C) 2008 Abhishek Sagar <sagar.abhishek@gmail.com>
 * Copyright (C) 2010 Rabin Vincent <rabin@rab.in>
 *
 * For licencing details, see COPYING.
 *
 * Defines low-level handling of mcount calls when the kernel
 * is compiled with the -pg flag. When using dynamic ftrace, the
 * mcount call-sites get patched with NOP till they are enabled.
 * All code mutation routines here are called under stop_machine().
 */

#include <linux/ftrace.h>
#include <linux/uaccess.h>

#include <asm/cacheflush.h>
#include <asm/ftrace.h>

#ifdef CONFIG_THUMB2_KERNEL
#define	NOP		0xeb04f85d	/* pop.w {lr} */
#else
#define	NOP		0xe8bd4000	/* pop {lr} */
#endif

#ifdef CONFIG_OLD_MCOUNT
#define OLD_MCOUNT_ADDR	((unsigned long) mcount)
#define OLD_FTRACE_ADDR ((unsigned long) ftrace_caller_old)

#define	OLD_NOP		0xe1a00000	/* mov r0, r0 */

static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return rec->arch.old_mcount ? OLD_NOP : NOP;
}

static unsigned long adjust_address(struct dyn_ftrace *rec, unsigned long addr)
{
	if (!rec->arch.old_mcount)
		return addr;

	if (addr == MCOUNT_ADDR)
		addr = OLD_MCOUNT_ADDR;
	else if (addr == FTRACE_ADDR)
		addr = OLD_FTRACE_ADDR;

	return addr;
}
#else
static unsigned long ftrace_nop_replace(struct dyn_ftrace *rec)
{
	return NOP;
}

static unsigned long adjust_address(struct dyn_ftrace *rec, unsigned long addr)
{
	return addr;
}
#endif

/* construct a branch (BL) instruction to addr */
#ifdef CONFIG_THUMB2_KERNEL
static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	unsigned long s, j1, j2, i1, i2, imm10, imm11;
	unsigned long first, second;
	long offset;

	offset = (long)addr - (long)(pc + 4);
	if (offset < -16777216 || offset > 16777214) {
		WARN_ON_ONCE(1);
		return 0;
	}

	s	= (offset >> 24) & 0x1;
	i1	= (offset >> 23) & 0x1;
	i2	= (offset >> 22) & 0x1;
	imm10	= (offset >> 12) & 0x3ff;
	imm11	= (offset >>  1) & 0x7ff;

	j1 = (!i1) ^ s;
	j2 = (!i2) ^ s;

	first = 0xf000 | (s << 10) | imm10;
	second = 0xd000 | (j1 << 13) | (j2 << 11) | imm11;

	return (second << 16) | first;
}
#else
static unsigned long ftrace_call_replace(unsigned long pc, unsigned long addr)
{
	long offset;

	offset = (long)addr - (long)(pc + 8);
	if (unlikely(offset < -33554432 || offset > 33554428)) {
		/* Can't generate branches that far (from ARM ARM). Ftrace
		 * doesn't generate branches outside of kernel text.
		 */
		WARN_ON_ONCE(1);
		return 0;
	}

	offset = (offset >> 2) & 0x00ffffff;

	return 0xeb000000 | offset;
}
#endif

static int ftrace_modify_code(unsigned long pc, unsigned long old,
			      unsigned long new)
{
	unsigned long replaced;

	if (probe_kernel_read(&replaced, (void *)pc, MCOUNT_INSN_SIZE))
		return -EFAULT;

	if (replaced != old)
		return -EINVAL;

	if (probe_kernel_write((void *)pc, &new, MCOUNT_INSN_SIZE))
		return -EPERM;

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

#ifdef CONFIG_OLD_MCOUNT
	if (!ret) {
		pc = (unsigned long)&ftrace_call_old;
		memcpy(&old, &ftrace_call_old, MCOUNT_INSN_SIZE);
		new = ftrace_call_replace(pc, (unsigned long)func);

		ret = ftrace_modify_code(pc, old, new);
	}
#endif

	return ret;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long new, old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace(rec);
	new = ftrace_call_replace(ip, adjust_address(rec, addr));

	return ftrace_modify_code(rec->ip, old, new);
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned long old;
	unsigned long new;
	int ret;

	old = ftrace_call_replace(ip, adjust_address(rec, addr));
	new = ftrace_nop_replace(rec);
	ret = ftrace_modify_code(ip, old, new);

#ifdef CONFIG_OLD_MCOUNT
	if (ret == -EINVAL && addr == MCOUNT_ADDR) {
		rec->arch.old_mcount = true;

		old = ftrace_call_replace(ip, adjust_address(rec, addr));
		new = ftrace_nop_replace(rec);
		ret = ftrace_modify_code(ip, old, new);
	}
#endif

	return ret;
}

int __init ftrace_dyn_arch_init(void *data)
{
	*(unsigned long *)data = 0;

	return 0;
}
