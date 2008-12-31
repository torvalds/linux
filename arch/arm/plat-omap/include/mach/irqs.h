/*
 *  arch/arm/plat-omap/include/mach/irqs.h
 *
 *  Copyright (C) Greg Lonnon 2001
 *  Updated for OMAP-1610 by Tony Lindgren <tony@atomide.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
#define INT_CAMERA		1
#define INT_FIQ			3
#define INT_RTDX		6
#define INT_DSP_MMU_ABORT	7
#define INT_HOST		8
#define INT_ABORT		9
#define INT_BRIDGE_PRIV		13
#define INT_GPIO_BANK1		14
#define INT_UART3		15
#define INT_TIMER3		16
#define INT_DMA_CH0_6		19
#define INT_DMA_CH1_7		20
#define INT_DMA_CH2_8		21
#define INT_DMA_CH3		22
#define INT_DMA_CH4		23
#define INT_DMA_CH5		24
#define INT_DMA_LCD		25
#define INT_TIMER1		26
#define INT_WD_TIMER		27
#define INT_BRIDGE_PUB		28
#define INT_TIMER2		30
#define INT_LCD_CTRL		31

/*
 * OMAP-1510 specific IRQ numbers for interrupt handler 1
 */
#define INT_1510_IH2_IRQ	0
#define INT_1510_RES2		2
#define INT_1510_SPI_TX		4
#define INT_1510_SPI_RX		5
#define INT_1510_DSP_MAILBOX1	10
#define INT_1510_DSP_MAILBOX2	11
#define INT_1510_RES12		12
#define INT_1510_LB_MMU		17
#define INT_1510_RES18		18
#define INT_1510_LOCAL_BUS	29

/*
 * OMAP-1610 specific IRQ numbers for interrupt handler 1
 */
#define INT_1610_IH2_IRQ	0
#define INT_1610_IH2_FIQ	2
#define INT_1610_McBSP2_TX	4
#define INT_1610_McBSP2_RX	5
#define INT_1610_DSP_MAILBOX1	10
#define INT_1610_DSP_MAILBOX2	11
#define INT_1610_LCD_LINE	12
#define INT_1610_GPTIMER1	17
#define INT_1610_GPTIMER2	18
#define INT_1610_SSR_FIFO_0	29

/*
 * OMAP-730 specific IRQ numbers for interrupt handler 1
 */
#define INT_730_IH2_FIQ		0
#define INT_730_IH2_IRQ		1
#define INT_730_USB_NON_ISO	2
#define INT_730_USB_ISO		3
#define INT_730_ICR		4
#define INT_730_EAC		5
#define INT_730_GPIO_BANK1	6
#define INT_730_GPIO_BANK2	7
#define INT_730_GPIO_BANK3	8
#define INT_730_McBSP2TX	10
#define INT_730_McBSP2RX	11
#define INT_730_McBSP2RX_OVF	12
#define INT_730_LCD_LINE	14
#define INT_730_GSM_PROTECT	15
#define INT_730_TIMER3		16
#define INT_730_GPIO_BANK5	17
#define INT_730_GPIO_BANK6	18
#define INT_730_SPGIO_WR	29

/*
 * IRQ numbers for interrupt handler 2
 *
 * NOTE: See also the OMAP-1510 and 1610 specific IRQ numbers below
 */
#define IH2_BASE		32

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
 * OMAP-730 specific IRQ numbers for interrupt handler 2
 */
