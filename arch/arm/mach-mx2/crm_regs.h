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

#define CCM_CSCR_PRESC_OFFSET   29
#define CCM_CSCR_PRESC_MASK     (0x7 << CCM_CSCR_PRESC_OFFSET)

#define CCM_CSCR_USB_OFFSET     26
#define CCM_CSCR_USB_MASK       (0x7 << CCM_CSCR_USB_OFFSET)
#define CCM_CSCR_SD_OFFSET      24
#define CCM_CSCR_SD_MASK        (0x3 << CCM_CSCR_SD_OFFSET)
#define CCM_CSCR_SPLLRES        (1 << 22)
#define CCM_CSCR_MPLLRES        (1 << 21)
#define CCM_CSCR_SSI2_OFFSET    20
#define CCM_CSCR_SSI2           (1 << CCM_CSCR_SSI2_OFFSET)
#define CCM_CSCR_SSI1_OFFSET    19
#define CCM_CSCR_SSI1           (1 << CCM_CSCR_SSI1_OFFSET)
#define CCM_CSCR_FIR_OFFSET    	18
#define CCM_CSCR_FIR		(1 << CCM_CSCR_FIR_OFFSET)
#define CCM_CSCR_SP             (1 << 17)
#define CCM_CSCR_MCU            (1 << 16)
#define CCM_CSCR_BCLK_OFFSET	10
#define CCM_CSCR_BCLK_MASK      (0xf << CCM_CSCR_BCLK_OFFSET)
#define CCM_CSCR_IPDIV_OFFSET   9
#define CCM_CSCR_IPDIV          (1 << CCM_CSCR_IPDIV_OFFSET)

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
#define CCM_PCDR0_SSI1BAUDDIV_OFFSET    16
#define CCM_PCDR0_SSI1BAUDDIV_MASK      (0x3f << 16)
#define CCM_PCDR0_NFCDIV_OFFSET         12
#define CCM_PCDR0_NFCDIV_MASK           (0xf << 12)
#define CCM_PCDR0_48MDIV_OFFSET		5
#define CCM_PCDR0_48MDIV_MASK		(0x7 << CCM_PCDR0_48MDIV_OFFSET)
#define CCM_PCDR0_FIRIDIV_OFFSET	0
#define CCM_PCDR0_FIRIDIV_MASK		0x1f
#define CCM_PCDR1_PERDIV4_OFFSET        24
#define CCM_PCDR1_PERDIV4_MASK          (0x3f << 24)
#define CCM_PCDR1_PERDIV3_OFFSET        16
#define CCM_PCDR1_PERDIV3_MASK          (0x3f << 16)
#define CCM_PCDR1_PERDIV2_OFFSET        8
#define CCM_PCDR1_PERDIV2_MASK          (0x3f << 8)
#define CCM_PCDR1_PERDIV1_OFFSET        0
#define CCM_PCDR1_PERDIV1_MASK          0x3f

