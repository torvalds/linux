/*
 * timberdale.h timberdale FPGA MFD driver defines
 * Copyright (c) 2009 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* Supports:
 * Timberdale FPGA
 */

#ifndef MFD_TIMBERDALE_H
#define MFD_TIMBERDALE_H

#define DRV_VERSION		"0.3"

/* This driver only support versions >= 3.8 and < 4.0  */
#define TIMB_SUPPORTED_MAJOR	3

/* This driver only support minor >= 8 */
#define TIMB_REQUIRED_MINOR	8

/* Registers of the control area */
#define TIMB_REV_MAJOR	0x00
#define TIMB_REV_MINOR	0x04
#define TIMB_HW_CONFIG	0x08
#define TIMB_SW_RST	0x40

/* bits in the TIMB_HW_CONFIG register */
#define TIMB_HW_CONFIG_SPI_8BIT	0x80

#define TIMB_HW_VER_MASK	0x0f
#define TIMB_HW_VER0		0x00
#define TIMB_HW_VER1		0x01
#define TIMB_HW_VER2		0x02
#define TIMB_HW_VER3		0x03

#define OCORESOFFSET	0x0
#define OCORESEND	0x1f

#define SPIOFFSET	0x80
#define SPIEND		0xff

#define UARTLITEOFFSET	0x100
#define UARTLITEEND	0x10f

#define RDSOFFSET	0x180
#define RDSEND		0x183

#define ETHOFFSET	0x300
#define ETHEND		0x3ff

#define GPIOOFFSET	0x400
#define GPIOEND		0x7ff

#define CHIPCTLOFFSET	0x800
#define CHIPCTLEND	0x8ff
#define CHIPCTLSIZE	(CHIPCTLEND - CHIPCTLOFFSET + 1)

#define INTCOFFSET	0xc00
#define INTCEND		0xfff
#define INTCSIZE	(INTCEND - INTCOFFSET)

#define MOSTOFFSET	0x1000
#define MOSTEND		0x13ff

#define UARTOFFSET	0x1400
#define UARTEND		0x17ff

#define XIICOFFSET	0x1800
#define XIICEND		0x19ff

#define I2SOFFSET	0x1C00
#define I2SEND		0x1fff

#define LOGIWOFFSET	0x30000
#define LOGIWEND	0x37fff

#define MLCOREOFFSET	0x40000
#define MLCOREEND	0x43fff

#define DMAOFFSET	0x01000000
#define DMAEND		0x013fffff

/* SDHC0 is placed in PCI bar 1 */
#define SDHC0OFFSET	0x00
#define SDHC0END	0xff

/* SDHC1 is placed in PCI bar 2 */
#define SDHC1OFFSET	0x00
#define SDHC1END	0xff

#define PCI_VENDOR_ID_TIMB	0x10ee
#define PCI_DEVICE_ID_TIMB	0xa123

#define IRQ_TIMBERDALE_INIC		0
#define IRQ_TIMBERDALE_MLB		1
#define IRQ_TIMBERDALE_GPIO		2
#define IRQ_TIMBERDALE_I2C		3
#define IRQ_TIMBERDALE_UART		4
#define IRQ_TIMBERDALE_DMA		5
#define IRQ_TIMBERDALE_I2S		6
#define IRQ_TIMBERDALE_TSC_INT		7
#define IRQ_TIMBERDALE_SDHC		8
#define IRQ_TIMBERDALE_ADV7180		9
#define IRQ_TIMBERDALE_ETHSW_IF		10
#define IRQ_TIMBERDALE_SPI		11
#define IRQ_TIMBERDALE_UARTLITE		12
#define IRQ_TIMBERDALE_MLCORE		13
#define IRQ_TIMBERDALE_MLCORE_BUF	14
#define IRQ_TIMBERDALE_RDS		15
#define TIMBERDALE_NR_IRQS		16

#define GPIO_PIN_ASCB		8
#define GPIO_PIN_INIC_RST	14
#define GPIO_PIN_BT_RST		15
#define GPIO_NR_PINS		16

/* DMA Channels */
#define DMA_UART_RX         0
#define DMA_UART_TX         1
#define DMA_MLB_RX          2
#define DMA_MLB_TX          3
#define DMA_VIDEO_RX        4
#define DMA_VIDEO_DROP      5
#define DMA_SDHCI_RX        6
#define DMA_SDHCI_TX        7
#define DMA_ETH_RX          8
#define DMA_ETH_TX          9

#endif
