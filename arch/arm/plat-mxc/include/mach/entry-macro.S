/*
 *  Copyright (C) 2007 Lennert Buytenhek <buytenh@wantstofly.org>
 *  Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#define AVIC_NIMASK	0x04

	@ this macro disables fast irq (not implemented)
	.macro	disable_fiq
	.endm

	.macro  get_irqnr_preamble, base, tmp
	ldr	\base, =AVIC_IO_ADDRESS(AVIC_BASE_ADDR)
#ifdef CONFIG_MXC_IRQ_PRIOR
	ldr	r4, [\base, #AVIC_NIMASK]
#endif
	.endm

	.macro  arch_ret_to_user, tmp1, tmp2
	.endm

	@ this macro checks which interrupt occured
	@ and returns its number in irqnr
	@ and returns if an interrupt occured in irqstat
	.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
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
	.endm

	@ irq priority table (not used)
	.macro	irq_prio_table
	.endm
