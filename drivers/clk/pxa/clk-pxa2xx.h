/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __CLK_PXA2XX_H
#define __CLK_PXA2XX_H

#define CCCR		(0x0000)  /* Core Clock Configuration Register */
#define CCSR		(0x000C)  /* Core Clock Status Register */
#define CKEN		(0x0004)  /* Clock Enable Register */
#define OSCC		(0x0008)  /* Oscillator Configuration Register */

#define CCCR_N_MASK	0x0380	/* Run Mode Frequency to Turbo Mode Frequency Multiplier */
#define CCCR_M_MASK	0x0060	/* Memory Frequency to Run Mode Frequency Multiplier */
#define CCCR_L_MASK	0x001f	/* Crystal Frequency to Memory Frequency Multiplier */

#define CCCR_CPDIS_BIT	(31)
#define CCCR_PPDIS_BIT	(30)
#define CCCR_LCD_26_BIT	(27)
#define CCCR_A_BIT	(25)

#define CCSR_N2_MASK	CCCR_N_MASK
#define CCSR_M_MASK	CCCR_M_MASK
#define CCSR_L_MASK	CCCR_L_MASK
#define CCSR_N2_SHIFT	7

#define CKEN_AC97CONF   (31)    /* AC97 Controller Configuration */
#define CKEN_CAMERA	(24)	/* Camera Interface Clock Enable */
#define CKEN_SSP1	(23)	/* SSP1 Unit Clock Enable */
#define CKEN_MEMC	(22)	/* Memory Controller Clock Enable */
#define CKEN_MEMSTK	(21)	/* Memory Stick Host Controller */
#define CKEN_IM		(20)	/* Internal Memory Clock Enable */
#define CKEN_KEYPAD	(19)	/* Keypad Interface Clock Enable */
#define CKEN_USIM	(18)	/* USIM Unit Clock Enable */
#define CKEN_MSL	(17)	/* MSL Unit Clock Enable */
#define CKEN_LCD	(16)	/* LCD Unit Clock Enable */
#define CKEN_PWRI2C	(15)	/* PWR I2C Unit Clock Enable */
#define CKEN_I2C	(14)	/* I2C Unit Clock Enable */
#define CKEN_FICP	(13)	/* FICP Unit Clock Enable */
#define CKEN_MMC	(12)	/* MMC Unit Clock Enable */
#define CKEN_USB	(11)	/* USB Unit Clock Enable */
#define CKEN_ASSP	(10)	/* ASSP (SSP3) Clock Enable */
#define CKEN_USBHOST	(10)	/* USB Host Unit Clock Enable */
#define CKEN_OSTIMER	(9)	/* OS Timer Unit Clock Enable */
#define CKEN_NSSP	(9)	/* NSSP (SSP2) Clock Enable */
#define CKEN_I2S	(8)	/* I2S Unit Clock Enable */
#define CKEN_BTUART	(7)	/* BTUART Unit Clock Enable */
#define CKEN_FFUART	(6)	/* FFUART Unit Clock Enable */
#define CKEN_STUART	(5)	/* STUART Unit Clock Enable */
#define CKEN_HWUART	(4)	/* HWUART Unit Clock Enable */
#define CKEN_SSP3	(4)	/* SSP3 Unit Clock Enable */
#define CKEN_SSP	(3)	/* SSP Unit Clock Enable */
#define CKEN_SSP2	(3)	/* SSP2 Unit Clock Enable */
#define CKEN_AC97	(2)	/* AC97 Unit Clock Enable */
#define CKEN_PWM1	(1)	/* PWM1 Clock Enable */
#define CKEN_PWM0	(0)	/* PWM0 Clock Enable */

#define OSCC_OON	(1 << 1)	/* 32.768kHz OON (write-once only bit) */
#define OSCC_OOK	(1 << 0)	/* 32.768kHz OOK (read-only bit) */

#endif
