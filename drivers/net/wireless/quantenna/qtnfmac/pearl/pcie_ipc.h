/*
 * Copyright (c) 2015-2016 Quantenna Communications, Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _QTN_FMAC_PCIE_IPC_H_
#define _QTN_FMAC_PCIE_IPC_H_

#include <linux/types.h>

#include "shm_ipc_defs.h"

/* bitmap for EP status and flags: updated by EP, read by RC */
#define QTN_EP_HAS_UBOOT	BIT(0)
#define QTN_EP_HAS_FIRMWARE	BIT(1)
#define QTN_EP_REQ_UBOOT	BIT(2)
#define QTN_EP_REQ_FIRMWARE	BIT(3)
#define QTN_EP_ERROR_UBOOT	BIT(4)
#define QTN_EP_ERROR_FIRMWARE	BIT(5)

#define QTN_EP_FW_LOADRDY	BIT(8)
#define QTN_EP_FW_SYNC		BIT(9)
#define QTN_EP_FW_RETRY		BIT(10)
#define QTN_EP_FW_QLINK_DONE	BIT(15)
#define QTN_EP_FW_DONE		BIT(16)

/* bitmap for RC status and flags: updated by RC, read by EP */
#define QTN_RC_PCIE_LINK	BIT(0)
#define QTN_RC_NET_LINK		BIT(1)
#define QTN_RC_FW_FLASHBOOT	BIT(5)
#define QTN_RC_FW_QLINK		BIT(7)
#define QTN_RC_FW_LOADRDY	BIT(8)
#define QTN_RC_FW_SYNC		BIT(9)

/* state transition timeouts */
#define QTN_FW_DL_TIMEOUT_MS	3000
#define QTN_FW_QLINK_TIMEOUT_MS	30000

#define PCIE_HDP_INT_RX_BITS (0		\
	| PCIE_HDP_INT_EP_TXDMA		\
	| PCIE_HDP_INT_EP_TXEMPTY	\
	| PCIE_HDP_INT_HHBM_UF		\
	)

#define PCIE_HDP_INT_TX_BITS (0		\
	| PCIE_HDP_INT_EP_RXDMA		\
	)

#ifdef CONFIG_ARCH_DMA_ADDR_T_64BIT
#define QTN_HOST_HI32(a)	((u32)(((u64)a) >> 32))
#define QTN_HOST_LO32(a)	((u32)(((u64)a) & 0xffffffffUL))
#define QTN_HOST_ADDR(h, l)	((((u64)h) << 32) | ((u64)l))
#else
#define QTN_HOST_HI32(a)	0
#define QTN_HOST_LO32(a)	((u32)(((u32)a) & 0xffffffffUL))
#define QTN_HOST_ADDR(h, l)	((u32)l)
#endif

#define QTN_SYSCTL_BAR	0
#define QTN_SHMEM_BAR	2
#define QTN_DMA_BAR	3

#define QTN_PCIE_BDA_VERSION		0x1002

#define PCIE_BDA_NAMELEN		32
#define PCIE_HHBM_MAX_SIZE		2048

#define SKB_BUF_SIZE		2048

#define QTN_PCIE_BOARDFLG	"PCIEQTN"
#define QTN_PCIE_FW_DLMASK	0xF
#define QTN_PCIE_FW_BUFSZ	2048

#define QTN_ENET_ADDR_LENGTH	6

#define QTN_TXDONE_MASK		((u32)0x80000000)
#define QTN_GET_LEN(x)		((x) & 0xFFFF)

#define QTN_PCIE_TX_DESC_LEN_MASK	0xFFFF
#define QTN_PCIE_TX_DESC_LEN_SHIFT	0
#define QTN_PCIE_TX_DESC_PORT_MASK	0xF
#define QTN_PCIE_TX_DESC_PORT_SHIFT	16
#define QTN_PCIE_TX_DESC_TQE_BIT	BIT(24)

#define QTN_EP_LHOST_TQE_PORT	4

enum qtnf_pcie_bda_ipc_flags {
	QTN_PCIE_IPC_FLAG_HBM_MAGIC	= BIT(0),
	QTN_PCIE_IPC_FLAG_SHM_PIO	= BIT(1),
};

struct qtnf_pcie_bda {
	__le16 bda_len;
	__le16 bda_version;
	__le32 bda_pci_endian;
	__le32 bda_ep_state;
	__le32 bda_rc_state;
	__le32 bda_dma_mask;
	__le32 bda_msi_addr;
	__le32 bda_flashsz;
	u8 bda_boardname[PCIE_BDA_NAMELEN];
	__le32 bda_rc_msi_enabled;
	u8 bda_hhbm_list[PCIE_HHBM_MAX_SIZE];
	__le32 bda_dsbw_start_index;
	__le32 bda_dsbw_end_index;
	__le32 bda_dsbw_total_bytes;
	__le32 bda_rc_tx_bd_base;
	__le32 bda_rc_tx_bd_num;
	u8 bda_pcie_mac[QTN_ENET_ADDR_LENGTH];
	struct qtnf_shm_ipc_region bda_shm_reg1 __aligned(4096); /* host TX */
	struct qtnf_shm_ipc_region bda_shm_reg2 __aligned(4096); /* host RX */
} __packed;

struct qtnf_tx_bd {
	__le32 addr;
	__le32 addr_h;
	__le32 info;
	__le32 info_h;
} __packed;

struct qtnf_rx_bd {
	__le32 addr;
	__le32 addr_h;
	__le32 info;
	__le32 info_h;
	__le32 next_ptr;
	__le32 next_ptr_h;
} __packed;

enum qtnf_fw_loadtype {
	QTN_FW_DBEGIN,
	QTN_FW_DSUB,
	QTN_FW_DEND,
	QTN_FW_CTRL
};

struct qtnf_pcie_fw_hdr {
	u8 boardflg[8];
	__le32 fwsize;
	__le32 seqnum;
	__le32 type;
	__le32 pktlen;
	__le32 crc;
} __packed;

#endif /* _QTN_FMAC_PCIE_IPC_H_ */