#define INT_730_HW_ERRORS	(0 + IH2_BASE)
#define INT_730_NFIQ_PWR_FAIL	(1 + IH2_BASE)
#define INT_730_CFCD		(2 + IH2_BASE)
#define INT_730_CFIREQ		(3 + IH2_BASE)
#define INT_730_I2C		(4 + IH2_BASE)
#define INT_730_PCC		(5 + IH2_BASE)
#define INT_730_MPU_EXT_NIRQ	(6 + IH2_BASE)
#define INT_730_SPI_100K_1	(7 + IH2_BASE)
#define INT_730_SYREN_SPI	(8 + IH2_BASE)
#define INT_730_VLYNQ		(9 + IH2_BASE)
#define INT_730_GPIO_BANK4	(10 + IH2_BASE)
#define INT_730_McBSP1TX	(11 + IH2_BASE)
#define INT_730_McBSP1RX	(12 + IH2_BASE)
#define INT_730_McBSP1RX_OF	(13 + IH2_BASE)
#define INT_730_UART_MODEM_IRDA_2 (14 + IH2_BASE)
#define INT_730_UART_MODEM_1	(15 + IH2_BASE)
#define INT_730_MCSI		(16 + IH2_BASE)
#define INT_730_uWireTX		(17 + IH2_BASE)
#define INT_730_uWireRX		(18 + IH2_BASE)
#define INT_730_SMC_CD		(19 + IH2_BASE)
#define INT_730_SMC_IREQ	(20 + IH2_BASE)
#define INT_730_HDQ_1WIRE	(21 + IH2_BASE)
#define INT_730_TIMER32K	(22 + IH2_BASE)
#define INT_730_MMC_SDIO	(23 + IH2_BASE)
#define INT_730_UPLD		(24 + IH2_BASE)
#define INT_730_USB_HHC_1	(27 + IH2_BASE)
#define INT_730_USB_HHC_2	(28 + IH2_BASE)
#define INT_730_USB_GENI	(29 + IH2_BASE)
#define INT_730_USB_OTG		(30 + IH2_BASE)
#define INT_730_CAMERA_IF	(31 + IH2_BASE)
#define INT_730_RNG		(32 + IH2_BASE)
#define INT_730_DUAL_MODE_TIMER (33 + IH2_BASE)
#define INT_730_DBB_RF_EN	(34 + IH2_BASE)
#define INT_730_MPUIO_KEYPAD	(35 + IH2_BASE)
#define INT_730_SHA1_MD5	(36 + IH2_BASE)
#define INT_730_SPI_100K_2	(37 + IH2_BASE)
#define INT_730_RNG_IDLE	(38 + IH2_BASE)
#define INT_730_MPUIO		(39 + IH2_BASE)
#define INT_730_LLPC_LCD_CTRL_CAN_BE_OFF	(40 + IH2_BASE)
#define INT_730_LLPC_OE_FALLING (41 + IH2_BASE)
#define INT_730_LLPC_OE_RISING	(42 + IH2_BASE)
#define INT_730_LLPC_VSYNC	(43 + IH2_BASE)
#define INT_730_WAKE_UP_REQ	(46 + IH2_BASE)
#define INT_730_DMA_CH6		(53 + IH2_BASE)
#define INT_730_DMA_CH7		(54 + IH2_BASE)
#define INT_730_DMA_CH8		(55 + IH2_BASE)
#define INT_730_DMA_CH9		(56 + IH2_BASE)
#define INT_730_DMA_CH10	(57 + IH2_BASE)
#define INT_730_DMA_CH11	(58 + IH2_BASE)
#define INT_730_DMA_CH12	(59 + IH2_BASE)
#define INT_730_DMA_CH13	(60 + IH2_BASE)
#define INT_730_DMA_CH14	(61 + IH2_BASE)
#define INT_730_DMA_CH15	(62 + IH2_BASE)
#define INT_730_NAND		(63 + IH2_BASE)

