/*
 * arch/arm/mach-nuc93x/include/mach/irqs.h
 *
 * Copyright (c) 2008 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H

#define NUC93X_IRQ(x)	(x)

/* Main cpu interrupts */

#define IRQ_WDT		NUC93X_IRQ(1)
#define IRQ_IRQ0	NUC93X_IRQ(2)
#define IRQ_IRQ1	NUC93X_IRQ(3)
#define IRQ_IRQ2	NUC93X_IRQ(4)
#define IRQ_IRQ3	NUC93X_IRQ(5)
#define IRQ_USBH	NUC93X_IRQ(6)
#define IRQ_APU		NUC93X_IRQ(7)
#define IRQ_VPOST	NUC93X_IRQ(8)
#define IRQ_ADC		NUC93X_IRQ(9)
#define IRQ_UART0	NUC93X_IRQ(10)
#define IRQ_TIMER0	NUC93X_IRQ(11)
#define IRQ_GPU0	NUC93X_IRQ(12)
#define IRQ_GPU1	NUC93X_IRQ(13)
#define IRQ_GPU2	NUC93X_IRQ(14)
#define IRQ_GPU3	NUC93X_IRQ(15)
#define IRQ_GPU4	NUC93X_IRQ(16)
#define IRQ_VIN		NUC93X_IRQ(17)
#define IRQ_USBD	NUC93X_IRQ(18)
#define IRQ_VRAMLD	NUC93X_IRQ(19)
#define IRQ_GDMA0	NUC93X_IRQ(20)
#define IRQ_GDMA1	NUC93X_IRQ(21)
#define IRQ_SDIO	NUC93X_IRQ(22)
#define IRQ_FMI		NUC93X_IRQ(22)
#define IRQ_JPEG	NUC93X_IRQ(23)
#define IRQ_SPI0	NUC93X_IRQ(24)
#define IRQ_SPI1	NUC93X_IRQ(25)
#define IRQ_RTC		NUC93X_IRQ(26)
#define IRQ_PWM0	NUC93X_IRQ(27)
#define IRQ_PWM1	NUC93X_IRQ(28)
#define IRQ_PWM2	NUC93X_IRQ(29)
#define IRQ_PWM3	NUC93X_IRQ(30)
#define IRQ_I2SAC97	NUC93X_IRQ(31)
#define IRQ_CAP0	IRQ_PWM0
#define IRQ_CAP1	IRQ_PWM1
#define IRQ_CAP2	IRQ_PWM2
#define IRQ_CAP3	IRQ_PWM3
#define NR_IRQS		(IRQ_I2SAC97 + 1)

#endif /* __ASM_ARCH_IRQ_H */
