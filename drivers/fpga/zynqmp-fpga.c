// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2019 Xilinx, Inc.
 */

#include <linux/dma-mapping.h>
#include <linux/fpga/fpga-mgr.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/string.h>
#include <linux/firmware/xlnx-zynqmp.h>

/* Constant Definitions */
#define IXR_FPGA_DONE_MASK	BIT(3)

/**
 * struct zynqmp_fpga_priv - Private data structure
 * @dev:	Device data structure
 * @flags:	flags which is used to identify the bitfile type
 */
struct zynqmp_fpga_priv {
	struct device *dev;
	u32 flags;
};

static int zynqmp_fpga_ops_write_init(struct fpga_manager *mgr,
				      struct fpga_image_info *info,
				      const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;

	priv = mgr->priv;
	priv->flags = info->flags;

	return 0;
}

static int zynqmp_fpga_ops_write(struct fpga_manager *mgr,
				 const char *buf, size_t size)
{
	struct zynqmp_fpga_priv *priv;
	dma_addr_t dma_addr;
	u32 eemi_flags = 0;
	char *kbuf;
	int ret;

	priv = mgr->priv;

	kbuf = dma_alloc_coherent(priv->dev, size, &dma_addr, GFP_KERNEL);
	if (!kbuf)
		return -ENOMEM;

	memcpy(kbuf, buf, size);

	wmb(); /* ensure all writes are done before initiate FW call */

	if (priv->flags & FPGA_MGR_PARTIAL_RECONFIG)
		eemi_flags |= XILINX_ZYNQMP_PM_FPGA_PARTIAL;

	ret = zynqmp_pm_fpga_load(dma_addr, size, eemi_flags);

	dma_free_coherent(priv->dev, size, kbuf, dma_addr);

	return ret;
}

static int zynqmp_fpga_ops_write_complete(struct fpga_manager *mgr,
					  struct fpga_image_info *info)
{
	return 0;
}

static enum fpga_mgr_states zynqmp_fpga_ops_state(struct fpga_manager *mgr)
{
	u32 status = 0;

	zynqmp_pm_fpga_get_status(&status);
	if (status & IXR_FPGA_DONE_MASK)
		return FPGA_MGR_STATE_OPERATING;

	return FPGA_MGR_STATE_UNKNOWN;
}

static const struct fpga_manager_ops zynqmp_fpga_ops = {
	.state = zynqmp_fpga_ops_state,
	.write_init = zynqmp_fpga_ops_write_init,
	.write = zynqmp_fpga_ops_write,
	.write_complete = zynqmp_fpga_ops_write_complete,
};

static int zynqmp_fpga_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct zynqmp_fpga_priv *priv;
	struct fpga_manager *mgr;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->dev = dev;

	mgr = devm_fpga_mgr_create(dev, "Xilinx ZynqMP FPGA Manager",
				   &zynqmp_fpga_ops, priv);
	if (!mgr)
		return -ENOMEM;

	platform_set_drvdata(pdev, mgr);

	ret = fpga_mgr_register(mgr);
	if (ret) {
		dev_err(dev, "unable to register FPGA manager");
		return ret;
	}

	return 0;
}

static int zynqmp_fpga_remove(struct platform_device *pdev)
{
	struct fpga_manager *mgr = platform_get_drvdata(pdev);

	fpga_mgr_unregister(mgr);

	return 0;
}

static const struct of_device_id zynqmp_fpga_of_match[] = {
	{ .compatible = "xlnx,zynqmp-pcap-fpga", },
	{},
};

MODULE_DEVICE_TABLE(of, zynqmp_fpga_of_match);

static struct platform_driver zynqmp_fpga_driver = {
	.probe = zynqmp_fpga_probe,
	.remove = zynqmp_fpga_remove,
	.driver = {
		.name = "zynqmp_fpga_manager",
		.of_match_table = of_match_ptr(zynqmp_fpga_of_match),
	},
};

module_platform_driver(zynqmp_fpga_driver);

MODULE_AUTHOR("Nava kishore Manne <navam@xilinx.com>");
MODULE_DESCRIPTION("Xilinx ZynqMp FPGA Manager");
MODULE_LICENSE("GPL");
