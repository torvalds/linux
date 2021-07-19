// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Shujuan Chen <shujuan.chen@stericsson.com> for ST-Ericsson.
 * Author: Jonas Linde <jonas.linde@stericsson.com> for ST-Ericsson.
 * Author: Joakim Bech <joakim.xx.bech@stericsson.com> for ST-Ericsson.
 * Author: Berne Hebark <berne.herbark@stericsson.com> for ST-Ericsson.
 * Author: Niklas Hernaeus <niklas.hernaeus@stericsson.com> for ST-Ericsson.
 */

#include <linux/kernel.h>
#include <linux/bitmap.h>
#include <linux/device.h>

#include "cryp.h"
#include "cryp_p.h"
#include "cryp_irq.h"
#include "cryp_irqp.h"

void cryp_enable_irq_src(struct cryp_device_data *device_data, u32 irq_src)
{
	u32 i;

	dev_dbg(device_data->dev, "[%s]", __func__);

	i = readl_relaxed(&device_data->base->imsc);
	i = i | irq_src;
	writel_relaxed(i, &device_data->base->imsc);
}

void cryp_disable_irq_src(struct cryp_device_data *device_data, u32 irq_src)
{
	u32 i;

	dev_dbg(device_data->dev, "[%s]", __func__);

	i = readl_relaxed(&device_data->base->imsc);
	i = i & ~irq_src;
	writel_relaxed(i, &device_data->base->imsc);
}

bool cryp_pending_irq_src(struct cryp_device_data *device_data, u32 irq_src)
{
	return (readl_relaxed(&device_data->base->mis) & irq_src) > 0;
}
