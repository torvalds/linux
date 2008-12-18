/*
 * Copyright (C) 1997,1998 Russell King
 * Copyright (C) 1999 ARM Limited
 * Copyright 2004-2007 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright (c) 2008 Paulius Zaleckas <paulius.zaleckas@teltonika.lt>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_MXC_MX1_H__
#define __ASM_ARCH_MXC_MX1_H__

#ifndef __ASM_ARCH_MXC_HARDWARE_H__
#error "Do not include directly."
#endif

#include <mach/vmalloc.h>

/*
 * Memory map
 */
#define IMX_IO_PHYS	0x00200000
#define IMX_IO_SIZE	0x00100000
#define IMX_IO_BASE	VMALLOC_END

#define IMX_CS0_PHYS	0x10000000
#define IMX_CS0_SIZE	0x02000000

#define IMX_CS1_PHYS	0x12000000
#define IMX_CS1_SIZE	0x01000000

#define IMX_CS2_PHYS	0x13000000
#define IMX_CS2_SIZE	0x01000000

#define IMX_CS3_PHYS	0x14000000
#define IMX_CS3_SIZE	0x01000000

#define IMX_CS4_PHYS	0x15000000
#define IMX_CS4_SIZE	0x01000000

#define IMX_CS5_PHYS	0x16000000
#define IMX_CS5_SIZE	0x01000000

/*
 *  Register BASEs, based on OFFSETs
 */
#define AIPI1_BASE_ADDR		(0x00000 + IMX_IO_PHYS)
#define WDT_BASE_ADDR		(0x01000 + IMX_IO_PHYS)
#define TIM1_BASE_ADDR		(0x02000 + IMX_IO_PHYS)
#define TIM2_BASE_ADDR		(0x03000 + IMX_IO_PHYS)
#define RTC_BASE_ADDR		(0x04000 + IMX_IO_PHYS)
#define LCDC_BASE_ADDR		(0x05000 + IMX_IO_PHYS)
#define UART1_BASE_ADDR		(0x06000 + IMX_IO_PHYS)
#define UART2_BASE_ADDR		(0x07000 + IMX_IO_PHYS)
#define PWM_BASE_ADDR		(0x08000 + IMX_IO_PHYS)
#define DMA_BASE_ADDR		(0x09000 + IMX_IO_PHYS)
#define AIPI2_BASE_ADDR		(0x10000 + IMX_IO_PHYS)
#define SIM_BASE_ADDR		(0x11000 + IMX_IO_PHYS)
#define USBD_BASE_ADDR		(0x12000 + IMX_IO_PHYS)
#define SPI1_BASE_ADDR		(0x13000 + IMX_IO_PHYS)
#define MMC_BASE_ADDR		(0x14000 + IMX_IO_PHYS)
#define ASP_BASE_ADDR		(0x15000 + IMX_IO_PHYS)
#define BTA_BASE_ADDR		(0x16000 + IMX_IO_PHYS)
#define I2C_BASE_ADDR		(0x17000 + IMX_IO_PHYS)
#define SSI_BASE_ADDR		(0x18000 + IMX_IO_PHYS)
#define SPI2_BASE_ADDR		(0x19000 + IMX_IO_PHYS)
#define MSHC_BASE_ADDR		(0x1A000 + IMX_IO_PHYS)
#define CCM_BASE_ADDR		(0x1B000 + IMX_IO_PHYS)
#define SCM_BASE_ADDR		(0x1B804 + IMX_IO_PHYS)
#define GPIO_BASE_ADDR		(0x1C000 + IMX_IO_PHYS)
#define EIM_BASE_ADDR		(0x20000 + IMX_IO_PHYS)
#define SDRAMC_BASE_ADDR	(0x21000 + IMX_IO_PHYS)
#define MMA_BASE_ADDR		(0x22000 + IMX_IO_PHYS)
#define AVIC_BASE_ADDR		(0x23000 + IMX_IO_PHYS)
#define CSI_BASE_ADDR		(0x24000 + IMX_IO_PHYS)

