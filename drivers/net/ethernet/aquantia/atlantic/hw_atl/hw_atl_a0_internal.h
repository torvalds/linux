/*
 * aQuantia Corporation Network Driver
 * Copyright (C) 2014-2017 aQuantia Corporation. All rights reserved
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 */

/* File hw_atl_a0_internal.h: Definition of Atlantic A0 chip specific
 * constants.
 */

#ifndef HW_ATL_A0_INTERNAL_H
#define HW_ATL_A0_INTERNAL_H

#include "../aq_common.h"

#define HW_ATL_A0_MTU_JUMBO 9014U

#define HW_ATL_A0_TX_RINGS 4U
#define HW_ATL_A0_RX_RINGS 4U

#define HW_ATL_A0_RINGS_MAX 32U
#define HW_ATL_A0_TXD_SIZE  16U
#define HW_ATL_A0_RXD_SIZE  16U

#define HW_ATL_A0_MAC      0U
#define HW_ATL_A0_MAC_MIN  1U
#define HW_ATL_A0_MAC_MAX  33U

/* interrupts */
#define HW_ATL_A0_ERR_INT 8U
#define HW_ATL_A0_INT_MASK  0xFFFFFFFFU

#define HW_ATL_A0_TXD_CTL2_LEN        0xFFFFC000U
#define HW_ATL_A0_TXD_CTL2_CTX_EN     0x00002000U
#define HW_ATL_A0_TXD_CTL2_CTX_IDX    0x00001000U

#define HW_ATL_A0_TXD_CTL_DESC_TYPE_TXD   0x00000001U
#define HW_ATL_A0_TXD_CTL_DESC_TYPE_TXC   0x00000002U
#define HW_ATL_A0_TXD_CTL_BLEN        0x000FFFF0U
#define HW_ATL_A0_TXD_CTL_DD          0x00100000U
#define HW_ATL_A0_TXD_CTL_EOP         0x00200000U

#define HW_ATL_A0_TXD_CTL_CMD_X       0x3FC00000U

#define HW_ATL_A0_TXD_CTL_CMD_VLAN    BIT(22)
#define HW_ATL_A0_TXD_CTL_CMD_FCS     BIT(23)
#define HW_ATL_A0_TXD_CTL_CMD_IPCSO   BIT(24)
#define HW_ATL_A0_TXD_CTL_CMD_TUCSO   BIT(25)
#define HW_ATL_A0_TXD_CTL_CMD_LSO     BIT(26)
#define HW_ATL_A0_TXD_CTL_CMD_WB      BIT(27)
#define HW_ATL_A0_TXD_CTL_CMD_VXLAN   BIT(28)

#define HW_ATL_A0_TXD_CTL_CMD_IPV6    BIT(21)
#define HW_ATL_A0_TXD_CTL_CMD_TCP     BIT(22)

#define HW_ATL_A0_MPI_CONTROL_ADR     0x0368U
#define HW_ATL_A0_MPI_STATE_ADR       0x036CU

#define HW_ATL_A0_MPI_SPEED_MSK       0xFFFFU
#define HW_ATL_A0_MPI_SPEED_SHIFT     16U

#define HW_ATL_A0_RATE_10G            BIT(0)
#define HW_ATL_A0_RATE_5G             BIT(1)
#define HW_ATL_A0_RATE_2G5            BIT(3)
#define HW_ATL_A0_RATE_1G             BIT(4)
#define HW_ATL_A0_RATE_100M           BIT(5)

#define HW_ATL_A0_TXBUF_MAX 160U
#define HW_ATL_A0_RXBUF_MAX 320U

#define HW_ATL_A0_RSS_REDIRECTION_MAX 64U
#define HW_ATL_A0_RSS_REDIRECTION_BITS 3U

#define HW_ATL_A0_TC_MAX 1U
#define HW_ATL_A0_RSS_MAX 8U

#define HW_ATL_A0_FW_SEMA_RAM           0x2U

#define HW_ATL_A0_RXD_DD    0x1U
#define HW_ATL_A0_RXD_NCEA0 0x1U

#define HW_ATL_A0_RXD_WB_STAT2_EOP     0x0002U

#define HW_ATL_A0_UCP_0X370_REG  0x370U

#define HW_ATL_A0_FW_VER_EXPECTED 0x01050006U

/* Hardware tx descriptor */
struct __packed hw_atl_txd_s {
	u64 buf_addr;
	u32 ctl;
	u32 ctl2; /* 63..46 - payload length, 45 - ctx enable, 44 - ctx index */
};

/* Hardware tx context descriptor */
struct __packed hw_atl_txc_s {
	u32 rsvd;
	u32 len;
	u32 ctl;
	u32 len2;
};

/* Hardware rx descriptor */
struct __packed hw_atl_rxd_s {
	u64 buf_addr;
	u64 hdr_addr;
};

/* Hardware rx descriptor writeback */
struct __packed hw_atl_rxd_wb_s {
	u32 type;
	u32 rss_hash;
	u16 status;
	u16 pkt_len;
	u16 next_desc_ptr;
	u16 vlan;
};

/* HW layer capabilities */
static struct aq_hw_caps_s hw_atl_a0_hw_caps_ = {
	.ports = 1U,
	.is_64_dma = true,
	.msix_irqs = 4U,
	.irq_mask = ~0U,
	.vecs = HW_ATL_A0_RSS_MAX,
	.tcs = HW_ATL_A0_TC_MAX,
	.rxd_alignment = 1U,
	.rxd_size = HW_ATL_A0_RXD_SIZE,
	.rxds = 248U,
	.txd_alignment = 1U,
	.txd_size = HW_ATL_A0_TXD_SIZE,
	.txds = 8U * 1024U,
	.txhwb_alignment = 4096U,
	.tx_rings = HW_ATL_A0_TX_RINGS,
	.rx_rings = HW_ATL_A0_RX_RINGS,
	.hw_features = NETIF_F_HW_CSUM |
			NETIF_F_RXHASH |
			NETIF_F_SG |
			NETIF_F_TSO,
	.hw_priv_flags = IFF_UNICAST_FLT,
	.link_speed_msk = (HW_ATL_A0_RATE_10G |
			HW_ATL_A0_RATE_5G |
			HW_ATL_A0_RATE_2G5 |
			HW_ATL_A0_RATE_1G |
			HW_ATL_A0_RATE_100M),
	.flow_control = true,
	.mtu = HW_ATL_A0_MTU_JUMBO,
	.mac_regs_count = 88,
	.fw_ver_expected = HW_ATL_A0_FW_VER_EXPECTED,
};

#endif /* HW_ATL_A0_INTERNAL_H */
