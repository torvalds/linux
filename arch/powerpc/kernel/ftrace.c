/*
 * Code for replacing ftrace calls with jumps.
 *
 * Copyright (C) 2007-2008 Steven Rostedt <srostedt@redhat.com>
 *
 * Thanks goes out to P.A. Semi, Inc for supplying me with a PPC64 box.
 *
 */

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/cacheflush.h>

#define CALL_BACK		4

static unsigned int ftrace_nop = 0x60000000;

#ifdef CONFIG_PPC32
# define GET_ADDR(addr) addr
#else
/* PowerPC64's functions are data that points to the functions */
# define GET_ADDR(addr) *(unsigned long *)addr
#endif

notrace int ftrace_ip_converted(unsigned long ip)
{
	unsigned int save;

	ip -= CALL_BACK;
	save = *(unsigned int *)ip;

	return save == ftrace_nop;
}

static unsigned int notrace ftrace_calc_offset(long ip, long addr)
{
	return (int)((addr + CALL_BACK) - ip);
}

notrace unsigned char *ftrace_nop_replace(void)
{
	return (char *)&ftrace_nop;
}

notrace unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	static unsigned int op;

	addr = GET_ADDR(addr);

	/* Set to "bl addr" */
	op = 0x48000001 | (ftrace_calc_offset(ip, addr) & 0x03fffffe);

	/*
	 * No locking needed, this must be called via kstop_machine
	 * which in essence is like running on a uniprocessor machine.
	 */
	return (unsigned char *)&op;
}

#ifdef CONFIG_PPC64
# define _ASM_ALIGN	" .align 3 "
# define _ASM_PTR	" .llong "
#else
# define _ASM_ALIGN	" .align 2 "
# define _ASM_PTR	" .long "
#endif

notrace int
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		   unsigned char *new_code)
{
	unsigned replaced;
	unsigned old = *(unsigned *)old_code;
	unsigned new = *(unsigned *)new_code;
	int faulted = 0;

	/* move the IP back to the start of the call */
	ip -= CALL_BACK;

	/*
	 * Note: Due to modules and __init, code can
	 *  disappear and change, we need to protect against faulting
	 *  as well as code changing.
	 *
	 * No real locking needed, this code is run through
	 * kstop_machine.
	 */
	asm volatile (
		"1: lwz		%1, 0(%2)\n"
		"   cmpw	%1, %5\n"
		"   bne		2f\n"
		"   stwu	%3, 0(%2)\n"
		"2:\n"
		".section .fixup, \"ax\"\n"
		"3:	li %0, 1\n"
		"	b 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		_ASM_ALIGN "\n"
		_ASM_PTR "1b, 3b\n"
		".previous"
		: "=r"(faulted), "=r"(replaced)
		: "r"(ip), "r"(new),
		  "0"(faulted), "r"(old)
		: "memory");

	if (replaced != old && replaced != new)
		faulted = 2;

	if (!faulted)
		flush_icache_range(ip, ip + 8);

	return faulted;
}

notrace int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char old[4], *new;
	int ret;

	ip += CALL_BACK;

	memcpy(old, &ftrace_call, 4);
	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

notrace int ftrace_mcount_set(unsigned long *data)
{
	unsigned long ip = (long)(&mcount_call);
	unsigned long *addr = data;
	unsigned char old[4], *new;

	/* ip is at the location, but modify code will subtact this */
	ip += CALL_BACK;

	/*
	 * Replace the mcount stub with a pointer to the
	 * ip recorder function.
	 */
	memcpy(old, &mcount_call, 4);
	new = ftrace_call_replace(ip, *addr);
	*addr = ftrace_modify_code(ip, old, new);

	return 0;
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* This is running in kstop_machine */

	ftrace_mcount_set(data);

	return 0;
}