#define CCM_PCCR_HCLK_CSI_OFFSET       	31
#define CCM_PCCR_HCLK_CSI_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_DMA_OFFSET       	30
#define CCM_PCCR_HCLK_DMA_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_BROM_OFFSET      	28
#define CCM_PCCR_HCLK_BROM_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_EMMA_OFFSET      	27
#define CCM_PCCR_HCLK_EMMA_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_LCDC_OFFSET      	26
#define CCM_PCCR_HCLK_LCDC_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_SLCDC_OFFSET     	25
#define CCM_PCCR_HCLK_SLCDC_REG	        CCM_PCCR0
#define CCM_PCCR_HCLK_USBOTG_OFFSET    	24
#define CCM_PCCR_HCLK_USBOTG_REG	CCM_PCCR0
#define CCM_PCCR_HCLK_BMI_OFFSET    	23
#define CCM_PCCR_BMI_MASK          	(1 << CCM_PCCR_BMI_MASK)
#define CCM_PCCR_HCLK_BMI_REG	    	CCM_PCCR0
#define CCM_PCCR_PERCLK4_OFFSET        	22
#define CCM_PCCR_PERCLK4_REG	    	CCM_PCCR0
#define CCM_PCCR_SLCDC_OFFSET          	21
#define CCM_PCCR_SLCDC_REG		CCM_PCCR0
#define CCM_PCCR_FIRI_BAUD_OFFSET       20
#define CCM_PCCR_FIRI_BAUD_MASK         (1 << CCM_PCCR_FIRI_BAUD_MASK)
#define CCM_PCCR_FIRI_BAUD_REG	        CCM_PCCR0
#define CCM_PCCR_NFC_OFFSET		19
#define CCM_PCCR_NFC_REG		CCM_PCCR0
#define CCM_PCCR_LCDC_OFFSET           	18
#define CCM_PCCR_LCDC_REG		CCM_PCCR0
#define CCM_PCCR_SSI1_BAUD_OFFSET      	17
#define CCM_PCCR_SSI1_BAUD_REG	    	CCM_PCCR0
#define CCM_PCCR_SSI2_BAUD_OFFSET      	16
#define CCM_PCCR_SSI2_BAUD_REG	    	CCM_PCCR0
#define CCM_PCCR_EMMA_OFFSET           	15
#define CCM_PCCR_EMMA_REG		CCM_PCCR0
#define CCM_PCCR_USBOTG_OFFSET         	14
#define CCM_PCCR_USBOTG_REG		CCM_PCCR0
#define CCM_PCCR_DMA_OFFSET            	13
#define CCM_PCCR_DMA_REG            	CCM_PCCR0
#define CCM_PCCR_I2C1_OFFSET           	12
#define CCM_PCCR_I2C1_REG		CCM_PCCR0
#define CCM_PCCR_GPIO_OFFSET           	11
#define CCM_PCCR_GPIO_REG		CCM_PCCR0
#define CCM_PCCR_SDHC2_OFFSET          	10
#define CCM_PCCR_SDHC2_REG		CCM_PCCR0
#define CCM_PCCR_SDHC1_OFFSET          	9
#define CCM_PCCR_SDHC1_REG		CCM_PCCR0
#define CCM_PCCR_FIRI_OFFSET		8
#define CCM_PCCR_FIRI_MASK		(1 << CCM_PCCR_BAUD_MASK)
#define CCM_PCCR_FIRI_REG		CCM_PCCR0
#define CCM_PCCR_SSI2_IPG_OFFSET       	7
#define CCM_PCCR_SSI2_REG		CCM_PCCR0
#define CCM_PCCR_SSI1_IPG_OFFSET       	6
#define CCM_PCCR_SSI1_REG		CCM_PCCR0
#define CCM_PCCR_CSPI2_OFFSET		5
#define	CCM_PCCR_CSPI2_REG		CCM_PCCR0
#define CCM_PCCR_CSPI1_OFFSET		4
#define	CCM_PCCR_CSPI1_REG		CCM_PCCR0
#define CCM_PCCR_UART4_OFFSET          	3
#define CCM_PCCR_UART4_REG		CCM_PCCR0
#define CCM_PCCR_UART3_OFFSET          	2
#define CCM_PCCR_UART3_REG		CCM_PCCR0
#define CCM_PCCR_UART2_OFFSET          	1
#define CCM_PCCR_UART2_REG		CCM_PCCR0
#define CCM_PCCR_UART1_OFFSET          	0
#define CCM_PCCR_UART1_REG		CCM_PCCR0

#define CCM_PCCR_OWIRE_OFFSET          	31
#define CCM_PCCR_OWIRE_REG		CCM_PCCR1
#define CCM_PCCR_KPP_OFFSET            	30
#define CCM_PCCR_KPP_REG		CCM_PCCR1
#define CCM_PCCR_RTC_OFFSET            	29
#define CCM_PCCR_RTC_REG		CCM_PCCR1
#define CCM_PCCR_PWM_OFFSET            	28
#define CCM_PCCR_PWM_REG		CCM_PCCR1
#define CCM_PCCR_GPT3_OFFSET           	27
#define CCM_PCCR_GPT3_REG		CCM_PCCR1
#define CCM_PCCR_GPT2_OFFSET           	26
#define CCM_PCCR_GPT2_REG		CCM_PCCR1
#define CCM_PCCR_GPT1_OFFSET           	25
#define CCM_PCCR_GPT1_REG		CCM_PCCR1
#define CCM_PCCR_WDT_OFFSET            	24
#define CCM_PCCR_WDT_REG		CCM_PCCR1
#define CCM_PCCR_CSPI3_OFFSET		23
#define	CCM_PCCR_CSPI3_REG		CCM_PCCR1

