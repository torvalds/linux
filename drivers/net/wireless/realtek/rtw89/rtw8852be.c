// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/* Copyright(c) 2020-2022  Realtek Corporation
 */

#include <linux/module.h>
#include <linux/pci.h>

#include "pci.h"
#include "reg.h"

static const struct rtw89_pci_info rtw8852b_pci_info = {
	.dma_stop1		= {R_AX_PCIE_DMA_STOP1, B_AX_TX_STOP1_MASK_V1},
	.dma_stop2		= {0},
	.dma_busy1		= {R_AX_PCIE_DMA_BUSY1, DMA_BUSY1_CHECK_V1},
	.dma_busy2_reg		= 0,
	.dma_busy3_reg		= R_AX_PCIE_DMA_BUSY1,

	.tx_dma_ch_mask		= BIT(RTW89_TXCH_ACH4) | BIT(RTW89_TXCH_ACH5) |
				  BIT(RTW89_TXCH_ACH6) | BIT(RTW89_TXCH_ACH7) |
				  BIT(RTW89_TXCH_CH10) | BIT(RTW89_TXCH_CH11),
};

MODULE_AUTHOR("Realtek Corporation");
MODULE_DESCRIPTION("Realtek 802.11ax wireless 8852BE driver");
MODULE_LICENSE("Dual BSD/GPL");
