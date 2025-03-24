// SPDX-License-Identifier: GPL-2.0
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
#include <linux/memory.h>
#include <linux/vmalloc.h>
#include <linux/set_memory.h>
#include <linux/execmem.h>

#include <trace/syscall.h>

#include <asm/kprobes.h>
#include <asm/ftrace.h>
#include <asm/nops.h>
#include <asm/text-patching.h>

#ifdef CONFIG_DYNAMIC_FTRACE

static int ftrace_poke_late = 0;

void ftrace_arch_code_modify_prepare(void)
    __acquires(&text_mutex)
{
	/*
	 * Need to grab text_mutex to prevent a race from module loading
	 * and live kernel patching from changing the text permissions while
	 * ftrace has it set to "read/write".
	 */
	mutex_lock(&text_mutex);
	ftrace_poke_late = 1;
}

void ftrace_arch_code_modify_post_process(void)
    __releases(&text_mutex)
{
	/*
	 * ftrace_make_{call,nop}() may be called during
	 * module load, and we need to finish the text_poke_queue()
	 * that they do, here.
	 */
	text_poke_finish();
	ftrace_poke_late = 0;
	mutex_unlock(&text_mutex);
}

static const char *ftrace_nop_replace(void)
{
	return x86_nops[5];
}

static const char *ftrace_call_replace(unsigned long ip, unsigned long addr)
{
	/*
	 * No need to translate into a callthunk. The trampoline does
	 * the depth accounting itself.
	 */
	return text_gen_insn(CALL_INSN_OPCODE, (void *)ip, (void *)addr);
}

static int ftrace_verify_code(unsigned long ip, const char *old_code)
{
	char cur_code[MCOUNT_INSN_SIZE];

	/*
	 * Note:
	 * We are paranoid about modifying text, as if a bug was to happen, it
	 * could cause us to read or write to someplace that could cause harm.
	 * Carefully read and modify the code with probe_kernel_*(), and make
	 * sure what we read is what we expected it to be before modifying it.
	 */
	/* read the text we want to modify */
	if (copy_from_kernel_nofault(cur_code, (void *)ip, MCOUNT_INSN_SIZE)) {
		WARN_ON(1);
		return -EFAULT;
	}

	/* Make sure it is what we expect it to be */
	if (memcmp(cur_code, old_code, MCOUNT_INSN_SIZE) != 0) {
		ftrace_expected = old_code;
		WARN_ON(1);
		return -EINVAL;
	}

	return 0;
}

/*
 * Marked __ref because it calls text_poke_early() which is .init.text. That is
 * ok because that call will happen early, during boot, when .init sections are
 * still present.
 */
static int __ref
ftrace_modify_code_direct(unsigned long ip, const char *old_code,
			  const char *new_code)
{
	int ret = ftrace_verify_code(ip, old_code);
	if (ret)
		return ret;

	/* replace the text with the new text */
	if (ftrace_poke_late) {
		text_poke_queue((void *)ip, new_code, MCOUNT_INSN_SIZE, NULL);
	} else {
		mutex_lock(&text_mutex);
		text_poke((void *)ip, new_code, MCOUNT_INSN_SIZE);
		mutex_unlock(&text_mutex);
	}
	return 0;
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	const char *new, *old;

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
		return ftrace_modify_code_direct(ip, old, new);

	/*
	 * x86 overrides ftrace_replace_code -- this function will never be used
	 * in this case.
	 */
	WARN_ONCE(1, "invalid use of ftrace_make_nop");
	return -EINVAL;
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long ip = rec->ip;
	const char *new, *old;

	old = ftrace_nop_replace();
	new = ftrace_call_replace(ip, addr);

	/* Should only be called when module is loaded */
	return ftrace_modify_code_direct(rec->ip, old, new);
}

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
	unsigned long ip;
	const char *new;

	ip = (unsigned long)(&ftrace_call);
	new = ftrace_call_replace(ip, (unsigned long)func);
	text_poke_bp((void *)ip, new, MCOUNT_INSN_SIZE, NULL);

	ip = (unsigned long)(&ftrace_regs_call);
	new = ftrace_call_replace(ip, (unsigned long)func);
	text_poke_bp((void *)ip, new, MCOUNT_INSN_SIZE, NULL);

	return 0;
}

