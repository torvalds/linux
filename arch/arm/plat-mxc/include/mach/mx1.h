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

#ifndef __MACH_MX1_H__
#define __MACH_MX1_H__

#include <mach/vmalloc.h>

/*
 * Memory map
 */
#define MX1_IO_BASE_ADDR	0x00200000
#define MX1_IO_SIZE		SZ_1M

#define MX1_CS0_PHYS		0x10000000
#define MX1_CS0_SIZE		0x02000000

#define MX1_CS1_PHYS		0x12000000
#define MX1_CS1_SIZE		0x01000000

#define MX1_CS2_PHYS		0x13000000
#define MX1_CS2_SIZE		0x01000000

#define MX1_CS3_PHYS		0x14000000
#define MX1_CS3_SIZE		0x01000000

#define MX1_CS4_PHYS		0x15000000
#define MX1_CS4_SIZE		0x01000000

#define MX1_CS5_PHYS		0x16000000
#define MX1_CS5_SIZE		0x01000000

/*
 *  Register BASEs, based on OFFSETs
 */
#define MX1_AIPI1_BASE_ADDR		(0x00000 + MX1_IO_BASE_ADDR)
#define MX1_WDT_BASE_ADDR		(0x01000 + MX1_IO_BASE_ADDR)
#define MX1_TIM1_BASE_ADDR		(0x02000 + MX1_IO_BASE_ADDR)
#define MX1_TIM2_BASE_ADDR		(0x03000 + MX1_IO_BASE_ADDR)
#define MX1_RTC_BASE_ADDR		(0x04000 + MX1_IO_BASE_ADDR)
#define MX1_LCDC_BASE_ADDR		(0x05000 + MX1_IO_BASE_ADDR)
#define MX1_UART1_BASE_ADDR		(0x06000 + MX1_IO_BASE_ADDR)
#define MX1_UART2_BASE_ADDR		(0x07000 + MX1_IO_BASE_ADDR)
#define MX1_PWM_BASE_ADDR		(0x08000 + MX1_IO_BASE_ADDR)
#define MX1_DMA_BASE_ADDR		(0x09000 + MX1_IO_BASE_ADDR)
#define MX1_AIPI2_BASE_ADDR		(0x10000 + MX1_IO_BASE_ADDR)
#define MX1_SIM_BASE_ADDR		(0x11000 + MX1_IO_BASE_ADDR)
#define MX1_USBD_BASE_ADDR		(0x12000 + MX1_IO_BASE_ADDR)
#define MX1_CSPI1_BASE_ADDR		(0x13000 + MX1_IO_BASE_ADDR)
#define MX1_MMC_BASE_ADDR		(0x14000 + MX1_IO_BASE_ADDR)
#define MX1_ASP_BASE_ADDR		(0x15000 + MX1_IO_BASE_ADDR)
#define MX1_BTA_BASE_ADDR		(0x16000 + MX1_IO_BASE_ADDR)
#define MX1_I2C_BASE_ADDR		(0x17000 + MX1_IO_BASE_ADDR)
#define MX1_SSI_BASE_ADDR		(0x18000 + MX1_IO_BASE_ADDR)
#define MX1_CSPI2_BASE_ADDR		(0x19000 + MX1_IO_BASE_ADDR)
#define MX1_MSHC_BASE_ADDR		(0x1A000 + MX1_IO_BASE_ADDR)
#define MX1_CCM_BASE_ADDR		(0x1B000 + MX1_IO_BASE_ADDR)
#define MX1_SCM_BASE_ADDR		(0x1B804 + MX1_IO_BASE_ADDR)
#define MX1_GPIO_BASE_ADDR		(0x1C000 + MX1_IO_BASE_ADDR)
#define MX1_GPIO1_BASE_ADDR		(0x1C000 + MX1_IO_BASE_ADDR)
#define MX1_GPIO2_BASE_ADDR		(0x1C100 + MX1_IO_BASE_ADDR)
#define MX1_GPIO3_BASE_ADDR		(0x1C200 + MX1_IO_BASE_ADDR)
#define MX1_GPIO4_BASE_ADDR		(0x1C300 + MX1_IO_BASE_ADDR)
#define MX1_EIM_BASE_ADDR		(0x20000 + MX1_IO_BASE_ADDR)
#define MX1_SDRAMC_BASE_ADDR		(0x21000 + MX1_IO_BASE_ADDR)
#define MX1_MMA_BASE_ADDR		(0x22000 + MX1_IO_BASE_ADDR)
#define MX1_AVIC_BASE_ADDR		(0x23000 + MX1_IO_BASE_ADDR)
#define MX1_CSI_BASE_ADDR		(0x24000 + MX1_IO_BASE_ADDR)

