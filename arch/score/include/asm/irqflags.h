#ifndef _ASM_SCORE_IRQFLAGS_H
#define _ASM_SCORE_IRQFLAGS_H

#ifndef __ASSEMBLY__

#define raw_local_irq_save(x)			\
{						\
	__asm__ __volatile__(			\
		"mfcr	r8, cr0;"		\
		"li	r9, 0xfffffffe;"	\
		"nop;"				\
		"mv	%0, r8;"		\
		"and	r8, r8, r9;"		\
		"mtcr	r8, cr0;"		\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		: "=r" (x)			\
		:				\
		: "r8", "r9"			\
		);				\
}

#define raw_local_irq_restore(x)		\
{						\
	__asm__ __volatile__(			\
		"mfcr	r8, cr0;"		\
		"ldi	r9, 0x1;"		\
		"and	%0, %0, r9;"		\
		"or	r8, r8, %0;"		\
		"mtcr	r8, cr0;"		\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		:				\
		: "r"(x)			\
		: "r8", "r9"			\
		);				\
}

#define raw_local_irq_enable(void)		\
{						\
	__asm__ __volatile__(			\
		"mfcr\tr8,cr0;"			\
		"nop;"				\
		"nop;"				\
		"ori\tr8,0x1;"			\
		"mtcr\tr8,cr0;"			\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		:				\
		:				\
		: "r8");			\
}

#define raw_local_irq_disable(void)		\
{						\
	__asm__ __volatile__(			\
		"mfcr\tr8,cr0;"			\
		"nop;"				\
		"nop;"				\
		"srli\tr8,r8,1;"		\
		"slli\tr8,r8,1;"		\
		"mtcr\tr8,cr0;"			\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		:				\
		:				\
		: "r8");			\
}

#define raw_local_save_flags(x)			\
{						\
	__asm__ __volatile__(			\
		"mfcr	r8, cr0;"		\
		"nop;"				\
		"nop;"				\
		"mv	%0, r8;"		\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"nop;"				\
		"ldi	r9, 0x1;"		\
		"and	%0, %0, r9;"		\
		: "=r" (x)			\
		:				\
		: "r8", "r9"			\
		);				\
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & 1);
}

#endif

#endif /* _ASM_SCORE_IRQFLAGS_H */
