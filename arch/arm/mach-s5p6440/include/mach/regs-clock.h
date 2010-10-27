/* linux/arch/arm/mach-s5p6440/include/mach/regs-clock.h
 *
 * Copyright (c) 2009 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * S5P6440 - Clock register definitions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_REGS_CLOCK_H
#define __ASM_ARCH_REGS_CLOCK_H __FILE__

#include <mach/map.h>

#define S5P_CLKREG(x)		(S3C_VA_SYS + (x))

#define S5P_APLL_LOCK		S5P_CLKREG(0x00)
#define S5P_MPLL_LOCK		S5P_CLKREG(0x04)
#define S5P_EPLL_LOCK		S5P_CLKREG(0x08)
#define S5P_APLL_CON		S5P_CLKREG(0x0C)
#define S5P_MPLL_CON		S5P_CLKREG(0x10)
#define S5P_EPLL_CON		S5P_CLKREG(0x14)
#define S5P_EPLL_CON_K		S5P_CLKREG(0x18)
#define S5P_CLK_SRC0		S5P_CLKREG(0x1C)
#define S5P_CLK_DIV0		S5P_CLKREG(0x20)
#define S5P_CLK_DIV1		S5P_CLKREG(0x24)
#define S5P_CLK_DIV2		S5P_CLKREG(0x28)
#define S5P_CLK_OUT		S5P_CLKREG(0x2C)
#define S5P_CLK_GATE_HCLK0	S5P_CLKREG(0x30)
#define S5P_CLK_GATE_PCLK	S5P_CLKREG(0x34)
#define S5P_CLK_GATE_SCLK0	S5P_CLKREG(0x38)
#define S5P_CLK_GATE_MEM0	S5P_CLKREG(0x3C)
#define S5P_CLK_DIV3		S5P_CLKREG(0x40)
#define S5P_CLK_GATE_HCLK1	S5P_CLKREG(0x44)
#define S5P_CLK_GATE_SCLK1	S5P_CLKREG(0x48)
#define S5P_AHB_CON0           	S5P_CLKREG(0x100)
#define S5P_CLK_SRC1           	S5P_CLKREG(0x10C)
#define S5P_SWRESET		S5P_CLKREG(0x114)
#define S5P_SYS_ID		S5P_CLKREG(0x118)
#define S5P_SYS_OTHERS		S5P_CLKREG(0x11C)
#define S5P_MEM_CFG_STAT	S5P_CLKREG(0x12C)
#define S5P_PWR_CFG		S5P_CLKREG(0x804)
#define S5P_EINT_WAKEUP_MASK	S5P_CLKREG(0x808)
#define S5P_NORMAL_CFG		S5P_CLKREG(0x810)
#define S5P_STOP_CFG		S5P_CLKREG(0x814)
#define S5P_SLEEP_CFG		S5P_CLKREG(0x818)
#define S5P_OSC_FREQ		S5P_CLKREG(0x820)
#define S5P_OSC_STABLE		S5P_CLKREG(0x824)
#define S5P_PWR_STABLE		S5P_CLKREG(0x828)
#define S5P_MTC_STABLE		S5P_CLKREG(0x830)
#define S5P_OTHERS		S5P_CLKREG(0x900)
#define S5P_RST_STAT		S5P_CLKREG(0x904)
#define S5P_WAKEUP_STAT		S5P_CLKREG(0x908)
#define S5P_SLPEN		S5P_CLKREG(0x930)
#define S5P_INFORM0		S5P_CLKREG(0xA00)
#define S5P_INFORM1		S5P_CLKREG(0xA04)
#define S5P_INFORM2		S5P_CLKREG(0xA08)
#define S5P_INFORM3		S5P_CLKREG(0xA0C)

/* CLKDIV0 */
#define S5P_CLKDIV0_PCLK_MASK		(0xf << 12)
#define S5P_CLKDIV0_PCLK_SHIFT		(12)
#define S5P_CLKDIV0_HCLK_MASK		(0xf << 8)
#define S5P_CLKDIV0_HCLK_SHIFT		(8)
#define S5P_CLKDIV0_MPLL_MASK		(0x1 << 4)
#define S5P_CLKDIV0_ARM_MASK		(0xf << 0)
#define S5P_CLKDIV0_ARM_SHIFT		(0)