/* macro to get at IO space when running virtually */
#define IO_ADDRESS(x)	((x) - IMX_IO_PHYS + IMX_IO_BASE)

/* define macros needed for entry-macro.S */
#define AVIC_IO_ADDRESS(x)	IO_ADDRESS(x)

/* fixed interrput numbers */
#define INT_SOFTINT		0
#define CSI_INT			6
#define DSPA_MAC_INT		7
#define DSPA_INT		8
#define COMP_INT		9
#define MSHC_XINT		10
#define GPIO_INT_PORTA		11
#define GPIO_INT_PORTB		12
#define GPIO_INT_PORTC		13
#define LCDC_INT		14
#define SIM_INT			15
#define SIM_DATA_INT		16
#define RTC_INT			17
#define RTC_SAMINT		18
#define UART2_MINT_PFERR	19
#define UART2_MINT_RTS		20
#define UART2_MINT_DTR		21
#define UART2_MINT_UARTC	22
#define UART2_MINT_TX		23
#define UART2_MINT_RX		24
#define UART1_MINT_PFERR	25
#define UART1_MINT_RTS		26
#define UART1_MINT_DTR		27
#define UART1_MINT_UARTC	28
#define UART1_MINT_TX		29
#define UART1_MINT_RX		30
#define VOICE_DAC_INT		31
#define VOICE_ADC_INT		32
#define PEN_DATA_INT		33
#define PWM_INT			34
#define SDHC_INT		35
#define I2C_INT			39
#define CSPI_INT		41
#define SSI_TX_INT		42
#define SSI_TX_ERR_INT		43
#define SSI_RX_INT		44
#define SSI_RX_ERR_INT		45
#define TOUCH_INT		46
#define USBD_INT0		47
#define USBD_INT1		48
#define USBD_INT2		49
#define USBD_INT3		50
#define USBD_INT4		51
#define USBD_INT5		52
#define USBD_INT6		53
#define BTSYS_INT		55
#define BTTIM_INT		56
#define BTWUI_INT		57
#define TIM2_INT		58
#define TIM1_INT		59
#define DMA_ERR			60
#define DMA_INT			61
#define GPIO_INT_PORTD		62
#define WDT_INT			63

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

/* DMA */
#define DMA_REQ_UART3_T		2
#define DMA_REQ_UART3_R		3
#define DMA_REQ_SSI2_T		4
#define DMA_REQ_SSI2_R		5
#define DMA_REQ_CSI_STAT	6
#define DMA_REQ_CSI_R		7
#define DMA_REQ_MSHC		8
#define DMA_REQ_DSPA_DCT_DOUT	9
#define DMA_REQ_DSPA_DCT_DIN	10
#define DMA_REQ_DSPA_MAC	11
#define DMA_REQ_EXT		12
#define DMA_REQ_SDHC		13
#define DMA_REQ_SPI1_R		14
#define DMA_REQ_SPI1_T		15
#define DMA_REQ_SSI_T		16
#define DMA_REQ_SSI_R		17
#define DMA_REQ_ASP_DAC		18
#define DMA_REQ_ASP_ADC		19
#define DMA_REQ_USP_EP(x)	(20 + (x))
#define DMA_REQ_SPI2_R		26
#define DMA_REQ_SPI2_T		27
#define DMA_REQ_UART2_T		28
#define DMA_REQ_UART2_R		29
#define DMA_REQ_UART1_T		30
#define DMA_REQ_UART1_R		31

/* mandatory for CONFIG_LL_DEBUG */
#define MXC_LL_UART_PADDR	UART1_BASE_ADDR
#define MXC_LL_UART_VADDR	IO_ADDRESS(UART1_BASE_ADDR)

#endif /*  __ASM_ARCH_MXC_MX1_H__ */
