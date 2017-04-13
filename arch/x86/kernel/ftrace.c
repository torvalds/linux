/*
 * Dynamic function tracing support.
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
#include <linux/slab.h>
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
		unsigned char e8;
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

static unsigned long text_ip_addr(unsigned long ip)
{
	/*
	 * On x86_64, kernel text mappings are mapped read-only, so we use
	 * the kernel identity mapping instead of the kernel text mapping
	 * to modify the kernel text.
	 *
	 * For 32bit kernels, these mappings are same and we can use
	 * kernel identity mapping to modify code.
	 */
	if (within(ip, (unsigned long)_text, (unsigned long)_etext))
		ip = (unsigned long)__va(__pa_symbol(ip));

	return ip;
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

	ftrace_expected = old_code;

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

	ip = text_ip_addr(ip);

	/* replace the text with the new text */
	if (probe_kernel_write((void *)ip, new_code, MCOUNT_INSN_SIZE))
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

	ftrace_expected = NULL;

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
	ftrace_expected = NULL;
	return -EINVAL;
}

static unsigned long ftrace_update_func;

static int update_ftrace_func(unsigned long ip, void *new)
{
	unsigned char old[MCOUNT_INSN_SIZE];
	int ret;

	memcpy(old, (void *)ip, MCOUNT_INSN_SIZE);

	ftrace_update_func = ip;
	/* Make sure the breakpoints see the ftrace_update_func update */
	smp_wmb();

	/* See comment above by declaration of modifying_ftrace_code */
	atomic_inc(&modifying_ftrace_code);

	ret = ftrace_modify_code(ip, old, new);

	atomic_dec(&modifying_ftrace_code);

	return ret;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long ip = (unsigned long)(&ftrace_call);
	unsigned char *new;
	int ret;

	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = update_ftrace_func(ip, new);

	/* Also update the regs callback function */
	if (!ret) {
		ip = (unsigned long)(&ftrace_regs_call);
		new = ftrace_call_replace(ip, (unsigned long)func);
		ret = update_ftrace_func(ip, new);
	}

	return ret;
}

static int is_ftrace_caller(unsigned long ip)
{
	if (ip == ftrace_update_func)
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
	ip = text_ip_addr(ip);

	if (probe_kernel_write((void *)ip, val, size))
		return -EPERM;

	return 0;
}

static int add_break(unsigned long ip, const char *old)
{
	unsigned char replaced[MCOUNT_INSN_SIZE];
	unsigned char brk = BREAKPOINT_INSTRUCTION;

	if (probe_kernel_read(replaced, (void *)ip, MCOUNT_INSN_SIZE))
		return -EFAULT;

	ftrace_expected = old;

	/* Make sure it is what we expect it to be */
	if (memcmp(replaced, old, MCOUNT_INSN_SIZE) != 0)
		return -EINVAL;

	return ftrace_write(ip, &brk, 1);
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

static int add_breakpoints(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	int ret;

	ftrace_addr = ftrace_get_addr_curr(rec);

	ret = ftrace_test_record(rec, enable);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

	case FTRACE_UPDATE_MAKE_CALL:
		/* converting nop to call */
		return add_brk_on_nop(rec);

	case FTRACE_UPDATE_MODIFY_CALL:
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
		return 0;

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
		ftrace_addr = ftrace_get_addr_new(rec);
		nop = ftrace_call_replace(ip, ftrace_addr);

		if (memcmp(&ins[1], &nop[1], MCOUNT_INSN_SIZE - 1) == 0)
			goto update;

		/* Check both ftrace_addr and ftrace_old_addr */
		ftrace_addr = ftrace_get_addr_curr(rec);
		nop = ftrace_call_replace(ip, ftrace_addr);

		ftrace_expected = nop;

		if (memcmp(&ins[1], &nop[1], MCOUNT_INSN_SIZE - 1) != 0)
			return -EINVAL;
	}

 update:
	return ftrace_write(ip, nop, 1);
}

static int add_update_code(unsigned long ip, unsigned const char *new)
{
	/* skip breakpoint */
	ip++;
	new++;
	return ftrace_write(ip, new, MCOUNT_INSN_SIZE - 1);
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

	ftrace_addr  = ftrace_get_addr_new(rec);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

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

	return ftrace_write(ip, new, 1);
}

