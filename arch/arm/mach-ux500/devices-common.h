/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#ifndef __DEVICES_COMMON_H
#define __DEVICES_COMMON_H

#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/sys_soc.h>
#include <linux/amba/bus.h>

struct amba_pl011_data;

static inline struct amba_device *
dbx500_add_uart(struct device *parent, const char *name, resource_size_t base,
		int irq, struct amba_pl011_data *pdata)
{
	return amba_ahb_device_add(parent, name, base, SZ_4K, irq, 0, pdata, 0);
}

#endif
