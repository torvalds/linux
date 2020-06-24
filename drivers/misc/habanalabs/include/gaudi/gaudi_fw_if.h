/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_FW_IF_H
#define GAUDI_FW_IF_H

#define GAUDI_EVENT_QUEUE_MSI_IDX	8
#define GAUDI_NIC_PORT1_MSI_IDX		10
#define GAUDI_NIC_PORT3_MSI_IDX		12
#define GAUDI_NIC_PORT5_MSI_IDX		14
#define GAUDI_NIC_PORT7_MSI_IDX		16
#define GAUDI_NIC_PORT9_MSI_IDX		18

#define UBOOT_FW_OFFSET			0x100000	/* 1MB in SRAM */
#define LINUX_FW_OFFSET			0x800000	/* 8MB in HBM */

enum gaudi_pll_index {
	CPU_PLL = 0,
	PCI_PLL,
	SRAM_PLL,
	HBM_PLL,
	NIC_PLL,
	DMA_PLL,
	MESH_PLL,
	MME_PLL,
	TPC_PLL,
	IF_PLL
};

#define GAUDI_PLL_FREQ_LOW		200000000 /* 200 MHz */

#endif /* GAUDI_FW_IF_H */