#define INT_24XX_SYS_NIRQ	7
#define INT_24XX_SDMA_IRQ0	12
#define INT_24XX_SDMA_IRQ1	13
#define INT_24XX_SDMA_IRQ2	14
#define INT_24XX_SDMA_IRQ3	15
#define INT_24XX_CAM_IRQ	24
#define INT_24XX_DSS_IRQ	25
#define INT_24XX_MAIL_U0_MPU	26
#define INT_24XX_DSP_UMA	27
#define INT_24XX_DSP_MMU	28
#define INT_24XX_GPIO_BANK1	29
#define INT_24XX_GPIO_BANK2	30
#define INT_24XX_GPIO_BANK3	31
#define INT_24XX_GPIO_BANK4	32
#define INT_24XX_GPIO_BANK5	33
#define INT_24XX_MAIL_U3_MPU	34
#define INT_24XX_GPTIMER1	37
#define INT_24XX_GPTIMER2	38
#define INT_24XX_GPTIMER3	39
#define INT_24XX_GPTIMER4	40
#define INT_24XX_GPTIMER5	41
#define INT_24XX_GPTIMER6	42
#define INT_24XX_GPTIMER7	43
#define INT_24XX_GPTIMER8	44
#define INT_24XX_GPTIMER9	45
#define INT_24XX_GPTIMER10	46
#define INT_24XX_GPTIMER11	47
#define INT_24XX_GPTIMER12	48
#define INT_24XX_SHA1MD5	51
#define INT_24XX_MCBSP4_IRQ_TX	54
#define INT_24XX_MCBSP4_IRQ_RX	55
#define INT_24XX_I2C1_IRQ	56
#define INT_24XX_I2C2_IRQ	57
#define INT_24XX_HDQ_IRQ	58
#define INT_24XX_MCBSP1_IRQ_TX	59
#define INT_24XX_MCBSP1_IRQ_RX	60
#define INT_24XX_MCBSP2_IRQ_TX	62
#define INT_24XX_MCBSP2_IRQ_RX	63
#define INT_24XX_SPI1_IRQ	65
#define INT_24XX_SPI2_IRQ	66
#define INT_24XX_UART1_IRQ	72
#define INT_24XX_UART2_IRQ	73
#define INT_24XX_UART3_IRQ	74
#define INT_24XX_USB_IRQ_GEN	75
#define INT_24XX_USB_IRQ_NISO	76
#define INT_24XX_USB_IRQ_ISO	77
#define INT_24XX_USB_IRQ_HGEN	78
#define INT_24XX_USB_IRQ_HSOF	79
#define INT_24XX_USB_IRQ_OTG	80
#define INT_24XX_MCBSP5_IRQ_TX	81
#define INT_24XX_MCBSP5_IRQ_RX	82
#define INT_24XX_MMC_IRQ	83
#define INT_24XX_MMC2_IRQ	86
#define INT_24XX_MCBSP3_IRQ_TX	89
#define INT_24XX_MCBSP3_IRQ_RX	90
#define INT_24XX_SPI3_IRQ	91

#define INT_243X_MCBSP2_IRQ	16
#define INT_243X_MCBSP3_IRQ	17
#define INT_243X_MCBSP4_IRQ	18
#define INT_243X_MCBSP5_IRQ	19
#define INT_243X_MCBSP1_IRQ	64
#define INT_243X_HS_USB_MC	92
#define INT_243X_HS_USB_DMA	93
#define INT_243X_CARKIT_IRQ	94

