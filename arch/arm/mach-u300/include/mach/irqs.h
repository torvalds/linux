/*
 *
 * arch/arm/mach-u300/include/mach/irqs.h
 *
 *
 * Copyright (C) 2006-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * IRQ channel definitions for the U300 platforms.
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 */

#ifndef __MACH_IRQS_H
#define __MACH_IRQS_H

#define IRQ_U300_INTCON0_START		0
#define IRQ_U300_INTCON1_START		32
/* These are on INTCON0 - 30 lines */
#define IRQ_U300_IRQ0_EXT		0
#define IRQ_U300_IRQ1_EXT		1
#define IRQ_U300_DMA			2
#define IRQ_U300_VIDEO_ENC_0		3
#define IRQ_U300_VIDEO_ENC_1		4
#define IRQ_U300_AAIF_RX		5
#define IRQ_U300_AAIF_TX		6
#define IRQ_U300_AAIF_VGPIO		7
#define IRQ_U300_AAIF_WAKEUP		8
#define IRQ_U300_PCM_I2S0_FRAME		9
#define IRQ_U300_PCM_I2S0_FIFO		10
#define IRQ_U300_PCM_I2S1_FRAME		11
#define IRQ_U300_PCM_I2S1_FIFO		12
#define IRQ_U300_XGAM_GAMCON		13
#define IRQ_U300_XGAM_CDI		14
#define IRQ_U300_XGAM_CDICON		15
#if defined(CONFIG_MACH_U300_BS2X) || defined(CONFIG_MACH_U300_BS330)
/* MMIACC not used on the DB3210 or DB3350 chips */
#define IRQ_U300_XGAM_MMIACC		16
#endif
#define IRQ_U300_XGAM_PDI		17
#define IRQ_U300_XGAM_PDICON		18
#define IRQ_U300_XGAM_GAMEACC		19
#define IRQ_U300_XGAM_MCIDCT		20
#define IRQ_U300_APEX			21
#define IRQ_U300_UART0			22
#define IRQ_U300_SPI			23
#define IRQ_U300_TIMER_APP_OS		24
#define IRQ_U300_TIMER_APP_DD		25
#define IRQ_U300_TIMER_APP_GP1		26
#define IRQ_U300_TIMER_APP_GP2		27
#define IRQ_U300_TIMER_OS		28
#define IRQ_U300_TIMER_MS		29
#define IRQ_U300_KEYPAD_KEYBF		30
#define IRQ_U300_KEYPAD_KEYBR		31
/* These are on INTCON1 - 32 lines */
#define IRQ_U300_GPIO_PORT0		32
#define IRQ_U300_GPIO_PORT1		33
#define IRQ_U300_GPIO_PORT2		34

#if defined(CONFIG_MACH_U300_BS2X) || defined(CONFIG_MACH_U300_BS330) || \
    defined(CONFIG_MACH_U300_BS335)
/* These are for DB3150, DB3200 and DB3350 */
#define IRQ_U300_WDOG			35
#define IRQ_U300_EVHIST			36
#define IRQ_U300_MSPRO			37
#define IRQ_U300_MMCSD_MCIINTR0		38
#define IRQ_U300_MMCSD_MCIINTR1		39
#define IRQ_U300_I2C0			40
#define IRQ_U300_I2C1			41
#define IRQ_U300_RTC			42
#define IRQ_U300_NFIF			43
#define IRQ_U300_NFIF2			44
#endif

/* DB3150 and DB3200 have only 45 IRQs */
#if defined(CONFIG_MACH_U300_BS2X) || defined(CONFIG_MACH_U300_BS330)
#define U300_NR_IRQS			45
#endif

/* The DB3350-specific interrupt lines */
#ifdef CONFIG_MACH_U300_BS335
#define IRQ_U300_ISP_F0			45
#define IRQ_U300_ISP_F1			46
#define IRQ_U300_ISP_F2			47
#define IRQ_U300_ISP_F3			48
#define IRQ_U300_ISP_F4			49
#define IRQ_U300_GPIO_PORT3		50
#define IRQ_U300_SYSCON_PLL_LOCK	51
#define IRQ_U300_UART1			52
#define IRQ_U300_GPIO_PORT4		53
#define IRQ_U300_GPIO_PORT5		54
#define IRQ_U300_GPIO_PORT6		55
#define U300_NR_IRQS			56
#endif

/* The DB3210-specific interrupt lines */
#ifdef CONFIG_MACH_U300_BS365
#define IRQ_U300_GPIO_PORT3		35
#define IRQ_U300_GPIO_PORT4		36
#define IRQ_U300_WDOG			37
#define IRQ_U300_EVHIST			38
#define IRQ_U300_MSPRO			39
#define IRQ_U300_MMCSD_MCIINTR0		40
#define IRQ_U300_MMCSD_MCIINTR1		41
#define IRQ_U300_I2C0			42
#define IRQ_U300_I2C1			43
#define IRQ_U300_RTC			44
#define IRQ_U300_NFIF			45
#define IRQ_U300_NFIF2			46
#define IRQ_U300_SYSCON_PLL_LOCK	47
#define U300_NR_IRQS			48
#endif

#define NR_IRQS U300_NR_IRQS

#endif
