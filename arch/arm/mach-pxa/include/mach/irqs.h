/*
 *  arch/arm/mach-pxa/include/mach/irqs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_MACH_IRQS_H
#define __ASM_MACH_IRQS_H

#ifdef CONFIG_PXA_HAVE_ISA_IRQS
#define PXA_ISA_IRQ(x)	(x)
#define PXA_ISA_IRQ_NUM	(16)
#else
#define PXA_ISA_IRQ_NUM	(0)
#endif

#define PXA_IRQ(x)	(PXA_ISA_IRQ_NUM + (x))

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#define IRQ_SSP3	PXA_IRQ(0)	/* SSP3 service request */
#define IRQ_MSL		PXA_IRQ(1)	/* MSL Interface interrupt */
#define IRQ_USBH2	PXA_IRQ(2)	/* USB Host interrupt 1 (OHCI) */
#define IRQ_USBH1	PXA_IRQ(3)	/* USB Host interrupt 2 (non-OHCI) */
#define IRQ_KEYPAD	PXA_IRQ(4)	/* Key pad controller */
#define IRQ_MEMSTK	PXA_IRQ(5)	/* Memory Stick interrupt */
#define IRQ_PWRI2C	PXA_IRQ(6)	/* Power I2C interrupt */
#endif

#define IRQ_HWUART	PXA_IRQ(7)	/* HWUART Transmit/Receive/Error (PXA26x) */
#define IRQ_OST_4_11	PXA_IRQ(7)	/* OS timer 4-11 matches (PXA27x) */
#define	IRQ_GPIO0	PXA_IRQ(8)	/* GPIO0 Edge Detect */
#define	IRQ_GPIO1	PXA_IRQ(9)	/* GPIO1 Edge Detect */
#define	IRQ_GPIO_2_x	PXA_IRQ(10)	/* GPIO[2-x] Edge Detect */
#define	IRQ_USB		PXA_IRQ(11)	/* USB Service */
#define	IRQ_PMU		PXA_IRQ(12)	/* Performance Monitoring Unit */
#define	IRQ_I2S		PXA_IRQ(13)	/* I2S Interrupt */
#define	IRQ_AC97	PXA_IRQ(14)	/* AC97 Interrupt */
#define IRQ_ASSP	PXA_IRQ(15)	/* Audio SSP Service Request (PXA25x) */
#define IRQ_USIM	PXA_IRQ(15)     /* Smart Card interface interrupt (PXA27x) */
#define IRQ_NSSP	PXA_IRQ(16)	/* Network SSP Service Request (PXA25x) */
#define IRQ_SSP2	PXA_IRQ(16)	/* SSP2 interrupt (PXA27x) */
#define	IRQ_LCD		PXA_IRQ(17)	/* LCD Controller Service Request */
#define	IRQ_I2C		PXA_IRQ(18)	/* I2C Service Request */
#define	IRQ_ICP		PXA_IRQ(19)	/* ICP Transmit/Receive/Error */
#define	IRQ_STUART	PXA_IRQ(20)	/* STUART Transmit/Receive/Error */
#define	IRQ_BTUART	PXA_IRQ(21)	/* BTUART Transmit/Receive/Error */
#define	IRQ_FFUART	PXA_IRQ(22)	/* FFUART Transmit/Receive/Error*/
#define	IRQ_MMC		PXA_IRQ(23)	/* MMC Status/Error Detection */
#define	IRQ_SSP		PXA_IRQ(24)	/* SSP Service Request */
#define	IRQ_DMA 	PXA_IRQ(25)	/* DMA Channel Service Request */
#define	IRQ_OST0 	PXA_IRQ(26)	/* OS Timer match 0 */
#define	IRQ_OST1 	PXA_IRQ(27)	/* OS Timer match 1 */
#define	IRQ_OST2 	PXA_IRQ(28)	/* OS Timer match 2 */
#define	IRQ_OST3 	PXA_IRQ(29)	/* OS Timer match 3 */
#define	IRQ_RTC1Hz	PXA_IRQ(30)	/* RTC HZ Clock Tick */
#define	IRQ_RTCAlrm	PXA_IRQ(31)	/* RTC Alarm */

#if defined(CONFIG_PXA27x) || defined(CONFIG_PXA3xx)
#define IRQ_TPM		PXA_IRQ(32)	/* TPM interrupt */
#define IRQ_CAMERA	PXA_IRQ(33)	/* Camera Interface */
#endif

#ifdef CONFIG_PXA3xx
#define IRQ_SSP4	PXA_IRQ(13)	/* SSP4 service request */
#define IRQ_CIR		PXA_IRQ(34)	/* Consumer IR */
#define IRQ_COMM_WDT	PXA_IRQ(35) 	/* Comm WDT interrupt */
#define IRQ_TSI		PXA_IRQ(36)	/* Touch Screen Interface (PXA320) */
#define IRQ_USIM2	PXA_IRQ(38)	/* USIM2 Controller */
#define IRQ_GCU		PXA_IRQ(39)	/* Graphics Controller */
#define IRQ_MMC2	PXA_IRQ(41)	/* MMC2 Controller */
#define IRQ_1WIRE	PXA_IRQ(44)	/* 1-Wire Controller */
#define IRQ_NAND	PXA_IRQ(45)	/* NAND Controller */
#define IRQ_USB2	PXA_IRQ(46)	/* USB 2.0 Device Controller */
#define IRQ_WAKEUP0	PXA_IRQ(49)	/* EXT_WAKEUP0 */
#define IRQ_WAKEUP1	PXA_IRQ(50)	/* EXT_WAKEUP1 */
#define IRQ_DMEMC	PXA_IRQ(51)	/* Dynamic Memory Controller */
#define IRQ_MMC3	PXA_IRQ(55)	/* MMC3 Controller (PXA310) */
#endif

#ifdef CONFIG_CPU_PXA935
#define IRQ_U2O		PXA_IRQ(64)	/* USB OTG 2.0 Controller (PXA935) */
#define IRQ_U2H		PXA_IRQ(65)	/* USB Host 2.0 Controller (PXA935) */

#define IRQ_MMC3_PXA935	PXA_IRQ(72)	/* MMC3 Controller (PXA935) */
#define IRQ_MMC4_PXA935	PXA_IRQ(73)	/* MMC4 Controller (PXA935) */
#define IRQ_MMC5_PXA935	PXA_IRQ(74)	/* MMC5 Controller (PXA935) */

#define IRQ_U2P		PXA_IRQ(93)	/* USB PHY D+/D- Lines (PXA935) */
#endif

#ifdef CONFIG_CPU_PXA930
#define IRQ_ENHROT	PXA_IRQ(37)	/* Enhanced Rotary (PXA930) */
#define IRQ_ACIPC0	PXA_IRQ(5)
#define IRQ_ACIPC1	PXA_IRQ(40)
#define IRQ_ACIPC2	PXA_IRQ(19)
#define IRQ_TRKBALL	PXA_IRQ(43)	/* Track Ball */
#endif

#ifdef CONFIG_CPU_PXA950
#define IRQ_GC500	PXA_IRQ(70)	/* Graphics Controller (PXA950) */
#endif

#define PXA_GPIO_IRQ_BASE	PXA_IRQ(96)
#define PXA_GPIO_IRQ_NUM	(192)

#define GPIO_2_x_TO_IRQ(x)	(PXA_GPIO_IRQ_BASE + (x))
#define IRQ_GPIO(x)	(((x) < 2) ? (IRQ_GPIO0 + (x)) : GPIO_2_x_TO_IRQ(x))

#define IRQ_TO_GPIO_2_x(i)	((i) - PXA_GPIO_IRQ_BASE)
#define IRQ_TO_GPIO(i)	(((i) < IRQ_GPIO(2)) ? ((i) - IRQ_GPIO0) : IRQ_TO_GPIO_2_x(i))

/*
 * The following interrupts are for board specific purposes. Since
 * the kernel can only run on one machine at a time, we can re-use
 * these.  There will be 16 IRQs by default.  If it is not enough,
 * IRQ_BOARD_END is allowed be customized for each board, but keep
 * the numbers within sensible limits and in descending order, so
 * when multiple config options are selected, the maximum will be
 * used.
 */
#define IRQ_BOARD_START		(PXA_GPIO_IRQ_BASE + PXA_GPIO_IRQ_NUM)

#if defined(CONFIG_MACH_H4700)
#define IRQ_BOARD_END		(IRQ_BOARD_START + 70)
#elif defined(CONFIG_MACH_ZYLONITE)
#define IRQ_BOARD_END		(IRQ_BOARD_START + 32)
#elif defined(CONFIG_PXA_EZX)
#define IRQ_BOARD_END		(IRQ_BOARD_START + 23)
#else
#define IRQ_BOARD_END		(IRQ_BOARD_START + 16)
#endif

#define IRQ_SA1111_START	(IRQ_BOARD_END)
#define IRQ_GPAIN0		(IRQ_BOARD_END + 0)
#define IRQ_GPAIN1		(IRQ_BOARD_END + 1)
#define IRQ_GPAIN2		(IRQ_BOARD_END + 2)
#define IRQ_GPAIN3		(IRQ_BOARD_END + 3)
#define IRQ_GPBIN0		(IRQ_BOARD_END + 4)
#define IRQ_GPBIN1		(IRQ_BOARD_END + 5)
#define IRQ_GPBIN2		(IRQ_BOARD_END + 6)
#define IRQ_GPBIN3		(IRQ_BOARD_END + 7)
#define IRQ_GPBIN4		(IRQ_BOARD_END + 8)
#define IRQ_GPBIN5		(IRQ_BOARD_END + 9)
#define IRQ_GPCIN0		(IRQ_BOARD_END + 10)
#define IRQ_GPCIN1		(IRQ_BOARD_END + 11)
#define IRQ_GPCIN2		(IRQ_BOARD_END + 12)
#define IRQ_GPCIN3		(IRQ_BOARD_END + 13)
#define IRQ_GPCIN4		(IRQ_BOARD_END + 14)
#define IRQ_GPCIN5		(IRQ_BOARD_END + 15)
#define IRQ_GPCIN6		(IRQ_BOARD_END + 16)
#define IRQ_GPCIN7		(IRQ_BOARD_END + 17)
#define IRQ_MSTXINT		(IRQ_BOARD_END + 18)
#define IRQ_MSRXINT		(IRQ_BOARD_END + 19)
#define IRQ_MSSTOPERRINT	(IRQ_BOARD_END + 20)
#define IRQ_TPTXINT		(IRQ_BOARD_END + 21)
#define IRQ_TPRXINT		(IRQ_BOARD_END + 22)
#define IRQ_TPSTOPERRINT	(IRQ_BOARD_END + 23)
#define SSPXMTINT		(IRQ_BOARD_END + 24)
#define SSPRCVINT		(IRQ_BOARD_END + 25)
#define SSPROR			(IRQ_BOARD_END + 26)
#define AUDXMTDMADONEA		(IRQ_BOARD_END + 32)
#define AUDRCVDMADONEA		(IRQ_BOARD_END + 33)
#define AUDXMTDMADONEB		(IRQ_BOARD_END + 34)
#define AUDRCVDMADONEB		(IRQ_BOARD_END + 35)
#define AUDTFSR			(IRQ_BOARD_END + 36)
#define AUDRFSR			(IRQ_BOARD_END + 37)
#define AUDTUR			(IRQ_BOARD_END + 38)
#define AUDROR			(IRQ_BOARD_END + 39)
#define AUDDTS			(IRQ_BOARD_END + 40)
#define AUDRDD			(IRQ_BOARD_END + 41)
#define AUDSTO			(IRQ_BOARD_END + 42)
#define IRQ_USBPWR		(IRQ_BOARD_END + 43)
#define IRQ_HCIM		(IRQ_BOARD_END + 44)
#define IRQ_HCIBUFFACC		(IRQ_BOARD_END + 45)
#define IRQ_HCIRMTWKP		(IRQ_BOARD_END + 46)
#define IRQ_NHCIMFCIR		(IRQ_BOARD_END + 47)
#define IRQ_USB_PORT_RESUME	(IRQ_BOARD_END + 48)
#define IRQ_S0_READY_NINT	(IRQ_BOARD_END + 49)
#define IRQ_S1_READY_NINT	(IRQ_BOARD_END + 50)
#define IRQ_S0_CD_VALID		(IRQ_BOARD_END + 51)
#define IRQ_S1_CD_VALID		(IRQ_BOARD_END + 52)
#define IRQ_S0_BVD1_STSCHG	(IRQ_BOARD_END + 53)
#define IRQ_S1_BVD1_STSCHG	(IRQ_BOARD_END + 54)

#define IRQ_LOCOMO_START	(IRQ_BOARD_END)
#define IRQ_LOCOMO_KEY		(IRQ_BOARD_END + 0)
#define IRQ_LOCOMO_GPIO0	(IRQ_BOARD_END + 1)
#define IRQ_LOCOMO_GPIO1	(IRQ_BOARD_END + 2)
#define IRQ_LOCOMO_GPIO2	(IRQ_BOARD_END + 3)
#define IRQ_LOCOMO_GPIO3	(IRQ_BOARD_END + 4)
#define IRQ_LOCOMO_GPIO4	(IRQ_BOARD_END + 5)
#define IRQ_LOCOMO_GPIO5	(IRQ_BOARD_END + 6)
#define IRQ_LOCOMO_GPIO6	(IRQ_BOARD_END + 7)
#define IRQ_LOCOMO_GPIO7	(IRQ_BOARD_END + 8)
#define IRQ_LOCOMO_GPIO8	(IRQ_BOARD_END + 9)
#define IRQ_LOCOMO_GPIO9	(IRQ_BOARD_END + 10)
#define IRQ_LOCOMO_GPIO10	(IRQ_BOARD_END + 11)
#define IRQ_LOCOMO_GPIO11	(IRQ_BOARD_END + 12)
#define IRQ_LOCOMO_GPIO12	(IRQ_BOARD_END + 13)
#define IRQ_LOCOMO_GPIO13	(IRQ_BOARD_END + 14)
#define IRQ_LOCOMO_GPIO14	(IRQ_BOARD_END + 15)
#define IRQ_LOCOMO_GPIO15	(IRQ_BOARD_END + 16)
#define IRQ_LOCOMO_LT		(IRQ_BOARD_END + 17)
#define IRQ_LOCOMO_SPI_RFR	(IRQ_BOARD_END + 18)
#define IRQ_LOCOMO_SPI_RFW	(IRQ_BOARD_END + 19)
#define IRQ_LOCOMO_SPI_OVRN	(IRQ_BOARD_END + 20)
#define IRQ_LOCOMO_SPI_TEND	(IRQ_BOARD_END + 21)

/*
 * Figure out the MAX IRQ number.
 *
 * If we have an SA1111, the max IRQ is S1_BVD1_STSCHG+1.
 * If we have an LoCoMo, the max IRQ is IRQ_LOCOMO_SPI_TEND+1
 * Otherwise, we have the standard IRQs only.
 */
#ifdef CONFIG_SA1111
#define NR_IRQS			(IRQ_S1_BVD1_STSCHG + 1)
#elif defined(CONFIG_SHARP_LOCOMO)
#define NR_IRQS			(IRQ_LOCOMO_SPI_TEND + 1)
#elif defined(CONFIG_PXA_HAVE_BOARD_IRQS)
#define NR_IRQS			(IRQ_BOARD_END)
#else
#define NR_IRQS			(IRQ_BOARD_START)
#endif

/*
 * Board specific IRQs.  Define them here.
 * Do not surround them with ifdefs.
 */
#define LUBBOCK_IRQ(x)		(IRQ_BOARD_START + (x))
#define LUBBOCK_SD_IRQ		LUBBOCK_IRQ(0)
#define LUBBOCK_SA1111_IRQ	LUBBOCK_IRQ(1)
#define LUBBOCK_USB_IRQ		LUBBOCK_IRQ(2)  /* usb connect */
#define LUBBOCK_ETH_IRQ		LUBBOCK_IRQ(3)
#define LUBBOCK_UCB1400_IRQ	LUBBOCK_IRQ(4)
#define LUBBOCK_BB_IRQ		LUBBOCK_IRQ(5)
#define LUBBOCK_USB_DISC_IRQ	LUBBOCK_IRQ(6)  /* usb disconnect */
#define LUBBOCK_LAST_IRQ	LUBBOCK_IRQ(6)

#define LPD270_IRQ(x)		(IRQ_BOARD_START + (x))
#define LPD270_USBC_IRQ		LPD270_IRQ(2)
#define LPD270_ETHERNET_IRQ	LPD270_IRQ(3)
#define LPD270_AC97_IRQ		LPD270_IRQ(4)

#define MAINSTONE_IRQ(x)	(IRQ_BOARD_START + (x))
#define MAINSTONE_MMC_IRQ	MAINSTONE_IRQ(0)
#define MAINSTONE_USIM_IRQ	MAINSTONE_IRQ(1)
#define MAINSTONE_USBC_IRQ	MAINSTONE_IRQ(2)
#define MAINSTONE_ETHERNET_IRQ	MAINSTONE_IRQ(3)
#define MAINSTONE_AC97_IRQ	MAINSTONE_IRQ(4)
#define MAINSTONE_PEN_IRQ	MAINSTONE_IRQ(5)
#define MAINSTONE_MSINS_IRQ	MAINSTONE_IRQ(6)
#define MAINSTONE_EXBRD_IRQ	MAINSTONE_IRQ(7)
#define MAINSTONE_S0_CD_IRQ	MAINSTONE_IRQ(9)
#define MAINSTONE_S0_STSCHG_IRQ	MAINSTONE_IRQ(10)
#define MAINSTONE_S0_IRQ	MAINSTONE_IRQ(11)
#define MAINSTONE_S1_CD_IRQ	MAINSTONE_IRQ(13)
#define MAINSTONE_S1_STSCHG_IRQ	MAINSTONE_IRQ(14)
#define MAINSTONE_S1_IRQ	MAINSTONE_IRQ(15)

/* Balloon3 Interrupts */
#define BALLOON3_IRQ(x)		(IRQ_BOARD_START + (x))

#define BALLOON3_BP_CF_NRDY_IRQ	BALLOON3_IRQ(0)
#define BALLOON3_BP_NSTSCHG_IRQ	BALLOON3_IRQ(1)

#define BALLOON3_AUX_NIRQ	IRQ_GPIO(BALLOON3_GPIO_AUX_NIRQ)
#define BALLOON3_CODEC_IRQ	IRQ_GPIO(BALLOON3_GPIO_CODEC_IRQ)
#define BALLOON3_S0_CD_IRQ	IRQ_GPIO(BALLOON3_GPIO_S0_CD)

/* LoCoMo Interrupts (CONFIG_SHARP_LOCOMO) */
#define IRQ_LOCOMO_KEY_BASE	(IRQ_BOARD_START + 0)
#define IRQ_LOCOMO_GPIO_BASE	(IRQ_BOARD_START + 1)
#define IRQ_LOCOMO_LT_BASE	(IRQ_BOARD_START + 2)
#define IRQ_LOCOMO_SPI_BASE	(IRQ_BOARD_START + 3)

/* phyCORE-PXA270 (PCM027) Interrupts */
#define PCM027_IRQ(x)          (IRQ_BOARD_START + (x))
#define PCM027_BTDET_IRQ       PCM027_IRQ(0)
#define PCM027_FF_RI_IRQ       PCM027_IRQ(1)
#define PCM027_MMCDET_IRQ      PCM027_IRQ(2)
#define PCM027_PM_5V_IRQ       PCM027_IRQ(3)

/* ITE8152 irqs */
/* add IT8152 IRQs beyond BOARD_END */
#ifdef CONFIG_PCI_HOST_ITE8152
#define IT8152_IRQ(x)   (IRQ_BOARD_END + (x))

/* IRQ-sources in 3 groups - local devices, LPC (serial), and external PCI */
#define IT8152_LD_IRQ_COUNT     9
#define IT8152_LP_IRQ_COUNT     16
#define IT8152_PD_IRQ_COUNT     15

/* Priorities: */
#define IT8152_PD_IRQ(i)        IT8152_IRQ(i)
#define IT8152_LP_IRQ(i)        (IT8152_IRQ(i) + IT8152_PD_IRQ_COUNT)
#define IT8152_LD_IRQ(i)        (IT8152_IRQ(i) + IT8152_PD_IRQ_COUNT + IT8152_LP_IRQ_COUNT)

#define IT8152_LAST_IRQ         IT8152_LD_IRQ(IT8152_LD_IRQ_COUNT - 1)

#if NR_IRQS < (IT8152_LAST_IRQ+1)
#undef NR_IRQS
#define NR_IRQS (IT8152_LAST_IRQ+1)
#endif

#endif /* CONFIG_PCI_HOST_ITE8152 */

#endif /* __ASM_MACH_IRQS_H */
