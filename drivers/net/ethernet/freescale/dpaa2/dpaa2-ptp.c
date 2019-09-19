// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/msi.h>
#include <linux/fsl/mc.h>
#include <linux/fsl/ptp_qoriq.h>

#include "dpaa2-ptp.h"

static int dpaa2_ptp_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *rq, int on)
{
	struct ptp_qoriq *ptp_qoriq = container_of(ptp, struct ptp_qoriq, caps);
	struct fsl_mc_device *mc_dev;
	struct device *dev;
	u32 mask = 0;
	u32 bit;
	int err;

	dev = ptp_qoriq->dev;
	mc_dev = to_fsl_mc_device(dev);

	switch (rq->type) {
	case PTP_CLK_REQ_PPS:
		bit = DPRTC_EVENT_PPS;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = dprtc_get_irq_mask(mc_dev->mc_io, 0, mc_dev->mc_handle,
				 DPRTC_IRQ_INDEX, &mask);
	if (err < 0) {
		dev_err(dev, "dprtc_get_irq_mask(): %d\n", err);
		return err;
	}

	if (on)
		mask |= bit;
	else
		mask &= ~bit;

	err = dprtc_set_irq_mask(mc_dev->mc_io, 0, mc_dev->mc_handle,
				 DPRTC_IRQ_INDEX, mask);
	if (err < 0) {
		dev_err(dev, "dprtc_set_irq_mask(): %d\n", err);
		return err;
	}

	return 0;
}

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
	.enable		= dpaa2_ptp_enable,
};

static irqreturn_t dpaa2_ptp_irq_handler_thread(int irq, void *priv)
{
	struct ptp_qoriq *ptp_qoriq = priv;
	struct ptp_clock_event event;
	struct fsl_mc_device *mc_dev;
	struct device *dev;
	u32 status = 0;
	int err;

	dev = ptp_qoriq->dev;
	mc_dev = to_fsl_mc_device(dev);

	err = dprtc_get_irq_status(mc_dev->mc_io, 0, mc_dev->mc_handle,
				   DPRTC_IRQ_INDEX, &status);
	if (unlikely(err)) {
		dev_err(dev, "dprtc_get_irq_status err %d\n", err);
		return IRQ_NONE;
	}

	if (status & DPRTC_EVENT_PPS) {
		event.type = PTP_CLOCK_PPS;
		ptp_clock_event(ptp_qoriq->clock, &event);
	}

	err = dprtc_clear_irq_status(mc_dev->mc_io, 0, mc_dev->mc_handle,
				     DPRTC_IRQ_INDEX, status);
	if (unlikely(err)) {
		dev_err(dev, "dprtc_clear_irq_status err %d\n", err);
		return IRQ_NONE;
	}

	return IRQ_HANDLED;
}

static int dpaa2_ptp_probe(struct fsl_mc_device *mc_dev)
{
	struct device *dev = &mc_dev->dev;
	struct fsl_mc_device_irq *irq;
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

	err = fsl_mc_allocate_irqs(mc_dev);
	if (err) {
		dev_err(dev, "MC irqs allocation failed\n");
		goto err_unmap;
	}

	irq = mc_dev->irqs[0];
	ptp_qoriq->irq = irq->msi_desc->irq;

	err = devm_request_threaded_irq(dev, ptp_qoriq->irq, NULL,
					dpaa2_ptp_irq_handler_thread,
					IRQF_NO_SUSPEND | IRQF_ONESHOT,
					dev_name(dev), ptp_qoriq);
	if (err < 0) {
		dev_err(dev, "devm_request_threaded_irq(): %d\n", err);
		goto err_free_mc_irq;
	}

	err = dprtc_set_irq_enable(mc_dev->mc_io, 0, mc_dev->mc_handle,
				   DPRTC_IRQ_INDEX, 1);
	if (err < 0) {
		dev_err(dev, "dprtc_set_irq_enable(): %d\n", err);
		goto err_free_mc_irq;
	}

	err = ptp_qoriq_init(ptp_qoriq, base, &dpaa2_ptp_caps);
	if (err)
		goto err_free_mc_irq;

	dpaa2_phc_index = ptp_qoriq->phc_index;
	dev_set_drvdata(dev, ptp_qoriq);

	return 0;

err_free_mc_irq:
	fsl_mc_free_irqs(mc_dev);
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

	fsl_mc_free_irqs(mc_dev);
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
