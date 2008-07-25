/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (C) 2008 by Sascha Hauer <kernel@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#ifndef __ARCH_ARM_MACH_MX3_CRM_REGS_H__
#define __ARCH_ARM_MACH_MX3_CRM_REGS_H__

#define CKIH_CLK_FREQ           26000000
#define CKIH_CLK_FREQ_27MHZ     27000000
#define CKIL_CLK_FREQ           32768

#define MXC_CCM_BASE		IO_ADDRESS(CCM_BASE_ADDR)

/* Register addresses */
#define MXC_CCM_CCMR		(MXC_CCM_BASE + 0x00)
#define MXC_CCM_PDR0		(MXC_CCM_BASE + 0x04)
#define MXC_CCM_PDR1		(MXC_CCM_BASE + 0x08)
#define MXC_CCM_RCSR		(MXC_CCM_BASE + 0x0C)
#define MXC_CCM_MPCTL		(MXC_CCM_BASE + 0x10)
#define MXC_CCM_UPCTL		(MXC_CCM_BASE + 0x14)
#define MXC_CCM_SRPCTL		(MXC_CCM_BASE + 0x18)
#define MXC_CCM_COSR		(MXC_CCM_BASE + 0x1C)
#define MXC_CCM_CGR0		(MXC_CCM_BASE + 0x20)
#define MXC_CCM_CGR1		(MXC_CCM_BASE + 0x24)
#define MXC_CCM_CGR2		(MXC_CCM_BASE + 0x28)
#define MXC_CCM_WIMR		(MXC_CCM_BASE + 0x2C)
#define MXC_CCM_LDC		(MXC_CCM_BASE + 0x30)
#define MXC_CCM_DCVR0		(MXC_CCM_BASE + 0x34)
#define MXC_CCM_DCVR1		(MXC_CCM_BASE + 0x38)
#define MXC_CCM_DCVR2		(MXC_CCM_BASE + 0x3C)
#define MXC_CCM_DCVR3		(MXC_CCM_BASE + 0x40)
#define MXC_CCM_LTR0		(MXC_CCM_BASE + 0x44)
#define MXC_CCM_LTR1		(MXC_CCM_BASE + 0x48)
#define MXC_CCM_LTR2		(MXC_CCM_BASE + 0x4C)
#define MXC_CCM_LTR3		(MXC_CCM_BASE + 0x50)
#define MXC_CCM_LTBR0		(MXC_CCM_BASE + 0x54)
#define MXC_CCM_LTBR1		(MXC_CCM_BASE + 0x58)
#define MXC_CCM_PMCR0		(MXC_CCM_BASE + 0x5C)
#define MXC_CCM_PMCR1		(MXC_CCM_BASE + 0x60)
#define MXC_CCM_PDR2		(MXC_CCM_BASE + 0x64)

/* Register bit definitions */
#define MXC_CCM_CCMR_WBEN                       (1 << 27)
#define MXC_CCM_CCMR_CSCS                       (1 << 25)
#define MXC_CCM_CCMR_PERCS                      (1 << 24)
#define MXC_CCM_CCMR_SSI1S_OFFSET               18
#define MXC_CCM_CCMR_SSI1S_MASK                 (0x3 << 18)
#define MXC_CCM_CCMR_SSI2S_OFFSET               21
#define MXC_CCM_CCMR_SSI2S_MASK                 (0x3 << 21)
#define MXC_CCM_CCMR_LPM_OFFSET                 14
#define MXC_CCM_CCMR_LPM_MASK                   (0x3 << 14)
#define MXC_CCM_CCMR_FIRS_OFFSET                11
#define MXC_CCM_CCMR_FIRS_MASK                  (0x3 << 11)
#define MXC_CCM_CCMR_UPE                        (1 << 9)
#define MXC_CCM_CCMR_SPE                        (1 << 8)
#define MXC_CCM_CCMR_MDS                        (1 << 7)
#define MXC_CCM_CCMR_SBYCS                      (1 << 4)
#define MXC_CCM_CCMR_MPE                        (1 << 3)
#define MXC_CCM_CCMR_PRCS_OFFSET                1
#define MXC_CCM_CCMR_PRCS_MASK                  (0x3 << 1)

