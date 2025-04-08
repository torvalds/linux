// SPDX-License-Identifier: GPL-2.0

#include <linux/types.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/security.h>
#include <linux/highmem.h>
#include <linux/umh.h>
#include <linux/sysctl.h>

#include "fallback.h"
#include "firmware.h"

/*
 * firmware fallback configuration table
 */

struct firmware_fallback_config fw_fallback_config = {
	.force_sysfs_fallback = IS_ENABLED(CONFIG_FW_LOADER_USER_HELPER_FALLBACK),
	.loading_timeout = 60,
	.old_timeout = 60,
};
EXPORT_SYMBOL_NS_GPL(fw_fallback_config, "FIRMWARE_LOADER_PRIVATE");

#ifdef CONFIG_SYSCTL
static const struct ctl_table firmware_config_table[] = {
	{
		.procname	= "force_sysfs_fallback",
		.data		= &fw_fallback_config.force_sysfs_fallback,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
	{
		.procname	= "ignore_sysfs_fallback",
		.data		= &fw_fallback_config.ignore_sysfs_fallback,
		.maxlen         = sizeof(unsigned int),
		.mode           = 0644,
		.proc_handler   = proc_douintvec_minmax,
		.extra1		= SYSCTL_ZERO,
		.extra2		= SYSCTL_ONE,
	},
};

static struct ctl_table_header *firmware_config_sysct_table_header;
int register_firmware_config_sysctl(void)
{
	firmware_config_sysct_table_header =
		register_sysctl("kernel/firmware_config",
				firmware_config_table);
	if (!firmware_config_sysct_table_header)
		return -ENOMEM;
	return 0;
}
EXPORT_SYMBOL_NS_GPL(register_firmware_config_sysctl, "FIRMWARE_LOADER_PRIVATE");

void unregister_firmware_config_sysctl(void)
{
	unregister_sysctl_table(firmware_config_sysct_table_header);
	firmware_config_sysct_table_header = NULL;
}
EXPORT_SYMBOL_NS_GPL(unregister_firmware_config_sysctl, "FIRMWARE_LOADER_PRIVATE");

#endif /* CONFIG_SYSCTL */
