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
#define PCIE8897_DEFAULT_FW_NAME "mrvl/pcie8897_uapsta.bin"

#define PCIE_VENDOR_ID_MARVELL              (0x11ab)
#define PCIE_DEVICE_ID_MARVELL_88W8766P		(0x2b30)
#define PCIE_DEVICE_ID_MARVELL_88W8897		(0x2b38)

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
#define PCIE_SCRATCH_4_REG				0xCD0
#define PCIE_SCRATCH_5_REG				0xCD4
#define PCIE_SCRATCH_6_REG				0xCD8
#define PCIE_SCRATCH_7_REG				0xCDC
#define PCIE_SCRATCH_8_REG				0xCE0
#define PCIE_SCRATCH_9_REG				0xCE4
#define PCIE_SCRATCH_10_REG				0xCE8
#define PCIE_SCRATCH_11_REG				0xCEC
#define PCIE_SCRATCH_12_REG				0xCF0
#define PCIE_RD_DATA_PTR_Q0_Q1                          0xC08C
#define PCIE_WR_DATA_PTR_Q0_Q1                          0xC05C

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
#define MWIFIEX_BD_FLAG_SOP				BIT(0)
#define MWIFIEX_BD_FLAG_EOP				BIT(1)
#define MWIFIEX_BD_FLAG_XS_SOP				BIT(2)
#define MWIFIEX_BD_FLAG_XS_EOP				BIT(3)
#define MWIFIEX_BD_FLAG_EVT_ROLLOVER_IND		BIT(7)
#define MWIFIEX_BD_FLAG_RX_ROLLOVER_IND			BIT(10)
#define MWIFIEX_BD_FLAG_TX_START_PTR			BIT(16)
#define MWIFIEX_BD_FLAG_TX_ROLLOVER_IND			BIT(26)

/* Max retry number of command write */
#define MAX_WRITE_IOMEM_RETRY				2
/* Define PCIE block size for firmware download */
#define MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD		256
/* FW awake cookie after FW ready */
#define FW_AWAKE_COOKIE						(0xAA55AA55)

struct mwifiex_pcie_card_reg {
	u16 cmd_addr_lo;
	u16 cmd_addr_hi;
	u16 fw_status;
	u16 cmd_size;
	u16 cmdrsp_addr_lo;
	u16 cmdrsp_addr_hi;
	u16 tx_rdptr;
	u16 tx_wrptr;
	u16 rx_rdptr;
	u16 rx_wrptr;
	u16 evt_rdptr;
	u16 evt_wrptr;
	u16 drv_rdy;
	u16 tx_start_ptr;
	u32 tx_mask;
	u32 tx_wrap_mask;
	u32 rx_mask;
	u32 rx_wrap_mask;
	u32 tx_rollover_ind;
	u32 rx_rollover_ind;
	u32 evt_rollover_ind;
	u8 ring_flag_sop;
	u8 ring_flag_eop;
	u8 ring_flag_xs_sop;
	u8 ring_flag_xs_eop;
	u32 ring_tx_start_ptr;
	u8 pfu_enabled;
	u8 sleep_cookie;
};

