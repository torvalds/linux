#ifndef __ASM_SH_SYSTEM_32_H
#define __ASM_SH_SYSTEM_32_H

#include <linux/types.h>
#include <asm/mmu.h>

#ifdef CONFIG_SH_DSP

#define is_dsp_enabled(tsk)						\
	(!!(tsk->thread.dsp_status.status & SR_DSP))

#define __restore_dsp(tsk)						\
do {									\
	register u32 *__ts2 __asm__ ("r2") =				\
			(u32 *)&tsk->thread.dsp_status;			\
	__asm__ __volatile__ (						\
		".balign 4\n\t"						\
		"movs.l	@r2+, a0\n\t"					\
		"movs.l	@r2+, a1\n\t"					\
		"movs.l	@r2+, a0g\n\t"					\
		"movs.l	@r2+, a1g\n\t"					\
		"movs.l	@r2+, m0\n\t"					\
		"movs.l	@r2+, m1\n\t"					\
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
		"stc.l	mod, @-r2\n\t"					\
		"stc.l	re, @-r2\n\t"					\
		"stc.l	rs, @-r2\n\t"					\
		"sts.l	dsr, @-r2\n\t"					\
		"movs.l	y1, @-r2\n\t"					\
		"movs.l	y0, @-r2\n\t"					\
		"movs.l	x1, @-r2\n\t"					\
		"movs.l	x0, @-r2\n\t"					\
		"movs.l	m1, @-r2\n\t"					\
		"movs.l	m0, @-r2\n\t"					\
		"movs.l	a1g, @-r2\n\t"					\
		"movs.l	a0g, @-r2\n\t"					\
		"movs.l	a1, @-r2\n\t"					\
		"movs.l	a0, @-r2\n\t"					\
		: : "r" (__ts2));					\
} while (0)

#else

#define is_dsp_enabled(tsk)	(0)
#define __save_dsp(tsk)		do { } while (0)
#define __restore_dsp(tsk)	do { } while (0)
#endif

#if defined(CONFIG_CPU_SH4A)
#define __icbi(addr)	__asm__ __volatile__ ( "icbi @%0\n\t" : : "r" (addr))
#else
#define __icbi(addr)	mb()
#endif

#define __ocbp(addr)	__asm__ __volatile__ ( "ocbp @%0\n\t" : : "r" (addr))
#define __ocbi(addr)	__asm__ __volatile__ ( "ocbi @%0\n\t" : : "r" (addr))
#define __ocbwb(addr)	__asm__ __volatile__ ( "ocbwb @%0\n\t" : : "r" (addr))

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

static inline reg_size_t register_align(void *val)
{
	return (unsigned long)(signed long)val;
}

int handle_unaligned_access(insn_size_t instruction, struct pt_regs *regs,
			    struct mem_access *ma, int, unsigned long address);

static inline void trigger_address_error(void)
{
	__asm__ __volatile__ (
		"ldc %0, sr\n\t"
		"mov.l @%1, %0"
		:
		: "r" (0x10000000), "r" (0x80000001)
	);
}

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

static inline void set_bl_bit(void)
{
	unsigned long __dummy0, __dummy1;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"or	%2, %0\n\t"
		"and	%3, %0\n\t"
		"ldc	%0, sr\n\t"
		: "=&r" (__dummy0), "=r" (__dummy1)
		: "r" (0x10000000), "r" (0xffffff0f)
		: "memory"
	);
}

static inline void clear_bl_bit(void)
{
	unsigned long __dummy0, __dummy1;

	__asm__ __volatile__ (
		"stc	sr, %0\n\t"
		"and	%2, %0\n\t"
		"ldc	%0, sr\n\t"
		: "=&r" (__dummy0), "=r" (__dummy1)
		: "1" (~0x10000000)
		: "memory"
	);
}

#endif /* __ASM_SH_SYSTEM_32_H */
