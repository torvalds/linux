// SPDX-License-Identifier: GPL-2.0

#include <linux/ftrace.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>

#ifndef CONFIG_DYNAMIC_FTRACE
extern void (*ftrace_trace_function)(unsigned long, unsigned long,
				     struct ftrace_ops*, struct pt_regs*);
extern void ftrace_graph_caller(void);

noinline void __naked ftrace_stub(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	__asm__ ("");  /* avoid to optimize as pure function */
}

noinline void _mcount(unsigned long parent_ip)
{
	/* save all state by the compiler prologue */

	unsigned long ip = (unsigned long)__builtin_return_address(0);

	if (ftrace_trace_function != ftrace_stub)
		ftrace_trace_function(ip - MCOUNT_INSN_SIZE, parent_ip,
				      NULL, NULL);

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	if (ftrace_graph_return != (trace_func_graph_ret_t)ftrace_stub
	    || ftrace_graph_entry != ftrace_graph_entry_stub)
		ftrace_graph_caller();
#endif

	/* restore all state by the compiler epilogue */
}
EXPORT_SYMBOL(_mcount);

#else /* CONFIG_DYNAMIC_FTRACE */

noinline void __naked ftrace_stub(unsigned long ip, unsigned long parent_ip,
				  struct ftrace_ops *op, struct ftrace_regs *fregs)
{
	__asm__ ("");  /* avoid to optimize as pure function */
}

noinline void __naked _mcount(unsigned long parent_ip)
{
	__asm__ ("");  /* avoid to optimize as pure function */
}
EXPORT_SYMBOL(_mcount);

#define XSTR(s) STR(s)
#define STR(s) #s
void _ftrace_caller(unsigned long parent_ip)
{
	/* save all state needed by the compiler prologue */

	/*
	 * prepare arguments for real tracing function
	 * first  arg : __builtin_return_address(0) - MCOUNT_INSN_SIZE
	 * second arg : parent_ip
	 */
	__asm__ __volatile__ (
		"move $r1, %0				   \n\t"
		"addi $r0, %1, #-" XSTR(MCOUNT_INSN_SIZE) "\n\t"
		:
		: "r" (parent_ip), "r" (__builtin_return_address(0)));

	/* a placeholder for the call to a real tracing function */
	__asm__ __volatile__ (
		"ftrace_call:		\n\t"
		"nop			\n\t"
		"nop			\n\t"
		"nop			\n\t");

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
	/* a placeholder for the call to ftrace_graph_caller */
	__asm__ __volatile__ (
		"ftrace_graph_call:	\n\t"
		"nop			\n\t"
		"nop			\n\t"
		"nop			\n\t");
#endif
	/* restore all state needed by the compiler epilogue */
}

int __init ftrace_dyn_arch_init(void)
{
	return 0;
}

static unsigned long gen_sethi_insn(unsigned long addr)
{
	unsigned long opcode = 0x46000000;
	unsigned long imm = addr >> 12;
	unsigned long rt_num = 0xf << 20;

	return ENDIAN_CONVERT(opcode | rt_num | imm);
}

static unsigned long gen_ori_insn(unsigned long addr)
{
	unsigned long opcode = 0x58000000;
	unsigned long imm = addr & 0x0000fff;
	unsigned long rt_num = 0xf << 20;
	unsigned long ra_num = 0xf << 15;

	return ENDIAN_CONVERT(opcode | rt_num | ra_num | imm);
}

static unsigned long gen_jral_insn(unsigned long addr)
{
	unsigned long opcode = 0x4a000001;
	unsigned long rt_num = 0x1e << 20;
	unsigned long rb_num = 0xf << 10;

	return ENDIAN_CONVERT(opcode | rt_num | rb_num);
}

static void ftrace_gen_call_insn(unsigned long *call_insns,
				 unsigned long addr)
{
	call_insns[0] = gen_sethi_insn(addr); /* sethi $r15, imm20u       */
	call_insns[1] = gen_ori_insn(addr);   /* ori   $r15, $r15, imm15u */
	call_insns[2] = gen_jral_insn(addr);  /* jral  $lp,  $r15         */
}

static int __ftrace_modify_code(unsigned long pc, unsigned long *old_insn,
				unsigned long *new_insn, bool validate)
{
	unsigned long orig_insn[3];

	if (validate) {
		if (copy_from_kernel_nofault(orig_insn, (void *)pc,
				MCOUNT_INSN_SIZE))
			return -EFAULT;
		if (memcmp(orig_insn, old_insn, MCOUNT_INSN_SIZE))
			return -EINVAL;
	}

	if (copy_to_kernel_nofault((void *)pc, new_insn, MCOUNT_INSN_SIZE))
		return -EPERM;

	return 0;
}

