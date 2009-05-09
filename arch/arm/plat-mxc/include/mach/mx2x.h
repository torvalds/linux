/*
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Juergen Beisert, kernel@pengutronix.de
 *
 * This contains hardware definitions that are common between i.MX21 and
 * i.MX27.
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
 * MA  02110-1301, USA.
 */

#ifndef __ASM_ARCH_MXC_MX2x_H__
#define __ASM_ARCH_MXC_MX2x_H__

#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#error "Do not include directly."
#endif

/* The following addresses are common between i.MX21 and i.MX27 */

/* Register offests */
#define AIPI_BASE_ADDR          0x10000000
#define AIPI_BASE_ADDR_VIRT     0xF4000000
#define AIPI_SIZE               SZ_1M

#define DMA_BASE_ADDR           (AIPI_BASE_ADDR + 0x01000)
#define WDOG_BASE_ADDR          (AIPI_BASE_ADDR + 0x02000)
#define GPT1_BASE_ADDR          (AIPI_BASE_ADDR + 0x03000)
#define GPT2_BASE_ADDR          (AIPI_BASE_ADDR + 0x04000)
#define GPT3_BASE_ADDR          (AIPI_BASE_ADDR + 0x05000)
#define PWM_BASE_ADDR           (AIPI_BASE_ADDR + 0x06000)
#define RTC_BASE_ADDR           (AIPI_BASE_ADDR + 0x07000)
#define KPP_BASE_ADDR           (AIPI_BASE_ADDR + 0x08000)
#define OWIRE_BASE_ADDR         (AIPI_BASE_ADDR + 0x09000)
#define UART1_BASE_ADDR         (AIPI_BASE_ADDR + 0x0A000)
#define UART2_BASE_ADDR         (AIPI_BASE_ADDR + 0x0B000)
#define UART3_BASE_ADDR         (AIPI_BASE_ADDR + 0x0C000)
#define UART4_BASE_ADDR         (AIPI_BASE_ADDR + 0x0D000)
#define CSPI1_BASE_ADDR         (AIPI_BASE_ADDR + 0x0E000)
#define CSPI2_BASE_ADDR         (AIPI_BASE_ADDR + 0x0F000)
#define SSI1_BASE_ADDR          (AIPI_BASE_ADDR + 0x10000)
#define SSI2_BASE_ADDR          (AIPI_BASE_ADDR + 0x11000)
#define I2C_BASE_ADDR           (AIPI_BASE_ADDR + 0x12000)
#define SDHC1_BASE_ADDR         (AIPI_BASE_ADDR + 0x13000)
#define SDHC2_BASE_ADDR         (AIPI_BASE_ADDR + 0x14000)
#define GPIO_BASE_ADDR          (AIPI_BASE_ADDR + 0x15000)
#define AUDMUX_BASE_ADDR        (AIPI_BASE_ADDR + 0x16000)
#define CSPI3_BASE_ADDR         (AIPI_BASE_ADDR + 0x17000)
#define LCDC_BASE_ADDR          (AIPI_BASE_ADDR + 0x21000)
#define SLCDC_BASE_ADDR         (AIPI_BASE_ADDR + 0x22000)
#define USBOTG_BASE_ADDR        (AIPI_BASE_ADDR + 0x24000)
#define EMMA_PP_BASE_ADDR       (AIPI_BASE_ADDR + 0x26000)
#define EMMA_PRP_BASE_ADDR      (AIPI_BASE_ADDR + 0x26400)
#define CCM_BASE_ADDR           (AIPI_BASE_ADDR + 0x27000)
#define SYSCTRL_BASE_ADDR       (AIPI_BASE_ADDR + 0x27800)
#define JAM_BASE_ADDR           (AIPI_BASE_ADDR + 0x3E000)
#define MAX_BASE_ADDR           (AIPI_BASE_ADDR + 0x3F000)

#define AVIC_BASE_ADDR          0x10040000

#define SAHB1_BASE_ADDR         0x80000000
#define SAHB1_BASE_ADDR_VIRT    0xF4100000
#define SAHB1_SIZE              SZ_1M

#define CSI_BASE_ADDR           (SAHB1_BASE_ADDR + 0x0000)

/*
 * This macro defines the physical to virtual address mapping for all the
 * peripheral modules. It is used by passing in the physical address as x
 * and returning the virtual address. If the physical address is not mapped,
 * it returns 0xDEADBEEF
 */
#define IO_ADDRESS(x)   \
	(void __force __iomem *) \
	(((x >= AIPI_BASE_ADDR) && (x < (AIPI_BASE_ADDR + AIPI_SIZE))) ? \
		AIPI_IO_ADDRESS(x) : \
	((x >= SAHB1_BASE_ADDR) && (x < (SAHB1_BASE_ADDR + SAHB1_SIZE))) ? \
		SAHB1_IO_ADDRESS(x) : \
	((x >= X_MEMC_BASE_ADDR) && (x < (X_MEMC_BASE_ADDR + X_MEMC_SIZE))) ? \
		X_MEMC_IO_ADDRESS(x) : 0xDEADBEEF)

