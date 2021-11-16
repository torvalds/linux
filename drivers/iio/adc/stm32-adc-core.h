/* SPDX-License-Identifier: GPL-2.0 */
/*
 * This file is part of STM32 ADC driver
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author: Fabrice Gasnier <fabrice.gasnier@st.com>.
 *
 */

#ifndef __STM32_ADC_H
#define __STM32_ADC_H

/*
 * STM32 - ADC global register map
 * ________________________________________________________
 * | Offset |                 Register                    |
 * --------------------------------------------------------
 * | 0x000  |                Master ADC1                  |
 * --------------------------------------------------------
 * | 0x100  |                Slave ADC2                   |
 * --------------------------------------------------------
 * | 0x200  |                Slave ADC3                   |
 * --------------------------------------------------------
 * | 0x300  |         Master & Slave common regs          |
 * --------------------------------------------------------
 */
#define STM32_ADC_MAX_ADCS		3
#define STM32_ADC_OFFSET		0x100
#define STM32_ADCX_COMN_OFFSET		0x300

/* STM32F4 - Registers for each ADC instance */
#define STM32F4_ADC_SR			0x00
#define STM32F4_ADC_CR1			0x04
#define STM32F4_ADC_CR2			0x08
#define STM32F4_ADC_SMPR1		0x0C
#define STM32F4_ADC_SMPR2		0x10
#define STM32F4_ADC_HTR			0x24
#define STM32F4_ADC_LTR			0x28
#define STM32F4_ADC_SQR1		0x2C
#define STM32F4_ADC_SQR2		0x30
#define STM32F4_ADC_SQR3		0x34
#define STM32F4_ADC_JSQR		0x38
#define STM32F4_ADC_JDR1		0x3C
#define STM32F4_ADC_JDR2		0x40
#define STM32F4_ADC_JDR3		0x44
#define STM32F4_ADC_JDR4		0x48
#define STM32F4_ADC_DR			0x4C

/* STM32F4 - common registers for all ADC instances: 1, 2 & 3 */
#define STM32F4_ADC_CSR			(STM32_ADCX_COMN_OFFSET + 0x00)
#define STM32F4_ADC_CCR			(STM32_ADCX_COMN_OFFSET + 0x04)

/* STM32F4_ADC_SR - bit fields */
#define STM32F4_OVR			BIT(5)
#define STM32F4_STRT			BIT(4)
#define STM32F4_EOC			BIT(1)

/* STM32F4_ADC_CR1 - bit fields */
#define STM32F4_OVRIE			BIT(26)
#define STM32F4_RES_SHIFT		24
#define STM32F4_RES_MASK		GENMASK(25, 24)
#define STM32F4_SCAN			BIT(8)
#define STM32F4_EOCIE			BIT(5)

/* STM32F4_ADC_CR2 - bit fields */
#define STM32F4_SWSTART			BIT(30)
#define STM32F4_EXTEN_SHIFT		28
#define STM32F4_EXTEN_MASK		GENMASK(29, 28)
#define STM32F4_EXTSEL_SHIFT		24
#define STM32F4_EXTSEL_MASK		GENMASK(27, 24)
#define STM32F4_EOCS			BIT(10)
#define STM32F4_DDS			BIT(9)
#define STM32F4_DMA			BIT(8)
#define STM32F4_ADON			BIT(0)

/* STM32F4_ADC_CSR - bit fields */
#define STM32F4_OVR3			BIT(21)
#define STM32F4_EOC3			BIT(17)
#define STM32F4_OVR2			BIT(13)
#define STM32F4_EOC2			BIT(9)
#define STM32F4_OVR1			BIT(5)
#define STM32F4_EOC1			BIT(1)

/* STM32F4_ADC_CCR - bit fields */
#define STM32F4_ADC_ADCPRE_SHIFT	16
#define STM32F4_ADC_ADCPRE_MASK		GENMASK(17, 16)

/* STM32H7 - Registers for each ADC instance */
#define STM32H7_ADC_ISR			0x00
#define STM32H7_ADC_IER			0x04
#define STM32H7_ADC_CR			0x08
#define STM32H7_ADC_CFGR		0x0C
#define STM32H7_ADC_SMPR1		0x14
#define STM32H7_ADC_SMPR2		0x18
#define STM32H7_ADC_PCSEL		0x1C
#define STM32H7_ADC_SQR1		0x30
#define STM32H7_ADC_SQR2		0x34
#define STM32H7_ADC_SQR3		0x38
#define STM32H7_ADC_SQR4		0x3C
#define STM32H7_ADC_DR			0x40
#define STM32H7_ADC_DIFSEL		0xC0
#define STM32H7_ADC_CALFACT		0xC4
#define STM32H7_ADC_CALFACT2		0xC8

