/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  Copyright (C) Greg Lonnon 2001
 *  Updated for OMAP-1610 by Tony Lindgren <tony@atomide.com>
 *
 * Copyright (C) 2009 Texas Instruments
 * Added OMAP4 support - Santosh Shilimkar <santosh.shilimkar@ti.com>
 *
 * NOTE: The interrupt vectors for the OMAP-1509, OMAP-1510, and OMAP-1610
 *	 are different.
 */

#ifndef __ASM_ARCH_OMAP15XX_IRQS_H
#define __ASM_ARCH_OMAP15XX_IRQS_H

/*
 * IRQ numbers for interrupt handler 1
 *
 * NOTE: See also the OMAP-1510 and 1610 specific IRQ numbers below
 *
 */
#define INT_CAMERA		(NR_IRQS_LEGACY + 1)
#define INT_FIQ			(NR_IRQS_LEGACY + 3)
#define INT_RTDX		(NR_IRQS_LEGACY + 6)
#define INT_DSP_MMU_ABORT	(NR_IRQS_LEGACY + 7)
#define INT_HOST		(NR_IRQS_LEGACY + 8)
#define INT_ABORT		(NR_IRQS_LEGACY + 9)
#define INT_BRIDGE_PRIV		(NR_IRQS_LEGACY + 13)
#define INT_GPIO_BANK1		(NR_IRQS_LEGACY + 14)
#define INT_UART3		(NR_IRQS_LEGACY + 15)
#define INT_TIMER3		(NR_IRQS_LEGACY + 16)
#define INT_DMA_CH0_6		(NR_IRQS_LEGACY + 19)
#define INT_DMA_CH1_7		(NR_IRQS_LEGACY + 20)
#define INT_DMA_CH2_8		(NR_IRQS_LEGACY + 21)
#define INT_DMA_CH3		(NR_IRQS_LEGACY + 22)
#define INT_DMA_CH4		(NR_IRQS_LEGACY + 23)
#define INT_DMA_CH5		(NR_IRQS_LEGACY + 24)
#define INT_TIMER1		(NR_IRQS_LEGACY + 26)
#define INT_WD_TIMER		(NR_IRQS_LEGACY + 27)
#define INT_BRIDGE_PUB		(NR_IRQS_LEGACY + 28)
#define INT_TIMER2		(NR_IRQS_LEGACY + 30)
#define INT_LCD_CTRL		(NR_IRQS_LEGACY + 31)

/*
 * OMAP-1510 specific IRQ numbers for interrupt handler 1
 */
#define INT_1510_IH2_IRQ	(NR_IRQS_LEGACY + 0)
#define INT_1510_RES2		(NR_IRQS_LEGACY + 2)
#define INT_1510_SPI_TX		(NR_IRQS_LEGACY + 4)
#define INT_1510_SPI_RX		(NR_IRQS_LEGACY + 5)
#define INT_1510_DSP_MAILBOX1	(NR_IRQS_LEGACY + 10)
#define INT_1510_DSP_MAILBOX2	(NR_IRQS_LEGACY + 11)
#define INT_1510_RES12		(NR_IRQS_LEGACY + 12)
#define INT_1510_LB_MMU		(NR_IRQS_LEGACY + 17)
#define INT_1510_RES18		(NR_IRQS_LEGACY + 18)
#define INT_1510_LOCAL_BUS	(NR_IRQS_LEGACY + 29)

/*
 * OMAP-1610 specific IRQ numbers for interrupt handler 1
 */
