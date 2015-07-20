/*
 * Copyright (c) 2013-2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
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
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/random.h>

#include <soc/tegra/fuse.h>

#include "fuse.h"

#define FUSE_BEGIN	0x100
#define FUSE_SIZE	0x1f8
#define FUSE_UID_LOW	0x08
#define FUSE_UID_HIGH	0x0c

static phys_addr_t fuse_phys;
static struct clk *fuse_clk;
static void __iomem __initdata *fuse_base;

static DEFINE_MUTEX(apb_dma_lock);
static DECLARE_COMPLETION(apb_dma_wait);
static struct dma_chan *apb_dma_chan;
static struct dma_slave_config dma_sconfig;
static u32 *apb_buffer;
static dma_addr_t apb_buffer_phys;

static void apb_dma_complete(void *args)
{
	complete(&apb_dma_wait);
}

static u32 tegra20_fuse_readl(const unsigned int offset)
{
	int ret;
	u32 val = 0;
	struct dma_async_tx_descriptor *dma_desc;
	unsigned long time_left;

	mutex_lock(&apb_dma_lock);

	dma_sconfig.src_addr = fuse_phys + FUSE_BEGIN + offset;
	ret = dmaengine_slave_config(apb_dma_chan, &dma_sconfig);
	if (ret)
		goto out;

	dma_desc = dmaengine_prep_slave_single(apb_dma_chan, apb_buffer_phys,
			sizeof(u32), DMA_DEV_TO_MEM,
			DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
	if (!dma_desc)
		goto out;

	dma_desc->callback = apb_dma_complete;
	dma_desc->callback_param = NULL;

	reinit_completion(&apb_dma_wait);

	clk_prepare_enable(fuse_clk);

	dmaengine_submit(dma_desc);
	dma_async_issue_pending(apb_dma_chan);
	time_left = wait_for_completion_timeout(&apb_dma_wait,
						msecs_to_jiffies(50));

	if (WARN(time_left == 0, "apb read dma timed out"))
		dmaengine_terminate_all(apb_dma_chan);
	else
		val = *apb_buffer;

	clk_disable_unprepare(fuse_clk);
out:
	mutex_unlock(&apb_dma_lock);

	return val;
}

static const struct of_device_id tegra20_fuse_of_match[] = {
	{ .compatible = "nvidia,tegra20-efuse" },
	{},
};

static int apb_dma_init(void)
{
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	apb_dma_chan = dma_request_channel(mask, NULL, NULL);
	if (!apb_dma_chan)
		return -EPROBE_DEFER;

	apb_buffer = dma_alloc_coherent(NULL, sizeof(u32), &apb_buffer_phys,
					GFP_KERNEL);
	if (!apb_buffer) {
		dma_release_channel(apb_dma_chan);
		return -ENOMEM;
	}

	dma_sconfig.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_sconfig.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	dma_sconfig.src_maxburst = 1;
	dma_sconfig.dst_maxburst = 1;

	return 0;
}

static int tegra20_fuse_probe(struct platform_device *pdev)
{
	struct resource *res;
	int err;

	fuse_clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(fuse_clk)) {
		dev_err(&pdev->dev, "missing clock");
		return PTR_ERR(fuse_clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	fuse_phys = res->start;

	err = apb_dma_init();
	if (err)
		return err;

	if (tegra_fuse_create_sysfs(&pdev->dev, FUSE_SIZE, tegra20_fuse_readl))
		return -ENODEV;

	dev_dbg(&pdev->dev, "loaded\n");

	return 0;
}

static struct platform_driver tegra20_fuse_driver = {
	.probe = tegra20_fuse_probe,
	.driver = {
		.name = "tegra20_fuse",
		.of_match_table = tegra20_fuse_of_match,
	}
};

static int __init tegra20_fuse_init(void)
{
	return platform_driver_register(&tegra20_fuse_driver);
}
postcore_initcall(tegra20_fuse_init);

/* Early boot code. This code is called before the devices are created */

u32 __init tegra20_fuse_early(const unsigned int offset)
{
	return readl_relaxed(fuse_base + FUSE_BEGIN + offset);
}

bool __init tegra20_spare_fuse_early(int spare_bit)
{
	u32 offset = spare_bit * 4;
	bool value;

	value = tegra20_fuse_early(offset + 0x100);

	return value;
}

static void __init tegra20_fuse_add_randomness(void)
{
	u32 randomness[7];

	randomness[0] = tegra_sku_info.sku_id;
	randomness[1] = tegra_read_straps();
	randomness[2] = tegra_read_chipid();
	randomness[3] = tegra_sku_info.cpu_process_id << 16;
	randomness[3] |= tegra_sku_info.core_process_id;
	randomness[4] = tegra_sku_info.cpu_speedo_id << 16;
	randomness[4] |= tegra_sku_info.soc_speedo_id;
	randomness[5] = tegra20_fuse_early(FUSE_UID_LOW);
	randomness[6] = tegra20_fuse_early(FUSE_UID_HIGH);

	add_device_randomness(randomness, sizeof(randomness));
}

void __init tegra20_init_fuse_early(void)
{
	fuse_base = ioremap(TEGRA_FUSE_BASE, TEGRA_FUSE_SIZE);

	tegra_init_revision();
	tegra20_init_speedo_data(&tegra_sku_info);
	tegra20_fuse_add_randomness();

	iounmap(fuse_base);
}