static int finish_update_nop(struct dyn_ftrace *rec)
{
	unsigned long ip = rec->ip;
	unsigned const char *new;

	new = ftrace_nop_replace();

	return ftrace_write(ip, new, 1);
}

static int finish_update(struct dyn_ftrace *rec, int enable)
{
	unsigned long ftrace_addr;
	int ret;

	ret = ftrace_update_record(rec, enable);

	ftrace_addr = ftrace_get_addr_new(rec);

	switch (ret) {
	case FTRACE_UPDATE_IGNORE:
		return 0;

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
	count = 0;

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		ret = add_update(rec, enable);
		if (ret)
			goto remove_breakpoints;
		count++;
	}

	run_sync();

	report = "removing breakpoints";
	count = 0;

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		ret = finish_update(rec, enable);
		if (ret)
			goto remove_breakpoints;
		count++;
	}

	run_sync();

	return;

 remove_breakpoints:
	pr_warn("Failed on %s (%d):\n", report, count);
	ftrace_bug(ret, rec);
	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);
		/*
		 * Breakpoints are handled only when this function is in
		 * progress. The system could not work with them.
		 */
		if (remove_breakpoint(rec))
			BUG();
	}
	run_sync();
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
	/*
	 * The breakpoint is handled only when this function is in progress.
	 * The system could not work if we could not remove it.
	 */
	BUG_ON(ret);
 out:
	run_sync();
	return ret;

 fail_update:
	/* Also here the system could not work with the breakpoint */
	if (ftrace_write(ip, old_code, 1))
		BUG();
	goto out;
}

void arch_ftrace_update_code(int command)
{
	/* See comment above by declaration of modifying_ftrace_code */
	atomic_inc(&modifying_ftrace_code);

	ftrace_modify_all_code(command);

	atomic_dec(&modifying_ftrace_code);
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}

#if defined(CONFIG_X86_64) || defined(CONFIG_FUNCTION_GRAPH_TRACER)
static unsigned char *ftrace_jmp_replace(unsigned long ip, unsigned long addr)
{
	static union ftrace_code_union calc;

	/* Jmp not a call (ignore the .e8) */
	calc.e8		= 0xe9;
	calc.offset	= ftrace_calc_offset(ip + MCOUNT_INSN_SIZE, addr);

	/*
	 * ftrace external locks synchronize the access to the static variable.
	 */
	return calc.code;
}
#endif

/* Currently only x86_64 supports dynamic trampolines */
#ifdef CONFIG_X86_64

#ifdef CONFIG_MODULES
#include <linux/moduleloader.h>
/* Module allocation simplifies allocating memory for code */
static inline void *alloc_tramp(unsigned long size)
{
	return module_alloc(size);
}
static inline void tramp_free(void *tramp)
{
	module_memfree(tramp);
}
#else
/* Trampolines can only be created if modules are supported */
static inline void *alloc_tramp(unsigned long size)
{
	return NULL;
}
static inline void tramp_free(void *tramp) { }
#endif

/* Defined as markers to the end of the ftrace default trampolines */
extern void ftrace_regs_caller_end(void);
extern void ftrace_epilogue(void);
extern void ftrace_caller_op_ptr(void);
extern void ftrace_regs_caller_op_ptr(void);

/* movq function_trace_op(%rip), %rdx */
/* 0x48 0x8b 0x15 <offset-to-ftrace_trace_op (4 bytes)> */
#define OP_REF_SIZE	7

/*
 * The ftrace_ops is passed to the function callback. Since the
 * trampoline only services a single ftrace_ops, we can pass in
 * that ops directly.
 *
 * The ftrace_op_code_union is used to create a pointer to the
 * ftrace_ops that will be passed to the callback function.
 */
union ftrace_op_code_union {
	char code[OP_REF_SIZE];
	struct {
		char op[3];
		int offset;
	} __attribute__((packed));
};

