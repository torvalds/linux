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

int rtc_nvmem_register(struct rtc_device *rtc,
		       struct nvmem_config *nvmem_config)
{
	struct nvmem_device *nvmem;

	if (!nvmem_config)
		return -ENODEV;

	nvmem_config->dev = rtc->dev.parent;
	nvmem_config->owner = rtc->owner;
	nvmem = devm_nvmem_register(rtc->dev.parent, nvmem_config);

	return PTR_ERR_OR_ZERO(nvmem);
}
EXPORT_SYMBOL_GPL(rtc_nvmem_register);
