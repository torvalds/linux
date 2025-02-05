// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2021-2022 Bootlin
 * Author: Paul Kocialkowski <paul.kocialkowski@bootlin.com>
 */

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <media/v4l2-device.h>
#include <media/v4l2-mc.h>

#include "sun6i_isp.h"
#include "sun6i_isp_capture.h"
#include "sun6i_isp_params.h"
#include "sun6i_isp_proc.h"
#include "sun6i_isp_reg.h"

/* Helpers */

u32 sun6i_isp_load_read(struct sun6i_isp_device *isp_dev, u32 offset)
{
	u32 *data = (u32 *)(isp_dev->tables.load.data + offset);

	return *data;
}

void sun6i_isp_load_write(struct sun6i_isp_device *isp_dev, u32 offset,
			  u32 value)
{
	u32 *data = (u32 *)(isp_dev->tables.load.data + offset);

	*data = value;
}

/* State */

/*
 * The ISP works with a load buffer, which gets copied to the actual registers
 * by the hardware before processing a frame when a specific flag is set.
 * This is represented by tracking the ISP state in the different parts of
 * the code with explicit sync points:
 * - state update: to update the load buffer for the next frame if necessary;
 * - state complete: to indicate that the state update was applied.
 */

static void sun6i_isp_state_ready(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;
	u32 value;

	regmap_read(regmap, SUN6I_ISP_FE_CTRL_REG, &value);
	value |= SUN6I_ISP_FE_CTRL_PARA_READY;
	regmap_write(regmap, SUN6I_ISP_FE_CTRL_REG, value);
}

static void sun6i_isp_state_complete(struct sun6i_isp_device *isp_dev)
{
	unsigned long flags;

	spin_lock_irqsave(&isp_dev->state_lock, flags);

	sun6i_isp_capture_state_complete(isp_dev);
	sun6i_isp_params_state_complete(isp_dev);

	spin_unlock_irqrestore(&isp_dev->state_lock, flags);
}

void sun6i_isp_state_update(struct sun6i_isp_device *isp_dev, bool ready_hold)
{
	bool update = false;
	unsigned long flags;

	spin_lock_irqsave(&isp_dev->state_lock, flags);

	sun6i_isp_capture_state_update(isp_dev, &update);
	sun6i_isp_params_state_update(isp_dev, &update);

	if (update && !ready_hold)
		sun6i_isp_state_ready(isp_dev);

	spin_unlock_irqrestore(&isp_dev->state_lock, flags);
}

/* Tables */

static int sun6i_isp_table_setup(struct sun6i_isp_device *isp_dev,
				 struct sun6i_isp_table *table)
{
	table->data = dma_alloc_coherent(isp_dev->dev, table->size,
					 &table->address, GFP_KERNEL);
	if (!table->data)
		return -ENOMEM;

	return 0;
}

static void sun6i_isp_table_cleanup(struct sun6i_isp_device *isp_dev,
				    struct sun6i_isp_table *table)
{
	dma_free_coherent(isp_dev->dev, table->size, table->data,
			  table->address);
}

void sun6i_isp_tables_configure(struct sun6i_isp_device *isp_dev)
{
	struct regmap *regmap = isp_dev->regmap;

	regmap_write(regmap, SUN6I_ISP_REG_LOAD_ADDR_REG,
		     SUN6I_ISP_ADDR_VALUE(isp_dev->tables.load.address));

	regmap_write(regmap, SUN6I_ISP_REG_SAVE_ADDR_REG,
		     SUN6I_ISP_ADDR_VALUE(isp_dev->tables.save.address));

	regmap_write(regmap, SUN6I_ISP_LUT_TABLE_ADDR_REG,
		     SUN6I_ISP_ADDR_VALUE(isp_dev->tables.lut.address));

	regmap_write(regmap, SUN6I_ISP_DRC_TABLE_ADDR_REG,
		     SUN6I_ISP_ADDR_VALUE(isp_dev->tables.drc.address));

	regmap_write(regmap, SUN6I_ISP_STATS_ADDR_REG,
		     SUN6I_ISP_ADDR_VALUE(isp_dev->tables.stats.address));
}

