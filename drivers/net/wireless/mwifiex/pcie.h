/* @file mwifiex_pcie.h
 *
 * @brief This file contains definitions for PCI-E interface.
 * driver.
 *
 * Copyright (C) 2011, Marvell International Ltd.
 *
 * This software file (the "File") is distributed by Marvell International
 * Ltd. under the terms of the GNU General Public License Version 2, June 1991
 * (the "License").  You may use, redistribute and/or modify this File in
 * accordance with the terms and conditions of the License, a copy of which
 * is available by writing to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 * worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 * THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 * ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 * this warranty disclaimer.
 */

#ifndef	_MWIFIEX_PCIE_H
#define	_MWIFIEX_PCIE_H

#include    <linux/pci.h>
#include    <linux/pcieport_if.h>
#include    <linux/interrupt.h>

#include    "main.h"

#define PCIE8766_DEFAULT_FW_NAME "mrvl/pcie8766_uapsta.bin"

/* Constants for Buffer Descriptor (BD) rings */
#define MWIFIEX_MAX_TXRX_BD			0x20
#define MWIFIEX_TXBD_MASK			0x3F
#define MWIFIEX_RXBD_MASK			0x3F

#define MWIFIEX_MAX_EVT_BD			0x04
#define MWIFIEX_EVTBD_MASK			0x07

/* PCIE INTERNAL REGISTERS */
#define PCIE_SCRATCH_0_REG				0xC10
#define PCIE_SCRATCH_1_REG				0xC14
#define PCIE_CPU_INT_EVENT				0xC18
#define PCIE_CPU_INT_STATUS				0xC1C
#define PCIE_HOST_INT_STATUS				0xC30
#define PCIE_HOST_INT_MASK				0xC34
#define PCIE_HOST_INT_STATUS_MASK			0xC3C
#define PCIE_SCRATCH_2_REG				0xC40
#define PCIE_SCRATCH_3_REG				0xC44
#define PCIE_SCRATCH_4_REG				0xCC0
#define PCIE_SCRATCH_5_REG				0xCC4
#define PCIE_SCRATCH_6_REG				0xCC8
#define PCIE_SCRATCH_7_REG				0xCCC
#define PCIE_SCRATCH_8_REG				0xCD0
#define PCIE_SCRATCH_9_REG				0xCD4
#define PCIE_SCRATCH_10_REG				0xCD8
#define PCIE_SCRATCH_11_REG				0xCDC
#define PCIE_SCRATCH_12_REG				0xCE0

#define CPU_INTR_DNLD_RDY				BIT(0)
#define CPU_INTR_DOOR_BELL				BIT(1)
#define CPU_INTR_SLEEP_CFM_DONE			BIT(2)
#define CPU_INTR_RESET					BIT(3)

#define HOST_INTR_DNLD_DONE				BIT(0)
#define HOST_INTR_UPLD_RDY				BIT(1)
#define HOST_INTR_CMD_DONE				BIT(2)
#define HOST_INTR_EVENT_RDY				BIT(3)
#define HOST_INTR_MASK					(HOST_INTR_DNLD_DONE | \
							 HOST_INTR_UPLD_RDY  | \
							 HOST_INTR_CMD_DONE  | \
							 HOST_INTR_EVENT_RDY)

#define MWIFIEX_BD_FLAG_ROLLOVER_IND			BIT(7)
#define MWIFIEX_BD_FLAG_FIRST_DESC			BIT(0)
#define MWIFIEX_BD_FLAG_LAST_DESC			BIT(1)
#define REG_CMD_ADDR_LO					PCIE_SCRATCH_0_REG
#define REG_CMD_ADDR_HI					PCIE_SCRATCH_1_REG
#define REG_CMD_SIZE					PCIE_SCRATCH_2_REG

#define REG_CMDRSP_ADDR_LO				PCIE_SCRATCH_4_REG
#define REG_CMDRSP_ADDR_HI				PCIE_SCRATCH_5_REG

/* TX buffer description read pointer */
#define REG_TXBD_RDPTR					PCIE_SCRATCH_6_REG
/* TX buffer description write pointer */
#define REG_TXBD_WRPTR					PCIE_SCRATCH_7_REG
/* RX buffer description read pointer */
#define REG_RXBD_RDPTR					PCIE_SCRATCH_8_REG
/* RX buffer description write pointer */
#define REG_RXBD_WRPTR					PCIE_SCRATCH_9_REG
/* Event buffer description read pointer */
#define REG_EVTBD_RDPTR					PCIE_SCRATCH_10_REG
/* Event buffer description write pointer */
#define REG_EVTBD_WRPTR					PCIE_SCRATCH_11_REG
/* Driver ready signature write pointer */
#define REG_DRV_READY					PCIE_SCRATCH_12_REG

/* Max retry number of command write */
#define MAX_WRITE_IOMEM_RETRY				2
/* Define PCIE block size for firmware download */
#define MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD		256
/* FW awake cookie after FW ready */
#define FW_AWAKE_COOKIE						(0xAA55AA55)

struct mwifiex_pcie_buf_desc {
	u64 paddr;
	u16 len;
	u16 flags;
} __packed;

struct pcie_service_card {
	struct pci_dev *dev;
	struct mwifiex_adapter *adapter;

	u32 txbd_wrptr;
	u32 txbd_rdptr;
	u32 txbd_ring_size;
	u8 *txbd_ring_vbase;
	phys_addr_t txbd_ring_pbase;
	struct mwifiex_pcie_buf_desc *txbd_ring[MWIFIEX_MAX_TXRX_BD];
	struct sk_buff *tx_buf_list[MWIFIEX_MAX_TXRX_BD];

	u32 rxbd_wrptr;
	u32 rxbd_rdptr;
	u32 rxbd_ring_size;
	u8 *rxbd_ring_vbase;
	phys_addr_t rxbd_ring_pbase;
	struct mwifiex_pcie_buf_desc *rxbd_ring[MWIFIEX_MAX_TXRX_BD];
	struct sk_buff *rx_buf_list[MWIFIEX_MAX_TXRX_BD];

	u32 evtbd_wrptr;
	u32 evtbd_rdptr;
	u32 evtbd_ring_size;
	u8 *evtbd_ring_vbase;
	phys_addr_t evtbd_ring_pbase;
	struct mwifiex_pcie_buf_desc *evtbd_ring[MWIFIEX_MAX_EVT_BD];
	struct sk_buff *evt_buf_list[MWIFIEX_MAX_EVT_BD];

	struct sk_buff *cmd_buf;
	struct sk_buff *cmdrsp_buf;
	struct sk_buff *sleep_cookie;
	void __iomem *pci_mmap;
	void __iomem *pci_mmap1;
};

#endif /* _MWIFIEX_PCIE_H */
