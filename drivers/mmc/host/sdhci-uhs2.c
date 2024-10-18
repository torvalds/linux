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
