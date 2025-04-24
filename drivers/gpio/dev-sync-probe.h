/* SPDX-License-Identifier: GPL-2.0 */

#ifndef DEV_SYNC_PROBE_H
#define DEV_SYNC_PROBE_H

#include <linux/completion.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>

struct dev_sync_probe_data {
	struct platform_device *pdev;
	const char *name;

	/* Synchronize with probe */
	struct notifier_block bus_notifier;
	struct completion probe_completion;
	bool driver_bound;
};

void dev_sync_probe_init(struct dev_sync_probe_data *data);
int dev_sync_probe_register(struct dev_sync_probe_data *data,
			    struct platform_device_info *pdevinfo);
void dev_sync_probe_unregister(struct dev_sync_probe_data *data);

#endif /* DEV_SYNC_PROBE_H */