/* define the address mapping macros: in physical address order */
#define AIPI_IO_ADDRESS(x)  \
	(((x) - AIPI_BASE_ADDR) + AIPI_BASE_ADDR_VIRT)

#define AVIC_IO_ADDRESS(x)	AIPI_IO_ADDRESS(x)

#define SAHB1_IO_ADDRESS(x)  \
	(((x) - SAHB1_BASE_ADDR) + SAHB1_BASE_ADDR_VIRT)

#define CS4_IO_ADDRESS(x)  \
	(((x) - CS4_BASE_ADDR) + CS4_BASE_ADDR_VIRT)

#define X_MEMC_IO_ADDRESS(x)  \
	(((x) - X_MEMC_BASE_ADDR) + X_MEMC_BASE_ADDR_VIRT)

#define PCMCIA_IO_ADDRESS(x) \
	(((x) - X_MEMC_BASE_ADDR) + X_MEMC_BASE_ADDR_VIRT)

/* fixed interrupt numbers */
#define MXC_INT_LCDC		61
#define MXC_INT_SLCDC		60
#define MXC_INT_EMMAPP		52
#define MXC_INT_EMMAPRP		51
#define MXC_INT_DMACH15		47
#define MXC_INT_DMACH14		46
#define MXC_INT_DMACH13		45
#define MXC_INT_DMACH12		44
#define MXC_INT_DMACH11		43
#define MXC_INT_DMACH10		42
#define MXC_INT_DMACH9		41
#define MXC_INT_DMACH8		40
#define MXC_INT_DMACH7		39
#define MXC_INT_DMACH6		38
#define MXC_INT_DMACH5		37
#define MXC_INT_DMACH4		36
#define MXC_INT_DMACH3		35
#define MXC_INT_DMACH2		34
#define MXC_INT_DMACH1		33
#define MXC_INT_DMACH0		32
#define MXC_INT_CSI		31
#define MXC_INT_NANDFC		29
#define MXC_INT_PCMCIA		28
#define MXC_INT_WDOG		27
#define MXC_INT_GPT1		26
#define MXC_INT_GPT2		25
#define MXC_INT_GPT3		24
#define MXC_INT_GPT		INT_GPT1
#define MXC_INT_PWM		23
#define MXC_INT_RTC		22
#define MXC_INT_KPP		21
#define MXC_INT_UART1		20
#define MXC_INT_UART2		19
#define MXC_INT_UART3		18
#define MXC_INT_UART4		17
#define MXC_INT_CSPI1		16
#define MXC_INT_CSPI2		15
#define MXC_INT_SSI1		14
#define MXC_INT_SSI2		13
#define MXC_INT_I2C		12
#define MXC_INT_SDHC1		11
#define MXC_INT_SDHC2		10
#define MXC_INT_GPIO		8
#define MXC_INT_CSPI3		6

/* gpio and gpio based interrupt handling */
#define GPIO_DR		 	0x1C
#define GPIO_GDIR	 	0x00
#define GPIO_PSR	 	0x24
#define GPIO_ICR1	 	0x28
#define GPIO_ICR2	 	0x2C
#define GPIO_IMR	 	0x30
#define GPIO_ISR	 	0x34
#define GPIO_INT_LOW_LEV	0x3
#define GPIO_INT_HIGH_LEV	0x2
#define GPIO_INT_RISE_EDGE 	0x0
#define GPIO_INT_FALL_EDGE	0x1
#define GPIO_INT_NONE		0x4

/* fixed DMA request numbers */
#define DMA_REQ_CSI_RX          31
#define DMA_REQ_CSI_STAT        30
#define DMA_REQ_UART1_TX        27
#define DMA_REQ_UART1_RX        26
#define DMA_REQ_UART2_TX        25
#define DMA_REQ_UART2_RX        24
#define DMA_REQ_UART3_TX        23
#define DMA_REQ_UART3_RX        22
#define DMA_REQ_UART4_TX        21
#define DMA_REQ_UART4_RX        20
#define DMA_REQ_CSPI1_TX        19
#define DMA_REQ_CSPI1_RX        18
#define DMA_REQ_CSPI2_TX        17
#define DMA_REQ_CSPI2_RX        16
#define DMA_REQ_SSI1_TX1        15
#define DMA_REQ_SSI1_RX1        14
#define DMA_REQ_SSI1_TX0        13
#define DMA_REQ_SSI1_RX0        12
#define DMA_REQ_SSI2_TX1        11
#define DMA_REQ_SSI2_RX1        10
#define DMA_REQ_SSI2_TX0        9
#define DMA_REQ_SSI2_RX0        8
#define DMA_REQ_SDHC1           7
#define DMA_REQ_SDHC2           6
#define DMA_REQ_EXT             3
#define DMA_REQ_CSPI3_TX        2
#define DMA_REQ_CSPI3_RX        1

#endif /* __ASM_ARCH_MXC_MX2x_H__ */
