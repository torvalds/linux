/* linux/arch/arm/mach-exynos4/include/mach/irqs.h
 *
 * Copyright (c) 2010-2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - IRQ definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H __FILE__

#include <plat/irqs.h>

/* PPI: Private Peripheral Interrupt */

#define IRQ_PPI(x)		S5P_IRQ(x+16)

/* SPI: Shared Peripheral Interrupt */

#define IRQ_SPI(x)		S5P_IRQ(x+32)

#define IRQ_EINT0		IRQ_SPI(16)
#define IRQ_EINT1		IRQ_SPI(17)
#define IRQ_EINT2		IRQ_SPI(18)
#define IRQ_EINT3		IRQ_SPI(19)
#define IRQ_EINT4		IRQ_SPI(20)
#define IRQ_EINT5		IRQ_SPI(21)
#define IRQ_EINT6		IRQ_SPI(22)
#define IRQ_EINT7		IRQ_SPI(23)
#define IRQ_EINT8		IRQ_SPI(24)
#define IRQ_EINT9		IRQ_SPI(25)
#define IRQ_EINT10		IRQ_SPI(26)
#define IRQ_EINT11		IRQ_SPI(27)
#define IRQ_EINT12		IRQ_SPI(28)
#define IRQ_EINT13		IRQ_SPI(29)
#define IRQ_EINT14		IRQ_SPI(30)
#define IRQ_EINT15		IRQ_SPI(31)
#define IRQ_EINT16_31		IRQ_SPI(32)

#define IRQ_PDMA0		IRQ_SPI(35)
#define IRQ_PDMA1		IRQ_SPI(36)
#define IRQ_TIMER0_VIC		IRQ_SPI(37)
#define IRQ_TIMER1_VIC		IRQ_SPI(38)
#define IRQ_TIMER2_VIC		IRQ_SPI(39)
#define IRQ_TIMER3_VIC		IRQ_SPI(40)
#define IRQ_TIMER4_VIC		IRQ_SPI(41)
#define IRQ_MCT_L0		IRQ_SPI(42)
#define IRQ_WDT			IRQ_SPI(43)
#define IRQ_RTC_ALARM		IRQ_SPI(44)
#define IRQ_RTC_TIC		IRQ_SPI(45)
#define IRQ_GPIO_XB		IRQ_SPI(46)
#define IRQ_GPIO_XA		IRQ_SPI(47)
#define IRQ_MCT_L1		IRQ_SPI(48)

#define IRQ_UART0		IRQ_SPI(52)
#define IRQ_UART1		IRQ_SPI(53)
#define IRQ_UART2		IRQ_SPI(54)
#define IRQ_UART3		IRQ_SPI(55)
#define IRQ_UART4		IRQ_SPI(56)
#define IRQ_MCT_G0		IRQ_SPI(57)
#define IRQ_IIC			IRQ_SPI(58)
#define IRQ_IIC1		IRQ_SPI(59)
#define IRQ_IIC2		IRQ_SPI(60)
#define IRQ_IIC3		IRQ_SPI(61)
#define IRQ_IIC4		IRQ_SPI(62)
#define IRQ_IIC5		IRQ_SPI(63)
#define IRQ_IIC6		IRQ_SPI(64)
#define IRQ_IIC7		IRQ_SPI(65)

#define IRQ_USB_HOST		IRQ_SPI(70)
#define IRQ_USB_HSOTG		IRQ_SPI(71)
#define IRQ_MODEM_IF		IRQ_SPI(72)
#define IRQ_HSMMC0		IRQ_SPI(73)
#define IRQ_HSMMC1		IRQ_SPI(74)
#define IRQ_HSMMC2		IRQ_SPI(75)
#define IRQ_HSMMC3		IRQ_SPI(76)
#define IRQ_DWMCI		IRQ_SPI(77)

#define IRQ_MIPICSI0		IRQ_SPI(78)

#define IRQ_MIPICSI1		IRQ_SPI(80)

#define IRQ_ONENAND_AUDI	IRQ_SPI(82)
#define IRQ_ROTATOR		IRQ_SPI(83)
#define IRQ_FIMC0		IRQ_SPI(84)
#define IRQ_FIMC1		IRQ_SPI(85)
#define IRQ_FIMC2		IRQ_SPI(86)
#define IRQ_FIMC3		IRQ_SPI(87)
#define IRQ_JPEG		IRQ_SPI(88)
#define IRQ_2D			IRQ_SPI(89)
#define IRQ_PCIE		IRQ_SPI(90)