/* CLKDIV3 */
#define S5P_CLKDIV3_PCLK_LOW_MASK	(0xf << 12)
#define S5P_CLKDIV3_PCLK_LOW_SHIFT	(12)
#define S5P_CLKDIV3_HCLK_LOW_MASK	(0xf << 8)
#define S5P_CLKDIV3_HCLK_LOW_SHIFT	(8)

/* HCLK0 GATE Registers */
#define S5P_CLKCON_HCLK0_USB		(1<<20)
#define S5P_CLKCON_HCLK0_HSMMC2		(1<<19)
#define S5P_CLKCON_HCLK0_HSMMC1		(1<<18)
#define S5P_CLKCON_HCLK0_HSMMC0		(1<<17)
#define S5P_CLKCON_HCLK0_POST0		(1<<5)

/* HCLK1 GATE Registers */
#define S5P_CLKCON_HCLK1_DISPCON	(1<<1)

/* PCLK GATE Registers */
#define S5P_CLKCON_PCLK_IIS2		(1<<26)
#define S5P_CLKCON_PCLK_SPI1		(1<<22)
#define S5P_CLKCON_PCLK_SPI0		(1<<21)
#define S5P_CLKCON_PCLK_GPIO		(1<<18)
#define S5P_CLKCON_PCLK_IIC0		(1<<17)
#define S5P_CLKCON_PCLK_TSADC		(1<<12)
#define S5P_CLKCON_PCLK_PWM		(1<<7)
#define S5P_CLKCON_PCLK_RTC		(1<<6)
#define S5P_CLKCON_PCLK_WDT		(1<<5)
#define S5P_CLKCON_PCLK_UART3		(1<<4)
#define S5P_CLKCON_PCLK_UART2		(1<<3)
#define S5P_CLKCON_PCLK_UART1		(1<<2)
#define S5P_CLKCON_PCLK_UART0		(1<<1)

/* SCLK0 GATE Registers */
#define S5P_CLKCON_SCLK0_MMC2_48	(1<<29)
#define S5P_CLKCON_SCLK0_MMC1_48	(1<<28)
#define S5P_CLKCON_SCLK0_MMC0_48	(1<<27)
#define S5P_CLKCON_SCLK0_MMC2		(1<<26)
#define S5P_CLKCON_SCLK0_MMC1		(1<<25)
#define S5P_CLKCON_SCLK0_MMC0		(1<<24)
#define S5P_CLKCON_SCLK0_SPI1_48 	(1<<23)
#define S5P_CLKCON_SCLK0_SPI0_48 	(1<<22)
#define S5P_CLKCON_SCLK0_SPI1		(1<<21)
#define S5P_CLKCON_SCLK0_SPI0		(1<<20)
#define S5P_CLKCON_SCLK0_UART		(1<<5)

/* SCLK1 GATE Registers */

/* MEM0 GATE Registers */
#define S5P_CLKCON_MEM0_HCLK_NFCON	(1<<2)

/*OTHERS Resgister */
#define S5P_OTHERS_USB_SIG_MASK		(1<<16)
#define S5P_OTHERS_HCLK_LOW_SEL_MPLL	(1<<6)

/* Compatibility defines */
#define ARM_CLK_DIV			S5P_CLK_DIV0
#define ARM_DIV_RATIO_SHIFT		0
#define ARM_DIV_MASK			(0xf << ARM_DIV_RATIO_SHIFT)

#endif /* __ASM_ARCH_REGS_CLOCK_H */
