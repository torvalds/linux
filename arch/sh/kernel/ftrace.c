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

	err = ftrace_push_return_trace(old, self_addr, &trace.depth, 0);
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

#ifdef CONFIG_FTRACE_SYSCALLS

extern unsigned long __start_syscalls_metadata[];
extern unsigned long __stop_syscalls_metadata[];
extern unsigned long *sys_call_table;

static struct syscall_metadata **syscalls_metadata;

static struct syscall_metadata *find_syscall_meta(unsigned long *syscall)
{
	struct syscall_metadata *start;
	struct syscall_metadata *stop;
	char str[KSYM_SYMBOL_LEN];


	start = (struct syscall_metadata *)__start_syscalls_metadata;
	stop = (struct syscall_metadata *)__stop_syscalls_metadata;
	kallsyms_lookup((unsigned long) syscall, NULL, NULL, NULL, str);

	for ( ; start < stop; start++) {
		if (start->name && !strcmp(start->name, str))
			return start;
	}

	return NULL;
}

struct syscall_metadata *syscall_nr_to_meta(int nr)
{
	if (!syscalls_metadata || nr >= FTRACE_SYSCALL_MAX || nr < 0)
		return NULL;

	return syscalls_metadata[nr];
}

int syscall_name_to_nr(char *name)
{
	int i;

	if (!syscalls_metadata)
		return -1;
	for (i = 0; i < NR_syscalls; i++)
		if (syscalls_metadata[i])
			if (!strcmp(syscalls_metadata[i]->name, name))
				return i;
	return -1;
}

void set_syscall_enter_id(int num, int id)
{
	syscalls_metadata[num]->enter_id = id;
}

void set_syscall_exit_id(int num, int id)
{
	syscalls_metadata[num]->exit_id = id;
}

static int __init arch_init_ftrace_syscalls(void)
{
	int i;
	struct syscall_metadata *meta;
	unsigned long **psys_syscall_table = &sys_call_table;

	syscalls_metadata = kzalloc(sizeof(*syscalls_metadata) *
					FTRACE_SYSCALL_MAX, GFP_KERNEL);
	if (!syscalls_metadata) {
		WARN_ON(1);
		return -ENOMEM;
	}

	for (i = 0; i < FTRACE_SYSCALL_MAX; i++) {
		meta = find_syscall_meta(psys_syscall_table[i]);
		syscalls_metadata[i] = meta;
	}

	return 0;
}
arch_initcall(arch_init_ftrace_syscalls);
#endif /* CONFIG_FTRACE_SYSCALLS */
