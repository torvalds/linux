/*
 * arch/arm/mach-h720x/include/mach/irqs.h
 *
 * Copyright (C) 2000 Jungjun Kim
 *           (C) 2003 Robert Schwebel <r.schwebel@pengutronix.de>
 *           (C) 2003 Thomas Gleixner <tglx@linutronix.de>
 *
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#if defined (CONFIG_CPU_H7201)

#define IRQ_PMU		0		/* 0x000001 */
#define IRQ_DMA		1 		/* 0x000002 */
#define IRQ_LCD		2		/* 0x000004 */
#define IRQ_VGA		3 		/* 0x000008 */
#define IRQ_PCMCIA1 	4 		/* 0x000010 */
#define IRQ_PCMCIA2 	5 		/* 0x000020 */
#define IRQ_AFE		6 		/* 0x000040 */
#define IRQ_AIC		7 		/* 0x000080 */
#define IRQ_KEYBOARD 	8 		/* 0x000100 */
#define IRQ_TIMER0	9 		/* 0x000200 */
#define IRQ_RTC		10		/* 0x000400 */
#define IRQ_SOUND	11		/* 0x000800 */
#define IRQ_USB		12		/* 0x001000 */
#define IRQ_IrDA 	13		/* 0x002000 */
#define IRQ_UART0	14		/* 0x004000 */
#define IRQ_UART1	15		/* 0x008000 */
#define IRQ_SPI		16		/* 0x010000 */
#define IRQ_GPIOA 	17		/* 0x020000 */
#define IRQ_GPIOB	18		/* 0x040000 */
#define IRQ_GPIOC	19		/* 0x080000 */
#define IRQ_GPIOD	20		/* 0x100000 */
#define IRQ_CommRX	21		/* 0x200000 */
#define IRQ_CommTX	22		/* 0x400000 */
#define IRQ_Soft	23		/* 0x800000 */

#define NR_GLBL_IRQS	24

#define IRQ_CHAINED_GPIOA(x)  (NR_GLBL_IRQS + x)
#define IRQ_CHAINED_GPIOB(x)  (IRQ_CHAINED_GPIOA(32) + x)
#define IRQ_CHAINED_GPIOC(x)  (IRQ_CHAINED_GPIOB(32) + x)
#define IRQ_CHAINED_GPIOD(x)  (IRQ_CHAINED_GPIOC(32) + x)
#define NR_IRQS               IRQ_CHAINED_GPIOD(32)

/* Enable mask for multiplexed interrupts */
#define IRQ_ENA_MUX	(1<<IRQ_GPIOA) | (1<<IRQ_GPIOB) \
			| (1<<IRQ_GPIOC) | (1<<IRQ_GPIOD)


#elif defined (CONFIG_CPU_H7202)

#define IRQ_PMU		0		/* 0x00000001 */
#define IRQ_DMA		1		/* 0x00000002 */
#define IRQ_LCD		2		/* 0x00000004 */
#define IRQ_SOUND	3		/* 0x00000008 */
#define IRQ_I2S		4		/* 0x00000010 */
#define IRQ_USB 	5		/* 0x00000020 */
#define IRQ_MMC 	6		/* 0x00000040 */
#define IRQ_RTC 	7		/* 0x00000080 */
#define IRQ_UART0 	8		/* 0x00000100 */
#define IRQ_UART1 	9		/* 0x00000200 */
#define IRQ_UART2 	10		/* 0x00000400 */
#define IRQ_UART3 	11		/* 0x00000800 */
#define IRQ_KBD 	12		/* 0x00001000 */
#define IRQ_PS2 	13		/* 0x00002000 */
#define IRQ_AIC 	14		/* 0x00004000 */
#define IRQ_TIMER0 	15		/* 0x00008000 */
#define IRQ_TIMERX 	16		/* 0x00010000 */
#define IRQ_WDT 	17		/* 0x00020000 */
#define IRQ_CAN0 	18		/* 0x00040000 */
#define IRQ_CAN1 	19		/* 0x00080000 */
#define IRQ_EXT0 	20		/* 0x00100000 */
#define IRQ_EXT1 	21		/* 0x00200000 */
#define IRQ_GPIOA 	22		/* 0x00400000 */
#define IRQ_GPIOB 	23		/* 0x00800000 */
#define IRQ_GPIOC 	24		/* 0x01000000 */
#define IRQ_GPIOD 	25		/* 0x02000000 */
#define IRQ_GPIOE 	26		/* 0x04000000 */
#define IRQ_COMMRX 	27		/* 0x08000000 */
#define IRQ_COMMTX 	28		/* 0x10000000 */
#define IRQ_SMC 	29		/* 0x20000000 */
#define IRQ_Soft 	30		/* 0x40000000 */
#define IRQ_RESERVED1 	31		/* 0x80000000 */
#define NR_GLBL_IRQS	32

#define NR_TIMERX_IRQS	3

#define IRQ_CHAINED_GPIOA(x)  (NR_GLBL_IRQS + x)
#define IRQ_CHAINED_GPIOB(x)  (IRQ_CHAINED_GPIOA(32) + x)
#define IRQ_CHAINED_GPIOC(x)  (IRQ_CHAINED_GPIOB(32) + x)
#define IRQ_CHAINED_GPIOD(x)  (IRQ_CHAINED_GPIOC(32) + x)
#define IRQ_CHAINED_GPIOE(x)  (IRQ_CHAINED_GPIOD(32) + x)
#define IRQ_CHAINED_TIMERX(x) (IRQ_CHAINED_GPIOE(32) + x)
#define IRQ_TIMER1            (IRQ_CHAINED_TIMERX(0))
#define IRQ_TIMER2            (IRQ_CHAINED_TIMERX(1))
#define IRQ_TIMER64B          (IRQ_CHAINED_TIMERX(2))

#define NR_IRQS		(IRQ_CHAINED_TIMERX(NR_TIMERX_IRQS))

/* Enable mask for multiplexed interrupts */
#define IRQ_ENA_MUX	(1<<IRQ_TIMERX) | (1<<IRQ_GPIOA) | (1<<IRQ_GPIOB) | \
			(1<<IRQ_GPIOC) 	| (1<<IRQ_GPIOD) | (1<<IRQ_GPIOE) | \
			(1<<IRQ_TIMERX)

#else
#error cpu definition mismatch
#endif

/* decode irq number to register number */
#define IRQ_TO_REGNO(irq) ((irq - NR_GLBL_IRQS) >> 5)
#define IRQ_TO_BIT(irq) (1 << ((irq - NR_GLBL_IRQS) % 32))

#endif
