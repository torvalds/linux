// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/security.h>
#include <linux/highmem.h>
#include <linux/umh.h>
#include <linux/sysctl.h>

#include "firmware_fallback.h"
#include "firmware_loader.h"

/*
 * firmware fallback configuration table
 */

/* Module or buit-in */
#ifdef CONFIG_FW_LOADER_USER_HELPER

struct firmware_fallback_config fw_fallback_config = {
	.force_sysfs_fallback = IS_ENABLED(CONFIG_FW_LOADER_USER_HELPER_FALLBACK),
	.loading_timeout = 60,
	.old_timeout = 60,
};
EXPORT_SYMBOL_GPL(fw_fallback_config);

#endif
