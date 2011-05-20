/* arch/arm/mach-rk29/include/mach/irqs.h
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */


#ifndef __ARCH_ARM_MACH_RK29_IRQS_H
#define __ARCH_ARM_MACH_RK29_IRQS_H

#define RK29XX_IRQ(x)   (x+32)

#define IRQ_DMAC0_0     RK29XX_IRQ(0)
#define IRQ_DMAC0_1     RK29XX_IRQ(1)
#define IRQ_DMAC0_2     RK29XX_IRQ(2)
#define IRQ_DMAC0_3     RK29XX_IRQ(3)

#define IRQ_DMAC1_0     RK29XX_IRQ(4)
#define IRQ_DMAC1_1     RK29XX_IRQ(5)
#define IRQ_DMAC1_2     RK29XX_IRQ(6)
#define IRQ_DMAC1_3     RK29XX_IRQ(7)
#define IRQ_DMAC1_4     RK29XX_IRQ(8)

#define IRQ_GPU         RK29XX_IRQ(9)
#define IRQ_VEPU        RK29XX_IRQ(10)
#define IRQ_VDPU        RK29XX_IRQ(11)
#define IRQ_VIP         RK29XX_IRQ(12)
#define IRQ_LCDC        RK29XX_IRQ(13)
#define IRQ_IPP         RK29XX_IRQ(14)
#define IRQ_EBC         RK29XX_IRQ(15)
#define IRQ_USB_OTG0    RK29XX_IRQ(16)
#define IRQ_USB_OTG1    RK29XX_IRQ(17)
#define IRQ_USB_HOST    RK29XX_IRQ(18)
#define IRQ_MAC         RK29XX_IRQ(19)
#define IRQ_HIF0        RK29XX_IRQ(20)
#define IRQ_HIF1        RK29XX_IRQ(21)
#define IRQ_HSADC_TSI   RK29XX_IRQ(22)
#define IRQ_SDMMC       RK29XX_IRQ(23)
#define IRQ_SDIO        RK29XX_IRQ(24)
#define IRQ_EMMC        RK29XX_IRQ(25)
#define IRQ_SARADC      RK29XX_IRQ(26)
#define IRQ_NANDC       RK29XX_IRQ(27)
#define IRQ_NANDC_RDY   RK29XX_IRQ(28)
#define IRQ_SMC         RK29XX_IRQ(29)
#define IRQ_PID_FILTER  RK29XX_IRQ(30)
#define IRQ_I2S_8CH     RK29XX_IRQ(31)
#define IRQ_I2S_2CH     RK29XX_IRQ(32)
#define IRQ_SPDIF       RK29XX_IRQ(33)

#define IRQ_UART0       RK29XX_IRQ(34)
#define IRQ_UART1       RK29XX_IRQ(35)
#define IRQ_UART2       RK29XX_IRQ(36)
#define IRQ_UART3       RK29XX_IRQ(37)

#define IRQ_SPI0        RK29XX_IRQ(38)
#define IRQ_SPI1        RK29XX_IRQ(39)

#define IRQ_I2C0        RK29XX_IRQ(40)
#define IRQ_I2C1        RK29XX_IRQ(41)
#define IRQ_I2C2        RK29XX_IRQ(42)
#define IRQ_I2C3        RK29XX_IRQ(43)

#define IRQ_TIMER0      RK29XX_IRQ(44)
#define IRQ_TIMER1      RK29XX_IRQ(45)
#define IRQ_TIMER2      RK29XX_IRQ(46)
#define IRQ_TIMER3      RK29XX_IRQ(47)

#define IRQ_PWM0        RK29XX_IRQ(48)
#define IRQ_PWM1        RK29XX_IRQ(49)
#define IRQ_PWM2        RK29XX_IRQ(50)
#define IRQ_PWM3        RK29XX_IRQ(51)

#define IRQ_WDT         RK29XX_IRQ(52)
#define IRQ_RTC         RK29XX_IRQ(53)
#define IRQ_PMU         RK29XX_IRQ(54)

#define IRQ_GPIO0       RK29XX_IRQ(55)
#define IRQ_GPIO1       RK29XX_IRQ(56)
#define IRQ_GPIO2       RK29XX_IRQ(57)
#define IRQ_GPIO3       RK29XX_IRQ(58)
#define IRQ_GPIO4       RK29XX_IRQ(59)
#define IRQ_GPIO5       RK29XX_IRQ(60)
#define IRQ_GPIO6       RK29XX_IRQ(61)

#define IRQ_USB_AHB_ARB         RK29XX_IRQ(62)
#define IRQ_PERI_AHB_ARB        RK29XX_IRQ(63)
#define IRQ_A8IRQ0              RK29XX_IRQ(64)
#define IRQ_A8IRQ1              RK29XX_IRQ(65)
#define IRQ_A8IRQ2              RK29XX_IRQ(66)
#define IRQ_A8IRQ3              RK29XX_IRQ(67)

#define NR_AIC_IRQS     (IRQ_A8IRQ3+1)
#define NR_GPIO_IRQS    (7*32)
#define NR_BOARD_IRQS   64
#define NR_IRQS         (NR_AIC_IRQS + NR_GPIO_IRQS + NR_BOARD_IRQS)
#endif