#define MXC_CCM_PDR0_CSI_PODF_OFFSET            26
#define MXC_CCM_PDR0_CSI_PODF_MASK              (0x3F << 26)
#define MXC_CCM_PDR0_CSI_PRDF_OFFSET            23
#define MXC_CCM_PDR0_CSI_PRDF_MASK              (0x7 << 23)
#define MXC_CCM_PDR0_PER_PODF_OFFSET            16
#define MXC_CCM_PDR0_PER_PODF_MASK              (0x1F << 16)
#define MXC_CCM_PDR0_HSP_PODF_OFFSET            11
#define MXC_CCM_PDR0_HSP_PODF_MASK              (0x7 << 11)
#define MXC_CCM_PDR0_NFC_PODF_OFFSET            8
#define MXC_CCM_PDR0_NFC_PODF_MASK              (0x7 << 8)
#define MXC_CCM_PDR0_IPG_PODF_OFFSET            6
#define MXC_CCM_PDR0_IPG_PODF_MASK              (0x3 << 6)
#define MXC_CCM_PDR0_MAX_PODF_OFFSET            3
#define MXC_CCM_PDR0_MAX_PODF_MASK              (0x7 << 3)
#define MXC_CCM_PDR0_MCU_PODF_OFFSET            0
#define MXC_CCM_PDR0_MCU_PODF_MASK              0x7

#define MXC_CCM_PDR0_HSP_DIV_1                  (0x0 << 11)
#define MXC_CCM_PDR0_HSP_DIV_2                  (0x1 << 11)
#define MXC_CCM_PDR0_HSP_DIV_3                  (0x2 << 11)
#define MXC_CCM_PDR0_HSP_DIV_4                  (0x3 << 11)
#define MXC_CCM_PDR0_HSP_DIV_5                  (0x4 << 11)
#define MXC_CCM_PDR0_HSP_DIV_6                  (0x5 << 11)
#define MXC_CCM_PDR0_HSP_DIV_7                  (0x6 << 11)
#define MXC_CCM_PDR0_HSP_DIV_8                  (0x7 << 11)

#define MXC_CCM_PDR0_IPG_DIV_1                  (0x0 << 6)
#define MXC_CCM_PDR0_IPG_DIV_2                  (0x1 << 6)
#define MXC_CCM_PDR0_IPG_DIV_3                  (0x2 << 6)
#define MXC_CCM_PDR0_IPG_DIV_4                  (0x3 << 6)

#define MXC_CCM_PDR0_MAX_DIV_1                  (0x0 << 3)
#define MXC_CCM_PDR0_MAX_DIV_2                  (0x1 << 3)
#define MXC_CCM_PDR0_MAX_DIV_3                  (0x2 << 3)
#define MXC_CCM_PDR0_MAX_DIV_4                  (0x3 << 3)
#define MXC_CCM_PDR0_MAX_DIV_5                  (0x4 << 3)
#define MXC_CCM_PDR0_MAX_DIV_6                  (0x5 << 3)
#define MXC_CCM_PDR0_MAX_DIV_7                  (0x6 << 3)
#define MXC_CCM_PDR0_MAX_DIV_8                  (0x7 << 3)

#define MXC_CCM_PDR0_NFC_DIV_1                  (0x0 << 8)
#define MXC_CCM_PDR0_NFC_DIV_2                  (0x1 << 8)
#define MXC_CCM_PDR0_NFC_DIV_3                  (0x2 << 8)
#define MXC_CCM_PDR0_NFC_DIV_4                  (0x3 << 8)
#define MXC_CCM_PDR0_NFC_DIV_5                  (0x4 << 8)
#define MXC_CCM_PDR0_NFC_DIV_6                  (0x5 << 8)
#define MXC_CCM_PDR0_NFC_DIV_7                  (0x6 << 8)
#define MXC_CCM_PDR0_NFC_DIV_8                  (0x7 << 8)

