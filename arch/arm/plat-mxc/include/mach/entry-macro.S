/*
 *  Copyright (C) 2007 Lennert Buytenhek <buytenh@wantstofly.org>
 *  Copyright 2004-2009 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/hardware.h>

#define AVIC_NIMASK	0x04

	@ this macro disables fast irq (not implemented)
	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
#ifndef CONFIG_MXC_TZIC
	ldr	\base, =avic_base
	ldr	\base, [\base]
#ifdef CONFIG_MXC_IRQ_PRIOR
	ldr	r4, [\base, #AVIC_NIMASK]
#endif
#elif defined CONFIG_MXC_TZIC
	ldr	\base, =tzic_base
	ldr	\base, [\base]
#endif /* CONFIG_MXC_TZIC */
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	@ this macro checks which interrupt occured
	@ and returns its number in irqnr
	@ and returns if an interrupt occured in irqstat
	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
#ifndef CONFIG_MXC_TZIC
	@ Load offset & priority of the highest priority
	@ interrupt pending from AVIC_NIVECSR
	ldr	\irqstat, [\base, #0x40]
	@ Shift to get the decoded IRQ number, using ASR so
	@ 'no interrupt pending' becomes 0xffffffff
	mov	\irqnr, \irqstat, asr #16
	@ set zero flag if IRQ + 1 == 0
	adds	\tmp, \irqnr, #1
#ifdef CONFIG_MXC_IRQ_PRIOR
	bicne	\tmp, \irqstat, #0xFFFFFFE0
	strne	\tmp, [\base, #AVIC_NIMASK]
	streq	r4, [\base, #AVIC_NIMASK]
#endif
#elif defined CONFIG_MXC_TZIC
	@ Load offset & priority of the highest priority
	@ interrupt pending.
	@ 0xD80 is HIPND0 register
	mov     \irqnr, #0
	mov     \irqstat, #0x0D80
1000:
	ldr     \tmp,   [\irqstat, \base]
	cmp     \tmp, #0
	bne     1001f
	addeq   \irqnr, \irqnr, #32
	addeq   \irqstat, \irqstat, #4
	cmp     \irqnr, #128
	blo     1000b
	b       2001f
1001:	mov     \irqstat, #1
1002:	tst     \tmp, \irqstat
	bne     2002f
	movs    \tmp, \tmp, lsr #1
	addne   \irqnr, \irqnr, #1
	bne     1002b
2001:
	mov  \irqnr, #0
2002:
	movs \irqnr, \irqnr
#endif
	.endm

	@ irq priority table (not used)
	.macro	irq_prio_table
	.endm
