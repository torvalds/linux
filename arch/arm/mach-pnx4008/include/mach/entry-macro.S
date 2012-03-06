/*
 * arch/arm/mach-pnx4008/include/mach/entry-macro.S
 *
 * Low-level IRQ helper macros for PNX4008-based platforms
 *
 * 2005-2006 (c) MontaVista Software, Inc.
 * Author: Vitaly Wool <vwool@ru.mvista.com>
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "platform.h"

#define IO_BASE         0xF0000000
#define IO_ADDRESS(x)  (((((x) & 0xff000000) >> 4) | ((x) & 0xfffff)) | IO_BASE)

#define INTRC_MASK			0x00
#define INTRC_RAW_STAT			0x04
#define INTRC_STAT			0x08
#define INTRC_POLAR			0x0C
#define INTRC_ACT_TYPE			0x10
#define INTRC_TYPE			0x14

#define SIC1_BASE_INT   32
#define SIC2_BASE_INT   64

		.macro  get_irqnr_preamble, base, tmp
		.endm

		.macro	get_irqnr_and_base, irqnr, irqstat, base, tmp
/* decode the MIC interrupt numbers */
		ldr	\base, =IO_ADDRESS(PNX4008_INTCTRLMIC_BASE)
		ldr	\irqstat, [\base, #INTRC_STAT]

		cmp	\irqstat,#1<<16
		movhs	\irqnr,#16
		movlo	\irqnr,#0
		movhs	\irqstat,\irqstat,lsr#16
		cmp	\irqstat,#1<<8
		addhs	\irqnr,\irqnr,#8
		movhs	\irqstat,\irqstat,lsr#8
		cmp	\irqstat,#1<<4
		addhs	\irqnr,\irqnr,#4
		movhs	\irqstat,\irqstat,lsr#4
		cmp	\irqstat,#1<<2
		addhs	\irqnr,\irqnr,#2
		movhs	\irqstat,\irqstat,lsr#2
		cmp	\irqstat,#1<<1
		addhs	\irqnr,\irqnr,#1

/* was there an interrupt ? if not then drop out with EQ status */
		teq	\irqstat,#0
		beq	1003f

/* and now check for extended IRQ reasons */
		cmp	\irqnr,#1
		bls	1003f
		cmp	\irqnr,#30
		blo	1002f

/* IRQ 31,30  : High priority cascade IRQ handle */
/* read the correct SIC */
/* decoding status after compare : eq is 30 (SIC1) , ne is 31 (SIC2) */
/* set the base IRQ number */
		ldreq	\base, =IO_ADDRESS(PNX4008_INTCTRLSIC1_BASE)
		moveq  \irqnr,#SIC1_BASE_INT
		ldrne	\base, =IO_ADDRESS(PNX4008_INTCTRLSIC2_BASE)
		movne   \irqnr,#SIC2_BASE_INT
		ldr	\irqstat, [\base, #INTRC_STAT]
		ldr	\tmp,	  [\base, #INTRC_TYPE]
/* and with inverted mask : low priority interrupts  */
		and	\irqstat,\irqstat,\tmp
		b	1004f

1003:
/* IRQ 1,0  : Low priority cascade IRQ handle */
/* read the correct SIC */
/* decoding status after compare : eq is 1 (SIC2) , ne is 0 (SIC1)*/
/* read the correct SIC */
/* set the base IRQ number  */
		ldrne	\base, =IO_ADDRESS(PNX4008_INTCTRLSIC1_BASE)
		movne   \irqnr,#SIC1_BASE_INT
		ldreq	\base, =IO_ADDRESS(PNX4008_INTCTRLSIC2_BASE)
		moveq   \irqnr,#SIC2_BASE_INT
		ldr	\irqstat, [\base, #INTRC_STAT]
		ldr	\tmp,	  [\base, #INTRC_TYPE]
/* and with inverted mask : low priority interrupts  */
		bic	\irqstat,\irqstat,\tmp

1004:

		cmp	\irqstat,#1<<16
		addhs	\irqnr,\irqnr,#16
		movhs	\irqstat,\irqstat,lsr#16
		cmp	\irqstat,#1<<8
		addhs	\irqnr,\irqnr,#8
		movhs	\irqstat,\irqstat,lsr#8
		cmp	\irqstat,#1<<4
		addhs	\irqnr,\irqnr,#4
		movhs	\irqstat,\irqstat,lsr#4
		cmp	\irqstat,#1<<2
		addhs	\irqnr,\irqnr,#2
		movhs	\irqstat,\irqstat,lsr#2
		cmp	\irqstat,#1<<1
		addhs	\irqnr,\irqnr,#1


/* is irqstat not zero */

1002:
/* we assert that irqstat is not equal to zero and return ne status if true*/
		teq	\irqstat,#0
1003:
		.endm

