/* SPDX-License-Identifier: GPL-2.0 */
/****************************************************************************/

/*
 *	mcfintc.h -- support definitions for the simple ColdFire
 *		     Interrupt Controller
 *
 * 	(C) Copyright 2009,  Greg Ungerer <gerg@uclinux.org>
 */

/****************************************************************************/
#ifndef	mcfintc_h
#define	mcfintc_h
/****************************************************************************/

/*
 * Most of the older ColdFire parts use the same simple interrupt
 * controller. This is currently used on the 5206, 5206e, 5249, 5307
 * and 5407 parts.
 *
 * The builtin peripherals are masked through dedicated bits in the
 * Interrupt Mask register (IMR) - and this is not indexed (or in any way
 * related to) the actual interrupt number they use. So knowing the IRQ
 * number doesn't explicitly map to a certain internal device for
 * interrupt control purposes.
 */

/*
 * Bit definitions for the ICR family of registers.
 */
#define	MCFSIM_ICR_AUTOVEC	0x80		/* Auto-vectored intr */
#define	MCFSIM_ICR_LEVEL0	0x00		/* Level 0 intr */
#define	MCFSIM_ICR_LEVEL1	0x04		/* Level 1 intr */
#define	MCFSIM_ICR_LEVEL2	0x08		/* Level 2 intr */
#define	MCFSIM_ICR_LEVEL3	0x0c		/* Level 3 intr */
#define	MCFSIM_ICR_LEVEL4	0x10		/* Level 4 intr */
#define	MCFSIM_ICR_LEVEL5	0x14		/* Level 5 intr */
#define	MCFSIM_ICR_LEVEL6	0x18		/* Level 6 intr */
#define	MCFSIM_ICR_LEVEL7	0x1c		/* Level 7 intr */

#define	MCFSIM_ICR_PRI0		0x00		/* Priority 0 intr */
#define	MCFSIM_ICR_PRI1		0x01		/* Priority 1 intr */
#define	MCFSIM_ICR_PRI2		0x02		/* Priority 2 intr */
#define	MCFSIM_ICR_PRI3		0x03		/* Priority 3 intr */

/*
 * IMR bit position definitions. Not all ColdFire parts with this interrupt
 * controller actually support all of these interrupt sources. But the bit
 * numbers are the same in all cores.
 */
#define	MCFINTC_EINT1		1		/* External int #1 */
#define	MCFINTC_EINT2		2		/* External int #2 */
#define	MCFINTC_EINT3		3		/* External int #3 */
#define	MCFINTC_EINT4		4		/* External int #4 */
#define	MCFINTC_EINT5		5		/* External int #5 */
#define	MCFINTC_EINT6		6		/* External int #6 */
#define	MCFINTC_EINT7		7		/* External int #7 */
#define	MCFINTC_SWT		8		/* Software Watchdog */
#define	MCFINTC_TIMER1		9
#define	MCFINTC_TIMER2		10
#define	MCFINTC_I2C		11		/* I2C / MBUS */
#define	MCFINTC_UART0		12
#define	MCFINTC_UART1		13
#define	MCFINTC_DMA0		14
#define	MCFINTC_DMA1		15
#define	MCFINTC_DMA2		16
#define	MCFINTC_DMA3		17
#define	MCFINTC_QSPI		18

#ifndef __ASSEMBLER__

/*
 * There is no one-is-one correspondance between the interrupt number (irq)
 * and the bit fields on the mask register. So we create a per-cpu type
 * mapping of irq to mask bit. The CPU platform code needs to register
 * its supported irq's at init time, using this function.
 */
extern unsigned char mcf_irq2imr[];
static inline void mcf_mapirq2imr(int irq, int imr)
{
	mcf_irq2imr[irq] = imr;
}

void mcf_autovector(int irq);
void mcf_setimr(int index);
void mcf_clrimr(int index);
#endif

/****************************************************************************/
#endif	/* mcfintc_h */
