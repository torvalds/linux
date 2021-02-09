/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_IRQ_STACK_H
#define _ASM_X86_IRQ_STACK_H

#include <linux/ptrace.h>

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
 *     For builds with CONFIG_UNWIND_FRAME_POINTER ASM_CALL_CONSTRAINT is
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
#define call_on_irqstack(func, asm_call, argconstr...)			\
{									\
	register void *tos asm("r11");					\
									\
	tos = ((void *)__this_cpu_read(hardirq_stack_ptr));		\
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

/* Macros to assert type correctness for run_*_on_irqstack macros */
#define assert_function_type(func, proto)				\
	static_assert(__builtin_types_compatible_p(typeof(&func), proto))

#define assert_arg_type(arg, proto)					\
	static_assert(__builtin_types_compatible_p(typeof(arg), proto))

static __always_inline bool irqstack_active(void)
{
	return __this_cpu_read(hardirq_stack_inuse);
}

void asm_call_on_stack(void *sp, void (*func)(void), void *arg);
void asm_call_sysvec_on_stack(void *sp, void (*func)(struct pt_regs *regs),
			      struct pt_regs *regs);
void asm_call_irq_on_stack(void *sp, void (*func)(struct irq_desc *desc),
			   struct irq_desc *desc);

static __always_inline void __run_on_irqstack(void (*func)(void))
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_write(hardirq_stack_inuse, true);
	asm_call_on_stack(tos, func, NULL);
	__this_cpu_write(hardirq_stack_inuse, false);
}

static __always_inline void
__run_sysvec_on_irqstack(void (*func)(struct pt_regs *regs),
			 struct pt_regs *regs)
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_write(hardirq_stack_inuse, true);
	asm_call_sysvec_on_stack(tos, func, regs);
	__this_cpu_write(hardirq_stack_inuse, false);
}

static __always_inline void
__run_irq_on_irqstack(void (*func)(struct irq_desc *desc),
		      struct irq_desc *desc)
{
	void *tos = __this_cpu_read(hardirq_stack_ptr);

	__this_cpu_write(hardirq_stack_inuse, true);
	asm_call_irq_on_stack(tos, func, desc);
	__this_cpu_write(hardirq_stack_inuse, false);
}

#else /* CONFIG_X86_64 */
static inline bool irqstack_active(void) { return false; }
static inline void __run_on_irqstack(void (*func)(void)) { }
static inline void __run_sysvec_on_irqstack(void (*func)(struct pt_regs *regs),
					    struct pt_regs *regs) { }
static inline void __run_irq_on_irqstack(void (*func)(struct irq_desc *desc),
					 struct irq_desc *desc) { }
#endif /* !CONFIG_X86_64 */

static __always_inline bool irq_needs_irq_stack(struct pt_regs *regs)
{
	if (IS_ENABLED(CONFIG_X86_32))
		return false;
	if (!regs)
		return !irqstack_active();
	return !user_mode(regs) && !irqstack_active();
}


static __always_inline void run_on_irqstack_cond(void (*func)(void),
						 struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_on_irqstack(func);
	else
		func();
}

static __always_inline void
run_sysvec_on_irqstack_cond(void (*func)(struct pt_regs *regs),
			    struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_sysvec_on_irqstack(func, regs);
	else
		func(regs);
}

static __always_inline void
run_irq_on_irqstack_cond(void (*func)(struct irq_desc *desc), struct irq_desc *desc,
			 struct pt_regs *regs)
{
	lockdep_assert_irqs_disabled();

	if (irq_needs_irq_stack(regs))
		__run_irq_on_irqstack(func, desc);
	else
		func(desc);
}

#endif
