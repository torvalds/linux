// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2019 Texas Instruments Incorporated - https://www.ti.com/
// Author: Vignesh Raghavendra <vigneshr@ti.com>

#include <linux/completion.h>
#include <linux/dma-direction.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mtd/cfi.h>
#include <linux/mtd/hyperbus.h>
#include <linux/mtd/mtd.h>
#include <linux/mux/consumer.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/sched/task_stack.h>
#include <linux/types.h>

#define AM654_HBMC_CALIB_COUNT 25

struct am654_hbmc_device_priv {
	struct completion rx_dma_complete;
	phys_addr_t device_base;
	struct hyperbus_ctlr *ctlr;
	struct dma_chan *rx_chan;
};

struct am654_hbmc_priv {
	struct hyperbus_ctlr ctlr;
	struct hyperbus_device hbdev;
	struct mux_control *mux_ctrl;
};

static int am654_hbmc_calibrate(struct hyperbus_device *hbdev)
{
	struct map_info *map = &hbdev->map;
	struct cfi_private cfi;
	int count = AM654_HBMC_CALIB_COUNT;
	int pass_count = 0;
	int ret;

	cfi.interleave = 1;
	cfi.device_type = CFI_DEVICETYPE_X16;
	cfi_send_gen_cmd(0xF0, 0, 0, map, &cfi, cfi.device_type, NULL);
	cfi_send_gen_cmd(0x98, 0x55, 0, map, &cfi, cfi.device_type, NULL);

	while (count--) {
		ret = cfi_qry_present(map, 0, &cfi);
		if (ret)
			pass_count++;
		else
			pass_count = 0;
		if (pass_count == 5)
			break;
	}

	cfi_qry_mode_off(0, map, &cfi);

	return ret;
}

static void am654_hbmc_dma_callback(void *param)
{
	struct am654_hbmc_device_priv *priv = param;

	complete(&priv->rx_dma_complete);
}

static int am654_hbmc_dma_read(struct am654_hbmc_device_priv *priv, void *to,
			       unsigned long from, ssize_t len)

{
	enum dma_ctrl_flags flags = DMA_CTRL_ACK | DMA_PREP_INTERRUPT;
	struct dma_chan *rx_chan = priv->rx_chan;
	struct dma_async_tx_descriptor *tx;
	dma_addr_t dma_dst, dma_src;
	dma_cookie_t cookie;
	int ret;

	if (!priv->rx_chan || !virt_addr_valid(to) || object_is_on_stack(to))
		return -EINVAL;

	dma_dst = dma_map_single(rx_chan->device->dev, to, len, DMA_FROM_DEVICE);
	if (dma_mapping_error(rx_chan->device->dev, dma_dst)) {
		dev_dbg(priv->ctlr->dev, "DMA mapping failed\n");
		return -EIO;
	}

	dma_src = priv->device_base + from;
	tx = dmaengine_prep_dma_memcpy(rx_chan, dma_dst, dma_src, len, flags);
	if (!tx) {
		dev_err(priv->ctlr->dev, "device_prep_dma_memcpy error\n");
		ret = -EIO;
		goto unmap_dma;
	}

	reinit_completion(&priv->rx_dma_complete);
	tx->callback = am654_hbmc_dma_callback;
	tx->callback_param = priv;
	cookie = dmaengine_submit(tx);

	ret = dma_submit_error(cookie);
	if (ret) {
		dev_err(priv->ctlr->dev, "dma_submit_error %d\n", cookie);
		goto unmap_dma;
	}

	dma_async_issue_pending(rx_chan);
	if (!wait_for_completion_timeout(&priv->rx_dma_complete,  msecs_to_jiffies(len + 1000))) {
		dmaengine_terminate_sync(rx_chan);
		dev_err(priv->ctlr->dev, "DMA wait_for_completion_timeout\n");
		ret = -ETIMEDOUT;
	}

unmap_dma:
	dma_unmap_single(rx_chan->device->dev, dma_dst, len, DMA_FROM_DEVICE);
	return ret;
}

static void am654_hbmc_read(struct hyperbus_device *hbdev, void *to,
			    unsigned long from, ssize_t len)
{
	struct am654_hbmc_device_priv *priv = hbdev->priv;

	if (len < SZ_1K || am654_hbmc_dma_read(priv, to, from, len))
		memcpy_fromio(to, hbdev->map.virt + from, len);
}

