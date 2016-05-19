/*
 * Qualcomm Technologies HIDMA DMA engine Management interface
 *
 * Copyright (c) 2015-2016, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/dmaengine.h>
#include <linux/acpi.h>
#include <linux/of.h>
#include <linux/property.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/bitops.h>
#include <linux/dma-mapping.h>

#include "hidma_mgmt.h"

#define HIDMA_QOS_N_OFFSET		0x300
#define HIDMA_CFG_OFFSET		0x400
#define HIDMA_MAX_BUS_REQ_LEN_OFFSET	0x41C
#define HIDMA_MAX_XACTIONS_OFFSET	0x420
#define HIDMA_HW_VERSION_OFFSET	0x424
#define HIDMA_CHRESET_TIMEOUT_OFFSET	0x418

#define HIDMA_MAX_WR_XACTIONS_MASK	GENMASK(4, 0)
#define HIDMA_MAX_RD_XACTIONS_MASK	GENMASK(4, 0)
#define HIDMA_WEIGHT_MASK		GENMASK(6, 0)
#define HIDMA_MAX_BUS_REQ_LEN_MASK	GENMASK(15, 0)
#define HIDMA_CHRESET_TIMEOUT_MASK	GENMASK(19, 0)

#define HIDMA_MAX_WR_XACTIONS_BIT_POS	16
#define HIDMA_MAX_BUS_WR_REQ_BIT_POS	16
#define HIDMA_WRR_BIT_POS		8
#define HIDMA_PRIORITY_BIT_POS		15

#define HIDMA_AUTOSUSPEND_TIMEOUT	2000
#define HIDMA_MAX_CHANNEL_WEIGHT	15

int hidma_mgmt_setup(struct hidma_mgmt_dev *mgmtdev)
{
	unsigned int i;
	u32 val;

	if (!is_power_of_2(mgmtdev->max_write_request) ||
	    (mgmtdev->max_write_request < 128) ||
	    (mgmtdev->max_write_request > 1024)) {
		dev_err(&mgmtdev->pdev->dev, "invalid write request %d\n",
			mgmtdev->max_write_request);
		return -EINVAL;
	}

	if (!is_power_of_2(mgmtdev->max_read_request) ||
	    (mgmtdev->max_read_request < 128) ||
	    (mgmtdev->max_read_request > 1024)) {
		dev_err(&mgmtdev->pdev->dev, "invalid read request %d\n",
			mgmtdev->max_read_request);
		return -EINVAL;
	}

	if (mgmtdev->max_wr_xactions > HIDMA_MAX_WR_XACTIONS_MASK) {
		dev_err(&mgmtdev->pdev->dev,
			"max_wr_xactions cannot be bigger than %ld\n",
			HIDMA_MAX_WR_XACTIONS_MASK);
		return -EINVAL;
	}

	if (mgmtdev->max_rd_xactions > HIDMA_MAX_RD_XACTIONS_MASK) {
		dev_err(&mgmtdev->pdev->dev,
			"max_rd_xactions cannot be bigger than %ld\n",
			HIDMA_MAX_RD_XACTIONS_MASK);
		return -EINVAL;
	}

	for (i = 0; i < mgmtdev->dma_channels; i++) {
		if (mgmtdev->priority[i] > 1) {
			dev_err(&mgmtdev->pdev->dev,
				"priority can be 0 or 1\n");
			return -EINVAL;
		}

		if (mgmtdev->weight[i] > HIDMA_MAX_CHANNEL_WEIGHT) {
			dev_err(&mgmtdev->pdev->dev,
				"max value of weight can be %d.\n",
				HIDMA_MAX_CHANNEL_WEIGHT);
			return -EINVAL;
		}

		/* weight needs to be at least one */
		if (mgmtdev->weight[i] == 0)
			mgmtdev->weight[i] = 1;
	}

	pm_runtime_get_sync(&mgmtdev->pdev->dev);
	val = readl(mgmtdev->virtaddr + HIDMA_MAX_BUS_REQ_LEN_OFFSET);
	val &= ~(HIDMA_MAX_BUS_REQ_LEN_MASK << HIDMA_MAX_BUS_WR_REQ_BIT_POS);
	val |= mgmtdev->max_write_request << HIDMA_MAX_BUS_WR_REQ_BIT_POS;
	val &= ~HIDMA_MAX_BUS_REQ_LEN_MASK;
	val |= mgmtdev->max_read_request;
	writel(val, mgmtdev->virtaddr + HIDMA_MAX_BUS_REQ_LEN_OFFSET);

	val = readl(mgmtdev->virtaddr + HIDMA_MAX_XACTIONS_OFFSET);
	val &= ~(HIDMA_MAX_WR_XACTIONS_MASK << HIDMA_MAX_WR_XACTIONS_BIT_POS);
	val |= mgmtdev->max_wr_xactions << HIDMA_MAX_WR_XACTIONS_BIT_POS;
	val &= ~HIDMA_MAX_RD_XACTIONS_MASK;
	val |= mgmtdev->max_rd_xactions;
	writel(val, mgmtdev->virtaddr + HIDMA_MAX_XACTIONS_OFFSET);

	mgmtdev->hw_version =
	    readl(mgmtdev->virtaddr + HIDMA_HW_VERSION_OFFSET);
	mgmtdev->hw_version_major = (mgmtdev->hw_version >> 28) & 0xF;
	mgmtdev->hw_version_minor = (mgmtdev->hw_version >> 16) & 0xF;

	for (i = 0; i < mgmtdev->dma_channels; i++) {
		u32 weight = mgmtdev->weight[i];
		u32 priority = mgmtdev->priority[i];

		val = readl(mgmtdev->virtaddr + HIDMA_QOS_N_OFFSET + (4 * i));
		val &= ~(1 << HIDMA_PRIORITY_BIT_POS);
		val |= (priority & 0x1) << HIDMA_PRIORITY_BIT_POS;
		val &= ~(HIDMA_WEIGHT_MASK << HIDMA_WRR_BIT_POS);
		val |= (weight & HIDMA_WEIGHT_MASK) << HIDMA_WRR_BIT_POS;
		writel(val, mgmtdev->virtaddr + HIDMA_QOS_N_OFFSET + (4 * i));
	}

	val = readl(mgmtdev->virtaddr + HIDMA_CHRESET_TIMEOUT_OFFSET);
	val &= ~HIDMA_CHRESET_TIMEOUT_MASK;
	val |= mgmtdev->chreset_timeout_cycles & HIDMA_CHRESET_TIMEOUT_MASK;
	writel(val, mgmtdev->virtaddr + HIDMA_CHRESET_TIMEOUT_OFFSET);

	pm_runtime_mark_last_busy(&mgmtdev->pdev->dev);
	pm_runtime_put_autosuspend(&mgmtdev->pdev->dev);
	return 0;
}
EXPORT_SYMBOL_GPL(hidma_mgmt_setup);