#define INT_34XX_BENCH_MPU_EMUL	3
#define INT_34XX_ST_MCBSP2_IRQ	4
#define INT_34XX_ST_MCBSP3_IRQ	5
#define INT_34XX_SSM_ABORT_IRQ	6
#define INT_34XX_SYS_NIRQ	7
#define INT_34XX_D2D_FW_IRQ	8
#define INT_34XX_PRCM_MPU_IRQ	11
#define INT_34XX_MCBSP1_IRQ	16
#define INT_34XX_MCBSP2_IRQ	17
#define INT_34XX_MCBSP3_IRQ	22
#define INT_34XX_MCBSP4_IRQ	23
#define INT_34XX_CAM_IRQ	24
#define INT_34XX_MCBSP5_IRQ	27
#define INT_34XX_GPIO_BANK1	29
#define INT_34XX_GPIO_BANK2	30
#define INT_34XX_GPIO_BANK3	31
#define INT_34XX_GPIO_BANK4	32
#define INT_34XX_GPIO_BANK5	33
#define INT_34XX_GPIO_BANK6	34
#define INT_34XX_USIM_IRQ	35
#define INT_34XX_WDT3_IRQ	36
#define INT_34XX_SPI4_IRQ	48
#define INT_34XX_SHA1MD52_IRQ	49
#define INT_34XX_FPKA_READY_IRQ	50
#define INT_34XX_SHA1MD51_IRQ	51
#define INT_34XX_RNG_IRQ	52
#define INT_34XX_I2C3_IRQ	61
#define INT_34XX_FPKA_ERROR_IRQ	64
#define INT_34XX_PBIAS_IRQ	75
#define INT_34XX_OHCI_IRQ	76
#define INT_34XX_EHCI_IRQ	77
#define INT_34XX_TLL_IRQ	78
#define INT_34XX_PARTHASH_IRQ	79
#define INT_34XX_MMC3_IRQ	94
#define INT_34XX_GPT12_IRQ	95

#define	INT_34XX_BENCH_MPU_EMUL	3

/* Max. 128 level 2 IRQs (OMAP1610), 192 GPIOs (OMAP730) and
 * 16 MPUIO lines */
#define OMAP_MAX_GPIO_LINES	192
#define IH_GPIO_BASE		(128 + IH2_BASE)
#define IH_MPUIO_BASE		(OMAP_MAX_GPIO_LINES + IH_GPIO_BASE)
#define OMAP_IRQ_END		(IH_MPUIO_BASE + 16)

/* External FPGA handles interrupts on Innovator boards */
#define	OMAP_FPGA_IRQ_BASE	(OMAP_IRQ_END)
#ifdef	CONFIG_MACH_OMAP_INNOVATOR
#define OMAP_FPGA_NR_IRQS	24
#else
#define OMAP_FPGA_NR_IRQS	0
#endif
#define OMAP_FPGA_IRQ_END	(OMAP_FPGA_IRQ_BASE + OMAP_FPGA_NR_IRQS)

/* External TWL4030 can handle interrupts on 2430 and 34xx boards */
#define	TWL4030_IRQ_BASE	(OMAP_FPGA_IRQ_END)
#ifdef	CONFIG_TWL4030_CORE
#define	TWL4030_BASE_NR_IRQS	8
#define	TWL4030_PWR_NR_IRQS	8
#else
#define	TWL4030_BASE_NR_IRQS	0
#define	TWL4030_PWR_NR_IRQS	0
#endif
#define TWL4030_IRQ_END		(TWL4030_IRQ_BASE + TWL4030_BASE_NR_IRQS)
#define TWL4030_PWR_IRQ_BASE	TWL4030_IRQ_END
#define	TWL4030_PWR_IRQ_END	(TWL4030_PWR_IRQ_BASE + TWL4030_PWR_NR_IRQS)

/* External TWL4030 gpio interrupts are optional */
#define TWL4030_GPIO_IRQ_BASE	TWL4030_PWR_IRQ_END
#ifdef	CONFIG_GPIO_TWL4030
#define TWL4030_GPIO_NR_IRQS	18
#else
#define	TWL4030_GPIO_NR_IRQS	0
#endif
#define TWL4030_GPIO_IRQ_END	(TWL4030_GPIO_IRQ_BASE + TWL4030_GPIO_NR_IRQS)

/* Total number of interrupts depends on the enabled blocks above */
#define NR_IRQS			TWL4030_GPIO_IRQ_END

#define OMAP_IRQ_BIT(irq)	(1 << ((irq) % 32))

#ifndef __ASSEMBLY__
extern void omap_init_irq(void);
#endif

#include <mach/hardware.h>

#endif
