/*
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the Broadcom
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
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __NLM_HAL_PCIBUS_H__
#define __NLM_HAL_PCIBUS_H__

/* PCIE Memory and IO regions */
#define PCIE_MEM_BASE			0xd0000000ULL
#define PCIE_MEM_LIMIT			0xdfffffffULL
#define PCIE_IO_BASE			0x14000000ULL
#define PCIE_IO_LIMIT			0x15ffffffULL

#define PCIE_BRIDGE_CMD			0x1
#define PCIE_BRIDGE_MSI_CAP		0x14
#define PCIE_BRIDGE_MSI_ADDRL		0x15
#define PCIE_BRIDGE_MSI_ADDRH		0x16
#define PCIE_BRIDGE_MSI_DATA		0x17

/* XLP Global PCIE configuration space registers */
#define PCIE_BYTE_SWAP_MEM_BASE		0x247
#define PCIE_BYTE_SWAP_MEM_LIM		0x248
#define PCIE_BYTE_SWAP_IO_BASE		0x249
#define PCIE_BYTE_SWAP_IO_LIM		0x24A

#define PCIE_BRIDGE_MSIX_ADDR_BASE	0x24F
#define PCIE_BRIDGE_MSIX_ADDR_LIMIT	0x250
#define PCIE_MSI_STATUS			0x25A
#define PCIE_MSI_EN			0x25B
#define PCIE_MSIX_STATUS		0x25D
#define PCIE_INT_STATUS0		0x25F
#define PCIE_INT_STATUS1		0x260
#define PCIE_INT_EN0			0x261
#define PCIE_INT_EN1			0x262

/* XLP9XX has basic changes */
#define PCIE_9XX_BYTE_SWAP_MEM_BASE	0x25c
#define PCIE_9XX_BYTE_SWAP_MEM_LIM	0x25d
#define PCIE_9XX_BYTE_SWAP_IO_BASE	0x25e
#define PCIE_9XX_BYTE_SWAP_IO_LIM	0x25f

#define PCIE_9XX_BRIDGE_MSIX_ADDR_BASE	0x264
#define PCIE_9XX_BRIDGE_MSIX_ADDR_LIMIT	0x265
#define PCIE_9XX_MSI_STATUS		0x283
#define PCIE_9XX_MSI_EN			0x284
/* 128 MSIX vectors available in 9xx */
#define PCIE_9XX_MSIX_STATUS0		0x286
#define PCIE_9XX_MSIX_STATUSX(n)	(n + 0x286)
#define PCIE_9XX_MSIX_VEC		0x296
#define PCIE_9XX_MSIX_VECX(n)		(n + 0x296)
#define PCIE_9XX_INT_STATUS0		0x397
#define PCIE_9XX_INT_STATUS1		0x398
#define PCIE_9XX_INT_EN0		0x399
#define PCIE_9XX_INT_EN1		0x39a

/* other */
#define PCIE_NLINKS			4

/* MSI addresses */
#define MSI_ADDR_BASE			0xfffee00000ULL
#define MSI_ADDR_SZ			0x10000
#define MSI_LINK_ADDR(n, l)		(MSI_ADDR_BASE + \
				(PCIE_NLINKS * (n) + (l)) * MSI_ADDR_SZ)
#define MSIX_ADDR_BASE			0xfffef00000ULL
#define MSIX_LINK_ADDR(n, l)		(MSIX_ADDR_BASE + \
				(PCIE_NLINKS * (n) + (l)) * MSI_ADDR_SZ)
#ifndef __ASSEMBLY__

#define nlm_read_pcie_reg(b, r)		nlm_read_reg(b, r)
#define nlm_write_pcie_reg(b, r, v)	nlm_write_reg(b, r, v)
#define nlm_get_pcie_base(node, inst)	nlm_pcicfg_base(cpu_is_xlp9xx() ? \
	XLP9XX_IO_PCIE_OFFSET(node, inst) : XLP_IO_PCIE_OFFSET(node, inst))

#ifdef CONFIG_PCI_MSI
void xlp_init_node_msi_irqs(int node, int link);
#else
static inline void xlp_init_node_msi_irqs(int node, int link) {}
#endif

struct pci_dev *xlp_get_pcie_link(const struct pci_dev *dev);

#endif
#endif /* __NLM_HAL_PCIBUS_H__ */
