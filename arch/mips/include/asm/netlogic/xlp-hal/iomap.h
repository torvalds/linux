/*
 * Copyright 2003-2011 NetLogic Microsystems, Inc. (NetLogic). All rights
 * reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the NetLogic
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY NETLOGIC ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL NETLOGIC OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __NLM_HAL_IOMAP_H__
#define __NLM_HAL_IOMAP_H__

#define XLP_DEFAULT_IO_BASE             0x18000000
#define XLP_DEFAULT_PCI_ECFG_BASE	XLP_DEFAULT_IO_BASE
#define XLP_DEFAULT_PCI_CFG_BASE	0x1c000000

#define NMI_BASE			0xbfc00000
#define	XLP_IO_CLK			133333333

#define XLP_PCIE_CFG_SIZE		0x1000		/* 4K */
#define XLP_PCIE_DEV_BLK_SIZE		(8 * XLP_PCIE_CFG_SIZE)
#define XLP_PCIE_BUS_BLK_SIZE		(256 * XLP_PCIE_DEV_BLK_SIZE)
#define XLP_IO_SIZE			(64 << 20)	/* ECFG space size */
#define XLP_IO_PCI_HDRSZ		0x100
#define XLP_IO_DEV(node, dev)		((dev) + (node) * 8)
#define XLP_HDR_OFFSET(node, bus, dev, fn)	(((bus) << 20) | \
				((XLP_IO_DEV(node, dev)) << 15) | ((fn) << 12))

#define XLP_IO_BRIDGE_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 0, 0)
/* coherent inter chip */
#define XLP_IO_CIC0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 0, 1)
#define XLP_IO_CIC1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 0, 2)
#define XLP_IO_CIC2_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 0, 3)
#define XLP_IO_PIC_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 0, 4)

#define XLP_IO_PCIE_OFFSET(node, i)	XLP_HDR_OFFSET(node, 0, 1, i)
#define XLP_IO_PCIE0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 1, 0)
#define XLP_IO_PCIE1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 1, 1)
#define XLP_IO_PCIE2_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 1, 2)
#define XLP_IO_PCIE3_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 1, 3)

#define XLP_IO_USB_OFFSET(node, i)	XLP_HDR_OFFSET(node, 0, 2, i)
#define XLP_IO_USB_EHCI0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 0)
#define XLP_IO_USB_OHCI0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 1)
#define XLP_IO_USB_OHCI1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 2)
#define XLP_IO_USB_EHCI1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 3)
#define XLP_IO_USB_OHCI2_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 4)
#define XLP_IO_USB_OHCI3_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 2, 5)

#define XLP_IO_NAE_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 3, 0)
#define XLP_IO_POE_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 3, 1)

#define XLP_IO_CMS_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 4, 0)

#define XLP_IO_DMA_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 5, 1)
#define XLP_IO_SEC_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 5, 2)
#define XLP_IO_CMP_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 5, 3)

#define XLP_IO_UART_OFFSET(node, i)	XLP_HDR_OFFSET(node, 0, 6, i)
#define XLP_IO_UART0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 0)
#define XLP_IO_UART1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 1)
#define XLP_IO_I2C_OFFSET(node, i)	XLP_HDR_OFFSET(node, 0, 6, 2 + i)
#define XLP_IO_I2C0_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 2)
#define XLP_IO_I2C1_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 3)
#define XLP_IO_GPIO_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 4)
/* system management */
#define XLP_IO_SYS_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 6, 5)
#define XLP_IO_JTAG_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 6, 6)

#define XLP_IO_NOR_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 7, 0)
#define XLP_IO_NAND_OFFSET(node)	XLP_HDR_OFFSET(node, 0, 7, 1)
#define XLP_IO_SPI_OFFSET(node)		XLP_HDR_OFFSET(node, 0, 7, 2)
/* SD flash */
#define XLP_IO_SD_OFFSET(node)          XLP_HDR_OFFSET(node, 0, 7, 3)
#define XLP_IO_MMC_OFFSET(node, slot)   \
		((XLP_IO_SD_OFFSET(node))+(slot*0x100)+XLP_IO_PCI_HDRSZ)

/* PCI config header register id's */
#define XLP_PCI_CFGREG0			0x00
#define XLP_PCI_CFGREG1			0x01
#define XLP_PCI_CFGREG2			0x02
#define XLP_PCI_CFGREG3			0x03
#define XLP_PCI_CFGREG4			0x04
#define XLP_PCI_CFGREG5			0x05
#define XLP_PCI_DEVINFO_REG0		0x30
#define XLP_PCI_DEVINFO_REG1		0x31
#define XLP_PCI_DEVINFO_REG2		0x32
#define XLP_PCI_DEVINFO_REG3		0x33
#define XLP_PCI_DEVINFO_REG4		0x34
#define XLP_PCI_DEVINFO_REG5		0x35
#define XLP_PCI_DEVINFO_REG6		0x36
#define XLP_PCI_DEVINFO_REG7		0x37
#define XLP_PCI_DEVSCRATCH_REG0		0x38
#define XLP_PCI_DEVSCRATCH_REG1		0x39
#define XLP_PCI_DEVSCRATCH_REG2		0x3a
#define XLP_PCI_DEVSCRATCH_REG3		0x3b
#define XLP_PCI_MSGSTN_REG		0x3c
#define XLP_PCI_IRTINFO_REG		0x3d
#define XLP_PCI_UCODEINFO_REG		0x3e
#define XLP_PCI_SBB_WT_REG		0x3f

/* PCI IDs for SoC device */
#define	PCI_VENDOR_NETLOGIC		0x184e

#define	PCI_DEVICE_ID_NLM_ROOT		0x1001
#define	PCI_DEVICE_ID_NLM_ICI		0x1002
#define	PCI_DEVICE_ID_NLM_PIC		0x1003
#define	PCI_DEVICE_ID_NLM_PCIE		0x1004
#define	PCI_DEVICE_ID_NLM_EHCI		0x1007
#define	PCI_DEVICE_ID_NLM_ILK		0x1008
#define	PCI_DEVICE_ID_NLM_NAE		0x1009
#define	PCI_DEVICE_ID_NLM_POE		0x100A
#define	PCI_DEVICE_ID_NLM_FMN		0x100B
#define	PCI_DEVICE_ID_NLM_RAID		0x100D
#define	PCI_DEVICE_ID_NLM_SAE		0x100D
#define	PCI_DEVICE_ID_NLM_RSA		0x100E
#define	PCI_DEVICE_ID_NLM_CMP		0x100F
#define	PCI_DEVICE_ID_NLM_UART		0x1010
#define	PCI_DEVICE_ID_NLM_I2C		0x1011
#define	PCI_DEVICE_ID_NLM_NOR		0x1015
#define	PCI_DEVICE_ID_NLM_NAND		0x1016
#define	PCI_DEVICE_ID_NLM_MMC		0x1018

#ifndef __ASSEMBLY__

#define nlm_read_pci_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_pci_reg(b, r, v)	nlm_write_reg(b, r, v)

#endif /* !__ASSEMBLY */

#endif /* __NLM_HAL_IOMAP_H__ */
