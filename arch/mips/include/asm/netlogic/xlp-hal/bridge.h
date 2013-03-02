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

#ifndef __NLM_HAL_BRIDGE_H__
#define __NLM_HAL_BRIDGE_H__

/**
* @file_name mio.h
* @author Netlogic Microsystems
* @brief Basic definitions of XLP memory and io subsystem
*/

/*
 * BRIDGE specific registers
 *
 * These registers start after the PCIe header, which has 0x40
 * standard entries
 */
#define BRIDGE_MODE			0x00
#define BRIDGE_PCI_CFG_BASE		0x01
#define BRIDGE_PCI_CFG_LIMIT		0x02
#define BRIDGE_PCIE_CFG_BASE		0x03
#define BRIDGE_PCIE_CFG_LIMIT		0x04
#define BRIDGE_BUSNUM_BAR0		0x05
#define BRIDGE_BUSNUM_BAR1		0x06
#define BRIDGE_BUSNUM_BAR2		0x07
#define BRIDGE_BUSNUM_BAR3		0x08
#define BRIDGE_BUSNUM_BAR4		0x09
#define BRIDGE_BUSNUM_BAR5		0x0a
#define BRIDGE_BUSNUM_BAR6		0x0b
#define BRIDGE_FLASH_BAR0		0x0c
#define BRIDGE_FLASH_BAR1		0x0d
#define BRIDGE_FLASH_BAR2		0x0e
#define BRIDGE_FLASH_BAR3		0x0f
#define BRIDGE_FLASH_LIMIT0		0x10
#define BRIDGE_FLASH_LIMIT1		0x11
#define BRIDGE_FLASH_LIMIT2		0x12
#define BRIDGE_FLASH_LIMIT3		0x13

#define BRIDGE_DRAM_BAR(i)		(0x14 + (i))
#define BRIDGE_DRAM_BAR0		0x14
#define BRIDGE_DRAM_BAR1		0x15
#define BRIDGE_DRAM_BAR2		0x16
#define BRIDGE_DRAM_BAR3		0x17
#define BRIDGE_DRAM_BAR4		0x18
#define BRIDGE_DRAM_BAR5		0x19
#define BRIDGE_DRAM_BAR6		0x1a
#define BRIDGE_DRAM_BAR7		0x1b

#define BRIDGE_DRAM_LIMIT(i)		(0x1c + (i))
#define BRIDGE_DRAM_LIMIT0		0x1c
#define BRIDGE_DRAM_LIMIT1		0x1d
#define BRIDGE_DRAM_LIMIT2		0x1e
#define BRIDGE_DRAM_LIMIT3		0x1f
#define BRIDGE_DRAM_LIMIT4		0x20
#define BRIDGE_DRAM_LIMIT5		0x21
#define BRIDGE_DRAM_LIMIT6		0x22
#define BRIDGE_DRAM_LIMIT7		0x23

