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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/spinlock.h>
#include <linux/hardirq.h>
#include <linux/uaccess.h>
#include <linux/ftrace.h>
#include <linux/percpu.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/module.h>

#include <trace/syscall.h>

#include <asm/cacheflush.h>
#include <asm/kprobes.h>
#include <asm/ftrace.h>
#include <asm/nops.h>

#ifdef CONFIG_DYNAMIC_FTRACE

int ftrace_arch_code_modify_prepare(void)
{
	set_kernel_text_rw();
	set_all_modules_text_rw();
	return 0;
}

int ftrace_arch_code_modify_post_process(void)
{
	set_all_modules_text_ro();
	set_kernel_text_ro();
	return 0;
}

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

static unsigned char *ftrace_call_replace(unsigned long ip, unsigned long addr)
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

static inline int
within(unsigned long addr, unsigned long start, unsigned long end)
{
	return addr >= start && addr < end;
}

static int
do_ftrace_mod_code(unsigned long ip, const void *new_code)
{
	/*
	 * On x86_64, kernel text mappings are mapped read-only with
	 * CONFIG_DEBUG_RODATA. So we use the kernel identity mapping instead
	 * of the kernel text mapping to modify the kernel text.
	 *
	 * For 32bit kernels, these mappings are same and we can use
	 * kernel identity mapping to modify code.
	 */
	if (within(ip, (unsigned long)_text, (unsigned long)_etext))
		ip = (unsigned long)__va(__pa_symbol(ip));

	return probe_kernel_write((void *)ip, new_code, MCOUNT_INSN_SIZE);
}

static const unsigned char *ftrace_nop_replace(void)
{
	return ideal_nops[NOP_ATOMIC5];
}

static int
ftrace_modify_code_direct(unsigned long ip, unsigned const char *old_code,
		   unsigned const char *new_code)
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

int ftrace_make_nop(struct module *mod,
		    struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned const char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, addr);
	new = ftrace_nop_replace();

	/*
	 * On boot up, and when modules are loaded, the MCOUNT_ADDR
	 * is converted to a nop, and will never become MCOUNT_ADDR
	 * again. This code is either running before SMP (on boot up)
	 * or before the code will ever be executed (module load).
	 * We do not want to use the breakpoint version in this case,
	 * just modify the code directly.
	 */
	if (addr == MCOUNT_ADDR)
		return ftrace_modify_code_direct(rec->ip, old, new);

	/* Normal cases use add_brk_on_nop */
	WARN_ONCE(1, "invalid use of ftrace_make_nop");
	return -EINVAL;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned const char *new, *old;
	unsigned long ip = rec->ip;

	old = ftrace_nop_replace();
	new = ftrace_call_replace(ip, addr);

	/* Should only be called when module is loaded */
	return ftrace_modify_code_direct(rec->ip, old, new);
}

/*
 * The modifying_ftrace_code is used to tell the breakpoint
 * handler to call ftrace_int3_handler(). If it fails to
 * call this handler for a breakpoint added by ftrace, then
 * the kernel may crash.
 *
 * As atomic_writes on x86 do not need a barrier, we do not
 * need to add smp_mb()s for this to work. It is also considered
 * that we can not read the modifying_ftrace_code before
 * executing the breakpoint. That would be quite remarkable if
 * it could do that. Here's the flow that is required:
 *
 *   CPU-0                          CPU-1
 *
 * atomic_inc(mfc);
 * write int3s
 *				<trap-int3> // implicit (r)mb
 *				if (atomic_read(mfc))
 *					call ftrace_int3_handler()
 *
 * Then when we are finished:
 *
 * atomic_dec(mfc);
 *
 * If we hit a breakpoint that was not set by ftrace, it does not
 * matter if ftrace_int3_handler() is called or not. It will
 * simply be ignored. But it is crucial that a ftrace nop/caller
 * breakpoint is handled. No other user should ever place a
 * breakpoint on an ftrace nop/caller location. It must only
 * be done by this code.
 */
atomic_t modifying_ftrace_code __read_mostly;

static int
ftrace_modify_code(unsigned long ip, unsigned const char *old_code,
		   unsigned const char *new_code);

/*
 * Should never be called:
 *  As it is only called by __ftrace_replace_code() which is called by
 *  ftrace_replace_code() that x86 overrides, and by ftrace_update_code()
 *  which is called to turn mcount into nops or nops into function calls
 *  but not to convert a function from not using regs to one that uses
 *  regs, which ftrace_modify_call() is for.
 */
