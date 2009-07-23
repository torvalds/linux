/*
 * arch/arm/mach-w90x900/include/mach/irqs.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * Based on arch/arm/mach-s3c2410/include/mach/irqs.h
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

/*
 * we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 *
 */

#define W90X900_IRQ(x)	(x)

/* Main cpu interrupts */

#define IRQ_WDT		W90X900_IRQ(1)
#define IRQ_GROUP0	W90X900_IRQ(2)
#define IRQ_GROUP1	W90X900_IRQ(3)
#define IRQ_ACTL	W90X900_IRQ(4)
#define IRQ_LCD		W90X900_IRQ(5)
#define IRQ_RTC		W90X900_IRQ(6)
#define IRQ_UART0	W90X900_IRQ(7)
#define IRQ_UART1	W90X900_IRQ(8)
#define IRQ_UART2	W90X900_IRQ(9)
#define IRQ_UART3	W90X900_IRQ(10)
#define IRQ_UART4	W90X900_IRQ(11)
#define IRQ_TIMER0	W90X900_IRQ(12)
#define IRQ_TIMER1	W90X900_IRQ(13)
#define IRQ_T_INT_GROUP	W90X900_IRQ(14)
#define IRQ_USBH	W90X900_IRQ(15)
#define IRQ_EMCTX	W90X900_IRQ(16)
#define IRQ_EMCRX	W90X900_IRQ(17)
#define IRQ_GDMAGROUP	W90X900_IRQ(18)
#define IRQ_DMAC	W90X900_IRQ(19)
#define IRQ_FMI		W90X900_IRQ(20)
#define IRQ_USBD	W90X900_IRQ(21)
#define IRQ_ATAPI	W90X900_IRQ(22)
#define IRQ_G2D		W90X900_IRQ(23)
#define IRQ_PCI		W90X900_IRQ(24)
#define IRQ_SCGROUP	W90X900_IRQ(25)
#define IRQ_I2CGROUP	W90X900_IRQ(26)
#define IRQ_SSP		W90X900_IRQ(27)
#define IRQ_PWM		W90X900_IRQ(28)
#define IRQ_KPI		W90X900_IRQ(29)
#define IRQ_P2SGROUP	W90X900_IRQ(30)
#define IRQ_ADC		W90X900_IRQ(31)
#define NR_IRQS		(IRQ_ADC+1)

/*for irq group*/

#define	IRQ_PS2_PORT0	0x10000000
#define	IRQ_PS2_PORT1	0x20000000
#define	IRQ_I2C_LINE0	0x04000000
#define	IRQ_I2C_LINE1	0x08000000
#define	IRQ_SC_CARD0	0x01000000
#define	IRQ_SC_CARD1	0x02000000
#define	IRQ_GDMA_CH0	0x00100000
#define	IRQ_GDMA_CH1	0x00200000
#define	IRQ_TIMER2	0x00010000
#define	IRQ_TIMER3	0x00020000
#define	IRQ_TIMER4	0x00040000
#define	IRQ_GROUP0_IRQ0	0x00000001
#define	IRQ_GROUP0_IRQ1	0x00000002
#define	IRQ_GROUP0_IRQ2	0x00000004
#define	IRQ_GROUP0_IRQ3	0x00000008
#define	IRQ_GROUP1_IRQ4	0x00000010
#define	IRQ_GROUP1_IRQ5	0x00000020
#define	IRQ_GROUP1_IRQ6	0x00000040
#define	IRQ_GROUP1_IRQ7	0x00000080

#endif /* __ASM_ARCH_IRQ_H */
