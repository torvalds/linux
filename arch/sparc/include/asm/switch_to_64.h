#ifndef __SPARC64_SWITCH_TO_64_H
#define __SPARC64_SWITCH_TO_64_H

#include <asm/visasm.h>

#define prepare_arch_switch(next)		\
do {						\
	flushw_all();				\
} while (0)

	/* See what happens when you design the chip correctly?
	 *
	 * We tell gcc we clobber all non-fixed-usage registers except
	 * for l0/l1.  It will use one for 'next' and the other to hold
	 * the output value of 'last'.  'next' is not referenced again
	 * past the invocation of switch_to in the scheduler, so we need
	 * not preserve it's value.  Hairy, but it lets us remove 2 loads
	 * and 2 stores in this critical code path.  -DaveM
	 */
#define switch_to(prev, next, last)					\
do {	flush_tlb_pending();						\
	save_and_clear_fpu();						\
	/* If you are tempted to conditionalize the following */	\
	/* so that ASI is only written if it changes, think again. */	\
	__asm__ __volatile__("wr %%g0, %0, %%asi"			\
	: : "r" (task_thread_info(next)->current_ds));\
	trap_block[current_thread_info()->cpu].thread =			\
		task_thread_info(next);					\
	__asm__ __volatile__(						\
	"mov	%%g4, %%g7\n\t"						\
	"stx	%%i6, [%%sp + 2047 + 0x70]\n\t"				\
	"stx	%%i7, [%%sp + 2047 + 0x78]\n\t"				\
	"rdpr	%%wstate, %%o5\n\t"					\
	"stx	%%o6, [%%g6 + %6]\n\t"					\
	"stb	%%o5, [%%g6 + %5]\n\t"					\
	"rdpr	%%cwp, %%o5\n\t"					\
	"stb	%%o5, [%%g6 + %8]\n\t"					\
	"wrpr	%%g0, 15, %%pil\n\t"					\
	"mov	%4, %%g6\n\t"						\
	"ldub	[%4 + %8], %%g1\n\t"					\
	"wrpr	%%g1, %%cwp\n\t"					\
	"ldx	[%%g6 + %6], %%o6\n\t"					\
	"ldub	[%%g6 + %5], %%o5\n\t"					\
	"ldub	[%%g6 + %7], %%o7\n\t"					\
	"wrpr	%%o5, 0x0, %%wstate\n\t"				\
	"ldx	[%%sp + 2047 + 0x70], %%i6\n\t"				\
	"ldx	[%%sp + 2047 + 0x78], %%i7\n\t"				\
	"ldx	[%%g6 + %9], %%g4\n\t"					\
	"wrpr	%%g0, 14, %%pil\n\t"					\
	"brz,pt %%o7, switch_to_pc\n\t"					\
	" mov	%%g7, %0\n\t"						\
	"sethi	%%hi(ret_from_syscall), %%g1\n\t"			\
	"jmpl	%%g1 + %%lo(ret_from_syscall), %%g0\n\t"		\
	" nop\n\t"							\
	".globl switch_to_pc\n\t"					\
	"switch_to_pc:\n\t"						\
	: "=&r" (last), "=r" (current), "=r" (current_thread_info_reg),	\
	  "=r" (__local_per_cpu_offset)					\
	: "0" (task_thread_info(next)),					\
	  "i" (TI_WSTATE), "i" (TI_KSP), "i" (TI_NEW_CHILD),            \
	  "i" (TI_CWP), "i" (TI_TASK)					\
	: "cc",								\
	        "g1", "g2", "g3",                   "g7",		\
	        "l1", "l2", "l3", "l4", "l5", "l6", "l7",		\
	  "i0", "i1", "i2", "i3", "i4", "i5",				\
	  "o0", "o1", "o2", "o3", "o4", "o5",       "o7");		\
} while(0)

extern void synchronize_user_stack(void);
extern void fault_in_user_windows(void);

#endif /* __SPARC64_SWITCH_TO_64_H */