#define INT_1610_IH2_IRQ	INT_1510_IH2_IRQ
#define INT_1610_IH2_FIQ	(NR_IRQS_LEGACY + 2)
#define INT_1610_McBSP2_TX	(NR_IRQS_LEGACY + 4)
#define INT_1610_McBSP2_RX	(NR_IRQS_LEGACY + 5)
#define INT_1610_DSP_MAILBOX1	(NR_IRQS_LEGACY + 10)
#define INT_1610_DSP_MAILBOX2	(NR_IRQS_LEGACY + 11)
#define INT_1610_LCD_LINE	(NR_IRQS_LEGACY + 12)
#define INT_1610_GPTIMER1	(NR_IRQS_LEGACY + 17)
#define INT_1610_GPTIMER2	(NR_IRQS_LEGACY + 18)
#define INT_1610_SSR_FIFO_0	(NR_IRQS_LEGACY + 29)

/*
 * OMAP-7xx specific IRQ numbers for interrupt handler 1
 */
#define INT_7XX_IH2_FIQ		(NR_IRQS_LEGACY + 0)
#define INT_7XX_IH2_IRQ		(NR_IRQS_LEGACY + 1)
#define INT_7XX_USB_NON_ISO	(NR_IRQS_LEGACY + 2)
#define INT_7XX_USB_ISO		(NR_IRQS_LEGACY + 3)
#define INT_7XX_ICR		(NR_IRQS_LEGACY + 4)
#define INT_7XX_EAC		(NR_IRQS_LEGACY + 5)
#define INT_7XX_GPIO_BANK1	(NR_IRQS_LEGACY + 6)
#define INT_7XX_GPIO_BANK2	(NR_IRQS_LEGACY + 7)
#define INT_7XX_GPIO_BANK3	(NR_IRQS_LEGACY + 8)
#define INT_7XX_McBSP2TX	(NR_IRQS_LEGACY + 10)
#define INT_7XX_McBSP2RX	(NR_IRQS_LEGACY + 11)
#define INT_7XX_McBSP2RX_OVF	(NR_IRQS_LEGACY + 12)
#define INT_7XX_LCD_LINE	(NR_IRQS_LEGACY + 14)
#define INT_7XX_GSM_PROTECT	(NR_IRQS_LEGACY + 15)
#define INT_7XX_TIMER3		(NR_IRQS_LEGACY + 16)
#define INT_7XX_GPIO_BANK5	(NR_IRQS_LEGACY + 17)
#define INT_7XX_GPIO_BANK6	(NR_IRQS_LEGACY + 18)
#define INT_7XX_SPGIO_WR	(NR_IRQS_LEGACY + 29)

/*
 * IRQ numbers for interrupt handler 2
 *
 * NOTE: See also the OMAP-1510 and 1610 specific IRQ numbers below
 */
#define IH2_BASE		(NR_IRQS_LEGACY + 32)

#define INT_KEYBOARD		(1 + IH2_BASE)
#define INT_uWireTX		(2 + IH2_BASE)
#define INT_uWireRX		(3 + IH2_BASE)
#define INT_I2C			(4 + IH2_BASE)
#define INT_MPUIO		(5 + IH2_BASE)
#define INT_USB_HHC_1		(6 + IH2_BASE)
#define INT_McBSP3TX		(10 + IH2_BASE)
#define INT_McBSP3RX		(11 + IH2_BASE)
#define INT_McBSP1TX		(12 + IH2_BASE)
#define INT_McBSP1RX		(13 + IH2_BASE)
#define INT_UART1		(14 + IH2_BASE)
#define INT_UART2		(15 + IH2_BASE)
#define INT_BT_MCSI1TX		(16 + IH2_BASE)
#define INT_BT_MCSI1RX		(17 + IH2_BASE)
#define INT_SOSSI_MATCH		(19 + IH2_BASE)
#define INT_USB_W2FC		(20 + IH2_BASE)
#define INT_1WIRE		(21 + IH2_BASE)
#define INT_OS_TIMER		(22 + IH2_BASE)
#define INT_MMC			(23 + IH2_BASE)
#define INT_GAUGE_32K		(24 + IH2_BASE)
#define INT_RTC_TIMER		(25 + IH2_BASE)
#define INT_RTC_ALARM		(26 + IH2_BASE)
#define INT_MEM_STICK		(27 + IH2_BASE)

