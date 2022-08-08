/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_S390_STACKTRACE_H
#define _ASM_S390_STACKTRACE_H

#include <linux/uaccess.h>
#include <linux/ptrace.h>
#include <asm/switch_to.h>

enum stack_type {
	STACK_TYPE_UNKNOWN,
	STACK_TYPE_TASK,
	STACK_TYPE_IRQ,
	STACK_TYPE_NODAT,
	STACK_TYPE_RESTART,
	STACK_TYPE_MCCK,
};

struct stack_info {
	enum stack_type type;
	unsigned long begin, end;
};

const char *stack_type_name(enum stack_type type);
int get_stack_info(unsigned long sp, struct task_struct *task,
		   struct stack_info *info, unsigned long *visit_mask);

static inline bool on_stack(struct stack_info *info,
			    unsigned long addr, size_t len)
{
	if (info->type == STACK_TYPE_UNKNOWN)
		return false;
	if (addr + len < addr)
		return false;
	return addr >= info->begin && addr + len <= info->end;
}

static __always_inline unsigned long get_stack_pointer(struct task_struct *task,
						       struct pt_regs *regs)
{
	if (regs)
		return (unsigned long) kernel_stack_pointer(regs);
	if (task == current)
		return current_stack_pointer();
	return (unsigned long) task->thread.ksp;
}

/*
 * Stack layout of a C stack frame.
 */
#ifndef __PACK_STACK
struct stack_frame {
	unsigned long back_chain;
	unsigned long empty1[5];
	unsigned long gprs[10];
	unsigned int  empty2[8];
};
#else
struct stack_frame {
	unsigned long empty1[5];
	unsigned int  empty2[8];
	unsigned long gprs[10];
	unsigned long back_chain;
};
#endif

/*
 * Unlike current_stack_pointer() which simply returns current value of %r15
 * current_frame_address() returns function stack frame address, which matches
 * %r15 upon function invocation. It may differ from %r15 later if function
 * allocates stack for local variables or new stack frame to call other
 * functions.
 */
#define current_frame_address()						\
	((unsigned long)__builtin_frame_address(0) -			\
	 offsetof(struct stack_frame, back_chain))

#define CALL_ARGS_0()							\
	register unsigned long r2 asm("2")
#define CALL_ARGS_1(arg1)						\
	register unsigned long r2 asm("2") = (unsigned long)(arg1)
#define CALL_ARGS_2(arg1, arg2)						\
	CALL_ARGS_1(arg1);						\
	register unsigned long r3 asm("3") = (unsigned long)(arg2)
#define CALL_ARGS_3(arg1, arg2, arg3)					\
	CALL_ARGS_2(arg1, arg2);					\
	register unsigned long r4 asm("4") = (unsigned long)(arg3)
#define CALL_ARGS_4(arg1, arg2, arg3, arg4)				\
	CALL_ARGS_3(arg1, arg2, arg3);					\
	register unsigned long r4 asm("5") = (unsigned long)(arg4)
#define CALL_ARGS_5(arg1, arg2, arg3, arg4, arg5)			\
	CALL_ARGS_4(arg1, arg2, arg3, arg4);				\
	register unsigned long r4 asm("6") = (unsigned long)(arg5)

/*
 * To keep this simple mark register 2-6 as being changed (volatile)
 * by the called function, even though register 6 is saved/nonvolatile.
 */
#define CALL_FMT_0 "=&d" (r2)
#define CALL_FMT_1 "+&d" (r2)
#define CALL_FMT_2 CALL_FMT_1, "+&d" (r3)
#define CALL_FMT_3 CALL_FMT_2, "+&d" (r4)
#define CALL_FMT_4 CALL_FMT_3, "+&d" (r5)
#define CALL_FMT_5 CALL_FMT_4, "+&d" (r6)

#define CALL_CLOBBER_5 "0", "1", "14", "cc", "memory"
#define CALL_CLOBBER_4 CALL_CLOBBER_5
#define CALL_CLOBBER_3 CALL_CLOBBER_4, "5"
#define CALL_CLOBBER_2 CALL_CLOBBER_3, "4"
#define CALL_CLOBBER_1 CALL_CLOBBER_2, "3"
#define CALL_CLOBBER_0 CALL_CLOBBER_1

#define CALL_ON_STACK(fn, stack, nr, args...)				\
({									\
	unsigned long frame = current_frame_address();			\
	CALL_ARGS_##nr(args);						\
	unsigned long prev;						\
									\
	asm volatile(							\
		"	la	%[_prev],0(15)\n"			\
		"	lg	15,%[_stack]\n"				\
		"	stg	%[_frame],%[_bc](15)\n"			\
		"	brasl	14,%[_fn]\n"				\
		"	la	15,0(%[_prev])\n"			\
		: [_prev] "=&a" (prev), CALL_FMT_##nr			\
		: [_stack] "R" (stack),					\
		  [_bc] "i" (offsetof(struct stack_frame, back_chain)),	\
		  [_frame] "d" (frame),					\
		  [_fn] "X" (fn) : CALL_CLOBBER_##nr);			\
	r2;								\
})

#define CALL_ON_STACK_NORETURN(fn, stack)				\
({									\
	asm volatile(							\
		"	la	15,0(%[_stack])\n"			\
		"	xc	%[_bc](8,15),%[_bc](15)\n"		\
		"	brasl	14,%[_fn]\n"				\
		::[_bc] "i" (offsetof(struct stack_frame, back_chain)),	\
		  [_stack] "a" (stack), [_fn] "X" (fn));		\
	BUG();								\
})

#endif /* _ASM_S390_STACKTRACE_H */