static const struct mwifiex_pcie_card_reg mwifiex_reg_8766 = {
	.cmd_addr_lo = PCIE_SCRATCH_0_REG,
	.cmd_addr_hi = PCIE_SCRATCH_1_REG,
	.cmd_size = PCIE_SCRATCH_2_REG,
	.fw_status = PCIE_SCRATCH_3_REG,
	.cmdrsp_addr_lo = PCIE_SCRATCH_4_REG,
	.cmdrsp_addr_hi = PCIE_SCRATCH_5_REG,
	.tx_rdptr = PCIE_SCRATCH_6_REG,
	.tx_wrptr = PCIE_SCRATCH_7_REG,
	.rx_rdptr = PCIE_SCRATCH_8_REG,
	.rx_wrptr = PCIE_SCRATCH_9_REG,
	.evt_rdptr = PCIE_SCRATCH_10_REG,
	.evt_wrptr = PCIE_SCRATCH_11_REG,
	.drv_rdy = PCIE_SCRATCH_12_REG,
	.tx_start_ptr = 0,
	.tx_mask = MWIFIEX_TXBD_MASK,
	.tx_wrap_mask = 0,
	.rx_mask = MWIFIEX_RXBD_MASK,
	.rx_wrap_mask = 0,
	.tx_rollover_ind = MWIFIEX_BD_FLAG_ROLLOVER_IND,
	.rx_rollover_ind = MWIFIEX_BD_FLAG_ROLLOVER_IND,
	.evt_rollover_ind = MWIFIEX_BD_FLAG_ROLLOVER_IND,
	.ring_flag_sop = 0,
	.ring_flag_eop = 0,
	.ring_flag_xs_sop = 0,
	.ring_flag_xs_eop = 0,
	.ring_tx_start_ptr = 0,
	.pfu_enabled = 0,
	.sleep_cookie = 1,
};

static const struct mwifiex_pcie_card_reg mwifiex_reg_8897 = {
	.cmd_addr_lo = PCIE_SCRATCH_0_REG,
	.cmd_addr_hi = PCIE_SCRATCH_1_REG,
	.cmd_size = PCIE_SCRATCH_2_REG,
	.fw_status = PCIE_SCRATCH_3_REG,
	.cmdrsp_addr_lo = PCIE_SCRATCH_4_REG,
	.cmdrsp_addr_hi = PCIE_SCRATCH_5_REG,
	.tx_rdptr = PCIE_RD_DATA_PTR_Q0_Q1,
	.tx_wrptr = PCIE_WR_DATA_PTR_Q0_Q1,
	.rx_rdptr = PCIE_WR_DATA_PTR_Q0_Q1,
	.rx_wrptr = PCIE_RD_DATA_PTR_Q0_Q1,
	.evt_rdptr = PCIE_SCRATCH_10_REG,
	.evt_wrptr = PCIE_SCRATCH_11_REG,
	.drv_rdy = PCIE_SCRATCH_12_REG,
	.tx_start_ptr = 16,
	.tx_mask = 0x03FF0000,
	.tx_wrap_mask = 0x07FF0000,
	.rx_mask = 0x000003FF,
	.rx_wrap_mask = 0x000007FF,
	.tx_rollover_ind = MWIFIEX_BD_FLAG_TX_ROLLOVER_IND,
	.rx_rollover_ind = MWIFIEX_BD_FLAG_RX_ROLLOVER_IND,
	.evt_rollover_ind = MWIFIEX_BD_FLAG_EVT_ROLLOVER_IND,
	.ring_flag_sop = MWIFIEX_BD_FLAG_SOP,
	.ring_flag_eop = MWIFIEX_BD_FLAG_EOP,
	.ring_flag_xs_sop = MWIFIEX_BD_FLAG_XS_SOP,
	.ring_flag_xs_eop = MWIFIEX_BD_FLAG_XS_EOP,
	.ring_tx_start_ptr = MWIFIEX_BD_FLAG_TX_START_PTR,
	.pfu_enabled = 1,
	.sleep_cookie = 0,
};

struct mwifiex_pcie_device {
	const char *firmware;
	const struct mwifiex_pcie_card_reg *reg;
	u16 blksz_fw_dl;
};

static const struct mwifiex_pcie_device mwifiex_pcie8766 = {
	.firmware       = PCIE8766_DEFAULT_FW_NAME,
	.reg            = &mwifiex_reg_8766,
	.blksz_fw_dl = MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD,
};

static const struct mwifiex_pcie_device mwifiex_pcie8897 = {
	.firmware       = PCIE8897_DEFAULT_FW_NAME,
	.reg            = &mwifiex_reg_8897,
	.blksz_fw_dl = MWIFIEX_PCIE_BLOCK_SIZE_FW_DNLD,
};