/*
 * OMAP-1510 specific IRQ numbers for interrupt handler 2
 */
#define INT_1510_DSP_MMU	(28 + IH2_BASE)
#define INT_1510_COM_SPI_RO	(31 + IH2_BASE)

/*
 * OMAP-1610 specific IRQ numbers for interrupt handler 2
 */
#define INT_1610_FAC		(0 + IH2_BASE)
#define INT_1610_USB_HHC_2	(7 + IH2_BASE)
#define INT_1610_USB_OTG	(8 + IH2_BASE)
#define INT_1610_SoSSI		(9 + IH2_BASE)
#define INT_1610_SoSSI_MATCH	(19 + IH2_BASE)
#define INT_1610_DSP_MMU	(28 + IH2_BASE)
#define INT_1610_McBSP2RX_OF	(31 + IH2_BASE)
#define INT_1610_STI		(32 + IH2_BASE)
#define INT_1610_STI_WAKEUP	(33 + IH2_BASE)
#define INT_1610_GPTIMER3	(34 + IH2_BASE)
#define INT_1610_GPTIMER4	(35 + IH2_BASE)
#define INT_1610_GPTIMER5	(36 + IH2_BASE)
#define INT_1610_GPTIMER6	(37 + IH2_BASE)
#define INT_1610_GPTIMER7	(38 + IH2_BASE)
#define INT_1610_GPTIMER8	(39 + IH2_BASE)
#define INT_1610_GPIO_BANK2	(40 + IH2_BASE)
#define INT_1610_GPIO_BANK3	(41 + IH2_BASE)
#define INT_1610_MMC2		(42 + IH2_BASE)
#define INT_1610_CF		(43 + IH2_BASE)
#define INT_1610_WAKE_UP_REQ	(46 + IH2_BASE)
#define INT_1610_GPIO_BANK4	(48 + IH2_BASE)
#define INT_1610_SPI		(49 + IH2_BASE)
#define INT_1610_DMA_CH6	(53 + IH2_BASE)
#define INT_1610_DMA_CH7	(54 + IH2_BASE)
#define INT_1610_DMA_CH8	(55 + IH2_BASE)
#define INT_1610_DMA_CH9	(56 + IH2_BASE)
#define INT_1610_DMA_CH10	(57 + IH2_BASE)
#define INT_1610_DMA_CH11	(58 + IH2_BASE)
#define INT_1610_DMA_CH12	(59 + IH2_BASE)
#define INT_1610_DMA_CH13	(60 + IH2_BASE)
#define INT_1610_DMA_CH14	(61 + IH2_BASE)
#define INT_1610_DMA_CH15	(62 + IH2_BASE)
#define INT_1610_NAND		(63 + IH2_BASE)
#define INT_1610_SHA1MD5	(91 + IH2_BASE)

/*
 * OMAP-7xx specific IRQ numbers for interrupt handler 2
 */
