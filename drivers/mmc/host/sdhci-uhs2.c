// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  linux/drivers/mmc/host/sdhci_uhs2.c - Secure Digital Host Controller
 *  Interface driver
 *
 *  Copyright (C) 2014 Intel Corp, All Rights Reserved.
 *  Copyright (C) 2020 Genesys Logic, Inc.
 *  Authors: Ben Chuang <ben.chuang@genesyslogic.com.tw>
 *  Copyright (C) 2020 Linaro Limited
 *  Author: AKASHI Takahiro <takahiro.akashi@linaro.org>
 */

#include <linux/module.h>

#include "sdhci.h"
#include "sdhci-uhs2.h"

#define DRIVER_NAME "sdhci_uhs2"
#define DBG(f, x...) \
	pr_debug(DRIVER_NAME " [%s()]: " f, __func__, ## x)
#define SDHCI_UHS2_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

void sdhci_uhs2_dump_regs(struct sdhci_host *host)
{
	if (!(mmc_card_uhs2(host->mmc)))
		return;

	SDHCI_UHS2_DUMP("==================== UHS2 ==================\n");
	SDHCI_UHS2_DUMP("Blk Size:  0x%08x | Blk Cnt:  0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_BLOCK_SIZE),
			sdhci_readl(host, SDHCI_UHS2_BLOCK_COUNT));
	SDHCI_UHS2_DUMP("Cmd:       0x%08x | Trn mode: 0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_CMD),
			sdhci_readw(host, SDHCI_UHS2_TRANS_MODE));
	SDHCI_UHS2_DUMP("Int Stat:  0x%08x | Dev Sel : 0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_DEV_INT_STATUS),
			sdhci_readb(host, SDHCI_UHS2_DEV_SELECT));
	SDHCI_UHS2_DUMP("Dev Int Code:  0x%08x\n",
			sdhci_readb(host, SDHCI_UHS2_DEV_INT_CODE));
	SDHCI_UHS2_DUMP("Reset:     0x%08x | Timer:    0x%08x\n",
			sdhci_readw(host, SDHCI_UHS2_SW_RESET),
			sdhci_readw(host, SDHCI_UHS2_TIMER_CTRL));
	SDHCI_UHS2_DUMP("ErrInt:    0x%08x | ErrIntEn: 0x%08x\n",
			sdhci_readl(host, SDHCI_UHS2_INT_STATUS),
			sdhci_readl(host, SDHCI_UHS2_INT_STATUS_ENABLE));
	SDHCI_UHS2_DUMP("ErrSigEn:  0x%08x\n",
			sdhci_readl(host, SDHCI_UHS2_INT_SIGNAL_ENABLE));
}
EXPORT_SYMBOL_GPL(sdhci_uhs2_dump_regs);

/*****************************************************************************\
 *                                                                           *
 * Driver init/exit                                                          *
 *                                                                           *
\*****************************************************************************/

static int __init sdhci_uhs2_mod_init(void)
{
	return 0;
}
module_init(sdhci_uhs2_mod_init);

static void __exit sdhci_uhs2_mod_exit(void)
{
}
module_exit(sdhci_uhs2_mod_exit);

MODULE_AUTHOR("Intel, Genesys Logic, Linaro");
MODULE_DESCRIPTION("MMC UHS-II Support");
MODULE_LICENSE("GPL");
