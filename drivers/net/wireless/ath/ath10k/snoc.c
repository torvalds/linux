/*
 * Copyright (c) 2018 The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include "debug.h"
#include "hif.h"
#include "htc.h"
#include "ce.h"
#include "snoc.h"
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>

static const struct ath10k_snoc_drv_priv drv_priv = {
	.hw_rev = ATH10K_HW_WCN3990,
	.dma_mask = DMA_BIT_MASK(37),
};

void ath10k_snoc_write32(struct ath10k *ar, u32 offset, u32 value)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);

	iowrite32(value, ar_snoc->mem + offset);
}

u32 ath10k_snoc_read32(struct ath10k *ar, u32 offset)
{
	struct ath10k_snoc *ar_snoc = ath10k_snoc_priv(ar);
	u32 val;

	val = ioread32(ar_snoc->mem + offset);

	return val;
}

static const struct ath10k_hif_ops ath10k_snoc_hif_ops = {
	.read32			= ath10k_snoc_read32,
	.write32		= ath10k_snoc_write32,
};

static const struct ath10k_bus_ops ath10k_snoc_bus_ops = {
	.read32		= ath10k_snoc_read32,
	.write32	= ath10k_snoc_write32,
};

static const struct of_device_id ath10k_snoc_dt_match[] = {
	{ .compatible = "qcom,wcn3990-wifi",
	 .data = &drv_priv,
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ath10k_snoc_dt_match);

static int ath10k_snoc_probe(struct platform_device *pdev)
{
	const struct ath10k_snoc_drv_priv *drv_data;
	const struct of_device_id *of_id;
	struct ath10k_snoc *ar_snoc;
	struct device *dev;
	struct ath10k *ar;
	int ret;

	of_id = of_match_device(ath10k_snoc_dt_match, &pdev->dev);
	if (!of_id) {
		dev_err(&pdev->dev, "failed to find matching device tree id\n");
		return -EINVAL;
	}

	drv_data = of_id->data;
	dev = &pdev->dev;

	ret = dma_set_mask_and_coherent(dev, drv_data->dma_mask);
	if (ret) {
		dev_err(dev, "failed to set dma mask: %d", ret);
		return ret;
	}

	ar = ath10k_core_create(sizeof(*ar_snoc), dev, ATH10K_BUS_SNOC,
				drv_data->hw_rev, &ath10k_snoc_hif_ops);
	if (!ar) {
		dev_err(dev, "failed to allocate core\n");
		return -ENOMEM;
	}

	ar_snoc = ath10k_snoc_priv(ar);
	ar_snoc->dev = pdev;
	platform_set_drvdata(pdev, ar);
	ar_snoc->ar = ar;
	ar_snoc->ce.bus_ops = &ath10k_snoc_bus_ops;
	ar->ce_priv = &ar_snoc->ce;

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc probe\n");
	ath10k_warn(ar, "Warning: SNOC support is still work-in-progress, it will not work properly!");

	return ret;
}

static int ath10k_snoc_remove(struct platform_device *pdev)
{
	struct ath10k *ar = platform_get_drvdata(pdev);

	ath10k_dbg(ar, ATH10K_DBG_SNOC, "snoc remove\n");
	ath10k_core_destroy(ar);

	return 0;
}

static struct platform_driver ath10k_snoc_driver = {
		.probe  = ath10k_snoc_probe,
		.remove = ath10k_snoc_remove,
		.driver = {
			.name   = "ath10k_snoc",
			.owner = THIS_MODULE,
			.of_match_table = ath10k_snoc_dt_match,
		},
};

static int __init ath10k_snoc_init(void)
{
	int ret;

	ret = platform_driver_register(&ath10k_snoc_driver);
	if (ret)
		pr_err("failed to register ath10k snoc driver: %d\n",
		       ret);

	return ret;
}
module_init(ath10k_snoc_init);

static void __exit ath10k_snoc_exit(void)
{
	platform_driver_unregister(&ath10k_snoc_driver);
}
module_exit(ath10k_snoc_exit);

MODULE_AUTHOR("Qualcomm");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("Driver support for Atheros WCN3990 SNOC devices");
