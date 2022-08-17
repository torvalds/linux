// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2022, Intel Corporation.
 */

#include <linux/slab.h>

#include "t7xx_uevent.h"

/* Update the uevent in work queue context */
static void t7xx_uevent_work(struct work_struct *data)
{
	struct t7xx_uevent_info *info;
	char *envp[2] = { NULL, NULL };

	info = container_of(data, struct t7xx_uevent_info, work);
	envp[0] = info->uevent;

	if (kobject_uevent_env(&info->dev->kobj, KOBJ_CHANGE, envp))
		pr_err("uevent %s failed to sent", info->uevent);

	kfree(info);
}

/**
 * t7xx_uevent_send - Send modem event to user space.
 * @dev:	Generic device pointer
 * @uevent:	Uevent information
 */
void t7xx_uevent_send(struct device *dev, char *uevent)
{
	struct t7xx_uevent_info *info = kzalloc(sizeof(*info), GFP_ATOMIC);

	if (!info)
		return;

	INIT_WORK(&info->work, t7xx_uevent_work);
	info->dev = dev;
	snprintf(info->uevent, T7XX_MAX_UEVENT_LEN, "T7XX_EVENT=%s", uevent);
	schedule_work(&info->work);
}
