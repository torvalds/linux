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
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>

#include <asm/ftrace.h>
#include <linux/ftrace.h>
#include <asm/nops.h>
#include <asm/nmi.h>



#ifdef CONFIG_FUNCTION_RET_TRACER

/*
 * These functions are picked from those used on
 * this page for dynamic ftrace. They have been
 * simplified to ignore all traces in NMI context.
 */
static atomic_t in_nmi;

void ftrace_nmi_enter(void)
{
	atomic_inc(&in_nmi);
}

void ftrace_nmi_exit(void)
{
	atomic_dec(&in_nmi);
}

/* Add a function return address to the trace stack on thread info.*/
static int push_return_trace(unsigned long ret, unsigned long long time,
				unsigned long func)
{
	int index;
	struct thread_info *ti = current_thread_info();

	/* The return trace stack is full */
	if (ti->curr_ret_stack == FTRACE_RET_STACK_SIZE - 1)
		return -EBUSY;

	index = ++ti->curr_ret_stack;
	ti->ret_stack[index].ret = ret;
	ti->ret_stack[index].func = func;
	ti->ret_stack[index].calltime = time;

	return 0;
}

/* Retrieve a function return address to the trace stack on thread info.*/
static void pop_return_trace(unsigned long *ret, unsigned long long *time,
				unsigned long *func)
{
	int index;

	struct thread_info *ti = current_thread_info();
	index = ti->curr_ret_stack;
	*ret = ti->ret_stack[index].ret;
	*func = ti->ret_stack[index].func;
	*time = ti->ret_stack[index].calltime;
	ti->curr_ret_stack--;
}

/*
 * Send the trace to the ring-buffer.
 * @return the original return address.
 */
unsigned long ftrace_return_to_handler(void)
{
	struct ftrace_retfunc trace;
	pop_return_trace(&trace.ret, &trace.calltime, &trace.func);
	trace.rettime = cpu_clock(raw_smp_processor_id());
	ftrace_function_return(&trace);

	return trace.ret;
}

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
asmlinkage
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	unsigned long old;
	unsigned long long calltime;
	int faulted;
	unsigned long return_hooker = (unsigned long)
				&return_to_handler;

	/* Nmi's are currently unsupported */
	if (atomic_read(&in_nmi))
		return;

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	asm volatile(
		"1: movl (%[parent_old]), %[old]\n"
		"2: movl %[return_hooker], (%[parent_replaced])\n"
		"   movl $0, %[faulted]\n"

		".section .fixup, \"ax\"\n"
		"3: movl $1, %[faulted]\n"
		".previous\n"

		".section __ex_table, \"a\"\n"
		"   .long 1b, 3b\n"
		"   .long 2b, 3b\n"
		".previous\n"

		: [parent_replaced] "=r" (parent), [old] "=r" (old),
		  [faulted] "=r" (faulted)
		: [parent_old] "0" (parent), [return_hooker] "r" (return_hooker)
		: "memory"
	);

	if (WARN_ON(faulted)) {
		unregister_ftrace_return();
		return;
	}

	if (WARN_ON(!__kernel_text_address(old))) {
		unregister_ftrace_return();
		*parent = old;
		return;
	}

	calltime = cpu_clock(raw_smp_processor_id());

	if (push_return_trace(old, calltime, self_addr) == -EBUSY)
		*parent = old;
}

#endif

#ifdef CONFIG_DYNAMIC_FTRACE

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

/*
 * Modifying code must take extra care. On an SMP machine, if
 * the code being modified is also being executed on another CPU
 * that CPU will have undefined results and possibly take a GPF.
 * We use kstop_machine to stop other CPUS from exectuing code.
 * But this does not stop NMIs from happening. We still need
 * to protect against that. We separate out the modification of
 * the code to take care of this.
 *
 * Two buffers are added: An IP buffer and a "code" buffer.
 *
 * 1) Put the instruction pointer into the IP buffer
 *    and the new code into the "code" buffer.
 * 2) Set a flag that says we are modifying code
 * 3) Wait for any running NMIs to finish.
 * 4) Write the code
 * 5) clear the flag.
 * 6) Wait for any running NMIs to finish.
 *
 * If an NMI is executed, the first thing it does is to call
 * "ftrace_nmi_enter". This will check if the flag is set to write
 * and if it is, it will write what is in the IP and "code" buffers.
 *
 * The trick is, it does not matter if everyone is writing the same
 * content to the code location. Also, if a CPU is executing code
 * it is OK to write to that code location if the contents being written
 * are the same as what exists.
 */

static atomic_t in_nmi = ATOMIC_INIT(0);
static int mod_code_status;		/* holds return value of text write */
static int mod_code_write;		/* set when NMI should do the write */
static void *mod_code_ip;		/* holds the IP to write to */
static void *mod_code_newcode;		/* holds the text to write to the IP */

static unsigned nmi_wait_count;
static atomic_t nmi_update_count = ATOMIC_INIT(0);

int ftrace_arch_read_dyn_info(char *buf, int size)
{
	int r;

	r = snprintf(buf, size, "%u %u",
		     nmi_wait_count,
		     atomic_read(&nmi_update_count));
	return r;
}

static void ftrace_mod_code(void)
{
	/*
	 * Yes, more than one CPU process can be writing to mod_code_status.
	 *    (and the code itself)
	 * But if one were to fail, then they all should, and if one were
	 * to succeed, then they all should.
	 */
	mod_code_status = probe_kernel_write(mod_code_ip, mod_code_newcode,
					     MCOUNT_INSN_SIZE);

}

void ftrace_nmi_enter(void)
{
	atomic_inc(&in_nmi);
	/* Must have in_nmi seen before reading write flag */
	smp_mb();
	if (mod_code_write) {
		ftrace_mod_code();
		atomic_inc(&nmi_update_count);
	}
}

void ftrace_nmi_exit(void)
{
	/* Finish all executions before clearing in_nmi */
	smp_wmb();
	atomic_dec(&in_nmi);
}

static void wait_for_nmi(void)
{
	int waited = 0;

	while (atomic_read(&in_nmi)) {
		waited = 1;
		cpu_relax();
	}

	if (waited)
		nmi_wait_count++;
}

static int
do_ftrace_mod_code(unsigned long ip, void *new_code)
{
	mod_code_ip = (void *)ip;
	mod_code_newcode = new_code;

	/* The buffers need to be visible before we let NMIs write them */
	smp_wmb();

	mod_code_write = 1;

	/* Make sure write bit is visible before we wait on NMIs */
	smp_mb();

	wait_for_nmi();

	/* Make sure all running NMIs have finished before we write the code */
	smp_mb();

	ftrace_mod_code();

	/* Make sure the write happens before clearing the bit */
	smp_wmb();

	mod_code_write = 0;

	/* make sure NMIs see the cleared bit */
	smp_mb();

	wait_for_nmi();

	return mod_code_status;
}




static unsigned char ftrace_nop[MCOUNT_INSN_SIZE];

unsigned char *ftrace_nop_replace(void)
{
	return ftrace_nop;
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
	if (do_ftrace_mod_code(ip, new_code))
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
#endif