#define INT_7XX_HW_ERRORS	(0 + IH2_BASE)
#define INT_7XX_NFIQ_PWR_FAIL	(1 + IH2_BASE)
#define INT_7XX_CFCD		(2 + IH2_BASE)
#define INT_7XX_CFIREQ		(3 + IH2_BASE)
#define INT_7XX_I2C		(4 + IH2_BASE)
#define INT_7XX_PCC		(5 + IH2_BASE)
#define INT_7XX_MPU_EXT_NIRQ	(6 + IH2_BASE)
#define INT_7XX_SPI_100K_1	(7 + IH2_BASE)
#define INT_7XX_SYREN_SPI	(8 + IH2_BASE)
#define INT_7XX_VLYNQ		(9 + IH2_BASE)
#define INT_7XX_GPIO_BANK4	(10 + IH2_BASE)
#define INT_7XX_McBSP1TX	(11 + IH2_BASE)
#define INT_7XX_McBSP1RX	(12 + IH2_BASE)
#define INT_7XX_McBSP1RX_OF	(13 + IH2_BASE)
#define INT_7XX_UART_MODEM_IRDA_2 (14 + IH2_BASE)
#define INT_7XX_UART_MODEM_1	(15 + IH2_BASE)
#define INT_7XX_MCSI		(16 + IH2_BASE)
#define INT_7XX_uWireTX		(17 + IH2_BASE)
#define INT_7XX_uWireRX		(18 + IH2_BASE)
#define INT_7XX_SMC_CD		(19 + IH2_BASE)
#define INT_7XX_SMC_IREQ	(20 + IH2_BASE)
#define INT_7XX_HDQ_1WIRE	(21 + IH2_BASE)
#define INT_7XX_TIMER32K	(22 + IH2_BASE)
#define INT_7XX_MMC_SDIO	(23 + IH2_BASE)
#define INT_7XX_UPLD		(24 + IH2_BASE)
#define INT_7XX_USB_HHC_1	(27 + IH2_BASE)
#define INT_7XX_USB_HHC_2	(28 + IH2_BASE)
#define INT_7XX_USB_GENI	(29 + IH2_BASE)
#define INT_7XX_USB_OTG		(30 + IH2_BASE)
#define INT_7XX_CAMERA_IF	(31 + IH2_BASE)
#define INT_7XX_RNG		(32 + IH2_BASE)
#define INT_7XX_DUAL_MODE_TIMER (33 + IH2_BASE)
#define INT_7XX_DBB_RF_EN	(34 + IH2_BASE)
#define INT_7XX_MPUIO_KEYPAD	(35 + IH2_BASE)
#define INT_7XX_SHA1_MD5	(36 + IH2_BASE)
#define INT_7XX_SPI_100K_2	(37 + IH2_BASE)
#define INT_7XX_RNG_IDLE	(38 + IH2_BASE)
#define INT_7XX_MPUIO		(39 + IH2_BASE)
#define INT_7XX_LLPC_LCD_CTRL_CAN_BE_OFF	(40 + IH2_BASE)
#define INT_7XX_LLPC_OE_FALLING (41 + IH2_BASE)
#define INT_7XX_LLPC_OE_RISING	(42 + IH2_BASE)
#define INT_7XX_LLPC_VSYNC	(43 + IH2_BASE)
#define INT_7XX_WAKE_UP_REQ	(46 + IH2_BASE)
#define INT_7XX_DMA_CH6		(53 + IH2_BASE)
#define INT_7XX_DMA_CH7		(54 + IH2_BASE)
#define INT_7XX_DMA_CH8		(55 + IH2_BASE)
#define INT_7XX_DMA_CH9		(56 + IH2_BASE)
#define INT_7XX_DMA_CH10	(57 + IH2_BASE)
#define INT_7XX_DMA_CH11	(58 + IH2_BASE)
#define INT_7XX_DMA_CH12	(59 + IH2_BASE)
#define INT_7XX_DMA_CH13	(60 + IH2_BASE)
#define INT_7XX_DMA_CH14	(61 + IH2_BASE)
#define INT_7XX_DMA_CH15	(62 + IH2_BASE)
#define INT_7XX_NAND		(63 + IH2_BASE)

/* Max. 128 level 2 IRQs (OMAP1610), 192 GPIOs (OMAP730/850) and
 * 16 MPUIO lines */
#define OMAP_MAX_GPIO_LINES	192
#define IH_GPIO_BASE		(128 + IH2_BASE)
#define IH_MPUIO_BASE		(OMAP_MAX_GPIO_LINES + IH_GPIO_BASE)
#define OMAP_IRQ_END		(IH_MPUIO_BASE + 16)

#define OMAP_IRQ_BIT(irq)	(1 << ((irq - NR_IRQS_LEGACY) % 32))

#ifdef CONFIG_FIQ
#define FIQ_START		1024
#endif

#endif