void ftrace_replace_code(int enable)
{
	struct ftrace_rec_iter *iter;
	struct dyn_ftrace *rec;
	const char *new, *old;
	int ret;

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		switch (ftrace_test_record(rec, enable)) {
		case FTRACE_UPDATE_IGNORE:
		default:
			continue;

		case FTRACE_UPDATE_MAKE_CALL:
			old = ftrace_nop_replace();
			break;

		case FTRACE_UPDATE_MODIFY_CALL:
		case FTRACE_UPDATE_MAKE_NOP:
			old = ftrace_call_replace(rec->ip, ftrace_get_addr_curr(rec));
			break;
		}

		ret = ftrace_verify_code(rec->ip, old);
		if (ret) {
			ftrace_expected = old;
			ftrace_bug(ret, rec);
			ftrace_expected = NULL;
			return;
		}
	}

	for_ftrace_rec_iter(iter) {
		rec = ftrace_rec_iter_record(iter);

		switch (ftrace_test_record(rec, enable)) {
		case FTRACE_UPDATE_IGNORE:
		default:
			continue;

		case FTRACE_UPDATE_MAKE_CALL:
		case FTRACE_UPDATE_MODIFY_CALL:
			new = ftrace_call_replace(rec->ip, ftrace_get_addr_new(rec));
			break;

		case FTRACE_UPDATE_MAKE_NOP:
			new = ftrace_nop_replace();
			break;
		}

		text_poke_queue((void *)rec->ip, new, MCOUNT_INSN_SIZE, NULL);
		ftrace_update_record(rec, enable);
	}
	text_poke_finish();
}

void arch_ftrace_update_code(int command)
{
	ftrace_modify_all_code(command);
}

/* Currently only x86_64 supports dynamic trampolines */
#ifdef CONFIG_X86_64

static inline void *alloc_tramp(unsigned long size)
{
	return execmem_alloc(EXECMEM_FTRACE, size);
}
static inline void tramp_free(void *tramp)
{
	execmem_free(tramp);
}

/* Defined as markers to the end of the ftrace default trampolines */
extern void ftrace_regs_caller_end(void);
extern void ftrace_caller_end(void);
extern void ftrace_caller_op_ptr(void);
extern void ftrace_regs_caller_op_ptr(void);
extern void ftrace_regs_caller_jmp(void);

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

#define RET_SIZE \
	(IS_ENABLED(CONFIG_MITIGATION_RETPOLINE) ? 5 : 1 + IS_ENABLED(CONFIG_MITIGATION_SLS))