static int hidma_mgmt_probe(struct platform_device *pdev)
{
	struct hidma_mgmt_dev *mgmtdev;
	struct resource *res;
	void __iomem *virtaddr;
	int irq;
	int rc;
	u32 val;

	pm_runtime_set_autosuspend_delay(&pdev->dev, HIDMA_AUTOSUSPEND_TIMEOUT);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	virtaddr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(virtaddr)) {
		rc = -ENOMEM;
		goto out;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "irq resources not found\n");
		rc = irq;
		goto out;
	}

	mgmtdev = devm_kzalloc(&pdev->dev, sizeof(*mgmtdev), GFP_KERNEL);
	if (!mgmtdev) {
		rc = -ENOMEM;
		goto out;
	}

	mgmtdev->pdev = pdev;
	mgmtdev->addrsize = resource_size(res);
	mgmtdev->virtaddr = virtaddr;

	rc = device_property_read_u32(&pdev->dev, "dma-channels",
				      &mgmtdev->dma_channels);
	if (rc) {
		dev_err(&pdev->dev, "number of channels missing\n");
		goto out;
	}

	rc = device_property_read_u32(&pdev->dev,
				      "channel-reset-timeout-cycles",
				      &mgmtdev->chreset_timeout_cycles);
	if (rc) {
		dev_err(&pdev->dev, "channel reset timeout missing\n");
		goto out;
	}

	rc = device_property_read_u32(&pdev->dev, "max-write-burst-bytes",
				      &mgmtdev->max_write_request);
	if (rc) {
		dev_err(&pdev->dev, "max-write-burst-bytes missing\n");
		goto out;
	}

	rc = device_property_read_u32(&pdev->dev, "max-read-burst-bytes",
				      &mgmtdev->max_read_request);
	if (rc) {
		dev_err(&pdev->dev, "max-read-burst-bytes missing\n");
		goto out;
	}

	rc = device_property_read_u32(&pdev->dev, "max-write-transactions",
				      &mgmtdev->max_wr_xactions);
	if (rc) {
		dev_err(&pdev->dev, "max-write-transactions missing\n");
		goto out;
	}

	rc = device_property_read_u32(&pdev->dev, "max-read-transactions",
				      &mgmtdev->max_rd_xactions);
	if (rc) {
		dev_err(&pdev->dev, "max-read-transactions missing\n");
		goto out;
	}

	mgmtdev->priority = devm_kcalloc(&pdev->dev,
					 mgmtdev->dma_channels,
					 sizeof(*mgmtdev->priority),
					 GFP_KERNEL);
	if (!mgmtdev->priority) {
		rc = -ENOMEM;
		goto out;
	}

	mgmtdev->weight = devm_kcalloc(&pdev->dev,
				       mgmtdev->dma_channels,
				       sizeof(*mgmtdev->weight), GFP_KERNEL);
	if (!mgmtdev->weight) {
		rc = -ENOMEM;
		goto out;
	}

	rc = hidma_mgmt_setup(mgmtdev);
	if (rc) {
		dev_err(&pdev->dev, "setup failed\n");
		goto out;
	}

	/* start the HW */
	val = readl(mgmtdev->virtaddr + HIDMA_CFG_OFFSET);
	val |= 1;
	writel(val, mgmtdev->virtaddr + HIDMA_CFG_OFFSET);

	rc = hidma_mgmt_init_sys(mgmtdev);
	if (rc) {
		dev_err(&pdev->dev, "sysfs setup failed\n");
		goto out;
	}

	dev_info(&pdev->dev,
		 "HW rev: %d.%d @ %pa with %d physical channels\n",
		 mgmtdev->hw_version_major, mgmtdev->hw_version_minor,
		 &res->start, mgmtdev->dma_channels);

	platform_set_drvdata(pdev, mgmtdev);
	pm_runtime_mark_last_busy(&pdev->dev);
	pm_runtime_put_autosuspend(&pdev->dev);
	return 0;
