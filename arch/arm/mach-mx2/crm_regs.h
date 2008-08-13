/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
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

#ifndef __ARCH_ARM_MACH_MX2_CRM_REGS_H__
#define __ARCH_ARM_MACH_MX2_CRM_REGS_H__

#include <mach/hardware.h>

/* Register offsets */
#define CCM_CSCR                (IO_ADDRESS(CCM_BASE_ADDR) + 0x0)
#define CCM_MPCTL0              (IO_ADDRESS(CCM_BASE_ADDR) + 0x4)
#define CCM_MPCTL1              (IO_ADDRESS(CCM_BASE_ADDR) + 0x8)
#define CCM_SPCTL0              (IO_ADDRESS(CCM_BASE_ADDR) + 0xC)
#define CCM_SPCTL1              (IO_ADDRESS(CCM_BASE_ADDR) + 0x10)
#define CCM_OSC26MCTL           (IO_ADDRESS(CCM_BASE_ADDR) + 0x14)
#define CCM_PCDR0               (IO_ADDRESS(CCM_BASE_ADDR) + 0x18)
#define CCM_PCDR1               (IO_ADDRESS(CCM_BASE_ADDR) + 0x1c)
#define CCM_PCCR0               (IO_ADDRESS(CCM_BASE_ADDR) + 0x20)
#define CCM_PCCR1               (IO_ADDRESS(CCM_BASE_ADDR) + 0x24)
#define CCM_CCSR                (IO_ADDRESS(CCM_BASE_ADDR) + 0x28)
#define CCM_PMCTL               (IO_ADDRESS(CCM_BASE_ADDR) + 0x2c)
#define CCM_PMCOUNT             (IO_ADDRESS(CCM_BASE_ADDR) + 0x30)
#define CCM_WKGDCTL             (IO_ADDRESS(CCM_BASE_ADDR) + 0x34)

#define CCM_CSCR_USB_OFFSET     28
#define CCM_CSCR_USB_MASK       (0x7 << 28)
#define CCM_CSCR_SD_OFFSET      24
#define CCM_CSCR_SD_MASK        (0x3 << 24)
#define CCM_CSCR_SSI2           (1 << 23)
#define CCM_CSCR_SSI2_OFFSET    23
#define CCM_CSCR_SSI1           (1 << 22)
#define CCM_CSCR_SSI1_OFFSET    22
#define CCM_CSCR_VPU           (1 << 21)
#define CCM_CSCR_VPU_OFFSET    21
#define CCM_CSCR_MSHC           (1 << 20)
#define CCM_CSCR_SPLLRES        (1 << 19)
#define CCM_CSCR_MPLLRES        (1 << 18)
#define CCM_CSCR_SP             (1 << 17)
#define CCM_CSCR_MCU            (1 << 16)
/* CCM_CSCR_ARM_xxx just be avaliable on i.MX27 TO2*/
#define CCM_CSCR_ARM_SRC        (1 << 15)
#define CCM_CSCR_ARM_OFFSET     12
#define CCM_CSCR_ARM_MASK       (0x3 << 12)
/* CCM_CSCR_ARM_xxx just be avaliable on i.MX27 TO2*/
#define CCM_CSCR_PRESC_OFFSET   13
#define CCM_CSCR_PRESC_MASK     (0x7 << 13)
#define CCM_CSCR_BCLK_OFFSET    9
#define CCM_CSCR_BCLK_MASK      (0xf << 9)
#define CCM_CSCR_IPDIV_OFFSET   8
#define CCM_CSCR_IPDIV          (1 << 8)
/* CCM_CSCR_AHB_xxx just be avaliable on i.MX27 TO2*/
#define CCM_CSCR_AHB_OFFSET     8
#define CCM_CSCR_AHB_MASK       (0x3 << 8)
/* CCM_CSCR_AHB_xxx just be avaliable on i.MX27 TO2*/
#define CCM_CSCR_OSC26MDIV      (1 << 4)
#define CCM_CSCR_OSC26M         (1 << 3)
#define CCM_CSCR_FPM            (1 << 2)
#define CCM_CSCR_SPEN           (1 << 1)
#define CCM_CSCR_MPEN           1

