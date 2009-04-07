#ifndef __ASM_SH_SYSTEM_32_H
#define __ASM_SH_SYSTEM_32_H

#include <linux/types.h>

#ifdef CONFIG_SH_DSP

#define is_dsp_enabled(tsk)						\
	(!!(tsk->thread.dsp_status.status & SR_DSP))

#define __restore_dsp(tsk)						\
do {									\
	register u32 *__ts2 __asm__ ("r2") =				\
			(u32 *)&tsk->thread.dsp_status;			\
	__asm__ __volatile__ (						\
		".balign 4\n\t"						\
		"movs.l	@r2+, a1\n\t"					\
		"movs.l	@r2+, a0g\n\t"					\
		"movs.l	@r2+, a1g\n\t"					\
		"movs.l	@r2+, m0\n\t"					\
		"movs.l	@r2+, m1\n\t"					\
		"movs.l	@r2+, a0\n\t"					\
		"movs.l	@r2+, x0\n\t"					\
		"movs.l	@r2+, x1\n\t"					\
		"movs.l	@r2+, y0\n\t"					\
		"movs.l	@r2+, y1\n\t"					\
		"lds.l	@r2+, dsr\n\t"					\
		"ldc.l	@r2+, rs\n\t"					\
		"ldc.l	@r2+, re\n\t"					\
		"ldc.l	@r2+, mod\n\t"					\
		: : "r" (__ts2));					\
} while (0)


#define __save_dsp(tsk)							\
do {									\
	register u32 *__ts2 __asm__ ("r2") =				\
			(u32 *)&tsk->thread.dsp_status + 14;		\
									\
	__asm__ __volatile__ (						\
		".balign 4\n\t"						\
		"stc.l	mod, @-r2\n\t"				\
		"stc.l	re, @-r2\n\t"					\
		"stc.l	rs, @-r2\n\t"					\
		"sts.l	dsr, @-r2\n\t"				\
		"sts.l	y1, @-r2\n\t"					\
		"sts.l	y0, @-r2\n\t"					\
		"sts.l	x1, @-r2\n\t"					\
		"sts.l	x0, @-r2\n\t"					\
		"sts.l	a0, @-r2\n\t"					\
		".word	0xf653		! movs.l	a1, @-r2\n\t"	\
		".word	0xf6f3		! movs.l	a0g, @-r2\n\t"	\
		".word	0xf6d3		! movs.l	a1g, @-r2\n\t"	\
		".word	0xf6c3		! movs.l        m0, @-r2\n\t"	\
		".word	0xf6e3		! movs.l        m1, @-r2\n\t"	\
		: : "r" (__ts2));					\
} while (0)

#else

#define is_dsp_enabled(tsk)	(0)
#define __save_dsp(tsk)		do { } while (0)
#define __restore_dsp(tsk)	do { } while (0)
#endif

struct task_struct *__switch_to(struct task_struct *prev,
				struct task_struct *next);

/*
 *	switch_to() should switch tasks to task nr n, first
 */
