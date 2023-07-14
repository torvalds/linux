// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * Based on drivers/misc/eeprom/sunxi_sid.c
 */

#include <linux/device.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define FUSE_BEGIN	0x100
#define FUSE_UID_LOW	0x08
#define FUSE_UID_HIGH	0x0c

static u32 tegra20_fuse_read_early(struct tegra_fuse *fuse, unsigned int offset)
{
	return readl_relaxed(fuse->base + FUSE_BEGIN + offset);
}

static void apb_dma_complete(void *args)
{
	struct tegra_fuse *fuse = args;

	complete(&fuse->apbdma.wait);
}

static u32 tegra20_fuse_read(struct tegra_fuse *fuse, unsigned int offset)
{
	unsigned long flags = DMA_PREP_INTERRUPT | DMA_CTRL_ACK;
	struct dma_async_tx_descriptor *dma_desc;
	unsigned long time_left;
	u32 value = 0;
	int err;

	err = pm_runtime_resume_and_get(fuse->dev);
	if (err)
		return err;

	mutex_lock(&fuse->apbdma.lock);

	fuse->apbdma.config.src_addr = fuse->phys + FUSE_BEGIN + offset;

	err = dmaengine_slave_config(fuse->apbdma.chan, &fuse->apbdma.config);
	if (err)
		goto out;

	dma_desc = dmaengine_prep_slave_single(fuse->apbdma.chan,
					       fuse->apbdma.phys,
					       sizeof(u32), DMA_DEV_TO_MEM,
					       flags);
	if (!dma_desc)
		goto out;

	dma_desc->callback = apb_dma_complete;
	dma_desc->callback_param = fuse;

	reinit_completion(&fuse->apbdma.wait);

	dmaengine_submit(dma_desc);
	dma_async_issue_pending(fuse->apbdma.chan);
	time_left = wait_for_completion_timeout(&fuse->apbdma.wait,
						msecs_to_jiffies(50));

	if (WARN(time_left == 0, "apb read dma timed out"))
		dmaengine_terminate_all(fuse->apbdma.chan);
	else
		value = *fuse->apbdma.virt;

out:
	mutex_unlock(&fuse->apbdma.lock);
	pm_runtime_put(fuse->dev);
	return value;
}

static bool dma_filter(struct dma_chan *chan, void *filter_param)
{
	struct device_node *np = chan->device->dev->of_node;

	return of_device_is_compatible(np, "nvidia,tegra20-apbdma");
}

static void tegra20_fuse_release_channel(void *data)
{
	struct tegra_fuse *fuse = data;

	dma_release_channel(fuse->apbdma.chan);
	fuse->apbdma.chan = NULL;
}

static void tegra20_fuse_free_coherent(void *data)
{
	struct tegra_fuse *fuse = data;

	dma_free_coherent(fuse->dev, sizeof(u32), fuse->apbdma.virt,
			  fuse->apbdma.phys);
	fuse->apbdma.virt = NULL;
	fuse->apbdma.phys = 0x0;
}

static int tegra20_fuse_probe(struct tegra_fuse *fuse)
{
	dma_cap_mask_t mask;
	int err;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);

	fuse->apbdma.chan = dma_request_channel(mask, dma_filter, NULL);
	if (!fuse->apbdma.chan)
		return -EPROBE_DEFER;

	err = devm_add_action_or_reset(fuse->dev, tegra20_fuse_release_channel,
				       fuse);
	if (err)
		return err;

	fuse->apbdma.virt = dma_alloc_coherent(fuse->dev, sizeof(u32),
					       &fuse->apbdma.phys,
					       GFP_KERNEL);
	if (!fuse->apbdma.virt)
		return -ENOMEM;

	err = devm_add_action_or_reset(fuse->dev, tegra20_fuse_free_coherent,
				       fuse);
	if (err)
		return err;

	fuse->apbdma.config.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	fuse->apbdma.config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	fuse->apbdma.config.src_maxburst = 1;
	fuse->apbdma.config.dst_maxburst = 1;
	fuse->apbdma.config.direction = DMA_DEV_TO_MEM;
	fuse->apbdma.config.device_fc = false;

	init_completion(&fuse->apbdma.wait);
	mutex_init(&fuse->apbdma.lock);
	fuse->read = tegra20_fuse_read;

	return 0;
}

static const struct tegra_fuse_info tegra20_fuse_info = {
	.read = tegra20_fuse_read,
	.size = 0x1f8,
	.spare = 0x100,
};

/* Early boot code. This code is called before the devices are created */

static void __init tegra20_fuse_add_randomness(void)
{
	u32 randomness[7];

	randomness[0] = tegra_sku_info.sku_id;
	randomness[1] = tegra_read_straps();
	randomness[2] = tegra_read_chipid();
	randomness[3] = tegra_sku_info.cpu_process_id << 16;
	randomness[3] |= tegra_sku_info.soc_process_id;
	randomness[4] = tegra_sku_info.cpu_speedo_id << 16;
	randomness[4] |= tegra_sku_info.soc_speedo_id;
	randomness[5] = tegra_fuse_read_early(FUSE_UID_LOW);
	randomness[6] = tegra_fuse_read_early(FUSE_UID_HIGH);

	add_device_randomness(randomness, sizeof(randomness));
}

static void __init tegra20_fuse_init(struct tegra_fuse *fuse)
{
	fuse->read_early = tegra20_fuse_read_early;

	tegra_init_revision();
	fuse->soc->speedo_init(&tegra_sku_info);
	tegra20_fuse_add_randomness();
}

const struct tegra_fuse_soc tegra20_fuse_soc = {
	.init = tegra20_fuse_init,
	.speedo_init = tegra20_init_speedo_data,
	.probe = tegra20_fuse_probe,
	.info = &tegra20_fuse_info,
	.soc_attr_group = &tegra_soc_attr_group,
	.clk_suspend_on = false,
};
