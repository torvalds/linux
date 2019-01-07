// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright 2013-2016 Freescale Semiconductor Inc.
 * Copyright 2016-2018 NXP
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/fsl/mc.h>

#include "dpaa2-ptp.h"

struct ptp_dpaa2_priv {
	struct fsl_mc_device *ptp_mc_dev;
	struct ptp_clock *clock;
	struct ptp_clock_info caps;
	u32 freq_comp;
};

/* PTP clock operations */
static int ptp_dpaa2_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	struct ptp_dpaa2_priv *ptp_dpaa2 =
		container_of(ptp, struct ptp_dpaa2_priv, caps);
	struct fsl_mc_device *mc_dev = ptp_dpaa2->ptp_mc_dev;
	struct device *dev = &mc_dev->dev;
	u64 adj;
	u32 diff, tmr_add;
	int neg_adj = 0;
	int err = 0;

	if (ppb < 0) {
		neg_adj = 1;
		ppb = -ppb;
	}

	tmr_add = ptp_dpaa2->freq_comp;
	adj = tmr_add;
	adj *= ppb;
	diff = div_u64(adj, 1000000000ULL);

	tmr_add = neg_adj ? tmr_add - diff : tmr_add + diff;

	err = dprtc_set_freq_compensation(mc_dev->mc_io, 0,
					  mc_dev->mc_handle, tmr_add);
	if (err)
		dev_err(dev, "dprtc_set_freq_compensation err %d\n", err);
	return err;
}

static int ptp_dpaa2_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	struct ptp_dpaa2_priv *ptp_dpaa2 =
		container_of(ptp, struct ptp_dpaa2_priv, caps);
	struct fsl_mc_device *mc_dev = ptp_dpaa2->ptp_mc_dev;
	struct device *dev = &mc_dev->dev;
	s64 now;
	int err = 0;

	err = dprtc_get_time(mc_dev->mc_io, 0, mc_dev->mc_handle, &now);
	if (err) {
		dev_err(dev, "dprtc_get_time err %d\n", err);
		return err;
	}

	now += delta;

	err = dprtc_set_time(mc_dev->mc_io, 0, mc_dev->mc_handle, now);
	if (err)
		dev_err(dev, "dprtc_set_time err %d\n", err);
	return err;
}

static int ptp_dpaa2_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct ptp_dpaa2_priv *ptp_dpaa2 =
		container_of(ptp, struct ptp_dpaa2_priv, caps);
	struct fsl_mc_device *mc_dev = ptp_dpaa2->ptp_mc_dev;
	struct device *dev = &mc_dev->dev;
	u64 ns;
	u32 remainder;
	int err = 0;

	err = dprtc_get_time(mc_dev->mc_io, 0, mc_dev->mc_handle, &ns);
	if (err) {
		dev_err(dev, "dprtc_get_time err %d\n", err);
		return err;
	}

	ts->tv_sec = div_u64_rem(ns, 1000000000, &remainder);
	ts->tv_nsec = remainder;
	return err;
}

static int ptp_dpaa2_settime(struct ptp_clock_info *ptp,
			     const struct timespec64 *ts)
{
	struct ptp_dpaa2_priv *ptp_dpaa2 =
		container_of(ptp, struct ptp_dpaa2_priv, caps);
	struct fsl_mc_device *mc_dev = ptp_dpaa2->ptp_mc_dev;
	struct device *dev = &mc_dev->dev;
	u64 ns;
	int err = 0;

	ns = ts->tv_sec * 1000000000ULL;
	ns += ts->tv_nsec;

	err = dprtc_set_time(mc_dev->mc_io, 0, mc_dev->mc_handle, ns);
	if (err)
		dev_err(dev, "dprtc_set_time err %d\n", err);
	return err;
}

static const struct ptp_clock_info ptp_dpaa2_caps = {
	.owner		= THIS_MODULE,
	.name		= "DPAA2 PTP Clock",
	.max_adj	= 512000,
	.n_alarm	= 2,
	.n_ext_ts	= 2,
	.n_per_out	= 3,
	.n_pins		= 0,
	.pps		= 1,
	.adjfreq	= ptp_dpaa2_adjfreq,
	.adjtime	= ptp_dpaa2_adjtime,
	.gettime64	= ptp_dpaa2_gettime,
	.settime64	= ptp_dpaa2_settime,
};

static int dpaa2_ptp_probe(struct fsl_mc_device *mc_dev)
{
	struct device *dev = &mc_dev->dev;
	struct ptp_dpaa2_priv *ptp_dpaa2;
	u32 tmr_add = 0;
	int err;

	ptp_dpaa2 = devm_kzalloc(dev, sizeof(*ptp_dpaa2), GFP_KERNEL);
	if (!ptp_dpaa2)
		return -ENOMEM;

	err = fsl_mc_portal_allocate(mc_dev, 0, &mc_dev->mc_io);
	if (err) {
		dev_err(dev, "fsl_mc_portal_allocate err %d\n", err);
		goto err_exit;
	}

	err = dprtc_open(mc_dev->mc_io, 0, mc_dev->obj_desc.id,
			 &mc_dev->mc_handle);
	if (err) {
		dev_err(dev, "dprtc_open err %d\n", err);
		goto err_free_mcp;
	}

	ptp_dpaa2->ptp_mc_dev = mc_dev;

	err = dprtc_get_freq_compensation(mc_dev->mc_io, 0,
					  mc_dev->mc_handle, &tmr_add);
	if (err) {
		dev_err(dev, "dprtc_get_freq_compensation err %d\n", err);
		goto err_close;
	}

	ptp_dpaa2->freq_comp = tmr_add;
	ptp_dpaa2->caps = ptp_dpaa2_caps;

	ptp_dpaa2->clock = ptp_clock_register(&ptp_dpaa2->caps, dev);
	if (IS_ERR(ptp_dpaa2->clock)) {
		err = PTR_ERR(ptp_dpaa2->clock);
		goto err_close;
	}

	dpaa2_phc_index = ptp_clock_index(ptp_dpaa2->clock);

	dev_set_drvdata(dev, ptp_dpaa2);

	return 0;

err_close:
	dprtc_close(mc_dev->mc_io, 0, mc_dev->mc_handle);
err_free_mcp:
	fsl_mc_portal_free(mc_dev->mc_io);
err_exit:
	return err;
}

static int dpaa2_ptp_remove(struct fsl_mc_device *mc_dev)
{
	struct ptp_dpaa2_priv *ptp_dpaa2;
	struct device *dev = &mc_dev->dev;

	ptp_dpaa2 = dev_get_drvdata(dev);
	ptp_clock_unregister(ptp_dpaa2->clock);

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
