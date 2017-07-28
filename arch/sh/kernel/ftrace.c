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
#include <linux/kernel.h>
#include <asm/ftrace.h>
#include <asm/cacheflush.h>
#include <asm/unistd.h>
#include <trace/syscall.h>

#ifdef CONFIG_DYNAMIC_FTRACE
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
 * 2) Wait for any running NMIs to finish and set a flag that says
 *    we are modifying code, it is done in an atomic operation.
 * 3) Write the code
 * 4) clear the flag.
 * 5) Wait for any running NMIs to finish.
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
#define MOD_CODE_WRITE_FLAG (1 << 31)	/* set when NMI should do the write */
static atomic_t nmi_running = ATOMIC_INIT(0);
static int mod_code_status;		/* holds return value of text write */
static void *mod_code_ip;		/* holds the IP to write to */
static void *mod_code_newcode;		/* holds the text to write to the IP */

static void clear_mod_flag(void)
{
	int old = atomic_read(&nmi_running);

	for (;;) {
		int new = old & ~MOD_CODE_WRITE_FLAG;

		if (old == new)
			break;

		old = atomic_cmpxchg(&nmi_running, old, new);
	}
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

	/* if we fail, then kill any new writers */
	if (mod_code_status)
		clear_mod_flag();
}

void arch_ftrace_nmi_enter(void)
{
	if (atomic_inc_return(&nmi_running) & MOD_CODE_WRITE_FLAG) {
		smp_rmb();
		ftrace_mod_code();
	}
	/* Must have previous changes seen before executions */
	smp_mb();
}

void arch_ftrace_nmi_exit(void)
{
	/* Finish all executions before clearing nmi_running */
	smp_mb();
	atomic_dec(&nmi_running);
}

static void wait_for_nmi_and_set_mod_flag(void)
{
	if (!atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG))
		return;

	do {
		cpu_relax();
	} while (atomic_cmpxchg(&nmi_running, 0, MOD_CODE_WRITE_FLAG));
}

static void wait_for_nmi(void)
{
	if (!atomic_read(&nmi_running))
		return;

	do {
		cpu_relax();
	} while (atomic_read(&nmi_running));
}

static int
do_ftrace_mod_code(unsigned long ip, void *new_code)
{
	mod_code_ip = (void *)ip;
	mod_code_newcode = new_code;

	/* The buffers need to be visible before we let NMIs write them */
	smp_mb();

	wait_for_nmi_and_set_mod_flag();

	/* Make sure all running NMIs have finished before we write the code */
	smp_mb();

	ftrace_mod_code();

	/* Make sure the write happens before clearing the bit */
	smp_mb();

	clear_mod_flag();
	wait_for_nmi();

	return mod_code_status;
}

static int ftrace_modify_code(unsigned long ip, unsigned char *old_code,
		       unsigned char *new_code)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug was to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with probe_kernel_*(), and make
	 * sure what we read is what we expected it to be before modifying it.
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

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_graph_call(void);

static int ftrace_mod(unsigned long ip, unsigned long old_addr,
		      unsigned long new_addr)
{
	unsigned char code[MCOUNT_INSN_SIZE];

	if (probe_kernel_read(code, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	if (old_addr != __raw_readl((unsigned long *)code))
		return -EINVAL;

	__raw_writel(new_addr, ip);
	return 0;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip, old_addr, new_addr;

	ip = (unsigned long)(&ftrace_graph_call) + GRAPH_INSN_OFFSET;
	old_addr = (unsigned long)(&skip_trace);
	new_addr = (unsigned long)(&ftrace_graph_caller);

	return ftrace_mod(ip, old_addr, new_addr);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned long ip, old_addr, new_addr;

	ip = (unsigned long)(&ftrace_graph_call) + GRAPH_INSN_OFFSET;
	old_addr = (unsigned long)(&ftrace_graph_caller);
	new_addr = (unsigned long)(&skip_trace);

	return ftrace_mod(ip, old_addr, new_addr);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

/*
 * Hook the return address and push it in the stack of return addrs
 * in the current thread info.
 *
 * This is the main routine for the function graph tracer. The function
 * graph tracer essentially works like this:
 *
 * parent is the stack address containing self_addr's return address.
 * We pull the real return address out of parent and store it in
 * current's ret_stack. Then, we replace the return address on the stack
 * with the address of return_to_handler. self_addr is the function that
 * called mcount.
 *
 * When self_addr returns, it will jump to return_to_handler which calls
 * ftrace_return_to_handler. ftrace_return_to_handler will pull the real
 * return address off of current's ret_stack and jump to it.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr)
{
	unsigned long old;
	int faulted, err;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(ftrace_graph_is_dead()))
		return;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	__asm__ __volatile__(
		"1:						\n\t"
		"mov.l		@%2, %0				\n\t"
		"2:						\n\t"
		"mov.l		%3, @%2				\n\t"
		"mov		#0, %1				\n\t"
		"3:						\n\t"
		".section .fixup, \"ax\"			\n\t"
		"4:						\n\t"
		"mov.l		5f, %0				\n\t"
		"jmp		@%0				\n\t"
		" mov		#1, %1				\n\t"
		".balign 4					\n\t"
		"5:	.long 3b				\n\t"
		".previous					\n\t"
		".section __ex_table,\"a\"			\n\t"
		".long 1b, 4b					\n\t"
		".long 2b, 4b					\n\t"
		".previous					\n\t"
		: "=&r" (old), "=r" (faulted)
		: "r" (parent), "r" (return_hooker)
	);

	if (unlikely(faulted)) {
		ftrace_graph_stop();
		WARN_ON(1);
		return;
	}

	err = ftrace_push_return_trace(old, self_addr, &trace.depth, 0, NULL);
	if (err == -EBUSY) {
		__raw_writel(old, parent);
		return;
	}

	trace.func = self_addr;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		current->curr_ret_stack--;
		__raw_writel(old, parent);
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