static unsigned long
create_trampoline(struct ftrace_ops *ops, unsigned int *tramp_size)
{
	unsigned const char *jmp;
	unsigned long start_offset;
	unsigned long end_offset;
	unsigned long op_offset;
	unsigned long offset;
	unsigned long size;
	unsigned long ip;
	unsigned long *ptr;
	void *trampoline;
	/* 48 8b 15 <offset> is movq <offset>(%rip), %rdx */
	unsigned const char op_ref[] = { 0x48, 0x8b, 0x15 };
	union ftrace_op_code_union op_ptr;
	int ret;

	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
		start_offset = (unsigned long)ftrace_regs_caller;
		end_offset = (unsigned long)ftrace_regs_caller_end;
		op_offset = (unsigned long)ftrace_regs_caller_op_ptr;
	} else {
		start_offset = (unsigned long)ftrace_caller;
		end_offset = (unsigned long)ftrace_epilogue;
		op_offset = (unsigned long)ftrace_caller_op_ptr;
	}

	size = end_offset - start_offset;

	/*
	 * Allocate enough size to store the ftrace_caller code,
	 * the jmp to ftrace_epilogue, as well as the address of
	 * the ftrace_ops this trampoline is used for.
	 */
	trampoline = alloc_tramp(size + MCOUNT_INSN_SIZE + sizeof(void *));
	if (!trampoline)
		return 0;

	*tramp_size = size + MCOUNT_INSN_SIZE + sizeof(void *);

	/* Copy ftrace_caller onto the trampoline memory */
	ret = probe_kernel_read(trampoline, (void *)start_offset, size);
	if (WARN_ON(ret < 0)) {
		tramp_free(trampoline);
		return 0;
	}

	ip = (unsigned long)trampoline + size;

	/* The trampoline ends with a jmp to ftrace_epilogue */
	jmp = ftrace_jmp_replace(ip, (unsigned long)ftrace_epilogue);
	memcpy(trampoline + size, jmp, MCOUNT_INSN_SIZE);

	/*
	 * The address of the ftrace_ops that is used for this trampoline
	 * is stored at the end of the trampoline. This will be used to
	 * load the third parameter for the callback. Basically, that
	 * location at the end of the trampoline takes the place of
	 * the global function_trace_op variable.
	 */

	ptr = (unsigned long *)(trampoline + size + MCOUNT_INSN_SIZE);
	*ptr = (unsigned long)ops;

	op_offset -= start_offset;
	memcpy(&op_ptr, trampoline + op_offset, OP_REF_SIZE);

	/* Are we pointing to the reference? */
	if (WARN_ON(memcmp(op_ptr.op, op_ref, 3) != 0)) {
		tramp_free(trampoline);
		return 0;
	}

	/* Load the contents of ptr into the callback parameter */
	offset = (unsigned long)ptr;
	offset -= (unsigned long)trampoline + op_offset + OP_REF_SIZE;

	op_ptr.offset = offset;

	/* put in the new offset to the ftrace_ops */
	memcpy(trampoline + op_offset, &op_ptr, OP_REF_SIZE);

	/* ALLOC_TRAMP flags lets us know we created it */
	ops->flags |= FTRACE_OPS_FL_ALLOC_TRAMP;

	return (unsigned long)trampoline;
}

static unsigned long calc_trampoline_call_offset(bool save_regs)
{
	unsigned long start_offset;
	unsigned long call_offset;

	if (save_regs) {
		start_offset = (unsigned long)ftrace_regs_caller;
		call_offset = (unsigned long)ftrace_regs_call;
	} else {
		start_offset = (unsigned long)ftrace_caller;
		call_offset = (unsigned long)ftrace_call;
	}

	return call_offset - start_offset;
}

void arch_ftrace_update_trampoline(struct ftrace_ops *ops)
{
	ftrace_func_t func;
	unsigned char *new;
	unsigned long offset;
	unsigned long ip;
	unsigned int size;
	int ret;

	if (ops->trampoline) {
		/*
		 * The ftrace_ops caller may set up its own trampoline.
		 * In such a case, this code must not modify it.
		 */
		if (!(ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP))
			return;
	} else {
		ops->trampoline = create_trampoline(ops, &size);
		if (!ops->trampoline)
			return;
		ops->trampoline_size = size;
	}

	offset = calc_trampoline_call_offset(ops->flags & FTRACE_OPS_FL_SAVE_REGS);
	ip = ops->trampoline + offset;

	func = ftrace_ops_get_func(ops);

	/* Do a safe modify in case the trampoline is executing */
	new = ftrace_call_replace(ip, (unsigned long)func);
	ret = update_ftrace_func(ip, new);

	/* The update should never fail */
	WARN_ON(ret);
}

