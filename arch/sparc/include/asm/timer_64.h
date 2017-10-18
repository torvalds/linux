/* timer.h: System timer definitions for sun5.
 *
 * Copyright (C) 1997, 2008 David S. Miller (davem@davemloft.net)
 */

#ifndef _SPARC64_TIMER_H
#define _SPARC64_TIMER_H

#include <linux/types.h>
#include <linux/init.h>

/* The most frequently accessed fields should be first,
 * to fit into the same cacheline.
 */
struct sparc64_tick_ops {
	unsigned long ticks_per_nsec_quotient;
	unsigned long offset;
	unsigned long long (*get_tick)(void);
	int (*add_compare)(unsigned long);
	unsigned long softint_mask;
	void (*disable_irq)(void);

	void (*init_tick)(void);
	unsigned long (*add_tick)(unsigned long);
	unsigned long (*get_frequency)(void);
	unsigned long frequency;

	char *name;
};

extern struct sparc64_tick_ops *tick_ops;

unsigned long sparc64_get_clock_tick(unsigned int cpu);
void setup_sparc64_timer(void);
void __init time_init(void);

#define TICK_PRIV_BIT		BIT(63)
#define TICKCMP_IRQ_BIT		BIT(63)

#define HBIRD_STICKCMP_ADDR	0x1fe0000f060UL
#define HBIRD_STICK_ADDR	0x1fe0000f070UL

#define GET_TICK_NINSTR		13
struct get_tick_patch {
	unsigned int	addr;
	unsigned int	tick[GET_TICK_NINSTR];
	unsigned int	stick[GET_TICK_NINSTR];
};

extern struct get_tick_patch __get_tick_patch;
extern struct get_tick_patch __get_tick_patch_end;

static inline unsigned long get_tick(void)
{
	unsigned long tick, tmp1, tmp2;

	__asm__ __volatile__(
	/* read hbtick 13 instructions */
	"661:\n"
	"	mov	0x1fe, %1\n"
	"	sllx	%1, 0x20, %1\n"
	"	sethi	%%hi(0xf000), %2\n"
	"	or	%2, 0x70, %2\n"
	"	or	%1, %2, %1\n"	/* %1 = HBIRD_STICK_ADDR */
	"	add	%1, 8, %2\n"
	"	ldxa	[%2]%3, %0\n"
	"	ldxa	[%1]%3, %1\n"
	"	ldxa	[%2]%3, %2\n"
	"	sub	%2, %0, %0\n"	/* don't modify %xcc */
	"	brnz,pn	%0, 661b\n"	/* restart to save one register */
	"	 sllx	%2, 32, %2\n"
	"	or	%2, %1, %0\n"
	/* Common/not patched code */
	"	sllx	%0, 1, %0\n"
	"	srlx	%0, 1, %0\n"	/* Clear TICK_PRIV_BIT */
	/* Beginning of patch section */
	"	.section .get_tick_patch, \"ax\"\n"
	"	.word	661b\n"
	/* read tick 2 instructions and 11 skipped */
	"	ba	1f\n"
	"	 rd	%%tick, %0\n"
	"	.skip	4 * (%4 - 2)\n"
	"1:\n"
	/* read stick 2 instructions and 11 skipped */
	"	ba	1f\n"
	"	 rd	%%asr24, %0\n"
	"	.skip	4 * (%4 - 2)\n"
	"1:\n"
	/* End of patch section */
	"	.previous\n"
	: "=&r" (tick), "=&r" (tmp1), "=&r" (tmp2)
	: "i" (ASI_PHYS_BYPASS_EC_E), "i" (GET_TICK_NINSTR));

	return tick;
}

#endif /* _SPARC64_TIMER_H */