int ftrace_modify_call(struct dyn_ftrace *rec, unsigned long old_addr,
				 unsigned long addr)
{
	WARN_ON(1);
	return -EINVAL;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char old[MCOUNT_INSN_SIZE], *new;
	int ret;

	memcpy(old, &ftrace_call, MCOUNT_INSN_SIZE);
	new = ftrace_call_replace(ip, (unsigned long)func);

	/* See comment above by declaration of modifying_ftrace_code */
	atomic_inc(&modifying_ftrace_code);

	ret = ftrace_modify_code(ip, old, new);

	/* Also update the regs callback function */
	if (!ret) {
		ip = (unsigned long)(&ftrace_regs_call);
		memcpy(old, &ftrace_regs_call, MCOUNT_INSN_SIZE);
		new = ftrace_call_replace(ip, (unsigned long)func);
		ret = ftrace_modify_code(ip, old, new);
	}

	atomic_dec(&modifying_ftrace_code);

	return ret;
}

static int is_ftrace_caller(unsigned long ip)
{
	if (ip == (unsigned long)(&ftrace_call) ||
		ip == (unsigned long)(&ftrace_regs_call))
		return 1;

	return 0;
}

/*
 * A breakpoint was added to the code address we are about to
 * modify, and this is the handle that will just skip over it.
 * We are either changing a nop into a trace call, or a trace
 * call to a nop. While the change is taking place, we treat
 * it just like it was a nop.
 */
int ftrace_int3_handler(struct pt_regs *regs)
{
	unsigned long ip;

	if (WARN_ON_ONCE(!regs))
		return 0;

	ip = regs->ip - 1;
	if (!ftrace_location(ip) && !is_ftrace_caller(ip))
		return 0;

	regs->ip += MCOUNT_INSN_SIZE - 1;

	return 1;
}

static int ftrace_write(unsigned long ip, const char *val, int size)
{
	/*
	 * On x86_64, kernel text mappings are mapped read-only with
	 * CONFIG_DEBUG_RODATA. So we use the kernel identity mapping instead
	 * of the kernel text mapping to modify the kernel text.
	 *
	 * For 32bit kernels, these mappings are same and we can use
	 * kernel identity mapping to modify code.
	 */
	if (within(ip, (unsigned long)_text, (unsigned long)_etext))
		ip = (unsigned long)__va(__pa_symbol(ip));

	return probe_kernel_write((void *)ip, val, size);
}

static int add_break(unsigned long ip, const char *old)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];
	unsigned char brk = BREAKPOINT_INSTRUCTION;

	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* Make sure it is what we expect it to be */
	if (memcmp(replaced, old, MCOUNT_INSN_SIZE) != 0)
		return -EINVAL;

	if (ftrace_write(ip, &brk, 1))
		return -EPERM;

	return 0;
}

static int add_brk_on_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned const char *old;
	unsigned long ip = rec->ip;

	old = ftrace_call_replace(ip, addr);

	return add_break(rec->ip, old);
}


static int add_brk_on_nop(struct dyn_ftrace *rec)
{
	unsigned const char *old;

	old = ftrace_nop_replace();

	return add_break(rec->ip, old);
}

/*
 * If the record has the FTRACE_FL_REGS set, that means that it
 * wants to convert to a callback that saves all regs. If FTRACE_FL_REGS
 * is not not set, then it wants to convert to the normal callback.
 */
static unsigned long get_ftrace_addr(struct dyn_ftrace *rec)
{
	if (rec->flags & FTRACE_FL_REGS)
		return (unsigned long)FTRACE_REGS_ADDR;
	else
		return (unsigned long)FTRACE_ADDR;
}

/*
 * The FTRACE_FL_REGS_EN is set when the record already points to
 * a function that saves all the regs. Basically the '_EN' version
 * represents the current state of the function.
 */
static unsigned long get_ftrace_old_addr(struct dyn_ftrace *rec)
{
	if (rec->flags & FTRACE_FL_REGS_EN)
		return (unsigned long)FTRACE_REGS_ADDR;
	else
		return (unsigned long)FTRACE_ADDR;
}

static int add_breakpoints(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	int ret;

	ret = ftrace_test_record(rec, enable);

	ftrace_addr = get_ftrace_addr(rec);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MAKE_CALL:
		/* converting nop to call */
		return add_brk_on_nop(rec);

	case FTRACE_UPDATE_MODIFY_CALL_REGS:
	case FTRACE_UPDATE_MODIFY_CALL:
		ftrace_addr = get_ftrace_old_addr(rec);
		/* fall through */
	case FTRACE_UPDATE_MAKE_NOP:
		/* converting a call to a nop */
		return add_brk_on_call(rec, ftrace_addr);
	}
	return 0;
}

