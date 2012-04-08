/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * Author: Rabin Vincent <rabin.vincent@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL), version 2.
 */

#include <linux/kernel.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/amba/bus.h>

#include <plat/gpio-nomadik.h>

#include <mach/hardware.h>

#include "devices-common.h"

struct amba_device *
dbx500_add_amba_device(struct device *parent, const char *name,
		       resource_size_t base, int irq, void *pdata,
		       unsigned int periphid)
{
	struct amba_device *dev;
	int ret;

	dev = amba_device_alloc(name, base, SZ_4K);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->dma_mask = DMA_BIT_MASK(32);
	dev->dev.coherent_dma_mask = DMA_BIT_MASK(32);

	dev->irq[0] = irq;

	dev->periphid = periphid;

	dev->dev.platform_data = pdata;

	dev->dev.parent = parent;

	ret = amba_device_add(dev, &iomem_resource);
	if (ret) {
		amba_device_put(dev);
		return ERR_PTR(ret);
	}

	return dev;
}

static struct platform_device *
dbx500_add_gpio(struct device *parent, int id, resource_size_t addr, int irq,
		struct nmk_gpio_platform_data *pdata)
{
	struct resource resources[] = {
		{
			.start	= addr,
			.end	= addr + 127,
			.flags	= IORESOURCE_MEM,
		},
		{
			.start	= irq,
			.end	= irq,
			.flags	= IORESOURCE_IRQ,
		}
	};

	return platform_device_register_resndata(
		parent,
		"gpio",
		id,
		resources,
		ARRAY_SIZE(resources),
		pdata,
		sizeof(*pdata));
}

void dbx500_add_gpios(struct device *parent, resource_size_t *base, int num,
		      int irq, struct nmk_gpio_platform_data *pdata)
{
	int first = 0;
	int i;

	for (i = 0; i < num; i++, first += 32, irq++) {
		pdata->first_gpio = first;
		pdata->first_irq = NOMADIK_GPIO_TO_IRQ(first);
		pdata->num_gpio = 32;

		dbx500_add_gpio(parent, i, base[i], irq, pdata);
	}
}
