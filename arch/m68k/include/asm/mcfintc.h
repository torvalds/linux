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
 * Define the base address of the SIM within the MBAR address space.
 */
#define	MCFSIM_BASE		0x0		/* Base address within SIM */

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
 *	Bit definitions for the ICR family of registers.
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
 *	Bit definitions for the Interrupt Mask register (IMR).
 */
#define	MCFSIM_IMR_EINT1	0x0002		/* External intr # 1 */
#define	MCFSIM_IMR_EINT2	0x0004		/* External intr # 2 */
#define	MCFSIM_IMR_EINT3	0x0008		/* External intr # 3 */
#define	MCFSIM_IMR_EINT4	0x0010		/* External intr # 4 */
#define	MCFSIM_IMR_EINT5	0x0020		/* External intr # 5 */
#define	MCFSIM_IMR_EINT6	0x0040		/* External intr # 6 */
#define	MCFSIM_IMR_EINT7	0x0080		/* External intr # 7 */

#define	MCFSIM_IMR_SWD		0x0100		/* Software Watchdog intr */
#define	MCFSIM_IMR_TIMER1	0x0200		/* TIMER 1 intr */
#define	MCFSIM_IMR_TIMER2	0x0400		/* TIMER 2 intr */
#define MCFSIM_IMR_MBUS		0x0800		/* MBUS intr	*/
#define	MCFSIM_IMR_UART1	0x1000		/* UART 1 intr */
#define	MCFSIM_IMR_UART2	0x2000		/* UART 2 intr */

#if defined(CONFIG_M5206e)
#define	MCFSIM_IMR_DMA1		0x4000		/* DMA 1 intr */
#define	MCFSIM_IMR_DMA2		0x8000		/* DMA 2 intr */
#elif defined(CONFIG_M5249) || defined(CONFIG_M5307)
#define	MCFSIM_IMR_DMA0		0x4000		/* DMA 0 intr */
#define	MCFSIM_IMR_DMA1		0x8000		/* DMA 1 intr */
#define	MCFSIM_IMR_DMA2		0x10000		/* DMA 2 intr */
#define	MCFSIM_IMR_DMA3		0x20000		/* DMA 3 intr */
#endif

/*
 *	Mask for all of the SIM devices. Some parts have more or less
 *	SIM devices. This is a catchall for the sandard set.
 */
#ifndef MCFSIM_IMR_MASKALL
#define	MCFSIM_IMR_MASKALL	0x3ffe		/* All intr sources */
#endif


/****************************************************************************/
#endif	/* mcfintc_h */
