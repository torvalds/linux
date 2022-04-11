/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IRQ_STACK_H
#define _ASM_X86_IRQ_STACK_H

#include <linux/ptrace.h>
#include <linux/objtool.h>

#include <asm/processor.h>

#ifdef CONFIG_X86_64

/*
 * Macro to inline switching to an interrupt stack and invoking function
 * calls from there. The following rules apply:
 *
 * - Ordering:
 *
 *   1. Write the stack pointer into the top most place of the irq
 *	stack. This ensures that the various unwinders can link back to the
 *	original stack.
 *
 *   2. Switch the stack pointer to the top of the irq stack.
 *
 *   3. Invoke whatever needs to be done (@asm_call argument)
 *
 *   4. Pop the original stack pointer from the top of the irq stack
 *	which brings it back to the original stack where it left off.
 *
 * - Function invocation:
 *
 *   To allow flexible usage of the macro, the actual function code including
 *   the store of the arguments in the call ABI registers is handed in via
 *   the @asm_call argument.
 *
 * - Local variables:
 *
 *   @tos:
 *	The @tos variable holds a pointer to the top of the irq stack and
 *	_must_ be allocated in a non-callee saved register as this is a
 *	restriction coming from objtool.
 *
 *	Note, that (tos) is both in input and output constraints to ensure
 *	that the compiler does not assume that R11 is left untouched in
 *	case this macro is used in some place where the per cpu interrupt
 *	stack pointer is used again afterwards
 *
 * - Function arguments:
 *	The function argument(s), if any, have to be defined in register
 *	variables at the place where this is invoked. Storing the
 *	argument(s) in the proper register(s) is part of the @asm_call
 *
 * - Constraints:
 *
 *   The constraints have to be done very carefully because the compiler
 *   does not know about the assembly call.
 *
 *   output:
 *     As documented already above the @tos variable is required to be in
 *     the output constraints to make the compiler aware that R11 cannot be
 *     reused after the asm() statement.
 *
 *     For builds with CONFIG_UNWINDER_FRAME_POINTER, ASM_CALL_CONSTRAINT is
 *     required as well as this prevents certain creative GCC variants from
 *     misplacing the ASM code.
 *
 *  input:
 *    - func:
 *	  Immediate, which tells the compiler that the function is referenced.
 *
 *    - tos:
 *	  Register. The actual register is defined by the variable declaration.
 *
 *    - function arguments:
 *	  The constraints are handed in via the 'argconstr' argument list. They
 *	  describe the register arguments which are used in @asm_call.
 *
 *  clobbers:
 *     Function calls can clobber anything except the callee-saved
 *     registers. Tell the compiler.
 */
#define call_on_stack(stack, func, asm_call, argconstr...)		\
{									\
	register void *tos asm("r11");					\
									\
	tos = ((void *)(stack));					\
									\
	asm_inline volatile(						\
	"movq	%%rsp, (%[tos])				\n"		\
	"movq	%[tos], %%rsp				\n"		\
									\
	asm_call							\
									\
	"popq	%%rsp					\n"		\
									\
	: "+r" (tos), ASM_CALL_CONSTRAINT				\
	: [__func] "i" (func), [tos] "r" (tos) argconstr		\
	: "cc", "rax", "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10",	\
	  "memory"							\
	);								\
}

#define ASM_CALL_ARG0							\
	"call %P[__func]				\n"		\
	ASM_REACHABLE

#define ASM_CALL_ARG1							\
	"movq	%[arg1], %%rdi				\n"		\
	ASM_CALL_ARG0

#define ASM_CALL_ARG2							\
	"movq	%[arg2], %%rsi				\n"		\
	ASM_CALL_ARG1

#define ASM_CALL_ARG3							\
	"movq	%[arg3], %%rdx				\n"		\
	ASM_CALL_ARG2

#define call_on_irqstack(func, asm_call, argconstr...)			\
	call_on_stack(__this_cpu_read(hardirq_stack_ptr),		\
		      func, asm_call, argconstr)

/* Macros to assert type correctness for run_*_on_irqstack macros */
#define assert_function_type(func, proto)				\
	static_assert(__builtin_types_compatible_p(typeof(&func), proto))

#define assert_arg_type(arg, proto)					\
	static_assert(__builtin_types_compatible_p(typeof(arg), proto))