#define CCM_MPCTL0_CPLM         (1 << 31)
#define CCM_MPCTL0_PD_OFFSET    26
#define CCM_MPCTL0_PD_MASK      (0xf << 26)
#define CCM_MPCTL0_MFD_OFFSET   16
#define CCM_MPCTL0_MFD_MASK     (0x3ff << 16)
#define CCM_MPCTL0_MFI_OFFSET   10
#define CCM_MPCTL0_MFI_MASK     (0xf << 10)
#define CCM_MPCTL0_MFN_OFFSET   0
#define CCM_MPCTL0_MFN_MASK     0x3ff

#define CCM_MPCTL1_LF           (1 << 15)
#define CCM_MPCTL1_BRMO         (1 << 6)

#define CCM_SPCTL0_CPLM         (1 << 31)
#define CCM_SPCTL0_PD_OFFSET    26
#define CCM_SPCTL0_PD_MASK      (0xf << 26)
#define CCM_SPCTL0_MFD_OFFSET   16
#define CCM_SPCTL0_MFD_MASK     (0x3ff << 16)
#define CCM_SPCTL0_MFI_OFFSET   10
#define CCM_SPCTL0_MFI_MASK     (0xf << 10)
#define CCM_SPCTL0_MFN_OFFSET   0
#define CCM_SPCTL0_MFN_MASK     0x3ff

#define CCM_SPCTL1_LF           (1 << 15)
#define CCM_SPCTL1_BRMO         (1 << 6)

#define CCM_OSC26MCTL_PEAK_OFFSET       16
#define CCM_OSC26MCTL_PEAK_MASK         (0x3 << 16)
#define CCM_OSC26MCTL_AGC_OFFSET        8
#define CCM_OSC26MCTL_AGC_MASK          (0x3f << 8)
#define CCM_OSC26MCTL_ANATEST_OFFSET    0
#define CCM_OSC26MCTL_ANATEST_MASK      0x3f

#define CCM_PCDR0_SSI2BAUDDIV_OFFSET    26
#define CCM_PCDR0_SSI2BAUDDIV_MASK      (0x3f << 26)
#define CCM_PCDR0_CLKO_EN               25
#define CCM_PCDR0_CLKODIV_OFFSET        22
#define CCM_PCDR0_CLKODIV_MASK          (0x7 << 22)
#define CCM_PCDR0_SSI1BAUDDIV_OFFSET    16
#define CCM_PCDR0_SSI1BAUDDIV_MASK      (0x3f << 16)
/*The difinition for i.MX27 TO2*/
#define CCM_PCDR0_VPUDIV2_OFFSET        10
#define CCM_PCDR0_VPUDIV2_MASK          (0x3f << 10)
#define CCM_PCDR0_NFCDIV2_OFFSET         6
#define CCM_PCDR0_NFCDIV2_MASK           (0xf << 6)
#define CCM_PCDR0_MSHCDIV2_MASK          0x3f
/*The difinition for i.MX27 TO2*/
#define CCM_PCDR0_NFCDIV_OFFSET         12
#define CCM_PCDR0_NFCDIV_MASK           (0xf << 12)
#define CCM_PCDR0_VPUDIV_OFFSET        8
#define CCM_PCDR0_VPUDIV_MASK          (0xf << 8)
#define CCM_PCDR0_MSHCDIV_OFFSET        0
#define CCM_PCDR0_MSHCDIV_MASK          0x1f

#define CCM_PCDR1_PERDIV4_OFFSET        24
#define CCM_PCDR1_PERDIV4_MASK          (0x3f << 24)
#define CCM_PCDR1_PERDIV3_OFFSET        16
#define CCM_PCDR1_PERDIV3_MASK          (0x3f << 16)
#define CCM_PCDR1_PERDIV2_OFFSET        8
#define CCM_PCDR1_PERDIV2_MASK          (0x3f << 8)
#define CCM_PCDR1_PERDIV1_OFFSET        0
#define CCM_PCDR1_PERDIV1_MASK          0x3f

