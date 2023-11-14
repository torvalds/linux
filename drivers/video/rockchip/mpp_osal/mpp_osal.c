// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd
 *
 */
#include "mpp_osal.h"

struct device_node *mpp_dev_of_node(struct device *dev)
{
	return dev_of_node(dev);
}
EXPORT_SYMBOL(mpp_dev_of_node);

void mpp_pm_relax(struct device *dev)
{
	return pm_relax(dev);
}
EXPORT_SYMBOL(mpp_pm_relax);

void mpp_pm_stay_awake(struct device *dev)
{
	return pm_stay_awake(dev);
}
EXPORT_SYMBOL(mpp_pm_stay_awake);

int mpp_device_init_wakeup(struct device *dev, bool enable)
{
	return device_init_wakeup(dev, enable);
}
EXPORT_SYMBOL(mpp_device_init_wakeup);
