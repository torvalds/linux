// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fsl/mc.h>
#include <linux/fsl/ptp_qoriq.h>

#include "dpaa2-ptp.h"

static const struct ptp_clock_info dpaa2_ptp_caps = {
	.owner		= THIS_MODULE,
	.name		= "DPAA2 PTP Clock",
	.max_adj	= 512000,
	.n_alarm	= 2,
	.n_ext_ts	= 2,
	.n_per_out	= 3,
	.n_pins		= 0,
	.pps		= 1,
	.adjfine	= ptp_qoriq_adjfine,
	.adjtime	= ptp_qoriq_adjtime,
	.gettime64	= ptp_qoriq_gettime,
	.settime64	= ptp_qoriq_settime,
};

static int dpaa2_ptp_probe(struct fsl_mc_device *mc_dev)
{
	struct device *dev = &mc_dev->dev;
	struct ptp_qoriq *ptp_qoriq;
	struct device_node *node;
	void __iomem *base;
	int err;

	ptp_qoriq = devm_kzalloc(dev, sizeof(*ptp_qoriq), GFP_KERNEL);
	if (!ptp_qoriq)
		return -ENOMEM;

	err = fsl_mc_portal_allocate(mc_dev, 0, &mc_dev->mc_io);
	if (err) {
		if (err == -ENXIO)
			err = -EPROBE_DEFER;
		else
			dev_err(dev, "fsl_mc_portal_allocate err %d\n", err);
		goto err_exit;
	}

	err = dprtc_open(mc_dev->mc_io, 0, mc_dev->obj_desc.id,
			 &mc_dev->mc_handle);
	if (err) {
		dev_err(dev, "dprtc_open err %d\n", err);
		goto err_free_mcp;
	}

	ptp_qoriq->dev = dev;

	node = of_find_compatible_node(NULL, NULL, "fsl,dpaa2-ptp");
	if (!node) {
		err = -ENODEV;
		goto err_close;
	}

	dev->of_node = node;

	base = of_iomap(node, 0);
	if (!base) {
		err = -ENOMEM;
		goto err_close;
	}

	err = ptp_qoriq_init(ptp_qoriq, base, &dpaa2_ptp_caps);
	if (err)
		goto err_unmap;

	dpaa2_phc_index = ptp_qoriq->phc_index;
	dev_set_drvdata(dev, ptp_qoriq);

	return 0;

err_unmap:
	iounmap(base);
err_close:
	dprtc_close(mc_dev->mc_io, 0, mc_dev->mc_handle);
err_free_mcp:
	fsl_mc_portal_free(mc_dev->mc_io);
err_exit:
	return err;
}

static int dpaa2_ptp_remove(struct fsl_mc_device *mc_dev)
{
	struct device *dev = &mc_dev->dev;
	struct ptp_qoriq *ptp_qoriq;

	ptp_qoriq = dev_get_drvdata(dev);

	dpaa2_phc_index = -1;
	ptp_qoriq_free(ptp_qoriq);

	dprtc_close(mc_dev->mc_io, 0, mc_dev->mc_handle);
	fsl_mc_portal_free(mc_dev->mc_io);

	return 0;
}

static const struct fsl_mc_device_id dpaa2_ptp_match_id_table[] = {
	{
		.vendor = FSL_MC_VENDOR_FREESCALE,
		.obj_type = "dprtc",
	},
	{}
};
MODULE_DEVICE_TABLE(fslmc, dpaa2_ptp_match_id_table);

static struct fsl_mc_driver dpaa2_ptp_drv = {
	.driver = {
		.name = KBUILD_MODNAME,
		.owner = THIS_MODULE,
	},
	.probe = dpaa2_ptp_probe,
	.remove = dpaa2_ptp_remove,
	.match_id_table = dpaa2_ptp_match_id_table,
};

module_fsl_mc_driver(dpaa2_ptp_drv);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("DPAA2 PTP Clock Driver");