static int sun6i_isp_tables_setup(struct sun6i_isp_device *isp_dev,
				  const struct sun6i_isp_variant *variant)
{
	struct sun6i_isp_tables *tables = &isp_dev->tables;
	int ret;

	tables->load.size = variant->table_load_save_size;
	ret = sun6i_isp_table_setup(isp_dev, &tables->load);
	if (ret)
		return ret;

	tables->save.size = variant->table_load_save_size;
	ret = sun6i_isp_table_setup(isp_dev, &tables->save);
	if (ret)
		return ret;

	tables->lut.size = variant->table_lut_size;
	ret = sun6i_isp_table_setup(isp_dev, &tables->lut);
	if (ret)
		return ret;

	tables->drc.size = variant->table_drc_size;
	ret = sun6i_isp_table_setup(isp_dev, &tables->drc);
	if (ret)
		return ret;

	tables->stats.size = variant->table_stats_size;
	ret = sun6i_isp_table_setup(isp_dev, &tables->stats);
	if (ret)
		return ret;

	return 0;
}

static void sun6i_isp_tables_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_tables *tables = &isp_dev->tables;

	sun6i_isp_table_cleanup(isp_dev, &tables->stats);
	sun6i_isp_table_cleanup(isp_dev, &tables->drc);
	sun6i_isp_table_cleanup(isp_dev, &tables->lut);
	sun6i_isp_table_cleanup(isp_dev, &tables->save);
	sun6i_isp_table_cleanup(isp_dev, &tables->load);
}

/* Media */

static const struct media_device_ops sun6i_isp_media_ops = {
	.link_notify = v4l2_pipeline_link_notify,
};

/* V4L2 */

static int sun6i_isp_v4l2_setup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_v4l2 *v4l2 = &isp_dev->v4l2;
	struct v4l2_device *v4l2_dev = &v4l2->v4l2_dev;
	struct media_device *media_dev = &v4l2->media_dev;
	struct device *dev = isp_dev->dev;
	int ret;

	/* Media Device */

	strscpy(media_dev->model, SUN6I_ISP_DESCRIPTION,
		sizeof(media_dev->model));
	media_dev->ops = &sun6i_isp_media_ops;
	media_dev->hw_revision = 0;
	media_dev->dev = dev;

	media_device_init(media_dev);

	ret = media_device_register(media_dev);
	if (ret) {
		dev_err(dev, "failed to register media device\n");
		return ret;
	}

	/* V4L2 Device */

	v4l2_dev->mdev = media_dev;

	ret = v4l2_device_register(dev, v4l2_dev);
	if (ret) {
		dev_err(dev, "failed to register v4l2 device\n");
		goto error_media;
	}

	return 0;

error_media:
	media_device_unregister(media_dev);
	media_device_cleanup(media_dev);

	return ret;
}

static void sun6i_isp_v4l2_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct sun6i_isp_v4l2 *v4l2 = &isp_dev->v4l2;

	media_device_unregister(&v4l2->media_dev);
	v4l2_device_unregister(&v4l2->v4l2_dev);
	media_device_cleanup(&v4l2->media_dev);
}

/* Platform */

static irqreturn_t sun6i_isp_interrupt(int irq, void *private)
{
	struct sun6i_isp_device *isp_dev = private;
	struct regmap *regmap = isp_dev->regmap;
	u32 status = 0, enable = 0;

	regmap_read(regmap, SUN6I_ISP_FE_INT_STA_REG, &status);
	regmap_read(regmap, SUN6I_ISP_FE_INT_EN_REG, &enable);

	if (!status)
		return IRQ_NONE;
	else if (!(status & enable))
		goto complete;

	/*
	 * The ISP working cycle starts with a params-load, which makes the
	 * state from the load buffer active. Then it starts processing the
	 * frame and gives a finish interrupt. Soon after that, the next state
	 * coming from the load buffer will be applied for the next frame,
	 * giving a params-load as well.
	 *
	 * Because both frame finish and params-load are received almost
	 * at the same time (one ISR call), handle them in chronology order.
	 */

	if (status & SUN6I_ISP_FE_INT_STA_FINISH)
		sun6i_isp_capture_finish(isp_dev);

	if (status & SUN6I_ISP_FE_INT_STA_PARA_LOAD) {
		sun6i_isp_state_complete(isp_dev);
		sun6i_isp_state_update(isp_dev, false);
	}

complete:
	regmap_write(regmap, SUN6I_ISP_FE_INT_STA_REG, status);

	return IRQ_HANDLED;
}