struct mwifiex_evt_buf_desc {
	u64 paddr;
	u16 len;
	u16 flags;
} __packed;

struct mwifiex_pcie_buf_desc {
	u64 paddr;
	u16 len;
	u16 flags;
} __packed;

struct mwifiex_pfu_buf_desc {
	u16 flags;
	u16 offset;
	u16 frag_len;
	u16 len;
	u64 paddr;
	u32 reserved;
} __packed;

struct pcie_service_card {
	struct pci_dev *dev;
	struct mwifiex_adapter *adapter;
	struct mwifiex_pcie_device pcie;

	u8 txbd_flush;
	u32 txbd_wrptr;
	u32 txbd_rdptr;
	u32 txbd_ring_size;
	u8 *txbd_ring_vbase;
	dma_addr_t txbd_ring_pbase;
	void *txbd_ring[MWIFIEX_MAX_TXRX_BD];
	struct sk_buff *tx_buf_list[MWIFIEX_MAX_TXRX_BD];

	u32 rxbd_wrptr;
	u32 rxbd_rdptr;
	u32 rxbd_ring_size;
	u8 *rxbd_ring_vbase;
	dma_addr_t rxbd_ring_pbase;
	void *rxbd_ring[MWIFIEX_MAX_TXRX_BD];
	struct sk_buff *rx_buf_list[MWIFIEX_MAX_TXRX_BD];

	u32 evtbd_wrptr;
	u32 evtbd_rdptr;
	u32 evtbd_ring_size;
	u8 *evtbd_ring_vbase;
	dma_addr_t evtbd_ring_pbase;
	void *evtbd_ring[MWIFIEX_MAX_EVT_BD];
	struct sk_buff *evt_buf_list[MWIFIEX_MAX_EVT_BD];

	struct sk_buff *cmd_buf;
	struct sk_buff *cmdrsp_buf;
	u8 *sleep_cookie_vbase;
	dma_addr_t sleep_cookie_pbase;
	void __iomem *pci_mmap;
	void __iomem *pci_mmap1;
};

static inline int
mwifiex_pcie_txbd_empty(struct pcie_service_card *card, u32 rdptr)
{
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	switch (card->dev->device) {
	case PCIE_DEVICE_ID_MARVELL_88W8766P:
		if (((card->txbd_wrptr & reg->tx_mask) ==
		     (rdptr & reg->tx_mask)) &&
		    ((card->txbd_wrptr & reg->tx_rollover_ind) !=
		     (rdptr & reg->tx_rollover_ind)))
			return 1;
		break;
	case PCIE_DEVICE_ID_MARVELL_88W8897:
		if (((card->txbd_wrptr & reg->tx_mask) ==
		     (rdptr & reg->tx_mask)) &&
		    ((card->txbd_wrptr & reg->tx_rollover_ind) ==
			(rdptr & reg->tx_rollover_ind)))
			return 1;
		break;
	}

	return 0;
}

static inline int
mwifiex_pcie_txbd_not_full(struct pcie_service_card *card)
{
	const struct mwifiex_pcie_card_reg *reg = card->pcie.reg;

	switch (card->dev->device) {
	case PCIE_DEVICE_ID_MARVELL_88W8766P:
		if (((card->txbd_wrptr & reg->tx_mask) !=
		     (card->txbd_rdptr & reg->tx_mask)) ||
		    ((card->txbd_wrptr & reg->tx_rollover_ind) !=
		     (card->txbd_rdptr & reg->tx_rollover_ind)))
			return 1;
		break;
	case PCIE_DEVICE_ID_MARVELL_88W8897:
		if (((card->txbd_wrptr & reg->tx_mask) !=
		     (card->txbd_rdptr & reg->tx_mask)) ||
		    ((card->txbd_wrptr & reg->tx_rollover_ind) ==
		     (card->txbd_rdptr & reg->tx_rollover_ind)))
			return 1;
		break;
	}

	return 0;
}
#endif /* _MWIFIEX_PCIE_H */