#define CCM_PCCR0_CSPI1_OFFSET          31
#define CCM_PCCR0_CSPI1_MASK            (1 << 31)
#define CCM_PCCR0_CSPI2_OFFSET          30
#define CCM_PCCR0_CSPI2_MASK            (1 << 30)
#define CCM_PCCR0_CSPI3_OFFSET          29
#define CCM_PCCR0_CSPI3_MASK            (1 << 29)
#define CCM_PCCR0_DMA_OFFSET            28
#define CCM_PCCR0_DMA_MASK              (1 << 28)
#define CCM_PCCR0_EMMA_OFFSET           27
#define CCM_PCCR0_EMMA_MASK             (1 << 27)
#define CCM_PCCR0_FEC_OFFSET            26
#define CCM_PCCR0_FEC_MASK              (1 << 26)
#define CCM_PCCR0_GPIO_OFFSET           25
#define CCM_PCCR0_GPIO_MASK             (1 << 25)
#define CCM_PCCR0_GPT1_OFFSET           24
#define CCM_PCCR0_GPT1_MASK             (1 << 24)
#define CCM_PCCR0_GPT2_OFFSET           23
#define CCM_PCCR0_GPT2_MASK             (1 << 23)
#define CCM_PCCR0_GPT3_OFFSET           22
#define CCM_PCCR0_GPT3_MASK             (1 << 22)
#define CCM_PCCR0_GPT4_OFFSET           21
#define CCM_PCCR0_GPT4_MASK             (1 << 21)
#define CCM_PCCR0_GPT5_OFFSET           20
#define CCM_PCCR0_GPT5_MASK             (1 << 20)
#define CCM_PCCR0_GPT6_OFFSET           19
#define CCM_PCCR0_GPT6_MASK             (1 << 19)
#define CCM_PCCR0_I2C1_OFFSET           18
#define CCM_PCCR0_I2C1_MASK             (1 << 18)
#define CCM_PCCR0_I2C2_OFFSET           17
#define CCM_PCCR0_I2C2_MASK             (1 << 17)
#define CCM_PCCR0_IIM_OFFSET            16
#define CCM_PCCR0_IIM_MASK              (1 << 16)
#define CCM_PCCR0_KPP_OFFSET            15
#define CCM_PCCR0_KPP_MASK              (1 << 15)
#define CCM_PCCR0_LCDC_OFFSET           14
#define CCM_PCCR0_LCDC_MASK             (1 << 14)
#define CCM_PCCR0_MSHC_OFFSET           13
#define CCM_PCCR0_MSHC_MASK             (1 << 13)
#define CCM_PCCR0_OWIRE_OFFSET          12
#define CCM_PCCR0_OWIRE_MASK            (1 << 12)
#define CCM_PCCR0_PWM_OFFSET            11
#define CCM_PCCR0_PWM_MASK              (1 << 11)
#define CCM_PCCR0_RTC_OFFSET            9
#define CCM_PCCR0_RTC_MASK              (1 << 9)
#define CCM_PCCR0_RTIC_OFFSET           8
#define CCM_PCCR0_RTIC_MASK             (1 << 8)
#define CCM_PCCR0_SAHARA_OFFSET         7
#define CCM_PCCR0_SAHARA_MASK           (1 << 7)
#define CCM_PCCR0_SCC_OFFSET            6
#define CCM_PCCR0_SCC_MASK              (1 << 6)
#define CCM_PCCR0_SDHC1_OFFSET          5
#define CCM_PCCR0_SDHC1_MASK            (1 << 5)
#define CCM_PCCR0_SDHC2_OFFSET          4
#define CCM_PCCR0_SDHC2_MASK            (1 << 4)
#define CCM_PCCR0_SDHC3_OFFSET          3
#define CCM_PCCR0_SDHC3_MASK            (1 << 3)
#define CCM_PCCR0_SLCDC_OFFSET          2
#define CCM_PCCR0_SLCDC_MASK            (1 << 2)
#define CCM_PCCR0_SSI1_IPG_OFFSET       1
#define CCM_PCCR0_SSI1_IPG_MASK         (1 << 1)
#define CCM_PCCR0_SSI2_IPG_OFFSET       0
#define CCM_PCCR0_SSI2_IPG_MASK         (1 << 0)