/*
 * On error, we need to remove breakpoints. This needs to
 * be done caefully. If the address does not currently have a
 * breakpoint, we know we are done. Otherwise, we look at the
 * remaining 4 bytes of the instruction. If it matches a nop
 * we replace the breakpoint with the nop. Otherwise we replace
 * it with the call instruction.
 */
static int remove_breakpoint(struct dyn_ftrace *rec)
{
	unsigned char ins[MCOUNT_INSN_SIZE];
	unsigned char brk = BREAKPOINT_INSTRUCTION;
	const unsigned char *nop;
	unsigned long ftrace_addr;
	unsigned long ip = rec->ip;

	/* If we fail the read, just give up */
	if (probe_kernel_read(ins, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	/* If this does not have a breakpoint, we are done */
	if (ins[0] != brk)
		return -1;

	nop = ftrace_nop_replace();

	/*
	 * If the last 4 bytes of the instruction do not match
	 * a nop, then we assume that this is a call to ftrace_addr.
	 */
	if (memcmp(&ins[1], &nop[1], MCOUNT_INSN_SIZE - 1) != 0) {
		/*
		 * For extra paranoidism, we check if the breakpoint is on
		 * a call that would actually jump to the ftrace_addr.
		 * If not, don't touch the breakpoint, we make just create
		 * a disaster.
		 */
		ftrace_addr = get_ftrace_addr(rec);
		nop = ftrace_call_replace(ip, ftrace_addr);

		if (memcmp(&ins[1], &nop[1], MCOUNT_INSN_SIZE - 1) == 0)
			goto update;

		/* Check both ftrace_addr and ftrace_old_addr */
		ftrace_addr = get_ftrace_old_addr(rec);
		nop = ftrace_call_replace(ip, ftrace_addr);

		if (memcmp(&ins[1], &nop[1], MCOUNT_INSN_SIZE - 1) != 0)
			return -EINVAL;
	}

 update:
	return probe_kernel_write((void *)ip, &nop[0], 1);
}

static int add_update_code(unsigned long ip, unsigned const char *new)
{
	/* skip breakpoint */
	ip++;
	new++;
	if (ftrace_write(ip, new, MCOUNT_INSN_SIZE - 1))
		return -EPERM;
	return 0;
}

static int add_update_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned const char *new;

	new = ftrace_call_replace(ip, addr);
	return add_update_code(ip, new);
}

static int add_update_nop(struct dyn_ftrace *rec)
{
	unsigned long ip = rec->ip;
	unsigned const char *new;

	new = ftrace_nop_replace();
	return add_update_code(ip, new);
}

static int add_update(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	int ret;

	ret = ftrace_test_record(rec, enable);

	ftrace_addr  = get_ftrace_addr(rec);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MODIFY_CALL_REGS:
	case FTRACE_UPDATE_MODIFY_CALL:
	case FTRACE_UPDATE_MAKE_CALL:
		/* converting nop to call */
		return add_update_call(rec, ftrace_addr);

	case FTRACE_UPDATE_MAKE_NOP:
		/* converting a call to a nop */
		return add_update_nop(rec);
	}

	return 0;
}

static int finish_update_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	unsigned const char *new;

	new = ftrace_call_replace(ip, addr);

	if (ftrace_write(ip, new, 1))
		return -EPERM;

	return 0;
}

static int finish_update_nop(struct dyn_ftrace *rec)
{
	unsigned long ip = rec->ip;
	unsigned const char *new;

	new = ftrace_nop_replace();

	if (ftrace_write(ip, new, 1))
		return -EPERM;
	return 0;
}

static int finish_update(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	int ret;

	ret = ftrace_update_record(rec, enable);

	ftrace_addr = get_ftrace_addr(rec);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MODIFY_CALL_REGS:
	case FTRACE_UPDATE_MODIFY_CALL:
	case FTRACE_UPDATE_MAKE_CALL:
		/* converting nop to call */
		return finish_update_call(rec, ftrace_addr);

	case FTRACE_UPDATE_MAKE_NOP:
		/* converting a call to a nop */
		return finish_update_nop(rec);
	}

	return 0;
}

static void do_sync_core(void *data)
{
	sync_core();
}

static void run_sync(void)
{
	int enable_irqs = irqs_disabled();

	/* We may be called with interrupts disbled (on bootup). */
	if (enable_irqs)
		local_irq_enable();
	on_each_cpu(do_sync_core, NULL, 1);
	if (enable_irqs)
		local_irq_disable();
}