#define MXC_CCM_PDR0_MCU_DIV_1                  0x0
#define MXC_CCM_PDR0_MCU_DIV_2                  0x1
#define MXC_CCM_PDR0_MCU_DIV_3                  0x2
#define MXC_CCM_PDR0_MCU_DIV_4                  0x3
#define MXC_CCM_PDR0_MCU_DIV_5                  0x4
#define MXC_CCM_PDR0_MCU_DIV_6                  0x5
#define MXC_CCM_PDR0_MCU_DIV_7                  0x6
#define MXC_CCM_PDR0_MCU_DIV_8                  0x7

#define MXC_CCM_PDR1_USB_PRDF_OFFSET            30
#define MXC_CCM_PDR1_USB_PRDF_MASK              (0x3 << 30)
#define MXC_CCM_PDR1_USB_PODF_OFFSET            27
#define MXC_CCM_PDR1_USB_PODF_MASK              (0x7 << 27)
#define MXC_CCM_PDR1_FIRI_PRE_PODF_OFFSET       24
#define MXC_CCM_PDR1_FIRI_PRE_PODF_MASK         (0x7 << 24)
#define MXC_CCM_PDR1_FIRI_PODF_OFFSET           18
#define MXC_CCM_PDR1_FIRI_PODF_MASK             (0x3F << 18)
#define MXC_CCM_PDR1_SSI2_PRE_PODF_OFFSET       15
#define MXC_CCM_PDR1_SSI2_PRE_PODF_MASK         (0x7 << 15)
#define MXC_CCM_PDR1_SSI2_PODF_OFFSET           9
#define MXC_CCM_PDR1_SSI2_PODF_MASK             (0x3F << 9)
#define MXC_CCM_PDR1_SSI1_PRE_PODF_OFFSET       6
#define MXC_CCM_PDR1_SSI1_PRE_PODF_MASK         (0x7 << 6)
#define MXC_CCM_PDR1_SSI1_PODF_OFFSET           0
#define MXC_CCM_PDR1_SSI1_PODF_MASK             0x3F

/* Bit definitions for RCSR */
#define MXC_CCM_RCSR_NF16B			0x80000000

/* Bit definitions for both MCU, USB and SR PLL control registers */
#define MXC_CCM_PCTL_BRM                        0x80000000
#define MXC_CCM_PCTL_PD_OFFSET                  26
#define MXC_CCM_PCTL_PD_MASK                    (0xF << 26)
#define MXC_CCM_PCTL_MFD_OFFSET                 16
#define MXC_CCM_PCTL_MFD_MASK                   (0x3FF << 16)
#define MXC_CCM_PCTL_MFI_OFFSET                 10
#define MXC_CCM_PCTL_MFI_MASK                   (0xF << 10)
#define MXC_CCM_PCTL_MFN_OFFSET                 0
#define MXC_CCM_PCTL_MFN_MASK                   0x3FF