/* Return the address of the function the trampoline calls */
static void *addr_from_call(void *ptr)
{
	union ftrace_code_union calc;
	int ret;

	ret = probe_kernel_read(&calc, ptr, MCOUNT_INSN_SIZE);
	if (WARN_ON_ONCE(ret < 0))
		return NULL;

	/* Make sure this is a call */
	if (WARN_ON_ONCE(calc.e8 != 0xe8)) {
		pr_warn("Expected e8, got %x\n", calc.e8);
		return NULL;
	}

	return ptr + MCOUNT_INSN_SIZE + calc.offset;
}

void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent,
			   unsigned long frame_pointer);

/*
 * If the ops->trampoline was not allocated, then it probably
 * has a static trampoline func, or is the ftrace caller itself.
 */
static void *static_tramp_func(struct ftrace_ops *ops, struct dyn_ftrace *rec)
{
	unsigned long offset;
	bool save_regs = rec->flags & FTRACE_FL_REGS_EN;
	void *ptr;

	if (ops && ops->trampoline) {
#ifdef CONFIG_FUNCTION_GRAPH_TRACER
		/*
		 * We only know about function graph tracer setting as static
		 * trampoline.
		 */
		if (ops->trampoline == FTRACE_GRAPH_ADDR)
			return (void *)prepare_ftrace_return;
#endif
		return NULL;
	}

	offset = calc_trampoline_call_offset(save_regs);

	if (save_regs)
		ptr = (void *)FTRACE_REGS_ADDR + offset;
	else
		ptr = (void *)FTRACE_ADDR + offset;

	return addr_from_call(ptr);
}

void *arch_ftrace_trampoline_func(struct ftrace_ops *ops, struct dyn_ftrace *rec)
{
	unsigned long offset;

	/* If we didn't allocate this trampoline, consider it static */
	if (!ops || !(ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP))
		return static_tramp_func(ops, rec);

	offset = calc_trampoline_call_offset(ops->flags & FTRACE_OPS_FL_SAVE_REGS);
	return addr_from_call((void *)ops->trampoline + offset);
}

void arch_ftrace_trampoline_free(struct ftrace_ops *ops)
{
	if (!ops || !(ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP))
		return;

	tramp_free((void *)ops->trampoline);
	ops->trampoline = 0;
}

#endif /* CONFIG_X86_64 */
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER

#ifdef CONFIG_DYNAMIC_FTRACE
extern void ftrace_graph_call(void);

static int ftrace_mod_jmp(unsigned long ip, void *func)
{
	unsigned char *new;

	new = ftrace_jmp_replace(ip, (unsigned long)func);

	return update_ftrace_func(ip, new);
}

int ftrace_enable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);

	return ftrace_mod_jmp(ip, &ftrace_graph_caller);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	unsigned long ip = (unsigned long)(&ftrace_graph_call);

	return ftrace_mod_jmp(ip, &ftrace_stub);
}

#endif /* !CONFIG_DYNAMIC_FTRACE */

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long self_addr, unsigned long *parent,
			   unsigned long frame_pointer)
{
	unsigned long old;
	int faulted;
	struct ftrace_graph_ent trace;
	unsigned long return_hooker = (unsigned long)
				&return_to_handler;

	/*
	 * When resuming from suspend-to-ram, this function can be indirectly
	 * called from early CPU startup code while the CPU is in real mode,
	 * which would fail miserably.  Make sure the stack pointer is a
	 * virtual address.
	 *
	 * This check isn't as accurate as virt_addr_valid(), but it should be
	 * good enough for this purpose, and it's fast.
	 */
	if (unlikely((long)__builtin_frame_address(0) >= 0))
		return;

	if (unlikely(ftrace_graph_is_dead()))
		return;

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
				     frame_pointer, parent) == -EBUSY) {
		*parent = old;
		return;
	}
}
#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