static int sun6i_isp_suspend(struct device *dev)
{
	struct sun6i_isp_device *isp_dev = dev_get_drvdata(dev);

	reset_control_assert(isp_dev->reset);
	clk_disable_unprepare(isp_dev->clock_ram);
	clk_disable_unprepare(isp_dev->clock_mod);

	return 0;
}

static int sun6i_isp_resume(struct device *dev)
{
	struct sun6i_isp_device *isp_dev = dev_get_drvdata(dev);
	int ret;

	ret = reset_control_deassert(isp_dev->reset);
	if (ret) {
		dev_err(dev, "failed to deassert reset\n");
		return ret;
	}

	ret = clk_prepare_enable(isp_dev->clock_mod);
	if (ret) {
		dev_err(dev, "failed to enable module clock\n");
		goto error_reset;
	}

	ret = clk_prepare_enable(isp_dev->clock_ram);
	if (ret) {
		dev_err(dev, "failed to enable ram clock\n");
		goto error_clock_mod;
	}

	return 0;

error_clock_mod:
	clk_disable_unprepare(isp_dev->clock_mod);

error_reset:
	reset_control_assert(isp_dev->reset);

	return ret;
}

static const struct dev_pm_ops sun6i_isp_pm_ops = {
	.runtime_suspend	= sun6i_isp_suspend,
	.runtime_resume		= sun6i_isp_resume,
};

static const struct regmap_config sun6i_isp_regmap_config = {
	.reg_bits       = 32,
	.reg_stride     = 4,
	.val_bits       = 32,
	.max_register	= 0x400,
};

static int sun6i_isp_resources_setup(struct sun6i_isp_device *isp_dev,
				     struct platform_device *platform_dev)
{
	struct device *dev = isp_dev->dev;
	void __iomem *io_base;
	int irq;
	int ret;

	/* Registers */

	io_base = devm_platform_ioremap_resource(platform_dev, 0);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	isp_dev->regmap = devm_regmap_init_mmio_clk(dev, "bus", io_base,
						    &sun6i_isp_regmap_config);
	if (IS_ERR(isp_dev->regmap)) {
		dev_err(dev, "failed to init register map\n");
		return PTR_ERR(isp_dev->regmap);
	}

	/* Clocks */

	isp_dev->clock_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(isp_dev->clock_mod)) {
		dev_err(dev, "failed to acquire module clock\n");
		return PTR_ERR(isp_dev->clock_mod);
	}

	isp_dev->clock_ram = devm_clk_get(dev, "ram");
	if (IS_ERR(isp_dev->clock_ram)) {
		dev_err(dev, "failed to acquire ram clock\n");
		return PTR_ERR(isp_dev->clock_ram);
	}

	ret = clk_set_rate_exclusive(isp_dev->clock_mod, 297000000);
	if (ret) {
		dev_err(dev, "failed to set mod clock rate\n");
		return ret;
	}

	/* Reset */

	isp_dev->reset = devm_reset_control_get_shared(dev, NULL);
	if (IS_ERR(isp_dev->reset)) {
		dev_err(dev, "failed to acquire reset\n");
		ret = PTR_ERR(isp_dev->reset);
		goto error_clock_rate_exclusive;
	}

	/* Interrupt */

	irq = platform_get_irq(platform_dev, 0);
	if (irq < 0) {
		ret = irq;
		goto error_clock_rate_exclusive;
	}

	ret = devm_request_irq(dev, irq, sun6i_isp_interrupt, IRQF_SHARED,
			       SUN6I_ISP_NAME, isp_dev);
	if (ret) {
		dev_err(dev, "failed to request interrupt\n");
		goto error_clock_rate_exclusive;
	}

	/* Runtime PM */

	pm_runtime_enable(dev);

	return 0;

error_clock_rate_exclusive:
	clk_rate_exclusive_put(isp_dev->clock_mod);

	return ret;
}

static void sun6i_isp_resources_cleanup(struct sun6i_isp_device *isp_dev)
{
	struct device *dev = isp_dev->dev;

	pm_runtime_disable(dev);
	clk_rate_exclusive_put(isp_dev->clock_mod);
}