#define MXC_CCM_CGR0_SD_MMC1_OFFSET             0
#define MXC_CCM_CGR0_SD_MMC1_MASK               (0x3 << 0)
#define MXC_CCM_CGR0_SD_MMC2_OFFSET             2
#define MXC_CCM_CGR0_SD_MMC2_MASK               (0x3 << 2)
#define MXC_CCM_CGR0_GPT_OFFSET                 4
#define MXC_CCM_CGR0_GPT_MASK                   (0x3 << 4)
#define MXC_CCM_CGR0_EPIT1_OFFSET               6
#define MXC_CCM_CGR0_EPIT1_MASK                 (0x3 << 6)
#define MXC_CCM_CGR0_EPIT2_OFFSET               8
#define MXC_CCM_CGR0_EPIT2_MASK                 (0x3 << 8)
#define MXC_CCM_CGR0_IIM_OFFSET                 10
#define MXC_CCM_CGR0_IIM_MASK                   (0x3 << 10)
#define MXC_CCM_CGR0_ATA_OFFSET                 12
#define MXC_CCM_CGR0_ATA_MASK                   (0x3 << 12)
#define MXC_CCM_CGR0_SDMA_OFFSET                14
#define MXC_CCM_CGR0_SDMA_MASK                  (0x3 << 14)
#define MXC_CCM_CGR0_CSPI3_OFFSET               16
#define MXC_CCM_CGR0_CSPI3_MASK                 (0x3 << 16)
#define MXC_CCM_CGR0_RNG_OFFSET                 18
#define MXC_CCM_CGR0_RNG_MASK                   (0x3 << 18)
#define MXC_CCM_CGR0_UART1_OFFSET               20
#define MXC_CCM_CGR0_UART1_MASK                 (0x3 << 20)
#define MXC_CCM_CGR0_UART2_OFFSET               22
#define MXC_CCM_CGR0_UART2_MASK                 (0x3 << 22)
#define MXC_CCM_CGR0_SSI1_OFFSET                24
#define MXC_CCM_CGR0_SSI1_MASK                  (0x3 << 24)
#define MXC_CCM_CGR0_I2C1_OFFSET                26
#define MXC_CCM_CGR0_I2C1_MASK                  (0x3 << 26)
#define MXC_CCM_CGR0_I2C2_OFFSET                28
#define MXC_CCM_CGR0_I2C2_MASK                  (0x3 << 28)
#define MXC_CCM_CGR0_I2C3_OFFSET                30
#define MXC_CCM_CGR0_I2C3_MASK                  (0x3 << 30)

#define MXC_CCM_CGR1_HANTRO_OFFSET              0
#define MXC_CCM_CGR1_HANTRO_MASK                (0x3 << 0)
#define MXC_CCM_CGR1_MEMSTICK1_OFFSET           2
#define MXC_CCM_CGR1_MEMSTICK1_MASK             (0x3 << 2)
#define MXC_CCM_CGR1_MEMSTICK2_OFFSET           4
#define MXC_CCM_CGR1_MEMSTICK2_MASK             (0x3 << 4)
#define MXC_CCM_CGR1_CSI_OFFSET                 6
#define MXC_CCM_CGR1_CSI_MASK                   (0x3 << 6)
#define MXC_CCM_CGR1_RTC_OFFSET                 8
#define MXC_CCM_CGR1_RTC_MASK                   (0x3 << 8)
#define MXC_CCM_CGR1_WDOG_OFFSET                10
#define MXC_CCM_CGR1_WDOG_MASK                  (0x3 << 10)
#define MXC_CCM_CGR1_PWM_OFFSET                 12
#define MXC_CCM_CGR1_PWM_MASK                   (0x3 << 12)
#define MXC_CCM_CGR1_SIM_OFFSET                 14
#define MXC_CCM_CGR1_SIM_MASK                   (0x3 << 14)
#define MXC_CCM_CGR1_ECT_OFFSET                 16
#define MXC_CCM_CGR1_ECT_MASK                   (0x3 << 16)
#define MXC_CCM_CGR1_USBOTG_OFFSET              18
#define MXC_CCM_CGR1_USBOTG_MASK                (0x3 << 18)
#define MXC_CCM_CGR1_KPP_OFFSET                 20
#define MXC_CCM_CGR1_KPP_MASK                   (0x3 << 20)
#define MXC_CCM_CGR1_IPU_OFFSET                 22
#define MXC_CCM_CGR1_IPU_MASK                   (0x3 << 22)
#define MXC_CCM_CGR1_UART3_OFFSET               24
#define MXC_CCM_CGR1_UART3_MASK                 (0x3 << 24)
#define MXC_CCM_CGR1_UART4_OFFSET               26
#define MXC_CCM_CGR1_UART4_MASK                 (0x3 << 26)
#define MXC_CCM_CGR1_UART5_OFFSET               28
#define MXC_CCM_CGR1_UART5_MASK                 (0x3 << 28)
#define MXC_CCM_CGR1_OWIRE_OFFSET               30
#define MXC_CCM_CGR1_OWIRE_MASK                 (0x3 << 30)

