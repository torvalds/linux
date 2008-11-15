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
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/cacheflush.h>
#include <asm/ftrace.h>


static unsigned int ftrace_nop = 0x60000000;

#ifdef CONFIG_PPC32
# define GET_ADDR(addr) addr
#else
/* PowerPC64's functions are data that points to the functions */
# define GET_ADDR(addr) *(unsigned long *)addr
#endif


static unsigned int ftrace_calc_offset(long ip, long addr)
{
	return (int)(addr - ip);
}

static unsigned char *ftrace_nop_replace(void)
{
	return (char *)&ftrace_nop;
}

static unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	static unsigned int op;

	/*
	 * It would be nice to just use create_function_call, but that will
	 * update the code itself. Here we need to just return the
	 * instruction that is going to be modified, without modifying the
	 * code.
	 */
	addr = GET_ADDR(addr);

	/* Set to "bl addr" */
	op = 0x48000001 | (ftrace_calc_offset(ip, addr) & 0x03fffffc);

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

static int
ftrace_modify_code(unsigned long ip, unsigned char *old_code,
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

	flush_icache_range(ip, ip + 8);

	return 0;
}

static int test_24bit_addr(unsigned long ip, unsigned long addr)
{
	long diff;

	/*
	 * Can we get to addr from ip in 24 bits?
	 *  (26 really, since we mulitply by 4 for 4 byte alignment)
	 */
	diff = addr - ip;

	/*
	 * Return true if diff is less than 1 << 25
	 *  and greater than -1 << 26.
	 */
	return (diff < (1 << 25)) && (diff > (-1 << 26));
}

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *old, *new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(rec->ip, addr)) {
		/* within range */
		old = ftrace_call_replace(rec->ip, addr);
		new = ftrace_nop_replace();
		return ftrace_modify_code(rec->ip, old, new);
	}

	return 0;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned char *old, *new;

	/*
	 * If the calling address is more that 24 bits away,
	 * then we had to use a trampoline to make the call.
	 * Otherwise just update the call site.
	 */
	if (test_24bit_addr(rec->ip, addr)) {
		/* within range */
		old = ftrace_nop_replace();
		new = ftrace_call_replace(rec->ip, addr);
		return ftrace_modify_code(rec->ip, old, new);
	}

	return 0;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char old[MCOUNT_INSN_SIZE], *new;
	int ret;

	memcpy(old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = ftrace_modify_code(ip, old, new);

	return ret;
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* caller expects data to be zero */
	unsigned long *p = data;

	*p = 0;

	return 0;
}