#define IRQ_MFC			IRQ_SPI(94)

#define IRQ_AUDIO_SS		IRQ_SPI(96)
#define IRQ_I2S0		IRQ_SPI(97)
#define IRQ_I2S1		IRQ_SPI(98)
#define IRQ_I2S2		IRQ_SPI(99)
#define IRQ_AC97		IRQ_SPI(100)

#define IRQ_SPDIF		IRQ_SPI(104)
#define IRQ_ADC0		IRQ_SPI(105)
#define IRQ_PEN0		IRQ_SPI(106)
#define IRQ_ADC1		IRQ_SPI(107)
#define IRQ_PEN1		IRQ_SPI(108)
#define IRQ_KEYPAD		IRQ_SPI(109)
#define IRQ_PMU			IRQ_SPI(110)
#define IRQ_GPS			IRQ_SPI(111)
#define IRQ_INTFEEDCTRL_SSS	IRQ_SPI(112)
#define IRQ_SLIMBUS		IRQ_SPI(113)

#define IRQ_TSI			IRQ_SPI(115)
#define IRQ_SATA		IRQ_SPI(116)

#define MAX_IRQ_IN_COMBINER	8
#define COMBINER_GROUP(x)	((x) * MAX_IRQ_IN_COMBINER + IRQ_SPI(128))
#define COMBINER_IRQ(x, y)	(COMBINER_GROUP(x) + y)

#define IRQ_SYSMMU_MDMA0_0	COMBINER_IRQ(4, 0)
#define IRQ_SYSMMU_SSS_0	COMBINER_IRQ(4, 1)
#define IRQ_SYSMMU_FIMC0_0	COMBINER_IRQ(4, 2)
#define IRQ_SYSMMU_FIMC1_0	COMBINER_IRQ(4, 3)
#define IRQ_SYSMMU_FIMC2_0	COMBINER_IRQ(4, 4)
#define IRQ_SYSMMU_FIMC3_0	COMBINER_IRQ(4, 5)
#define IRQ_SYSMMU_JPEG_0	COMBINER_IRQ(4, 6)
#define IRQ_SYSMMU_2D_0		COMBINER_IRQ(4, 7)

#define IRQ_SYSMMU_ROTATOR_0	COMBINER_IRQ(5, 0)
#define IRQ_SYSMMU_MDMA1_0	COMBINER_IRQ(5, 1)
#define IRQ_SYSMMU_LCD0_M0_0	COMBINER_IRQ(5, 2)
#define IRQ_SYSMMU_LCD1_M1_0	COMBINER_IRQ(5, 3)
#define IRQ_SYSMMU_TV_M0_0	COMBINER_IRQ(5, 4)
#define IRQ_SYSMMU_MFC_M0_0	COMBINER_IRQ(5, 5)
#define IRQ_SYSMMU_MFC_M1_0	COMBINER_IRQ(5, 6)
#define IRQ_SYSMMU_PCIE_0	COMBINER_IRQ(5, 7)

#define IRQ_FIMD0_FIFO		COMBINER_IRQ(11, 0)
#define IRQ_FIMD0_VSYNC		COMBINER_IRQ(11, 1)
#define IRQ_FIMD0_SYSTEM	COMBINER_IRQ(11, 2)

#define MAX_COMBINER_NR		16

#define IRQ_ADC			IRQ_ADC0
#define IRQ_TC			IRQ_PEN0

#define S5P_IRQ_EINT_BASE	COMBINER_IRQ(MAX_COMBINER_NR, 0)

#define S5P_EINT_BASE1		(S5P_IRQ_EINT_BASE + 0)
#define S5P_EINT_BASE2		(S5P_IRQ_EINT_BASE + 16)

/* optional GPIO interrupts */
#define S5P_GPIOINT_BASE	(S5P_IRQ_EINT_BASE + 32)
#define IRQ_GPIO1_NR_GROUPS	16
#define IRQ_GPIO2_NR_GROUPS	9
#define IRQ_GPIO_END		(S5P_GPIOINT_BASE + S5P_GPIOINT_COUNT)

/* Set the default NR_IRQS */
#define NR_IRQS			(IRQ_GPIO_END + 64)

#endif /* __ASM_ARCH_IRQS_H */