#define MXC_CCM_CGR2_SSI2_OFFSET                0
#define MXC_CCM_CGR2_SSI2_MASK                  (0x3 << 0)
#define MXC_CCM_CGR2_CSPI1_OFFSET               2
#define MXC_CCM_CGR2_CSPI1_MASK                 (0x3 << 2)
#define MXC_CCM_CGR2_CSPI2_OFFSET               4
#define MXC_CCM_CGR2_CSPI2_MASK                 (0x3 << 4)
#define MXC_CCM_CGR2_GACC_OFFSET                6
#define MXC_CCM_CGR2_GACC_MASK                  (0x3 << 6)
#define MXC_CCM_CGR2_EMI_OFFSET                 8
#define MXC_CCM_CGR2_EMI_MASK                   (0x3 << 8)
#define MXC_CCM_CGR2_RTIC_OFFSET                10
#define MXC_CCM_CGR2_RTIC_MASK                  (0x3 << 10)
#define MXC_CCM_CGR2_FIRI_OFFSET                12
#define MXC_CCM_CGR2_FIRI_MASK                  (0x3 << 12)
#define MXC_CCM_CGR2_IPMUX1_OFFSET              14
#define MXC_CCM_CGR2_IPMUX1_MASK                (0x3 << 14)
#define MXC_CCM_CGR2_IPMUX2_OFFSET              16
#define MXC_CCM_CGR2_IPMUX2_MASK                (0x3 << 16)

/* These new CGR2 bits are added in MX32 */
#define MXC_CCM_CGR2_APMSYSCLKSEL_OFFSET	18
#define MXC_CCM_CGR2_APMSYSCLKSEL_MASK		(0x3 << 18)
#define MXC_CCM_CGR2_APMSSICLKSEL_OFFSET	20
#define MXC_CCM_CGR2_APMSSICLKSEL_MASK		(0x3 << 20)
#define MXC_CCM_CGR2_APMPERCLKSEL_OFFSET	22
#define MXC_CCM_CGR2_APMPERCLKSEL_MASK		(0x3 << 22)
#define MXC_CCM_CGR2_MXCCLKENSEL_OFFSET		24
#define MXC_CCM_CGR2_MXCCLKENSEL_MASK		(0x1 << 24)
#define MXC_CCM_CGR2_CHIKCAMPEN_OFFSET		25
#define MXC_CCM_CGR2_CHIKCAMPEN_MASK		(0x1 << 25)
#define MXC_CCM_CGR2_OVRVPUBUSY_OFFSET		26
#define MXC_CCM_CGR2_OVRVPUBUSY_MASK		(0x1 << 26)
#define MXC_CCM_CGR2_APMENA_OFFSET		30
#define MXC_CCM_CGR2_AOMENA_MASK		(0x1 << 30)

/*
 * LTR0 register offsets
 */
#define MXC_CCM_LTR0_DIV3CK_OFFSET              1
#define MXC_CCM_LTR0_DIV3CK_MASK                (0x3 << 1)
#define MXC_CCM_LTR0_DNTHR_OFFSET               16
#define MXC_CCM_LTR0_DNTHR_MASK                 (0x3F << 16)
#define MXC_CCM_LTR0_UPTHR_OFFSET               22
#define MXC_CCM_LTR0_UPTHR_MASK                 (0x3F << 22)

/*
 * LTR1 register offsets
 */
#define MXC_CCM_LTR1_PNCTHR_OFFSET              0
#define MXC_CCM_LTR1_PNCTHR_MASK                0x3F
#define MXC_CCM_LTR1_UPCNT_OFFSET               6
#define MXC_CCM_LTR1_UPCNT_MASK                 (0xFF << 6)
#define MXC_CCM_LTR1_DNCNT_OFFSET               14
#define MXC_CCM_LTR1_DNCNT_MASK                 (0xFF << 14)
#define MXC_CCM_LTR1_LTBRSR_MASK                0x400000
#define MXC_CCM_LTR1_LTBRSR_OFFSET              22
#define MXC_CCM_LTR1_LTBRSR                     0x400000
#define MXC_CCM_LTR1_LTBRSH                     0x800000

