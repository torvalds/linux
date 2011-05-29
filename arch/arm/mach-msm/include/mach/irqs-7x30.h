/* Copyright (c) 2009, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __ASM_ARCH_MSM_IRQS_7X30_H
#define __ASM_ARCH_MSM_IRQS_7X30_H

/* MSM ACPU Interrupt Numbers */

#define INT_DEBUG_TIMER_EXP	0
#define INT_GPT0_TIMER_EXP	1
#define INT_GPT1_TIMER_EXP	2
#define INT_WDT0_ACCSCSSBARK	3
#define INT_WDT1_ACCSCSSBARK	4
#define INT_AVS_SVIC		5
#define INT_AVS_SVIC_SW_DONE	6
#define INT_SC_DBG_RX_FULL	7
#define INT_SC_DBG_TX_EMPTY	8
#define INT_ARM11_PM		9
#define INT_AVS_REQ_DOWN	10
#define INT_AVS_REQ_UP		11
#define INT_SC_ACG		12
/* SCSS_VICFIQSTS0[13:15] are RESERVED */
#define INT_L2_SVICCPUIRPTREQ	16
#define INT_L2_SVICDMANSIRPTREQ 17
#define INT_L2_SVICDMASIRPTREQ  18
#define INT_L2_SVICSLVIRPTREQ	19
#define INT_AD5A_MPROC_APPS_0	20
#define INT_AD5A_MPROC_APPS_1	21
#define INT_A9_M2A_0		22
#define INT_A9_M2A_1		23
#define INT_A9_M2A_2		24
#define INT_A9_M2A_3		25
#define INT_A9_M2A_4		26
#define INT_A9_M2A_5		27
#define INT_A9_M2A_6		28
#define INT_A9_M2A_7		29
#define INT_A9_M2A_8		30
#define INT_A9_M2A_9		31

#define INT_AXI_EBI1_SC		(32 + 0)
#define INT_IMEM_ERR		(32 + 1)
#define INT_AXI_EBI0_SC		(32 + 2)
#define INT_PBUS_SC_IRQC	(32 + 3)
#define INT_PERPH_BUS_BPM	(32 + 4)
#define INT_CC_TEMP_SENSE	(32 + 5)
#define INT_UXMC_EBI0		(32 + 6)
#define INT_UXMC_EBI1		(32 + 7)
#define INT_EBI2_OP_DONE	(32 + 8)
#define INT_EBI2_WR_ER_DONE	(32 + 9)
#define INT_TCSR_SPSS_CE	(32 + 10)
#define INT_EMDH		(32 + 11)
#define INT_PMDH		(32 + 12)
#define INT_MDC			(32 + 13)
#define INT_MIDI_TO_SUPSS	(32 + 14)
#define INT_LPA_2		(32 + 15)
#define INT_GPIO_GROUP1_SECURE	(32 + 16)
#define INT_GPIO_GROUP2_SECURE	(32 + 17)
#define INT_GPIO_GROUP1		(32 + 18)
#define INT_GPIO_GROUP2		(32 + 19)
#define INT_MPRPH_SOFTRESET	(32 + 20)
#define INT_PWB_I2C		(32 + 21)
#define INT_PWB_I2C_2		(32 + 22)
#define INT_TSSC_SAMPLE		(32 + 23)
#define INT_TSSC_PENUP		(32 + 24)
#define INT_TCHSCRN_SSBI	(32 + 25)
#define INT_FM_RDS		(32 + 26)
#define INT_KEYSENSE 		(32 + 27)
#define INT_USB_OTG_HS		(32 + 28)
#define INT_USB_OTG_HS2		(32 + 29)
#define INT_USB_OTG_HS3		(32 + 30)
#define INT_CSI			(32 + 31)

#define INT_SPI_OUTPUT		(64 + 0)
#define INT_SPI_INPUT		(64 + 1)
#define INT_SPI_ERROR		(64 + 2)
#define INT_UART1		(64 + 3)
#define INT_UART1_RX		(64 + 4)
#define INT_UART2		(64 + 5)
#define INT_UART2_RX		(64 + 6)
#define INT_UART3		(64 + 7)
#define INT_UART3_RX		(64 + 8)
#define INT_UART1DM_IRQ		(64 + 9)
#define INT_UART1DM_RX		(64 + 10)
#define INT_UART2DM_IRQ		(64 + 11)
#define INT_UART2DM_RX		(64 + 12)
#define INT_TSIF		(64 + 13)
#define INT_ADM_SC1		(64 + 14)
#define INT_ADM_SC2		(64 + 15)
#define INT_MDP			(64 + 16)
#define INT_VPE			(64 + 17)
#define INT_GRP_2D		(64 + 18)
#define INT_GRP_3D		(64 + 19)
#define INT_ROTATOR		(64 + 20)
#define INT_MFC720		(64 + 21)
#define INT_JPEG		(64 + 22)
#define INT_VFE			(64 + 23)
#define INT_TV_ENC		(64 + 24)
#define INT_PMIC_SSBI		(64 + 25)
#define INT_MPM_1		(64 + 26)
#define INT_TCSR_SPSS_SAMPLE	(64 + 27)
#define INT_TCSR_SPSS_PENUP	(64 + 28)
#define INT_MPM_2		(64 + 29)
#define INT_SDC1_0		(64 + 30)
#define INT_SDC1_1		(64 + 31)

#define INT_SDC3_0		(96 + 0)
#define INT_SDC3_1		(96 + 1)
#define INT_SDC2_0		(96 + 2)
#define INT_SDC2_1		(96 + 3)
#define INT_SDC4_0		(96 + 4)
#define INT_SDC4_1		(96 + 5)
#define INT_PWB_QUP_IN		(96 + 6)
#define INT_PWB_QUP_OUT		(96 + 7)
#define INT_PWB_QUP_ERR		(96 + 8)
#define INT_SCSS_WDT0_BITE	(96 + 9)
/* SCSS_VICFIQSTS3[10:31] are RESERVED */

/* Retrofit universal macro names */
#define INT_ADM_AARM		INT_ADM_SC2
#define INT_USB_HS   		INT_USB_OTG_HS
#define INT_USB_OTG   		INT_USB_OTG_HS
#define INT_TCHSCRN1 		INT_TSSC_SAMPLE
#define INT_TCHSCRN2 		INT_TSSC_PENUP
#define INT_GP_TIMER_EXP 	INT_GPT0_TIMER_EXP
#define INT_ADSP_A11 		INT_AD5A_MPROC_APPS_0
#define INT_ADSP_A9_A11 	INT_AD5A_MPROC_APPS_1
#define INT_MDDI_EXT		INT_EMDH
#define INT_MDDI_PRI		INT_PMDH
#define INT_MDDI_CLIENT		INT_MDC
#define INT_NAND_WR_ER_DONE	INT_EBI2_WR_ER_DONE
#define INT_NAND_OP_DONE	INT_EBI2_OP_DONE

#define NR_MSM_IRQS		128
#define NR_GPIO_IRQS		182
#define PMIC8058_IRQ_BASE	(NR_MSM_IRQS + NR_GPIO_IRQS)
#define NR_PMIC8058_GPIO_IRQS	40
#define NR_PMIC8058_MPP_IRQS	12
#define NR_PMIC8058_MISC_IRQS	8
#define NR_PMIC8058_IRQS	(NR_PMIC8058_GPIO_IRQS +\
				NR_PMIC8058_MPP_IRQS +\
				NR_PMIC8058_MISC_IRQS)
#define NR_BOARD_IRQS		NR_PMIC8058_IRQS

#endif /* __ASM_ARCH_MSM_IRQS_7X30_H */
