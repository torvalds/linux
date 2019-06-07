/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GOYA_FW_IF_H
#define GOYA_FW_IF_H

#define GOYA_EVENT_QUEUE_MSIX_IDX	5

#define CPU_BOOT_ADDR		0x7FF8040000ull

#define UBOOT_FW_OFFSET		0x100000		/* 1MB in SRAM */
#define LINUX_FW_OFFSET		0x800000		/* 8MB in DDR */

enum goya_pll_index {
	CPU_PLL = 0,
	IC_PLL,
	MC_PLL,
	MME_PLL,
	PCI_PLL,
	EMMC_PLL,
	TPC_PLL
};

#define GOYA_PLL_FREQ_LOW		50000000 /* 50 MHz */

#endif /* GOYA_FW_IF_H */