/*
 * LTR2 bit definitions. x ranges from 0 for WSW9 to 6 for WSW15
 */
#define MXC_CCM_LTR2_WSW_OFFSET(x)              (11 + (x) * 3)
#define MXC_CCM_LTR2_WSW_MASK(x)                (0x7 << \
					MXC_CCM_LTR2_WSW_OFFSET((x)))
#define MXC_CCM_LTR2_EMAC_OFFSET                0
#define MXC_CCM_LTR2_EMAC_MASK                  0x1FF

/*
 * LTR3 bit definitions. x ranges from 0 for WSW0 to 8 for WSW8
 */
#define MXC_CCM_LTR3_WSW_OFFSET(x)              (5 + (x) * 3)
#define MXC_CCM_LTR3_WSW_MASK(x)                (0x7 << \
					MXC_CCM_LTR3_WSW_OFFSET((x)))

#define MXC_CCM_PMCR0_DFSUP1                    0x80000000
#define MXC_CCM_PMCR0_DFSUP1_SPLL               (0 << 31)
#define MXC_CCM_PMCR0_DFSUP1_MPLL               (1 << 31)
#define MXC_CCM_PMCR0_DFSUP0                    0x40000000
#define MXC_CCM_PMCR0_DFSUP0_PLL                (0 << 30)
#define MXC_CCM_PMCR0_DFSUP0_PDR                (1 << 30)
#define MXC_CCM_PMCR0_DFSUP_MASK                (0x3 << 30)

#define DVSUP_TURBO				0
#define DVSUP_HIGH				1
#define DVSUP_MEDIUM				2
#define DVSUP_LOW				3
#define MXC_CCM_PMCR0_DVSUP_TURBO               (DVSUP_TURBO << 28)
#define MXC_CCM_PMCR0_DVSUP_HIGH                (DVSUP_HIGH << 28)
#define MXC_CCM_PMCR0_DVSUP_MEDIUM              (DVSUP_MEDIUM << 28)
#define MXC_CCM_PMCR0_DVSUP_LOW                 (DVSUP_LOW << 28)
#define MXC_CCM_PMCR0_DVSUP_OFFSET              28
#define MXC_CCM_PMCR0_DVSUP_MASK                (0x3 << 28)
#define MXC_CCM_PMCR0_UDSC                      0x08000000
#define MXC_CCM_PMCR0_UDSC_MASK                 (1 << 27)
#define MXC_CCM_PMCR0_UDSC_UP                   (1 << 27)
#define MXC_CCM_PMCR0_UDSC_DOWN                 (0 << 27)