static unsigned long
create_trampoline(struct ftrace_ops *ops, unsigned int *tramp_size)
{
	unsigned long start_offset;
	unsigned long end_offset;
	unsigned long op_offset;
	unsigned long call_offset;
	unsigned long jmp_offset;
	unsigned long offset;
	unsigned long npages;
	unsigned long size;
	unsigned long *ptr;
	void *trampoline;
	void *ip, *dest;
	/* 48 8b 15 <offset> is movq <offset>(%rip), %rdx */
	unsigned const char op_ref[] = { 0x48, 0x8b, 0x15 };
	unsigned const char retq[] = { RET_INSN_OPCODE, INT3_INSN_OPCODE };
	union ftrace_op_code_union op_ptr;
	void *ret;

	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
		start_offset = (unsigned long)ftrace_regs_caller;
		end_offset = (unsigned long)ftrace_regs_caller_end;
		op_offset = (unsigned long)ftrace_regs_caller_op_ptr;
		call_offset = (unsigned long)ftrace_regs_call;
		jmp_offset = (unsigned long)ftrace_regs_caller_jmp;
	} else {
		start_offset = (unsigned long)ftrace_caller;
		end_offset = (unsigned long)ftrace_caller_end;
		op_offset = (unsigned long)ftrace_caller_op_ptr;
		call_offset = (unsigned long)ftrace_call;
		jmp_offset = 0;
	}

	size = end_offset - start_offset;

	/*
	 * Allocate enough size to store the ftrace_caller code,
	 * the iret , as well as the address of the ftrace_ops this
	 * trampoline is used for.
	 */
	trampoline = alloc_tramp(size + RET_SIZE + sizeof(void *));
	if (!trampoline)
		return 0;

	*tramp_size = size + RET_SIZE + sizeof(void *);
	npages = DIV_ROUND_UP(*tramp_size, PAGE_SIZE);

	/* Copy ftrace_caller onto the trampoline memory */
	ret = text_poke_copy(trampoline, (void *)start_offset, size);
	if (WARN_ON(!ret))
		goto fail;

	ip = trampoline + size;
	if (cpu_feature_enabled(X86_FEATURE_RETHUNK))
		__text_gen_insn(ip, JMP32_INSN_OPCODE, ip, x86_return_thunk, JMP32_INSN_SIZE);
	else
		text_poke_copy(ip, retq, sizeof(retq));

	/* No need to test direct calls on created trampolines */
	if (ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
		/* NOP the jnz 1f; but make sure it's a 2 byte jnz */
		ip = trampoline + (jmp_offset - start_offset);
		if (WARN_ON(*(char *)ip != 0x75))
			goto fail;
		if (!text_poke_copy(ip, x86_nops[2], 2))
			goto fail;
	}

	/*
	 * The address of the ftrace_ops that is used for this trampoline
	 * is stored at the end of the trampoline. This will be used to
	 * load the third parameter for the callback. Basically, that
	 * location at the end of the trampoline takes the place of
	 * the global function_trace_op variable.
	 */

	ptr = (unsigned long *)(trampoline + size + RET_SIZE);
	text_poke_copy(ptr, &ops, sizeof(unsigned long));

	op_offset -= start_offset;
	memcpy(&op_ptr, trampoline + op_offset, OP_REF_SIZE);

	/* Are we pointing to the reference? */
	if (WARN_ON(memcmp(op_ptr.op, op_ref, 3) != 0))
		goto fail;

	/* Load the contents of ptr into the callback parameter */
	offset = (unsigned long)ptr;
	offset -= (unsigned long)trampoline + op_offset + OP_REF_SIZE;

	op_ptr.offset = offset;

	/* put in the new offset to the ftrace_ops */
	text_poke_copy(trampoline + op_offset, &op_ptr, OP_REF_SIZE);

	/* put in the call to the function */
	mutex_lock(&text_mutex);
	call_offset -= start_offset;
	/*
	 * No need to translate into a callthunk. The trampoline does
	 * the depth accounting before the call already.
	 */
	dest = ftrace_ops_get_func(ops);
	text_poke_copy_locked(trampoline + call_offset,
	      text_gen_insn(CALL_INSN_OPCODE, trampoline + call_offset, dest),
	      CALL_INSN_SIZE, false);
	mutex_unlock(&text_mutex);

	/* ALLOC_TRAMP flags lets us know we created it */
	ops->flags |= FTRACE_OPS_FL_ALLOC_TRAMP;

	set_memory_rox((unsigned long)trampoline, npages);
	return (unsigned long)trampoline;
fail:
	tramp_free(trampoline);
	return 0;
}