#define switch_to(prev, next, last)				\
do {								\
	register u32 *__ts1 __asm__ ("r1");			\
	register u32 *__ts2 __asm__ ("r2");			\
	register u32 *__ts4 __asm__ ("r4");			\
	register u32 *__ts5 __asm__ ("r5");			\
	register u32 *__ts6 __asm__ ("r6");			\
	register u32 __ts7 __asm__ ("r7");			\
	struct task_struct *__last;				\
								\
	if (is_dsp_enabled(prev))				\
		__save_dsp(prev);				\
								\
	__ts1 = (u32 *)&prev->thread.sp;			\
	__ts2 = (u32 *)&prev->thread.pc;			\
	__ts4 = (u32 *)prev;					\
	__ts5 = (u32 *)next;					\
	__ts6 = (u32 *)&next->thread.sp;			\
	__ts7 = next->thread.pc;				\
								\
	__asm__ __volatile__ (					\
		".balign 4\n\t"					\
		"stc.l	gbr, @-r15\n\t"				\
		"sts.l	pr, @-r15\n\t"				\
		"mov.l	r8, @-r15\n\t"				\
		"mov.l	r9, @-r15\n\t"				\
		"mov.l	r10, @-r15\n\t"				\
		"mov.l	r11, @-r15\n\t"				\
		"mov.l	r12, @-r15\n\t"				\
		"mov.l	r13, @-r15\n\t"				\
		"mov.l	r14, @-r15\n\t"				\
		"mov.l	r15, @r1\t! save SP\n\t"		\
		"mov.l	@r6, r15\t! change to new stack\n\t"	\
		"mova	1f, %0\n\t"				\
		"mov.l	%0, @r2\t! save PC\n\t"			\
		"mov.l	2f, %0\n\t"				\
		"jmp	@%0\t! call __switch_to\n\t"		\
		" lds	r7, pr\t!  with return to new PC\n\t"	\
		".balign	4\n"				\
		"2:\n\t"					\
		".long	__switch_to\n"				\
		"1:\n\t"					\
		"mov.l	@r15+, r14\n\t"				\
		"mov.l	@r15+, r13\n\t"				\
		"mov.l	@r15+, r12\n\t"				\
		"mov.l	@r15+, r11\n\t"				\
		"mov.l	@r15+, r10\n\t"				\
		"mov.l	@r15+, r9\n\t"				\
		"mov.l	@r15+, r8\n\t"				\
		"lds.l	@r15+, pr\n\t"				\
		"ldc.l	@r15+, gbr\n\t"				\
		: "=z" (__last)					\
		: "r" (__ts1), "r" (__ts2), "r" (__ts4),	\
		  "r" (__ts5), "r" (__ts6), "r" (__ts7)		\
		: "r3", "t");					\
								\
	last = __last;						\
} while (0)

#define finish_arch_switch(prev)				\
do {								\
	if (is_dsp_enabled(prev))				\
		__restore_dsp(prev);				\
} while (0)

#define __uses_jump_to_uncached \
	noinline __attribute__ ((__section__ (".uncached.text")))

/*
 * Jump to uncached area.
 * When handling TLB or caches, we need to do it from an uncached area.
 */
#define jump_to_uncached()			\
do {						\
	unsigned long __dummy;			\
						\
	__asm__ __volatile__(			\
		"mova	1f, %0\n\t"		\
		"add	%1, %0\n\t"		\
		"jmp	@%0\n\t"		\
		" nop\n\t"			\
		".balign 4\n"			\
		"1:"				\
		: "=&z" (__dummy)		\
		: "r" (cached_to_uncached));	\
} while (0)

/*
 * Back to cached area.
 */
#define back_to_cached()				\
do {							\
	unsigned long __dummy;				\
	ctrl_barrier();					\
	__asm__ __volatile__(				\
		"mov.l	1f, %0\n\t"			\
		"jmp	@%0\n\t"			\
		" nop\n\t"				\
		".balign 4\n"				\
		"1:	.long 2f\n"			\
		"2:"					\
		: "=&r" (__dummy));			\
} while (0)

#ifdef CONFIG_CPU_HAS_SR_RB
#define lookup_exception_vector()	\
({					\
	unsigned long _vec;		\
					\
	__asm__ __volatile__ (		\
		"stc r2_bank, %0\n\t"	\
		: "=r" (_vec)		\
	);				\
					\
	_vec;				\
})
#else
#define lookup_exception_vector()	\
({					\
	unsigned long _vec;		\
	__asm__ __volatile__ (		\
		"mov r4, %0\n\t"	\
		: "=r" (_vec)		\
	);				\
					\
	_vec;				\
})
#endif

int handle_unaligned_access(opcode_t instruction, struct pt_regs *regs,
			    struct mem_access *ma);

asmlinkage void do_address_error(struct pt_regs *regs,
				 unsigned long writeaccess,
				 unsigned long address);
asmlinkage void do_divide_error(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs);
asmlinkage void do_reserved_inst(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs);
asmlinkage void do_illegal_slot_inst(unsigned long r4, unsigned long r5,
				unsigned long r6, unsigned long r7,
				struct pt_regs __regs);
asmlinkage void do_exception_error(unsigned long r4, unsigned long r5,
				   unsigned long r6, unsigned long r7,
				   struct pt_regs __regs);

#endif /* __ASM_SH_SYSTEM_32_H */
