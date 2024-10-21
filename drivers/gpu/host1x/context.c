// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2021, NVIDIA Corporation.
 */

#include <linux/device.h>
#include <linux/kref.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pid.h>
#include <linux/slab.h>

#include "context.h"
#include "dev.h"

static void host1x_memory_context_release(struct device *dev)
{
	/* context device is freed in host1x_memory_context_list_free() */
}

int host1x_memory_context_list_init(struct host1x *host1x)
{
	struct host1x_memory_context_list *cdl = &host1x->context_list;
	struct device_node *node = host1x->dev->of_node;
	struct host1x_memory_context *ctx;
	unsigned int i;
	int err;

	cdl->devs = NULL;
	cdl->len = 0;
	mutex_init(&cdl->lock);

	err = of_property_count_u32_elems(node, "iommu-map");
	if (err < 0)
		return 0;

	cdl->len = err / 4;
	cdl->devs = kcalloc(cdl->len, sizeof(*cdl->devs), GFP_KERNEL);
	if (!cdl->devs)
		return -ENOMEM;

	for (i = 0; i < cdl->len; i++) {
		ctx = &cdl->devs[i];

		ctx->host = host1x;

		device_initialize(&ctx->dev);

		/*
		 * Due to an issue with T194 NVENC, only 38 bits can be used.
		 * Anyway, 256GiB of IOVA ought to be enough for anyone.
		 */
		ctx->dma_mask = DMA_BIT_MASK(38);
		ctx->dev.dma_mask = &ctx->dma_mask;
		ctx->dev.coherent_dma_mask = ctx->dma_mask;
		dev_set_name(&ctx->dev, "host1x-ctx.%d", i);
		ctx->dev.bus = &host1x_context_device_bus_type;
		ctx->dev.parent = host1x->dev;
		ctx->dev.release = host1x_memory_context_release;

		ctx->dev.dma_parms = &ctx->dma_parms;
		dma_set_max_seg_size(&ctx->dev, UINT_MAX);

		err = device_add(&ctx->dev);
		if (err) {
			dev_err(host1x->dev, "could not add context device %d: %d\n", i, err);
			put_device(&ctx->dev);
			goto unreg_devices;
		}

		err = of_dma_configure_id(&ctx->dev, node, true, &i);
		if (err) {
			dev_err(host1x->dev, "IOMMU configuration failed for context device %d: %d\n",
				i, err);
			device_unregister(&ctx->dev);
			goto unreg_devices;
		}

		if (!tegra_dev_iommu_get_stream_id(&ctx->dev, &ctx->stream_id) ||
		    !device_iommu_mapped(&ctx->dev)) {
			dev_err(host1x->dev, "Context device %d has no IOMMU!\n", i);
			device_unregister(&ctx->dev);

			/*
			 * This means that if IOMMU is disabled but context devices
			 * are defined in the device tree, Host1x will fail to probe.
			 * That's probably OK in this time and age.
			 */
			err = -EINVAL;

			goto unreg_devices;
		}
	}

	return 0;

unreg_devices:
	while (i--)
		device_unregister(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->devs = NULL;
	cdl->len = 0;

	return err;
}

void host1x_memory_context_list_free(struct host1x_memory_context_list *cdl)
{
	unsigned int i;

	for (i = 0; i < cdl->len; i++)
		device_unregister(&cdl->devs[i].dev);

	kfree(cdl->devs);
	cdl->len = 0;
}

struct host1x_memory_context *host1x_memory_context_alloc(struct host1x *host1x,
							  struct device *dev,
							  struct pid *pid)
{
	struct host1x_memory_context_list *cdl = &host1x->context_list;
	struct host1x_memory_context *free = NULL;
	int i;

	if (!cdl->len)
		return ERR_PTR(-EOPNOTSUPP);

	mutex_lock(&cdl->lock);

	for (i = 0; i < cdl->len; i++) {
		struct host1x_memory_context *cd = &cdl->devs[i];

		if (cd->dev.iommu->iommu_dev != dev->iommu->iommu_dev)
			continue;

		if (cd->owner == pid) {
			refcount_inc(&cd->ref);
			mutex_unlock(&cdl->lock);
			return cd;
		} else if (!cd->owner && !free) {
			free = cd;
		}
	}

	if (!free) {
		mutex_unlock(&cdl->lock);
		return ERR_PTR(-EBUSY);
	}

	refcount_set(&free->ref, 1);
	free->owner = get_pid(pid);

	mutex_unlock(&cdl->lock);

	return free;
}
EXPORT_SYMBOL_GPL(host1x_memory_context_alloc);

void host1x_memory_context_get(struct host1x_memory_context *cd)
{
	refcount_inc(&cd->ref);
}
EXPORT_SYMBOL_GPL(host1x_memory_context_get);

void host1x_memory_context_put(struct host1x_memory_context *cd)
{
	struct host1x_memory_context_list *cdl = &cd->host->context_list;

	if (refcount_dec_and_mutex_lock(&cd->ref, &cdl->lock)) {
		put_pid(cd->owner);
		cd->owner = NULL;
		mutex_unlock(&cdl->lock);
	}
}
EXPORT_SYMBOL_GPL(host1x_memory_context_put);
