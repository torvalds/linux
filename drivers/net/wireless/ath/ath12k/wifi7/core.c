// SPDX-License-Identifier: BSD-3-Clause-Clear
/*
 * Copyright (c) 2018-2021 The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 */

#include <linux/module.h>
#include "../ahb.h"
#include "../pci.h"
#include "pci.h"
#include "ahb.h"
#include "core.h"
#include "dp.h"
#include "../debug.h"

static int ahb_err, pci_err;

int ath12k_wifi7_arch_init(struct ath12k_base *ab)
{
	struct ath12k_dp *dp;

	dp = ath12k_wifi7_dp_device_alloc(ab);
	if (!dp) {
		ath12k_err(ab, "dp alloc failed");
		return -EINVAL;
	}

	ab->dp = dp;

	return 0;
}

void ath12k_wifi7_arch_deinit(struct ath12k_base *ab)
{
	ath12k_wifi7_dp_device_free(ab->dp);
	ab->dp = NULL;
}

static int ath12k_wifi7_init(void)
{
	ahb_err = ath12k_wifi7_ahb_init();
	if (ahb_err)
		pr_warn("Failed to initialize ath12k Wi-Fi 7 AHB device: %d\n",
			ahb_err);

	pci_err = ath12k_wifi7_pci_init();
	if (pci_err)
		pr_warn("Failed to initialize ath12k Wi-Fi 7 PCI device: %d\n",
			pci_err);

	/* If both failed, return one of the failures (arbitrary) */
	return ahb_err && pci_err ? ahb_err : 0;
}

static void ath12k_wifi7_exit(void)
{
	if (!pci_err)
		ath12k_wifi7_pci_exit();

	if (!ahb_err)
		ath12k_wifi7_ahb_exit();
}

module_init(ath12k_wifi7_init);
module_exit(ath12k_wifi7_exit);

MODULE_DESCRIPTION("Driver support for Qualcomm Technologies 802.11be WLAN devices");
MODULE_LICENSE("Dual BSD/GPL");