/* macro to get at IO space when running virtually */
#define MX1_IO_P2V(x)			IMX_IO_P2V(x)
#define MX1_IO_ADDRESS(x)		IOMEM(MX1_IO_P2V(x))

/* fixed interrput numbers */
#define MX1_INT_SOFTINT		0
#define MX1_INT_CSI		6
#define MX1_DSPA_MAC_INT	7
#define MX1_DSPA_INT		8
#define MX1_COMP_INT		9
#define MX1_MSHC_XINT		10
#define MX1_GPIO_INT_PORTA	11
#define MX1_GPIO_INT_PORTB	12
#define MX1_GPIO_INT_PORTC	13
#define MX1_INT_LCDC		14
#define MX1_SIM_INT		15
#define MX1_SIM_DATA_INT	16
#define MX1_RTC_INT		17
#define MX1_RTC_SAMINT		18
#define MX1_INT_UART2PFERR	19
#define MX1_INT_UART2RTS	20
#define MX1_INT_UART2DTR	21
#define MX1_INT_UART2UARTC	22
#define MX1_INT_UART2TX		23
#define MX1_INT_UART2RX		24
#define MX1_INT_UART1PFERR	25
#define MX1_INT_UART1RTS	26
#define MX1_INT_UART1DTR	27
#define MX1_INT_UART1UARTC	28
#define MX1_INT_UART1TX		29
#define MX1_INT_UART1RX		30
#define MX1_VOICE_DAC_INT	31
#define MX1_VOICE_ADC_INT	32
#define MX1_PEN_DATA_INT	33
#define MX1_PWM_INT		34
#define MX1_SDHC_INT		35
#define MX1_INT_I2C		39
#define MX1_INT_CSPI2		40
#define MX1_INT_CSPI1		41
#define MX1_SSI_TX_INT		42
#define MX1_SSI_TX_ERR_INT	43
#define MX1_SSI_RX_INT		44
#define MX1_SSI_RX_ERR_INT	45
#define MX1_TOUCH_INT		46
#define MX1_INT_USBD0		47
#define MX1_INT_USBD1		48
#define MX1_INT_USBD2		49
#define MX1_INT_USBD3		50
#define MX1_INT_USBD4		51
#define MX1_INT_USBD5		52
#define MX1_INT_USBD6		53
#define MX1_BTSYS_INT		55
#define MX1_BTTIM_INT		56
#define MX1_BTWUI_INT		57
#define MX1_TIM2_INT		58
#define MX1_TIM1_INT		59
#define MX1_DMA_ERR		60
#define MX1_DMA_INT		61
#define MX1_GPIO_INT_PORTD	62
#define MX1_WDT_INT		63

/* DMA */
#define MX1_DMA_REQ_UART3_T		2
#define MX1_DMA_REQ_UART3_R		3
#define MX1_DMA_REQ_SSI2_T		4
#define MX1_DMA_REQ_SSI2_R		5
#define MX1_DMA_REQ_CSI_STAT		6
#define MX1_DMA_REQ_CSI_R		7
#define MX1_DMA_REQ_MSHC		8
#define MX1_DMA_REQ_DSPA_DCT_DOUT	9
#define MX1_DMA_REQ_DSPA_DCT_DIN	10
#define MX1_DMA_REQ_DSPA_MAC		11
#define MX1_DMA_REQ_EXT			12
#define MX1_DMA_REQ_SDHC		13
#define MX1_DMA_REQ_SPI1_R		14
#define MX1_DMA_REQ_SPI1_T		15
#define MX1_DMA_REQ_SSI_T		16
#define MX1_DMA_REQ_SSI_R		17
#define MX1_DMA_REQ_ASP_DAC		18
#define MX1_DMA_REQ_ASP_ADC		19
#define MX1_DMA_REQ_USP_EP(x)		(20 + (x))
#define MX1_DMA_REQ_SPI2_R		26
#define MX1_DMA_REQ_SPI2_T		27
#define MX1_DMA_REQ_UART2_T		28
#define MX1_DMA_REQ_UART2_R		29
#define MX1_DMA_REQ_UART1_T		30
#define MX1_DMA_REQ_UART1_R		31

/*
 * This doesn't depend on IMX_NEEDS_DEPRECATED_SYMBOLS
 * to not break drivers/usb/gadget/imx_udc.  Should go
 * away after this driver uses the new name.
 */
#define USBD_INT0		MX1_INT_USBD0

#endif /* ifndef __MACH_MX1_H__ */