static const struct hyperbus_ops am654_hbmc_ops = {
	.calibrate = am654_hbmc_calibrate,
	.copy_from = am654_hbmc_read,
};

static int am654_hbmc_request_mmap_dma(struct am654_hbmc_device_priv *priv)
{
	struct dma_chan *rx_chan;
	dma_cap_mask_t mask;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	rx_chan = dma_request_chan_by_mask(&mask);
	if (IS_ERR(rx_chan)) {
		if (PTR_ERR(rx_chan) == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		dev_dbg(priv->ctlr->dev, "No DMA channel available\n");
		return 0;
	}
	priv->rx_chan = rx_chan;
	init_completion(&priv->rx_dma_complete);

	return 0;
}

static int am654_hbmc_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct am654_hbmc_device_priv *dev_priv;
	struct device *dev = &pdev->dev;
	struct am654_hbmc_priv *priv;
	struct resource res;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->hbdev.np = of_get_next_child(np, NULL);
	ret = of_address_to_resource(priv->hbdev.np, 0, &res);
	if (ret)
		return ret;

	if (of_property_read_bool(dev->of_node, "mux-controls")) {
		struct mux_control *control = devm_mux_control_get(dev, NULL);

		if (IS_ERR(control))
			return PTR_ERR(control);

		ret = mux_control_select(control, 1);
		if (ret) {
			dev_err(dev, "Failed to select HBMC mux\n");
			return ret;
		}
		priv->mux_ctrl = control;
	}

	priv->hbdev.map.size = resource_size(&res);
	priv->hbdev.map.virt = devm_ioremap_resource(dev, &res);
	if (IS_ERR(priv->hbdev.map.virt))
		return PTR_ERR(priv->hbdev.map.virt);

	priv->ctlr.dev = dev;
	priv->ctlr.ops = &am654_hbmc_ops;
	priv->hbdev.ctlr = &priv->ctlr;

	dev_priv = devm_kzalloc(dev, sizeof(*dev_priv), GFP_KERNEL);
	if (!dev_priv) {
		ret = -ENOMEM;
		goto disable_mux;
	}

	priv->hbdev.priv = dev_priv;
	dev_priv->device_base = res.start;
	dev_priv->ctlr = &priv->ctlr;

	ret = am654_hbmc_request_mmap_dma(dev_priv);
	if (ret)
		goto disable_mux;

	ret = hyperbus_register_device(&priv->hbdev);
	if (ret) {
		dev_err(dev, "failed to register controller\n");
		goto release_dma;
	}

	return 0;
release_dma:
	if (dev_priv->rx_chan)
		dma_release_channel(dev_priv->rx_chan);
disable_mux:
	if (priv->mux_ctrl)
		mux_control_deselect(priv->mux_ctrl);
	return ret;
}

static int am654_hbmc_remove(struct platform_device *pdev)
{
	struct am654_hbmc_priv *priv = platform_get_drvdata(pdev);
	struct am654_hbmc_device_priv *dev_priv = priv->hbdev.priv;
	int ret;

	ret = hyperbus_unregister_device(&priv->hbdev);
	if (priv->mux_ctrl)
		mux_control_deselect(priv->mux_ctrl);

	if (dev_priv->rx_chan)
		dma_release_channel(dev_priv->rx_chan);

	return ret;
}

static const struct of_device_id am654_hbmc_dt_ids[] = {
	{
		.compatible = "ti,am654-hbmc",
	},
	{ /* end of table */ }
};

MODULE_DEVICE_TABLE(of, am654_hbmc_dt_ids);

static struct platform_driver am654_hbmc_platform_driver = {
	.probe = am654_hbmc_probe,
	.remove = am654_hbmc_remove,
	.driver = {
		.name = "hbmc-am654",
		.of_match_table = am654_hbmc_dt_ids,
	},
};

module_platform_driver(am654_hbmc_platform_driver);

MODULE_DESCRIPTION("HBMC driver for AM654 SoC");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:hbmc-am654");
MODULE_AUTHOR("Vignesh Raghavendra <vigneshr@ti.com>");
