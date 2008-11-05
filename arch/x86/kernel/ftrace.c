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
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/ftrace.h>
#include <asm/nops.h>


static unsigned char ftrace_nop[MCOUNT_INSN_SIZE];

union ftrace_code_union {
	char code[MCOUNT_INSN_SIZE];
	struct {
		char e8;
		int offset;
	} __attribute__((packed));
};


static int ftrace_calc_offset(long ip, long addr)
{
	return (int)(addr - ip);
}

unsigned char *ftrace_nop_replace(void)
{
	return ftrace_nop;
}

unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
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

int
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

	sync_core();

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
	extern const unsigned char ftrace_test_p6nop[];
	extern const unsigned char ftrace_test_nop5[];
	extern const unsigned char ftrace_test_jmp[];
	int faulted = 0;

	/*
	 * There is no good nop for all x86 archs.
	 * We will default to using the P6_NOP5, but first we
	 * will test to make sure that the nop will actually
	 * work on this CPU. If it faults, we will then
	 * go to a lesser efficient 5 byte nop. If that fails
	 * we then just use a jmp as our nop. This isn't the most
	 * efficient nop, but we can not use a multi part nop
	 * since we would then risk being preempted in the middle
	 * of that nop, and if we enabled tracing then, it might
	 * cause a system crash.
	 *
	 * TODO: check the cpuid to determine the best nop.
	 */
	asm volatile (
		"ftrace_test_jmp:"
		"jmp ftrace_test_p6nop\n"
		"nop\n"
		"nop\n"
		"nop\n"  /* 2 byte jmp + 3 bytes */
		"ftrace_test_p6nop:"
		P6_NOP5
		"jmp 1f\n"
		"ftrace_test_nop5:"
		".byte 0x66,0x66,0x66,0x66,0x90\n"
		"1:"
		".section .fixup, \"ax\"\n"
		"2:	movl $1, %0\n"
		"	jmp ftrace_test_nop5\n"
		"3:	movl $2, %0\n"
		"	jmp 1b\n"
		".previous\n"
		_ASM_EXTABLE(ftrace_test_p6nop, 2b)
		_ASM_EXTABLE(ftrace_test_nop5, 3b)
		: "=r"(faulted) : "0" (faulted));

	switch (faulted) {
	case 0:
		pr_info("ftrace: converting mcount calls to 0f 1f 44 00 00\n");
		memcpy(ftrace_nop, ftrace_test_p6nop, MCOUNT_INSN_SIZE);
		break;
	case 1:
		pr_info("ftrace: converting mcount calls to 66 66 66 66 90\n");
		memcpy(ftrace_nop, ftrace_test_nop5, MCOUNT_INSN_SIZE);
		break;
	case 2:
		pr_info("ftrace: converting mcount calls to jmp . + 5\n");
		memcpy(ftrace_nop, ftrace_test_jmp, MCOUNT_INSN_SIZE);
		break;
	}

	/* The return code is retured via data */
	*(unsigned long *)data = 0;

	return 0;
}