static int sun6i_isp_probe(struct platform_device *platform_dev)
{
	struct sun6i_isp_device *isp_dev;
	struct device *dev = &platform_dev->dev;
	const struct sun6i_isp_variant *variant;
	int ret;

	variant = of_device_get_match_data(dev);
	if (!variant)
		return -EINVAL;

	isp_dev = devm_kzalloc(dev, sizeof(*isp_dev), GFP_KERNEL);
	if (!isp_dev)
		return -ENOMEM;

	isp_dev->dev = dev;
	platform_set_drvdata(platform_dev, isp_dev);

	spin_lock_init(&isp_dev->state_lock);

	ret = sun6i_isp_resources_setup(isp_dev, platform_dev);
	if (ret)
		return ret;

	ret = sun6i_isp_tables_setup(isp_dev, variant);
	if (ret) {
		dev_err(dev, "failed to setup tables\n");
		goto error_resources;
	}

	ret = sun6i_isp_v4l2_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup v4l2\n");
		goto error_tables;
	}

	ret = sun6i_isp_proc_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup proc\n");
		goto error_v4l2;
	}

	ret = sun6i_isp_capture_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup capture\n");
		goto error_proc;
	}

	ret = sun6i_isp_params_setup(isp_dev);
	if (ret) {
		dev_err(dev, "failed to setup params\n");
		goto error_capture;
	}

	return 0;

error_capture:
	sun6i_isp_capture_cleanup(isp_dev);

error_proc:
	sun6i_isp_proc_cleanup(isp_dev);

error_v4l2:
	sun6i_isp_v4l2_cleanup(isp_dev);

error_tables:
	sun6i_isp_tables_cleanup(isp_dev);

error_resources:
	sun6i_isp_resources_cleanup(isp_dev);

	return ret;
}

static void sun6i_isp_remove(struct platform_device *platform_dev)
{
	struct sun6i_isp_device *isp_dev = platform_get_drvdata(platform_dev);

	sun6i_isp_params_cleanup(isp_dev);
	sun6i_isp_capture_cleanup(isp_dev);
	sun6i_isp_proc_cleanup(isp_dev);
	sun6i_isp_v4l2_cleanup(isp_dev);
	sun6i_isp_tables_cleanup(isp_dev);
	sun6i_isp_resources_cleanup(isp_dev);
}

/*
 * History of sun6i-isp:
 * - sun4i-a10-isp: initial ISP tied to the CSI0 controller,
 *   apparently unused in software implementations;
 * - sun6i-a31-isp: separate ISP loosely based on sun4i-a10-isp,
 *   adding extra modules and features;
 * - sun9i-a80-isp: based on sun6i-a31-isp with some register offset changes
 *   and new modules like saturation and cnr;
 * - sun8i-a23-isp/sun8i-h3-isp: based on sun9i-a80-isp with most modules
 *   related to raw removed;
 * - sun8i-a83t-isp: based on sun9i-a80-isp with some register offset changes
 * - sun8i-v3s-isp: based on sun8i-a83t-isp with a new disc module;
 */

static const struct sun6i_isp_variant sun8i_v3s_isp_variant = {
	.table_load_save_size	= 0x1000,
	.table_lut_size		= 0xe00,
	.table_drc_size		= 0x600,
	.table_stats_size	= 0x2100,
};

static const struct of_device_id sun6i_isp_of_match[] = {
	{
		.compatible	= "allwinner,sun8i-v3s-isp",
		.data		= &sun8i_v3s_isp_variant,
	},
	{},
};

MODULE_DEVICE_TABLE(of, sun6i_isp_of_match);

static struct platform_driver sun6i_isp_platform_driver = {
	.probe	= sun6i_isp_probe,
	.remove = sun6i_isp_remove,
	.driver	= {
		.name		= SUN6I_ISP_NAME,
		.of_match_table	= sun6i_isp_of_match,
		.pm		= &sun6i_isp_pm_ops,
	},
};

module_platform_driver(sun6i_isp_platform_driver);

MODULE_DESCRIPTION("Allwinner A31 Image Signal Processor driver");
MODULE_AUTHOR("Paul Kocialkowski <paul.kocialkowski@bootlin.com>");
MODULE_LICENSE("GPL");