void set_ftrace_ops_ro(void)
{
	struct ftrace_ops *ops;
	unsigned long start_offset;
	unsigned long end_offset;
	unsigned long npages;
	unsigned long size;

	do_for_each_ftrace_op(ops, ftrace_ops_list) {
		if (!(ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP))
			continue;

		if (ops->flags & FTRACE_OPS_FL_SAVE_REGS) {
			start_offset = (unsigned long)ftrace_regs_caller;
			end_offset = (unsigned long)ftrace_regs_caller_end;
		} else {
			start_offset = (unsigned long)ftrace_caller;
			end_offset = (unsigned long)ftrace_caller_end;
		}
		size = end_offset - start_offset;
		size = size + RET_SIZE + sizeof(void *);
		npages = DIV_ROUND_UP(size, PAGE_SIZE);
		set_memory_ro((unsigned long)ops->trampoline, npages);
	} while_for_each_ftrace_op(ops);
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
	unsigned long offset;
	unsigned long ip;
	unsigned int size;
	const char *new;

	if (!ops->trampoline) {
		ops->trampoline = create_trampoline(ops, &size);
		if (!ops->trampoline)
			return;
		ops->trampoline_size = size;
		return;
	}

	/*
	 * The ftrace_ops caller may set up its own trampoline.
	 * In such a case, this code must not modify it.
	 */
	if (!(ops->flags & FTRACE_OPS_FL_ALLOC_TRAMP))
		return;

	offset = calc_trampoline_call_offset(ops->flags & FTRACE_OPS_FL_SAVE_REGS);
	ip = ops->trampoline + offset;
	func = ftrace_ops_get_func(ops);

	mutex_lock(&text_mutex);
	/* Do a safe modify in case the trampoline is executing */
	new = ftrace_call_replace(ip, (unsigned long)func);
	text_poke_bp((void *)ip, new, MCOUNT_INSN_SIZE, NULL);
	mutex_unlock(&text_mutex);
}

/* Return the address of the function the trampoline calls */
static void *addr_from_call(void *ptr)
{
	union text_poke_insn call;
	int ret;

	ret = copy_from_kernel_nofault(&call, ptr, CALL_INSN_SIZE);
	if (WARN_ON_ONCE(ret < 0))
		return NULL;

	/* Make sure this is a call */
	if (WARN_ON_ONCE(call.opcode != CALL_INSN_OPCODE)) {
		pr_warn("Expected E8, got %x\n", call.opcode);
		return NULL;
	}

	return ptr + CALL_INSN_SIZE + call.disp;
}

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
#if !defined(CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS) && \
	defined(CONFIG_FUNCTION_GRAPH_TRACER)
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

#if defined(CONFIG_DYNAMIC_FTRACE) && !defined(CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS)
extern void ftrace_graph_call(void);
static const char *ftrace_jmp_replace(unsigned long ip, unsigned long addr)
{
	return text_gen_insn(JMP32_INSN_OPCODE, (void *)ip, (void *)addr);
}

static int ftrace_mod_jmp(unsigned long ip, void *func)
{
	const char *new;

	new = ftrace_jmp_replace(ip, (unsigned long)func);
	text_poke_bp((void *)ip, new, MCOUNT_INSN_SIZE, NULL);
	return 0;
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
#endif /* CONFIG_DYNAMIC_FTRACE && !CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS */

static inline bool skip_ftrace_return(void)
{
	/*
	 * When resuming from suspend-to-ram, this function can be indirectly
	 * called from early CPU startup code while the CPU is in real mode,
	 * which would fail miserably.  Make sure the stack pointer is a
	 * virtual address.
	 *
	 * This check isn't as accurate as virt_addr_valid(), but it should be
	 * good enough for this purpose, and it's fast.
	 */
	if ((long)__builtin_frame_address(0) >= 0)
		return true;

	if (ftrace_graph_is_dead())
		return true;

	if (atomic_read(&current->tracing_graph_pause))
		return true;
	return false;
}

/*
 * Hook the return address and push it in the stack of return addrs
 * in current thread info.
 */
void prepare_ftrace_return(unsigned long ip, unsigned long *parent,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;

	if (unlikely(skip_ftrace_return()))
		return;

	if (!function_graph_enter(*parent, ip, frame_pointer, parent))
		*parent = return_hooker;
}

#ifdef CONFIG_HAVE_DYNAMIC_FTRACE_WITH_ARGS
void ftrace_graph_func(unsigned long ip, unsigned long parent_ip,
		       struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	struct pt_regs *regs = &arch_ftrace_regs(fregs)->regs;
	unsigned long *stack = (unsigned long *)kernel_stack_pointer(regs);
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long *parent = (unsigned long *)stack;

	if (unlikely(skip_ftrace_return()))
		return;


	if (!function_graph_enter_regs(*parent, ip, 0, parent, fregs))
		*parent = return_hooker;
}
#endif

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */
