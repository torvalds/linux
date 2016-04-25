/*
 * arch/arm/mach-pxa/include/mach/pxa3xx-regs.h
 *
 * PXA3xx specific register definitions
 *
 * Copyright (C) 2007 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_PXA3XX_REGS_H
#define __ASM_ARCH_PXA3XX_REGS_H

#include <mach/hardware.h>

/*
 * Oscillator Configuration Register (OSCC)
 */
#define OSCC           io_p2v(0x41350000)  /* Oscillator Configuration Register */

#define OSCC_PEN       (1 << 11)       /* 13MHz POUT */


/*
 * Service Power Management Unit (MPMU)
 */
#define PMCR		__REG(0x40F50000)	/* Power Manager Control Register */
#define PSR		__REG(0x40F50004)	/* Power Manager S2 Status Register */
#define PSPR		__REG(0x40F50008)	/* Power Manager Scratch Pad Register */
#define PCFR		__REG(0x40F5000C)	/* Power Manager General Configuration Register */
#define PWER		__REG(0x40F50010)	/* Power Manager Wake-up Enable Register */
#define PWSR		__REG(0x40F50014)	/* Power Manager Wake-up Status Register */
#define PECR		__REG(0x40F50018)	/* Power Manager EXT_WAKEUP[1:0] Control Register */
#define DCDCSR		__REG(0x40F50080)	/* DC-DC Controller Status Register */
#define PVCR		__REG(0x40F50100)	/* Power Manager Voltage Change Control Register */
#define PCMD(x)		__REG(0x40F50110 + ((x) << 2))

/*
 * Slave Power Management Unit
 */
#define ASCR		__REG(0x40f40000)	/* Application Subsystem Power Status/Configuration */
#define ARSR		__REG(0x40f40004)	/* Application Subsystem Reset Status */
#define AD3ER		__REG(0x40f40008)	/* Application Subsystem Wake-Up from D3 Enable */
#define AD3SR		__REG(0x40f4000c)	/* Application Subsystem Wake-Up from D3 Status */
#define AD2D0ER		__REG(0x40f40010)	/* Application Subsystem Wake-Up from D2 to D0 Enable */
#define AD2D0SR		__REG(0x40f40014)	/* Application Subsystem Wake-Up from D2 to D0 Status */
#define AD2D1ER		__REG(0x40f40018)	/* Application Subsystem Wake-Up from D2 to D1 Enable */
#define AD2D1SR		__REG(0x40f4001c)	/* Application Subsystem Wake-Up from D2 to D1 Status */
#define AD1D0ER		__REG(0x40f40020)	/* Application Subsystem Wake-Up from D1 to D0 Enable */
#define AD1D0SR		__REG(0x40f40024)	/* Application Subsystem Wake-Up from D1 to D0 Status */
#define AGENP		__REG(0x40f4002c)	/* Application Subsystem General Purpose */
#define AD3R		__REG(0x40f40030)	/* Application Subsystem D3 Configuration */
#define AD2R		__REG(0x40f40034)	/* Application Subsystem D2 Configuration */
#define AD1R		__REG(0x40f40038)	/* Application Subsystem D1 Configuration */

/*
 * Application Subsystem Configuration bits.
 */
#define ASCR_RDH		(1 << 31)
#define ASCR_D1S		(1 << 2)
#define ASCR_D2S		(1 << 1)
#define ASCR_D3S		(1 << 0)

/*
 * Application Reset Status bits.
 */
#define ARSR_GPR		(1 << 3)
#define ARSR_LPMR		(1 << 2)
#define ARSR_WDT		(1 << 1)
#define ARSR_HWR		(1 << 0)

/*
 * Application Subsystem Wake-Up bits.
 */
#define ADXER_WRTC		(1 << 31)	/* RTC */
#define ADXER_WOST		(1 << 30)	/* OS Timer */
#define ADXER_WTSI		(1 << 29)	/* Touchscreen */
#define ADXER_WUSBH		(1 << 28)	/* USB host */
#define ADXER_WUSB2		(1 << 26)	/* USB client 2.0 */
#define ADXER_WMSL0		(1 << 24)	/* MSL port 0*/
#define ADXER_WDMUX3		(1 << 23)	/* USB EDMUX3 */
#define ADXER_WDMUX2		(1 << 22)	/* USB EDMUX2 */
#define ADXER_WKP		(1 << 21)	/* Keypad */
#define ADXER_WUSIM1		(1 << 20)	/* USIM Port 1 */
#define ADXER_WUSIM0		(1 << 19)	/* USIM Port 0 */
#define ADXER_WOTG		(1 << 16)	/* USBOTG input */
#define ADXER_MFP_WFLASH	(1 << 15)	/* MFP: Data flash busy */
#define ADXER_MFP_GEN12		(1 << 14)	/* MFP: MMC3/GPIO/OST inputs */
#define ADXER_MFP_WMMC2		(1 << 13)	/* MFP: MMC2 */
#define ADXER_MFP_WMMC1		(1 << 12)	/* MFP: MMC1 */
#define ADXER_MFP_WI2C		(1 << 11)	/* MFP: I2C */
#define ADXER_MFP_WSSP4		(1 << 10)	/* MFP: SSP4 */
#define ADXER_MFP_WSSP3		(1 << 9)	/* MFP: SSP3 */
#define ADXER_MFP_WMAXTRIX	(1 << 8)	/* MFP: matrix keypad */
#define ADXER_MFP_WUART3	(1 << 7)	/* MFP: UART3 */
#define ADXER_MFP_WUART2	(1 << 6)	/* MFP: UART2 */
#define ADXER_MFP_WUART1	(1 << 5)	/* MFP: UART1 */
#define ADXER_MFP_WSSP2		(1 << 4)	/* MFP: SSP2 */
#define ADXER_MFP_WSSP1		(1 << 3)	/* MFP: SSP1 */
#define ADXER_MFP_WAC97		(1 << 2)	/* MFP: AC97 */
#define ADXER_WEXTWAKE1		(1 << 1)	/* External Wake 1 */
#define ADXER_WEXTWAKE0		(1 << 0)	/* External Wake 0 */

/*
 * AD3R/AD2R/AD1R bits.  R2-R5 are only defined for PXA320.
 */
#define ADXR_L2			(1 << 8)
#define ADXR_R5			(1 << 5)
#define ADXR_R4			(1 << 4)
#define ADXR_R3			(1 << 3)
#define ADXR_R2			(1 << 2)
#define ADXR_R1			(1 << 1)
#define ADXR_R0			(1 << 0)

/*
 * Values for PWRMODE CP15 register
 */
#define PXA3xx_PM_S3D4C4	0x07	/* aka deep sleep */
#define PXA3xx_PM_S2D3C4	0x06	/* aka sleep */
#define PXA3xx_PM_S0D2C2	0x03	/* aka standby */
#define PXA3xx_PM_S0D1C2	0x02	/* aka LCD refresh */
#define PXA3xx_PM_S0D0C1	0x01

/*
 * Application Subsystem Clock
 */
#define ACCR		__REG(0x41340000)	/* Application Subsystem Clock Configuration Register */
#define ACSR		__REG(0x41340004)	/* Application Subsystem Clock Status Register */
#define AICSR		__REG(0x41340008)	/* Application Subsystem Interrupt Control/Status Register */
#define CKENA		__REG(0x4134000C)	/* A Clock Enable Register */
#define CKENB		__REG(0x41340010)	/* B Clock Enable Register */
#define CKENC		__REG(0x41340024)	/* C Clock Enable Register */
#define AC97_DIV	__REG(0x41340014)	/* AC97 clock divisor value register */

#define ACCR_XPDIS		(1 << 31)	/* Core PLL Output Disable */
#define ACCR_SPDIS		(1 << 30)	/* System PLL Output Disable */
#define ACCR_D0CS		(1 << 26)	/* D0 Mode Clock Select */
#define ACCR_PCCE		(1 << 11)	/* Power Mode Change Clock Enable */
#define ACCR_DDR_D0CS		(1 << 7)	/* DDR SDRAM clock frequency in D0CS (PXA31x only) */

#define ACCR_SMCFS_MASK		(0x7 << 23)	/* Static Memory Controller Frequency Select */
#define ACCR_SFLFS_MASK		(0x3 << 18)	/* Frequency Select for Internal Memory Controller */
#define ACCR_XSPCLK_MASK	(0x3 << 16)	/* Core Frequency during Frequency Change */
#define ACCR_HSS_MASK		(0x3 << 14)	/* System Bus-Clock Frequency Select */
#define ACCR_DMCFS_MASK		(0x3 << 12)	/* Dynamic Memory Controller Clock Frequency Select */
#define ACCR_XN_MASK		(0x7 << 8)	/* Core PLL Turbo-Mode-to-Run-Mode Ratio */
#define ACCR_XL_MASK		(0x1f)		/* Core PLL Run-Mode-to-Oscillator Ratio */

#define ACCR_SMCFS(x)		(((x) & 0x7) << 23)
#define ACCR_SFLFS(x)		(((x) & 0x3) << 18)
#define ACCR_XSPCLK(x)		(((x) & 0x3) << 16)
#define ACCR_HSS(x)		(((x) & 0x3) << 14)
#define ACCR_DMCFS(x)		(((x) & 0x3) << 12)
#define ACCR_XN(x)		(((x) & 0x7) << 8)
#define ACCR_XL(x)		((x) & 0x1f)

/*
 * Clock Enable Bit
 */
#define CKEN_LCD	1	/* < LCD Clock Enable */
#define CKEN_USBH	2	/* < USB host clock enable */
#define CKEN_CAMERA	3	/* < Camera interface clock enable */
#define CKEN_NAND	4	/* < NAND Flash Controller Clock Enable */
#define CKEN_USB2	6	/* < USB 2.0 client clock enable. */
#define CKEN_DMC	8	/* < Dynamic Memory Controller clock enable */
#define CKEN_SMC	9	/* < Static Memory Controller clock enable */
#define CKEN_ISC	10	/* < Internal SRAM Controller clock enable */
#define CKEN_BOOT	11	/* < Boot rom clock enable */
#define CKEN_MMC1	12	/* < MMC1 Clock enable */
#define CKEN_MMC2	13	/* < MMC2 clock enable */
#define CKEN_KEYPAD	14	/* < Keypand Controller Clock Enable */
#define CKEN_CIR	15	/* < Consumer IR Clock Enable */
#define CKEN_USIM0	17	/* < USIM[0] Clock Enable */
#define CKEN_USIM1	18	/* < USIM[1] Clock Enable */
#define CKEN_TPM	19	/* < TPM clock enable */
#define CKEN_UDC	20	/* < UDC clock enable */
#define CKEN_BTUART	21	/* < BTUART clock enable */
#define CKEN_FFUART	22	/* < FFUART clock enable */
#define CKEN_STUART	23	/* < STUART clock enable */
#define CKEN_AC97	24	/* < AC97 clock enable */
#define CKEN_TOUCH	25	/* < Touch screen Interface Clock Enable */
#define CKEN_SSP1	26	/* < SSP1 clock enable */
#define CKEN_SSP2	27	/* < SSP2 clock enable */
#define CKEN_SSP3	28	/* < SSP3 clock enable */
#define CKEN_SSP4	29	/* < SSP4 clock enable */
#define CKEN_MSL0	30	/* < MSL0 clock enable */
#define CKEN_PWM0	32	/* < PWM[0] clock enable */
#define CKEN_PWM1	33	/* < PWM[1] clock enable */
#define CKEN_I2C	36	/* < I2C clock enable */
#define CKEN_INTC	38	/* < Interrupt controller clock enable */
#define CKEN_GPIO	39	/* < GPIO clock enable */
#define CKEN_1WIRE	40	/* < 1-wire clock enable */
#define CKEN_HSIO2	41	/* < HSIO2 clock enable */
#define CKEN_MINI_IM	48	/* < Mini-IM */
#define CKEN_MINI_LCD	49	/* < Mini LCD */

#define CKEN_MMC3	5	/* < MMC3 Clock Enable */
#define CKEN_MVED	43	/* < MVED clock enable */

/* Note: GCU clock enable bit differs on PXA300/PXA310 and PXA320 */
#define CKEN_PXA300_GCU		42	/* Graphics controller clock enable */
#define CKEN_PXA320_GCU		7	/* Graphics controller clock enable */

#endif /* __ASM_ARCH_PXA3XX_REGS_H */