/*
 * Macro to invoke system vector and device interrupt C handlers.
 */
#define call_on_irqstack_cond(func, regs, asm_call, constr, c_args...)	\
{									\
	/*								\
	 * User mode entry and interrupt on the irq stack do not	\
	 * switch stacks. If from user mode the task stack is empty.	\
	 */								\
	if (user_mode(regs) || __this_cpu_read(hardirq_stack_inuse)) {	\
		irq_enter_rcu();					\
		func(c_args);						\
		irq_exit_rcu();						\
	} else {							\
		/*							\
		 * Mark the irq stack inuse _before_ and unmark _after_	\
		 * switching stacks. Interrupts are disabled in both	\
		 * places. Invoke the stack switch macro with the call	\
		 * sequence which matches the above direct invocation.	\
		 */							\
		__this_cpu_write(hardirq_stack_inuse, true);		\
		call_on_irqstack(func, asm_call, constr);		\
		__this_cpu_write(hardirq_stack_inuse, false);		\
	}								\
}

/*
 * Function call sequence for __call_on_irqstack() for system vectors.
 *
 * Note that irq_enter_rcu() and irq_exit_rcu() do not use the input
 * mechanism because these functions are global and cannot be optimized out
 * when compiling a particular source file which uses one of these macros.
 *
 * The argument (regs) does not need to be pushed or stashed in a callee
 * saved register to be safe vs. the irq_enter_rcu() call because the
 * clobbers already prevent the compiler from storing it in a callee
 * clobbered register. As the compiler has to preserve @regs for the final
 * call to idtentry_exit() anyway, it's likely that it does not cause extra
 * effort for this asm magic.
 */
#define ASM_CALL_SYSVEC							\
	"call irq_enter_rcu				\n"		\
	ASM_CALL_ARG1							\
	"call irq_exit_rcu				\n"

#define SYSVEC_CONSTRAINTS	, [arg1] "r" (regs)

#define run_sysvec_on_irqstack_cond(func, regs)				\
{									\
	assert_function_type(func, void (*)(struct pt_regs *));		\
	assert_arg_type(regs, struct pt_regs *);			\
									\
	call_on_irqstack_cond(func, regs, ASM_CALL_SYSVEC,		\
			      SYSVEC_CONSTRAINTS, regs);		\
}

/*
 * As in ASM_CALL_SYSVEC above the clobbers force the compiler to store
 * @regs and @vector in callee saved registers.
 */
#define ASM_CALL_IRQ							\
	"call irq_enter_rcu				\n"		\
	ASM_CALL_ARG2							\
	"call irq_exit_rcu				\n"

#define IRQ_CONSTRAINTS	, [arg1] "r" (regs), [arg2] "r" ((unsigned long)vector)

#define run_irq_on_irqstack_cond(func, regs, vector)			\
{									\
	assert_function_type(func, void (*)(struct pt_regs *, u32));	\
	assert_arg_type(regs, struct pt_regs *);			\
	assert_arg_type(vector, u32);					\
									\
	call_on_irqstack_cond(func, regs, ASM_CALL_IRQ,			\
			      IRQ_CONSTRAINTS, regs, vector);		\
}

#ifndef CONFIG_PREEMPT_RT
/*
 * Macro to invoke __do_softirq on the irq stack. This is only called from
 * task context when bottom halves are about to be reenabled and soft
 * interrupts are pending to be processed. The interrupt stack cannot be in
 * use here.
 */
#define do_softirq_own_stack()						\
{									\
	__this_cpu_write(hardirq_stack_inuse, true);			\
	call_on_irqstack(__do_softirq, ASM_CALL_ARG0);			\
	__this_cpu_write(hardirq_stack_inuse, false);			\
}

#endif

#else /* CONFIG_X86_64 */
/* System vector handlers always run on the stack they interrupted. */
#define run_sysvec_on_irqstack_cond(func, regs)				\
{									\
	irq_enter_rcu();						\
	func(regs);							\
	irq_exit_rcu();							\
}

/* Switches to the irq stack within func() */
#define run_irq_on_irqstack_cond(func, regs, vector)			\
{									\
	irq_enter_rcu();						\
	func(regs, vector);						\
	irq_exit_rcu();							\
}

#endif /* !CONFIG_X86_64 */

#endif