#define CCM_PCCR1_UART1_OFFSET          31
#define CCM_PCCR1_UART1_MASK            (1 << 31)
#define CCM_PCCR1_UART2_OFFSET          30
#define CCM_PCCR1_UART2_MASK            (1 << 30)
#define CCM_PCCR1_UART3_OFFSET          29
#define CCM_PCCR1_UART3_MASK            (1 << 29)
#define CCM_PCCR1_UART4_OFFSET          28
#define CCM_PCCR1_UART4_MASK            (1 << 28)
#define CCM_PCCR1_UART5_OFFSET          27
#define CCM_PCCR1_UART5_MASK            (1 << 27)
#define CCM_PCCR1_UART6_OFFSET          26
#define CCM_PCCR1_UART6_MASK            (1 << 26)
#define CCM_PCCR1_USBOTG_OFFSET         25
#define CCM_PCCR1_USBOTG_MASK           (1 << 25)
#define CCM_PCCR1_WDT_OFFSET            24
#define CCM_PCCR1_WDT_MASK              (1 << 24)
#define CCM_PCCR1_HCLK_ATA_OFFSET       23
#define CCM_PCCR1_HCLK_ATA_MASK         (1 << 23)
#define CCM_PCCR1_HCLK_BROM_OFFSET      22
#define CCM_PCCR1_HCLK_BROM_MASK        (1 << 22)
#define CCM_PCCR1_HCLK_CSI_OFFSET       21
#define CCM_PCCR1_HCLK_CSI_MASK         (1 << 21)
#define CCM_PCCR1_HCLK_DMA_OFFSET       20
#define CCM_PCCR1_HCLK_DMA_MASK         (1 << 20)
#define CCM_PCCR1_HCLK_EMI_OFFSET       19
#define CCM_PCCR1_HCLK_EMI_MASK         (1 << 19)
#define CCM_PCCR1_HCLK_EMMA_OFFSET      18
#define CCM_PCCR1_HCLK_EMMA_MASK        (1 << 18)
#define CCM_PCCR1_HCLK_FEC_OFFSET       17
#define CCM_PCCR1_HCLK_FEC_MASK         (1 << 17)
#define CCM_PCCR1_HCLK_VPU_OFFSET       16
#define CCM_PCCR1_HCLK_VPU_MASK         (1 << 16)
#define CCM_PCCR1_HCLK_LCDC_OFFSET      15
#define CCM_PCCR1_HCLK_LCDC_MASK        (1 << 15)
#define CCM_PCCR1_HCLK_RTIC_OFFSET      14
#define CCM_PCCR1_HCLK_RTIC_MASK        (1 << 14)
#define CCM_PCCR1_HCLK_SAHARA_OFFSET    13
#define CCM_PCCR1_HCLK_SAHARA_MASK      (1 << 13)
#define CCM_PCCR1_HCLK_SLCDC_OFFSET     12
#define CCM_PCCR1_HCLK_SLCDC_MASK       (1 << 12)
#define CCM_PCCR1_HCLK_USBOTG_OFFSET    11
#define CCM_PCCR1_HCLK_USBOTG_MASK      (1 << 11)
#define CCM_PCCR1_PERCLK1_OFFSET        10
#define CCM_PCCR1_PERCLK1_MASK          (1 << 10)
#define CCM_PCCR1_PERCLK2_OFFSET        9
#define CCM_PCCR1_PERCLK2_MASK          (1 << 9)
#define CCM_PCCR1_PERCLK3_OFFSET        8
#define CCM_PCCR1_PERCLK3_MASK          (1 << 8)
#define CCM_PCCR1_PERCLK4_OFFSET        7
#define CCM_PCCR1_PERCLK4_MASK          (1 << 7)
#define CCM_PCCR1_VPU_BAUD_OFFSET       6
#define CCM_PCCR1_VPU_BAUD_MASK         (1 << 6)
#define CCM_PCCR1_SSI1_BAUD_OFFSET      5
#define CCM_PCCR1_SSI1_BAUD_MASK        (1 << 5)
#define CCM_PCCR1_SSI2_BAUD_OFFSET      4
#define CCM_PCCR1_SSI2_BAUD_MASK        (1 << 4)
#define CCM_PCCR1_NFC_BAUD_OFFSET       3
#define CCM_PCCR1_NFC_BAUD_MASK         (1 << 3)
#define CCM_PCCR1_MSHC_BAUD_OFFSET      2
#define CCM_PCCR1_MSHC_BAUD_MASK        (1 << 2)

#define CCM_CCSR_32KSR          (1 << 15)
#define CCM_CCSR_CLKMODE1       (1 << 9)
#define CCM_CCSR_CLKMODE0       (1 << 8)
#define CCM_CCSR_CLKOSEL_OFFSET 0
#define CCM_CCSR_CLKOSEL_MASK   0x1f

#define SYS_FMCR                0x14	/*  Functional Muxing Control Reg */
#define SYS_CHIP_ID             0x00	/* The offset of CHIP ID register */

#endif /* __ARCH_ARM_MACH_MX2_CRM_REGS_H__ */