static int ftrace_modify_code(unsigned long pc, unsigned long *old_insn,
			      unsigned long *new_insn, bool validate)
{
	int ret;

	ret = __ftrace_modify_code(pc, old_insn, new_insn, validate);
	if (ret)
		return ret;

	flush_icache_range(pc, pc + MCOUNT_INSN_SIZE);

	return ret;
}

int ftrace_update_ftrace_func(ftrace_func_t func)
{
	unsigned long pc = (unsigned long)&ftrace_call;
	unsigned long old_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};
	unsigned long new_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};

	if (func != ftrace_stub)
		ftrace_gen_call_insn(new_insn, (unsigned long)func);

	return ftrace_modify_code(pc, old_insn, new_insn, false);
}

int ftrace_make_call(struct dyn_ftrace *rec, unsigned long addr)
{
	unsigned long pc = rec->ip;
	unsigned long nop_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};
	unsigned long call_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};

	ftrace_gen_call_insn(call_insn, addr);

	return ftrace_modify_code(pc, nop_insn, call_insn, true);
}

int ftrace_make_nop(struct module *mod, struct dyn_ftrace *rec,
		    unsigned long addr)
{
	unsigned long pc = rec->ip;
	unsigned long nop_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};
	unsigned long call_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};

	ftrace_gen_call_insn(call_insn, addr);

	return ftrace_modify_code(pc, call_insn, nop_insn, true);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#ifdef CONFIG_FUNCTION_GRAPH_TRACER
void prepare_ftrace_return(unsigned long *parent, unsigned long self_addr,
			   unsigned long frame_pointer)
{
	unsigned long return_hooker = (unsigned long)&return_to_handler;
	unsigned long old;

	if (unlikely(atomic_read(&current->tracing_graph_pause)))
		return;

	old = *parent;

	if (!function_graph_enter(old, self_addr, frame_pointer, NULL))
		*parent = return_hooker;
}

noinline void ftrace_graph_caller(void)
{
	unsigned long *parent_ip =
		(unsigned long *)(__builtin_frame_address(2) - 4);

	unsigned long selfpc =
		(unsigned long)(__builtin_return_address(1) - MCOUNT_INSN_SIZE);

	unsigned long frame_pointer =
		(unsigned long)__builtin_frame_address(3);

	prepare_ftrace_return(parent_ip, selfpc, frame_pointer);
}

extern unsigned long ftrace_return_to_handler(unsigned long frame_pointer);
void __naked return_to_handler(void)
{
	__asm__ __volatile__ (
		/* save state needed by the ABI     */
		"smw.adm $r0,[$sp],$r1,#0x0  \n\t"

		/* get original return address      */
		"move $r0, $fp               \n\t"
		"bal ftrace_return_to_handler\n\t"
		"move $lp, $r0               \n\t"

		/* restore state nedded by the ABI  */
		"lmw.bim $r0,[$sp],$r1,#0x0  \n\t");
}

#ifdef CONFIG_DYNAMIC_FTRACE
extern unsigned long ftrace_graph_call;

static int ftrace_modify_graph_caller(bool enable)
{
	unsigned long pc = (unsigned long)&ftrace_graph_call;
	unsigned long nop_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};
	unsigned long call_insn[3] = {INSN_NOP, INSN_NOP, INSN_NOP};

	ftrace_gen_call_insn(call_insn, (unsigned long)ftrace_graph_caller);

	if (enable)
		return ftrace_modify_code(pc, nop_insn, call_insn, true);
	else
		return ftrace_modify_code(pc, call_insn, nop_insn, true);
}

int ftrace_enable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(true);
}

int ftrace_disable_ftrace_graph_caller(void)
{
	return ftrace_modify_graph_caller(false);
}
#endif /* CONFIG_DYNAMIC_FTRACE */

#endif /* CONFIG_FUNCTION_GRAPH_TRACER */


#ifdef CONFIG_TRACE_IRQFLAGS
noinline void __trace_hardirqs_off(void)
{
	trace_hardirqs_off();
}
noinline void __trace_hardirqs_on(void)
{
	trace_hardirqs_on();
}
#endif /* CONFIG_TRACE_IRQFLAGS */