void ftrace_replace_code(int enable)
{
	struct ftrace_rec_iter *iter;
	struct dyn_ftrace *rec;
	const char *report = "adding breakpoints";
	int count = 0;
	int ret;

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		ret = add_breakpoints(rec, enable);
		if (ret)
			goto remove_breakpoints;
		count++;
	}

	run_sync();

	report = "updating code";

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		ret = add_update(rec, enable);
		if (ret)
			goto remove_breakpoints;
	}

	run_sync();

	report = "removing breakpoints";

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		ret = finish_update(rec, enable);
		if (ret)
			goto remove_breakpoints;
	}

	run_sync();

	return;

 remove_breakpoints:
	ftrace_bug(ret, rec ? rec->ip : 0);
	printk(KERN_WARNING "Failed on %s (%d):\n", report, count);
	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);
		remove_breakpoint(rec);
	}
}

static int
ftrace_modify_code(unsigned long ip, unsigned const char *old_code,
		   unsigned const char *new_code)
{
	int ret;

	ret = add_break(ip, old_code);
	if (ret)
		goto out;

	run_sync();

	ret = add_update_code(ip, new_code);
	if (ret)
		goto fail_update;

	run_sync();

	ret = ftrace_write(ip, new_code, 1);
	if (ret) {
		ret = -EPERM;
		goto out;
	}
	run_sync();
 out:
	return ret;

 fail_update:
	probe_kernel_write((void *)ip, &old_code[0], 1);
	goto out;
}

void arch_ftrace_update_code(int command)
{
	/* See comment above by declaration of modifying_ftrace_code */
	atomic_inc(&modifying_ftrace_code);

	ftrace_modify_all_code(command);

	atomic_dec(&modifying_ftrace_code);
}

int __init ftrace_dyn_arch_init(void *data)
{
	/* The return code is retured via data */
	*(unsigned long *)data = 0;

	return 0;
}
#endif

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_graph_call(void);

static int ftrace_mod_jmp(unsigned long ip,
			  int old_offset, int new_offset)
{
	unsigned char code[MCOUNT_INSN_SIZE];

	if (probe_kernel_read(code, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	if (code[0] != 0xe9 || old_offset != *(int *)(&code[1]))
		return -EINVAL;

	*(int *)(&code[1]) = new_offset;

	if (do_ftrace_mod_code(ip, &code))
		return -EPERM;

	return 0;
}

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	int old_offset, new_offset;

	old_offset = (unsigned long)(&ftrace_stub) - (ip + MCOUNT_INSN_SIZE);
	new_offset = (unsigned long)(&ftrace_graph_caller) - (ip + MCOUNT_INSN_SIZE);

	return ftrace_mod_jmp(ip, old_offset, new_offset);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);
	int old_offset, new_offset;

	old_offset = (unsigned long)(&ftrace_graph_caller) - (ip + MCOUNT_INSN_SIZE);
	new_offset = (unsigned long)(&ftrace_stub) - (ip + MCOUNT_INSN_SIZE);

	return ftrace_mod_jmp(ip, old_offset, new_offset);
}

#endif /* !CONFIG_DYNAMIC_FTRACE */

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long old;
	int faulted;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)
				&return_to_handler;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	/*
	 * Protect against fault, even if it shouldn't
	 * happen. This tool is too much intrusive to
	 * ignore such a protection.
	 */
	asm volatile(
		"1: " _ASM_MOV " (%[parent]), %[old]\n"
		"2: " _ASM_MOV " %[return_hooker], (%[parent])\n"
		"   movl $0, %[faulted]\n"
		"3:\n"

		".section .fixup, \"ax\"\n"
		"4: movl $1, %[faulted]\n"
		"   jmp 3b\n"
		".previous\n"

		_ASM_EXTABLE(1b, 4b)
		_ASM_EXTABLE(2b, 4b)

		: [old] "=&r" (old), [faulted] "=r" (faulted)
		: [parent] "r" (parent), [return_hooker] "r" (return_hooker)
		: "memory"
	);

	if (unlikely(faulted)) {
		ftrace_graph_stop();
		WARN_ON(1);
		return;
	}

	trace.func = self_addr;
	trace.depth = current->curr_ret_stack + 1;

	/* Only trace if the calling function expects to */
	if (!ftrace_graph_entry(&trace)) {
		*parent = old;
		return;
	}

	if (ftrace_push_return_trace(old, self_addr, &trace.depth,
		    frame_pointer) == -EBUSY) {
		*parent = old;
		return;
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