out:
	pm_runtime_put_sync_suspend(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	return rc;
}

#if IS_ENABLED(CONFIG_ACPI)
static const struct acpi_device_id hidma_mgmt_acpi_ids[] = {
	{"QCOM8060"},
	{},
};
#endif

static const struct of_device_id hidma_mgmt_match[] = {
	{.compatible = "qcom,hidma-mgmt-1.0",},
	{},
};
MODULE_DEVICE_TABLE(of, hidma_mgmt_match);

static struct platform_driver hidma_mgmt_driver = {
	.probe = hidma_mgmt_probe,
	.driver = {
		   .name = "hidma-mgmt",
		   .of_match_table = hidma_mgmt_match,
		   .acpi_match_table = ACPI_PTR(hidma_mgmt_acpi_ids),
	},
};

#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
static int object_counter;

static int __init hidma_mgmt_of_populate_channels(struct device_node *np)
{
	struct platform_device *pdev_parent = of_find_device_by_node(np);
	struct platform_device_info pdevinfo;
	struct of_phandle_args out_irq;
	struct device_node *child;
	struct resource *res;
	const __be32 *cell;
	int ret = 0, size, i, num;
	u64 addr, addr_size;

	for_each_available_child_of_node(np, child) {
		struct resource *res_iter;
		struct platform_device *new_pdev;

		cell = of_get_property(child, "reg", &size);
		if (!cell) {
			ret = -EINVAL;
			goto out;
		}

		size /= sizeof(*cell);
		num = size /
			(of_n_addr_cells(child) + of_n_size_cells(child)) + 1;

		/* allocate a resource array */
		res = kcalloc(num, sizeof(*res), GFP_KERNEL);
		if (!res) {
			ret = -ENOMEM;
			goto out;
		}

		/* read each reg value */
		i = 0;
		res_iter = res;
		while (i < size) {
			addr = of_read_number(&cell[i],
					      of_n_addr_cells(child));
			i += of_n_addr_cells(child);

			addr_size = of_read_number(&cell[i],
						   of_n_size_cells(child));
			i += of_n_size_cells(child);

			res_iter->start = addr;
			res_iter->end = res_iter->start + addr_size - 1;
			res_iter->flags = IORESOURCE_MEM;
			res_iter++;
		}

		ret = of_irq_parse_one(child, 0, &out_irq);
		if (ret)
			goto out;

		res_iter->start = irq_create_of_mapping(&out_irq);
		res_iter->name = "hidma event irq";
		res_iter->flags = IORESOURCE_IRQ;

		memset(&pdevinfo, 0, sizeof(pdevinfo));
		pdevinfo.fwnode = &child->fwnode;
		pdevinfo.parent = pdev_parent ? &pdev_parent->dev : NULL;
		pdevinfo.name = child->name;
		pdevinfo.id = object_counter++;
		pdevinfo.res = res;
		pdevinfo.num_res = num;
		pdevinfo.data = NULL;
		pdevinfo.size_data = 0;
		pdevinfo.dma_mask = DMA_BIT_MASK(64);
		new_pdev = platform_device_register_full(&pdevinfo);
		if (!new_pdev) {
			ret = -ENODEV;
			goto out;
		}
		of_dma_configure(&new_pdev->dev, child);

		kfree(res);
		res = NULL;
	}
out:
	kfree(res);

	return ret;
}
#endif

static int __init hidma_mgmt_init(void)
{
#if defined(CONFIG_OF) && defined(CONFIG_OF_IRQ)
	struct device_node *child;

	for (child = of_find_matching_node(NULL, hidma_mgmt_match); child;
	     child = of_find_matching_node(child, hidma_mgmt_match)) {
		/* device tree based firmware here */
		hidma_mgmt_of_populate_channels(child);
		of_node_put(child);
	}
#endif
	platform_driver_register(&hidma_mgmt_driver);

	return 0;
}
module_init(hidma_mgmt_init);
MODULE_LICENSE("GPL v2");
