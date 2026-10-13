/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_HW_H
#define _RNPGBE_HW_H

#include <linux/bits.h>

#define MUCSE_N500_FWPF_CTRL_BASE      0x28b00
#define MUCSE_N500_FWPF_SHM_BASE       0x2d000
#define MUCSE_N500_RING_MSIX_BASE      0x28700
#define M_DEFAULT_N500_MHZ             125
#define MUCSE_GBE_PFFW_MBX_CTRL_OFFSET 0x5500
#define MUCSE_GBE_FWPF_MBX_MASK_OFFSET 0x5700
#define MUCSE_N210_FWPF_CTRL_BASE      0x29400
#define MUCSE_N210_FWPF_SHM_BASE       0x2d900
#define MUCSE_N210_RING_MSIX_BASE      0x29000
#define M_DEFAULT_N210_MHZ             62

#define RNPGBE_DMA_STATUS              0x0008
#define TX_AXI_RW_EN                   0xc
/* DMA_STATUS_REG[23:20]: tx_wr, tx_rd, rx_wr, rx_rd done status. */
#define RNPGBE_DMA_TX_STATUS           GENMASK_U32(23, 22)
#define RNPGBE_DMA_RX_STATUS           GENMASK_U32(21, 20)
#define RX_AXI_RW_EN                   0x03
#define RNPGBE_DMA_AXI_EN              0x0010
#define RNPGBE_TX_MIN_PKT_LEN          33

#define MUCSE_ETH_OFF(_n)              (0x10000 + (_n))
#define RNPGBE_RX_RAR_LOW(_n)          MUCSE_ETH_OFF(0xa000 + 4 * (_n))
#define RNPGBE_RX_RAR_HIGH(_n)         MUCSE_ETH_OFF(0xa400 + 4 * (_n))
#define RNPGBE_RX_MCAST_HASH(_n)       MUCSE_ETH_OFF(0xac00 + 4 * (_n))
#define RNPGBE_RX_FILTER_CTRL          MUCSE_ETH_OFF(0x9110)
#define RNPGBE_RX_MCAST_CTRL           MUCSE_ETH_OFF(0x9114)
#define RNPGBE_RX_RAR_VALID            BIT(31)
#define RNPGBE_RX_FILTER_BCAST         BIT(10)
#define RNPGBE_RX_FILTER_UCAST_ALL     BIT(9)
#define RNPGBE_RX_FILTER_MCAST_ALL     BIT(8)
#define RNPGBE_RX_MCAST_HASH_EN        BIT(4)
#define RNPGBE_RX_UCAST_TABLE_EN       BIT(3)
/* The final two of the 32 hardware RAR entries are reserved for NCSI. */
#define RNPGBE_RX_RAR_ENTRIES          30
#define RNPGBE_RX_MCAST_HASH_ENTRIES   128
#define RNPGBE_MAX_QUEUES 8
#endif /* _RNPGBE_HW_H */