#define CCM_PCCR_CSPI1_MASK            	(1 << CCM_PCCR_CSPI1_OFFSET)
#define CCM_PCCR_CSPI2_MASK            	(1 << CCM_PCCR_CSPI2_OFFSET)
#define CCM_PCCR_CSPI3_MASK            	(1 << CCM_PCCR_CSPI3_OFFSET)
#define CCM_PCCR_DMA_MASK              	(1 << CCM_PCCR_DMA_OFFSET)
#define CCM_PCCR_EMMA_MASK             	(1 << CCM_PCCR_EMMA_OFFSET)
#define CCM_PCCR_GPIO_MASK             	(1 << CCM_PCCR_GPIO_OFFSET)
#define CCM_PCCR_GPT1_MASK             	(1 << CCM_PCCR_GPT1_OFFSET)
#define CCM_PCCR_GPT2_MASK             	(1 << CCM_PCCR_GPT2_OFFSET)
#define CCM_PCCR_GPT3_MASK             	(1 << CCM_PCCR_GPT3_OFFSET)
#define CCM_PCCR_HCLK_BROM_MASK		(1 << CCM_PCCR_HCLK_BROM_OFFSET)
#define CCM_PCCR_HCLK_CSI_MASK         	(1 << CCM_PCCR_HCLK_CSI_OFFSET)
#define CCM_PCCR_HCLK_DMA_MASK         	(1 << CCM_PCCR_HCLK_DMA_OFFSET)
#define CCM_PCCR_HCLK_EMMA_MASK        	(1 << CCM_PCCR_HCLK_EMMA_OFFSET)
#define CCM_PCCR_HCLK_LCDC_MASK        	(1 << CCM_PCCR_HCLK_LCDC_OFFSET)
#define CCM_PCCR_HCLK_SLCDC_MASK       	(1 << CCM_PCCR_HCLK_SLCDC_OFFSET)
#define CCM_PCCR_HCLK_USBOTG_MASK      	(1 << CCM_PCCR_HCLK_USBOTG_OFFSET)
#define CCM_PCCR_I2C1_MASK             	(1 << CCM_PCCR_I2C1_OFFSET)
#define CCM_PCCR_KPP_MASK              	(1 << CCM_PCCR_KPP_OFFSET)
#define CCM_PCCR_LCDC_MASK             	(1 << CCM_PCCR_LCDC_OFFSET)
#define CCM_PCCR_NFC_MASK		(1 << CCM_PCCR_NFC_OFFSET)
#define CCM_PCCR_OWIRE_MASK            	(1 << CCM_PCCR_OWIRE_OFFSET)
#define CCM_PCCR_PERCLK4_MASK          	(1 << CCM_PCCR_PERCLK4_OFFSET)
#define CCM_PCCR_PWM_MASK              	(1 << CCM_PCCR_PWM_OFFSET)
#define CCM_PCCR_RTC_MASK              	(1 << CCM_PCCR_RTC_OFFSET)
#define CCM_PCCR_SDHC1_MASK            	(1 << CCM_PCCR_SDHC1_OFFSET)
#define CCM_PCCR_SDHC2_MASK            	(1 << CCM_PCCR_SDHC2_OFFSET)
#define CCM_PCCR_SLCDC_MASK            	(1 << CCM_PCCR_SLCDC_OFFSET)
#define CCM_PCCR_SSI1_BAUD_MASK        	(1 << CCM_PCCR_SSI1_BAUD_OFFSET)
#define CCM_PCCR_SSI1_IPG_MASK         	(1 << CCM_PCCR_SSI1_IPG_OFFSET)
#define CCM_PCCR_SSI2_BAUD_MASK        	(1 << CCM_PCCR_SSI2_BAUD_OFFSET)
#define CCM_PCCR_SSI2_IPG_MASK         	(1 << CCM_PCCR_SSI2_IPG_OFFSET)
#define CCM_PCCR_UART1_MASK            	(1 << CCM_PCCR_UART1_OFFSET)
#define CCM_PCCR_UART2_MASK            	(1 << CCM_PCCR_UART2_OFFSET)
#define CCM_PCCR_UART3_MASK            	(1 << CCM_PCCR_UART3_OFFSET)
#define CCM_PCCR_UART4_MASK            	(1 << CCM_PCCR_UART4_OFFSET)
#define CCM_PCCR_USBOTG_MASK           	(1 << CCM_PCCR_USBOTG_OFFSET)
#define CCM_PCCR_WDT_MASK              	(1 << CCM_PCCR_WDT_OFFSET)


#define CCM_CCSR_32KSR          (1 << 15)

#define CCM_CCSR_CLKMODE1       (1 << 9)
#define CCM_CCSR_CLKMODE0       (1 << 8)

#define CCM_CCSR_CLKOSEL_OFFSET 0
#define CCM_CCSR_CLKOSEL_MASK   0x1f

#define SYS_FMCR                0x14	/*  Functional Muxing Control Reg */
#define SYS_CHIP_ID             0x00	/* The offset of CHIP ID register */

#endif /* __ARCH_ARM_MACH_MX2_CRM_REGS_H__ */
