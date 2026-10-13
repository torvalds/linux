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

#define RNPGBE_MAX_QUEUES 8
#endif /* _RNPGBE_HW_H */
