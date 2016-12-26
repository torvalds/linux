/*
 * Copyright (C) 1995-2004 Russell King
 *
 * Delay routines, using a pre-computed "loops_per_second" value.
 */
#ifndef __ASM_ARM_DELAY_H
#define __ASM_ARM_DELAY_H

#include <asm/memory.h>
#include <asm/param.h>	/* HZ */

#define MAX_UDELAY_MS	2
#define UDELAY_MULT	UL(2147 * HZ + 483648 * HZ / 1000000)
#define UDELAY_SHIFT	31

#ifndef __ASSEMBLY__

struct delay_timer {
	unsigned long (*read_current_timer)(void);
	unsigned long freq;
};

extern struct arm_delay_ops {
	void (*delay)(unsigned long);
	void (*const_udelay)(unsigned long);
	void (*udelay)(unsigned long);
	unsigned long ticks_per_jiffy;
} arm_delay_ops;

#define __delay(n)		arm_delay_ops.delay(n)

/*
 * This function intentionally does not exist; if you see references to
 * it, it means that you're calling udelay() with an out of range value.
 *
 * With currently imposed limits, this means that we support a max delay
 * of 2000us. Further limits: HZ<=1000
 */
extern void __bad_udelay(void);

/*
 * division by multiplication: you don't have to worry about
 * loss of precision.
 *
 * Use only for very small delays ( < 2 msec).  Should probably use a
 * lookup table, really, as the multiplications take much too long with
 * short delays.  This is a "reasonable" implementation, though (and the
 * first constant multiplications gets optimized away if the delay is
 * a constant)
 */
#define __udelay(n)		arm_delay_ops.udelay(n)
#define __const_udelay(n)	arm_delay_ops.const_udelay(n)

#define udelay(n)							\
	(__builtin_constant_p(n) ?					\
	  ((n) > (MAX_UDELAY_MS * 1000) ? __bad_udelay() :		\
			__const_udelay((n) * UDELAY_MULT)) :		\
	  __udelay(n))

/* Loop-based definitions for assembly code. */
extern void __loop_delay(unsigned long loops);
extern void __loop_udelay(unsigned long usecs);
extern void __loop_const_udelay(unsigned long);

/* Delay-loop timer registration. */
#define ARCH_HAS_READ_CURRENT_TIMER
extern void register_current_timer_delay(const struct delay_timer *timer);

#endif /* __ASSEMBLY__ */

#endif /* defined(_ARM_DELAY_H) */