#define BRIDGE_DRAM_NODE_TRANSLN0	0x24
#define BRIDGE_DRAM_NODE_TRANSLN1	0x25
#define BRIDGE_DRAM_NODE_TRANSLN2	0x26
#define BRIDGE_DRAM_NODE_TRANSLN3	0x27
#define BRIDGE_DRAM_NODE_TRANSLN4	0x28
#define BRIDGE_DRAM_NODE_TRANSLN5	0x29
#define BRIDGE_DRAM_NODE_TRANSLN6	0x2a
#define BRIDGE_DRAM_NODE_TRANSLN7	0x2b
#define BRIDGE_DRAM_CHNL_TRANSLN0	0x2c
#define BRIDGE_DRAM_CHNL_TRANSLN1	0x2d
#define BRIDGE_DRAM_CHNL_TRANSLN2	0x2e
#define BRIDGE_DRAM_CHNL_TRANSLN3	0x2f
#define BRIDGE_DRAM_CHNL_TRANSLN4	0x30
#define BRIDGE_DRAM_CHNL_TRANSLN5	0x31
#define BRIDGE_DRAM_CHNL_TRANSLN6	0x32
#define BRIDGE_DRAM_CHNL_TRANSLN7	0x33
#define BRIDGE_PCIEMEM_BASE0		0x34
#define BRIDGE_PCIEMEM_BASE1		0x35
#define BRIDGE_PCIEMEM_BASE2		0x36
#define BRIDGE_PCIEMEM_BASE3		0x37
#define BRIDGE_PCIEMEM_LIMIT0		0x38
#define BRIDGE_PCIEMEM_LIMIT1		0x39
#define BRIDGE_PCIEMEM_LIMIT2		0x3a
#define BRIDGE_PCIEMEM_LIMIT3		0x3b
#define BRIDGE_PCIEIO_BASE0		0x3c
#define BRIDGE_PCIEIO_BASE1		0x3d
#define BRIDGE_PCIEIO_BASE2		0x3e
#define BRIDGE_PCIEIO_BASE3		0x3f
#define BRIDGE_PCIEIO_LIMIT0		0x40
#define BRIDGE_PCIEIO_LIMIT1		0x41
#define BRIDGE_PCIEIO_LIMIT2		0x42
#define BRIDGE_PCIEIO_LIMIT3		0x43
#define BRIDGE_PCIEMEM_BASE4		0x44
#define BRIDGE_PCIEMEM_BASE5		0x45
#define BRIDGE_PCIEMEM_BASE6		0x46
#define BRIDGE_PCIEMEM_LIMIT4		0x47
#define BRIDGE_PCIEMEM_LIMIT5		0x48
#define BRIDGE_PCIEMEM_LIMIT6		0x49
#define BRIDGE_PCIEIO_BASE4		0x4a
#define BRIDGE_PCIEIO_BASE5		0x4b
#define BRIDGE_PCIEIO_BASE6		0x4c
#define BRIDGE_PCIEIO_LIMIT4		0x4d
#define BRIDGE_PCIEIO_LIMIT5		0x4e
#define BRIDGE_PCIEIO_LIMIT6		0x4f
#define BRIDGE_NBU_EVENT_CNT_CTL	0x50
#define BRIDGE_EVNTCTR1_LOW		0x51
#define BRIDGE_EVNTCTR1_HI		0x52
#define BRIDGE_EVNT_CNT_CTL2		0x53
#define BRIDGE_EVNTCTR2_LOW		0x54
#define BRIDGE_EVNTCTR2_HI		0x55
#define BRIDGE_TRACEBUF_MATCH0		0x56
#define BRIDGE_TRACEBUF_MATCH1		0x57
#define BRIDGE_TRACEBUF_MATCH_LOW	0x58
#define BRIDGE_TRACEBUF_MATCH_HI	0x59
#define BRIDGE_TRACEBUF_CTRL		0x5a
#define BRIDGE_TRACEBUF_INIT		0x5b
#define BRIDGE_TRACEBUF_ACCESS		0x5c
#define BRIDGE_TRACEBUF_READ_DATA0	0x5d
#define BRIDGE_TRACEBUF_READ_DATA1	0x5d
#define BRIDGE_TRACEBUF_READ_DATA2	0x5f
#define BRIDGE_TRACEBUF_READ_DATA3	0x60
#define BRIDGE_TRACEBUF_STATUS		0x61
#define BRIDGE_ADDRESS_ERROR0		0x62
#define BRIDGE_ADDRESS_ERROR1		0x63
#define BRIDGE_ADDRESS_ERROR2		0x64
#define BRIDGE_TAG_ECC_ADDR_ERROR0	0x65
#define BRIDGE_TAG_ECC_ADDR_ERROR1	0x66
#define BRIDGE_TAG_ECC_ADDR_ERROR2	0x67
#define BRIDGE_LINE_FLUSH0		0x68
#define BRIDGE_LINE_FLUSH1		0x69
#define BRIDGE_NODE_ID			0x6a
#define BRIDGE_ERROR_INTERRUPT_EN	0x6b
#define BRIDGE_PCIE0_WEIGHT		0x2c0
#define BRIDGE_PCIE1_WEIGHT		0x2c1
#define BRIDGE_PCIE2_WEIGHT		0x2c2
#define BRIDGE_PCIE3_WEIGHT		0x2c3
#define BRIDGE_USB_WEIGHT		0x2c4
#define BRIDGE_NET_WEIGHT		0x2c5
#define BRIDGE_POE_WEIGHT		0x2c6
#define BRIDGE_CMS_WEIGHT		0x2c7
#define BRIDGE_DMAENG_WEIGHT		0x2c8
#define BRIDGE_SEC_WEIGHT		0x2c9
#define BRIDGE_COMP_WEIGHT		0x2ca
#define BRIDGE_GIO_WEIGHT		0x2cb
#define BRIDGE_FLASH_WEIGHT		0x2cc

#ifndef __ASSEMBLY__

#define nlm_read_bridge_reg(b, r)	nlm_read_reg(b, r)
#define nlm_write_bridge_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_bridge_pcibase(node)	\
			nlm_pcicfg_base(XLP_IO_BRIDGE_OFFSET(node))
#define nlm_get_bridge_regbase(node)	\
			(nlm_get_bridge_pcibase(node) + XLP_IO_PCI_HDRSZ)

#endif /* __ASSEMBLY__ */
#endif /* __NLM_HAL_BRIDGE_H__ */