/* STM32MP1 - ADC2 instance option register */
#define STM32MP1_ADC2_OR		0xD0

/* STM32H7 - common registers for all ADC instances */
#define STM32H7_ADC_CSR			(STM32_ADCX_COMN_OFFSET + 0x00)
#define STM32H7_ADC_CCR			(STM32_ADCX_COMN_OFFSET + 0x08)

/* STM32H7_ADC_ISR - bit fields */
#define STM32MP1_VREGREADY		BIT(12)
#define STM32H7_OVR			BIT(4)
#define STM32H7_EOC			BIT(2)
#define STM32H7_ADRDY			BIT(0)

/* STM32H7_ADC_IER - bit fields */
#define STM32H7_OVRIE			STM32H7_OVR
#define STM32H7_EOCIE			STM32H7_EOC

/* STM32H7_ADC_CR - bit fields */
#define STM32H7_ADCAL			BIT(31)
#define STM32H7_ADCALDIF		BIT(30)
#define STM32H7_DEEPPWD			BIT(29)
#define STM32H7_ADVREGEN		BIT(28)
#define STM32H7_LINCALRDYW6		BIT(27)
#define STM32H7_LINCALRDYW5		BIT(26)
#define STM32H7_LINCALRDYW4		BIT(25)
#define STM32H7_LINCALRDYW3		BIT(24)
#define STM32H7_LINCALRDYW2		BIT(23)
#define STM32H7_LINCALRDYW1		BIT(22)
#define STM32H7_ADCALLIN		BIT(16)
#define STM32H7_BOOST			BIT(8)
#define STM32H7_ADSTP			BIT(4)
#define STM32H7_ADSTART			BIT(2)
#define STM32H7_ADDIS			BIT(1)
#define STM32H7_ADEN			BIT(0)

/* STM32H7_ADC_CFGR bit fields */
#define STM32H7_EXTEN_SHIFT		10
#define STM32H7_EXTEN_MASK		GENMASK(11, 10)
#define STM32H7_EXTSEL_SHIFT		5
#define STM32H7_EXTSEL_MASK		GENMASK(9, 5)
#define STM32H7_RES_SHIFT		2
#define STM32H7_RES_MASK		GENMASK(4, 2)
#define STM32H7_DMNGT_SHIFT		0
#define STM32H7_DMNGT_MASK		GENMASK(1, 0)

enum stm32h7_adc_dmngt {
	STM32H7_DMNGT_DR_ONLY,		/* Regular data in DR only */
	STM32H7_DMNGT_DMA_ONESHOT,	/* DMA one shot mode */
	STM32H7_DMNGT_DFSDM,		/* DFSDM mode */
	STM32H7_DMNGT_DMA_CIRC,		/* DMA circular mode */
};

/* STM32H7_ADC_CALFACT - bit fields */
#define STM32H7_CALFACT_D_SHIFT		16
#define STM32H7_CALFACT_D_MASK		GENMASK(26, 16)
#define STM32H7_CALFACT_S_SHIFT		0
#define STM32H7_CALFACT_S_MASK		GENMASK(10, 0)

/* STM32H7_ADC_CALFACT2 - bit fields */
#define STM32H7_LINCALFACT_SHIFT	0
#define STM32H7_LINCALFACT_MASK		GENMASK(29, 0)

/* STM32H7_ADC_CSR - bit fields */
#define STM32H7_OVR_SLV			BIT(20)
#define STM32H7_EOC_SLV			BIT(18)
#define STM32H7_OVR_MST			BIT(4)
#define STM32H7_EOC_MST			BIT(2)

/* STM32H7_ADC_CCR - bit fields */
#define STM32H7_VBATEN			BIT(24)
#define STM32H7_VREFEN			BIT(22)
#define STM32H7_PRESC_SHIFT		18
#define STM32H7_PRESC_MASK		GENMASK(21, 18)
#define STM32H7_CKMODE_SHIFT		16
#define STM32H7_CKMODE_MASK		GENMASK(17, 16)

/* STM32MP1_ADC2_OR - bit fields */
#define STM32MP1_VDDCOREEN		BIT(0)

/**
 * struct stm32_adc_common - stm32 ADC driver common data (for all instances)
 * @base:		control registers base cpu addr
 * @phys_base:		control registers base physical addr
 * @rate:		clock rate used for analog circuitry
 * @vref_mv:		vref voltage (mv)
 * @lock:		spinlock
 */
struct stm32_adc_common {
	void __iomem			*base;
	phys_addr_t			phys_base;
	unsigned long			rate;
	int				vref_mv;
	spinlock_t			lock;		/* lock for common register */
};

#endif
