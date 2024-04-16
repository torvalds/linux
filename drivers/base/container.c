// SPDX-License-Identifier: GPL-2.0
/*
 * System bus type for containers.
 *
 * Copyright (C) 2013, Intel Corporation
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include <linux/container.h>

#include "base.h"

#define CONTAINER_BUS_NAME	"container"

static int trivial_online(struct device *dev)
{
	return 0;
}

static int container_offline(struct device *dev)
{
	struct container_dev *cdev = to_container_dev(dev);

	return cdev->offline ? cdev->offline(cdev) : 0;
}

struct bus_type container_subsys = {
	.name = CONTAINER_BUS_NAME,
	.dev_name = CONTAINER_BUS_NAME,
	.online = trivial_online,
	.offline = container_offline,
};

void __init container_dev_init(void)
{
	int ret;

	ret = subsys_system_register(&container_subsys, NULL);
	if (ret)
		pr_err("%s() failed: %d\n", __func__, ret);
}