#define MXC_CCM_PMCR0_VSCNT_1                   (0x0 << 24)
#define MXC_CCM_PMCR0_VSCNT_2                   (0x1 << 24)
#define MXC_CCM_PMCR0_VSCNT_3                   (0x2 << 24)
#define MXC_CCM_PMCR0_VSCNT_4                   (0x3 << 24)
#define MXC_CCM_PMCR0_VSCNT_5                   (0x4 << 24)
#define MXC_CCM_PMCR0_VSCNT_6                   (0x5 << 24)
#define MXC_CCM_PMCR0_VSCNT_7                   (0x6 << 24)
#define MXC_CCM_PMCR0_VSCNT_8                   (0x7 << 24)
#define MXC_CCM_PMCR0_VSCNT_OFFSET              24
#define MXC_CCM_PMCR0_VSCNT_MASK                (0x7 << 24)
#define MXC_CCM_PMCR0_DVFEV                     0x00800000
#define MXC_CCM_PMCR0_DVFIS                     0x00400000
#define MXC_CCM_PMCR0_LBMI                      0x00200000
#define MXC_CCM_PMCR0_LBFL                      0x00100000
#define MXC_CCM_PMCR0_LBCF_4                    (0x0 << 18)
#define MXC_CCM_PMCR0_LBCF_8                    (0x1 << 18)
#define MXC_CCM_PMCR0_LBCF_12                   (0x2 << 18)
#define MXC_CCM_PMCR0_LBCF_16                   (0x3 << 18)
#define MXC_CCM_PMCR0_LBCF_OFFSET               18
#define MXC_CCM_PMCR0_LBCF_MASK                 (0x3 << 18)
#define MXC_CCM_PMCR0_PTVIS                     0x00020000
#define MXC_CCM_PMCR0_UPDTEN                    0x00010000
#define MXC_CCM_PMCR0_UPDTEN_MASK               (0x1 << 16)
#define MXC_CCM_PMCR0_FSVAIM                    0x00008000
#define MXC_CCM_PMCR0_FSVAI_OFFSET              13
#define MXC_CCM_PMCR0_FSVAI_MASK                (0x3 << 13)
#define MXC_CCM_PMCR0_DPVCR                     0x00001000
#define MXC_CCM_PMCR0_DPVV                      0x00000800
#define MXC_CCM_PMCR0_WFIM                      0x00000400
#define MXC_CCM_PMCR0_DRCE3                     0x00000200
#define MXC_CCM_PMCR0_DRCE2                     0x00000100
#define MXC_CCM_PMCR0_DRCE1                     0x00000080
#define MXC_CCM_PMCR0_DRCE0                     0x00000040
#define MXC_CCM_PMCR0_DCR                       0x00000020
#define MXC_CCM_PMCR0_DVFEN                     0x00000010
#define MXC_CCM_PMCR0_PTVAIM                    0x00000008
#define MXC_CCM_PMCR0_PTVAI_OFFSET              1
#define MXC_CCM_PMCR0_PTVAI_MASK                (0x3 << 1)
#define MXC_CCM_PMCR0_DPTEN                     0x00000001

#define MXC_CCM_PMCR1_DVGP_OFFSET               0
#define MXC_CCM_PMCR1_DVGP_MASK                 (0xF)

#define MXC_CCM_PMCR1_PLLRDIS                      (0x1 << 7)
#define MXC_CCM_PMCR1_EMIRQ_EN                      (0x1 << 8)

#define MXC_CCM_DCVR_ULV_MASK                   (0x3FF << 22)
#define MXC_CCM_DCVR_ULV_OFFSET                 22
#define MXC_CCM_DCVR_LLV_MASK                   (0x3FF << 12)
#define MXC_CCM_DCVR_LLV_OFFSET                 12
#define MXC_CCM_DCVR_ELV_MASK                   (0x3FF << 2)
#define MXC_CCM_DCVR_ELV_OFFSET                 2

#define MXC_CCM_PDR2_MST2_PDF_MASK              (0x3F << 7)
#define MXC_CCM_PDR2_MST2_PDF_OFFSET            7
#define MXC_CCM_PDR2_MST1_PDF_MASK              0x3F
#define MXC_CCM_PDR2_MST1_PDF_OFFSET            0

#define MXC_CCM_COSR_CLKOSEL_MASK               0x0F
#define MXC_CCM_COSR_CLKOSEL_OFFSET             0
#define MXC_CCM_COSR_CLKOUTDIV_MASK             (0x07 << 6)
#define MXC_CCM_COSR_CLKOUTDIV_OFFSET           6
#define MXC_CCM_COSR_CLKOEN                     (1 << 9)

/*
 * PMCR0 register offsets
 */
#define MXC_CCM_PMCR0_LBFL_OFFSET   20
#define MXC_CCM_PMCR0_DFSUP0_OFFSET 30
#define MXC_CCM_PMCR0_DFSUP1_OFFSET 31

#endif				/* __ARCH_ARM_MACH_MX3_CRM_REGS_H__ */
