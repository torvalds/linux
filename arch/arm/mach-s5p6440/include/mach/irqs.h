/* linux/arch/arm/mach-s5p6440/include/mach/irqs.h
 *
 * Copyright 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - IRQ definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_S5P_IRQS_H
#define __ASM_ARCH_S5P_IRQS_H __FILE__

#include <plat/irqs.h>

/* VIC0 */

#define IRQ_EINT0_3		S5P_IRQ_VIC0(0)
#define IRQ_EINT4_11		S5P_IRQ_VIC0(1)
#define IRQ_RTC_TIC		S5P_IRQ_VIC0(2)
#define IRQ_IIC1		S5P_IRQ_VIC0(5)
#define IRQ_I2SV40		S5P_IRQ_VIC0(6)
#define IRQ_GPS			S5P_IRQ_VIC0(7)
#define IRQ_POST0		S5P_IRQ_VIC0(9)
#define IRQ_2D			S5P_IRQ_VIC0(11)
#define IRQ_TIMER0_VIC		S5P_IRQ_VIC0(23)
#define IRQ_TIMER1_VIC		S5P_IRQ_VIC0(24)
#define IRQ_TIMER2_VIC		S5P_IRQ_VIC0(25)
#define IRQ_WDT			S5P_IRQ_VIC0(26)
#define IRQ_TIMER3_VIC		S5P_IRQ_VIC0(27)
#define IRQ_TIMER4_VIC		S5P_IRQ_VIC0(28)
#define IRQ_DISPCON0		S5P_IRQ_VIC0(29)
#define IRQ_DISPCON1		S5P_IRQ_VIC0(30)
#define IRQ_DISPCON2		S5P_IRQ_VIC0(31)

/* VIC1 */

#define IRQ_EINT12_15		S5P_IRQ_VIC1(0)
#define IRQ_PCM0		S5P_IRQ_VIC1(2)
#define IRQ_UART0		S5P_IRQ_VIC1(5)
#define IRQ_UART1		S5P_IRQ_VIC1(6)
#define IRQ_UART2		S5P_IRQ_VIC1(7)
#define IRQ_UART3		S5P_IRQ_VIC1(8)
#define IRQ_DMA0		S5P_IRQ_VIC1(9)
#define IRQ_NFC			S5P_IRQ_VIC1(13)
#define IRQ_SPI0		S5P_IRQ_VIC1(16)
#define IRQ_SPI1		S5P_IRQ_VIC1(17)
#define IRQ_IIC			S5P_IRQ_VIC1(18)
#define IRQ_DISPCON3		S5P_IRQ_VIC1(19)
#define IRQ_FIMGVG		S5P_IRQ_VIC1(20)
#define IRQ_EINT_GROUPS		S5P_IRQ_VIC1(21)
#define IRQ_PMUIRQ		S5P_IRQ_VIC1(23)
#define IRQ_HSMMC0		S5P_IRQ_VIC1(24)
#define IRQ_HSMMC1		S5P_IRQ_VIC1(25)
#define IRQ_HSMMC2		IRQ_SPI1	/* shared with SPI1 */
#define IRQ_OTG			S5P_IRQ_VIC1(26)
#define IRQ_DSI			S5P_IRQ_VIC1(27)
#define IRQ_RTC_ALARM		S5P_IRQ_VIC1(28)
#define IRQ_TSI			S5P_IRQ_VIC1(29)
#define IRQ_PENDN		S5P_IRQ_VIC1(30)
#define IRQ_TC			IRQ_PENDN
#define IRQ_ADC			S5P_IRQ_VIC1(31)

/*
 * Since the IRQ_EINT(x) are a linear mapping on s5p6440 we just defined
 * them as an IRQ_EINT(x) macro from S5P_IRQ_EINT_BASE which we place
 * after the pair of VICs.
 */

#define S5P_IRQ_EINT_BASE	(S5P_IRQ_VIC1(31) + 6)

#define S5P_EINT(x)		((x) + S5P_IRQ_EINT_BASE)

#define S5P_EINT_BASE1		(S5P_IRQ_EINT_BASE)
/*
 * S5P6440 has 0-15 external interrupts in group 0. Only these can be used
 * to wake up from sleep. If request is beyond this range, by mistake, a large
 * return value for an irq number should be indication of something amiss.
 */
#define S5P_EINT_BASE2		(0xf0000000)

/*
 * Next the external interrupt groups. These are similar to the IRQ_EINT(x)
 * that they are sourced from the GPIO pins but with a different scheme for
 * priority and source indication.
 *
 * The IRQ_EINT(x) can be thought of as 'group 0' of the available GPIO
 * interrupts, but for historical reasons they are kept apart from these
 * next interrupts.
 *
 * Use IRQ_EINT_GROUP(group, offset) to get the number for use in the
 * machine specific support files.
 */

/* Actually, #6 and #7 are missing in the EINT_GROUP1 */
#define IRQ_EINT_GROUP1_NR	(15)
#define IRQ_EINT_GROUP2_NR	(8)
#define IRQ_EINT_GROUP5_NR	(7)
#define IRQ_EINT_GROUP6_NR	(10)
/* Actually, #0, #1 and #2 are missing in the EINT_GROUP8 */
#define IRQ_EINT_GROUP8_NR	(11)

#define IRQ_EINT_GROUP_BASE	S5P_EINT(16)
#define IRQ_EINT_GROUP1_BASE	(IRQ_EINT_GROUP_BASE + 0)
#define IRQ_EINT_GROUP2_BASE	(IRQ_EINT_GROUP1_BASE + IRQ_EINT_GROUP1_NR)
#define IRQ_EINT_GROUP5_BASE	(IRQ_EINT_GROUP2_BASE + IRQ_EINT_GROUP2_NR)
#define IRQ_EINT_GROUP6_BASE	(IRQ_EINT_GROUP5_BASE + IRQ_EINT_GROUP5_NR)
#define IRQ_EINT_GROUP8_BASE	(IRQ_EINT_GROUP6_BASE + IRQ_EINT_GROUP6_NR)

#define IRQ_EINT_GROUP(grp, x)	(IRQ_EINT_GROUP##grp##_BASE + (x))

/* Set the default NR_IRQS */

#define NR_IRQS			(IRQ_EINT_GROUP8_BASE + IRQ_EINT_GROUP8_NR + 1)

#endif /* __ASM_ARCH_S5P_IRQS_H */
