/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes to Ingo Molnar, for suggesting the idea.
 * Mathieu Desnoyers, for suggesting postponing the modifications.
 * Arjan van de Ven, for keeping me straight, and explaining to me
 * the dangers of modifying code on the run.
 */

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/alternative.h>
#include <asm/ftrace.h>


/* Long is fine, even if it is only 4 bytes ;-) */
static long *ftrace_nop;

union ftrace_code_union {
	char code[MCOUNT_INSN_SIZE];
	struct {
		char e8;
		int offset;
	} __attribute__((packed));
};


static int notrace ftrace_calc_offset(long ip, long addr)
{
	return (int)(addr - ip);
}

notrace unsigned char *ftrace_nop_replace(void)
{
	return (char *)ftrace_nop;
}

notrace unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	static union ftrace_code_union calc;

	calc.e8		= 0xe8;
	calc.offset	= ftrace_calc_offset(ip + MCOUNT_INSN_SIZE, addr);

	/*
	 * No locking needed, this must be called via kstop_machine
	 * which in essence is like running on a uniprocessor machine.
	 */
	return calc.code;
}

notrace int
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		   unsigned char *new_code)
{
	unsigned replaced;
	unsigned old = *(unsigned *)old_code; /* 4 bytes */
	unsigned new = *(unsigned *)new_code; /* 4 bytes */
	unsigned char newch = new_code[4];
	int faulted = 0;

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine.
	 */
	asm volatile (
		"1: lock\n"
		"   cmpxchg %3, (%2)\n"
		"   jnz 2f\n"
		"   movb %b4, 4(%2)\n"
		"2:\n"
		".section .fixup, \"ax\"\n"
		"3:	movl $1, %0\n"
		"	jmp 2b\n"
		".previous\n"
		_ASM_EXTABLE(1b, 3b)
		: "=r"(faulted), "=a"(replaced)
		: "r"(ip), "r"(new), "c"(newch),
		  "0"(faulted), "a"(old)
		: "memory");
	sync_core();

	if (replaced != old && replaced != new)
		faulted = 2;

	return faulted;
}

notrace int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char old[MCOUNT_INSN_SIZE], *new;
	int ret;

	memcpy(old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

notrace int ftrace_mcount_set(unsigned long *data)
{
	unsigned long ip = (long)(&mcount_call);
	unsigned long *addr = data;
	unsigned char old[MCOUNT_INSN_SIZE], *new;

	/*
	 * Replace the mcount stub with a pointer to the
	 * ip recorder function.
	 */
	memcpy(old, &mcount_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, *addr);
	*addr = ftrace_modify_code(ip, old, new);

	return 0;
}

int __init ftrace_dyn_arch_init(void *data)
{
	const unsigned char *const *noptable = find_nop_table();

	/* This is running in kstop_machine */

	ftrace_mcount_set(data);

	ftrace_nop = (unsigned long *)noptable[MCOUNT_INSN_SIZE];

	return 0;
}
