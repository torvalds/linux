// SPDX-License-Identifier: GPL-2.0
/*
 * RTC subsystem, nvmem interface
 *
 * Copyright (C) 2017 Alexandre Belloni
 */

#include <linux/err.h>
#include <linux/types.h>
#include <linux/nvmem-consumer.h>
#include <linux/rtc.h>

int devm_rtc_nvmem_register(struct rtc_device *rtc,
		       struct nvmem_config *nvmem_config)
{
	struct device *dev = rtc->dev.parent;
	struct nvmem_device *nvmem;

	if (!nvmem_config)
		return -ENODEV;

	nvmem_config->dev = dev;
	nvmem_config->owner = rtc->owner;
	nvmem = devm_nvmem_register(dev, nvmem_config);
	if (IS_ERR(nvmem))
		dev_err(dev, "failed to register nvmem device for RTC\n");

	return PTR_ERR_OR_ZERO(nvmem);
}
EXPORT_SYMBOL_GPL(devm_rtc_nvmem_register);
