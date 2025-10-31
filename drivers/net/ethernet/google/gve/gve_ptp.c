// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2025 Google LLC
 */

#include "gve.h"
#include "gve_adminq.h"

/* Interval to schedule a nic timestamp calibration, 250ms. */
#define GVE_NIC_TS_SYNC_INTERVAL_MS 250

/* Read the nic timestamp from hardware via the admin queue. */
int gve_clock_nic_ts_read(struct gve_priv *priv)
{
	u64 nic_raw;
	int err;

	err = gve_adminq_report_nic_ts(priv, priv->nic_ts_report_bus);
	if (err)
		return err;

	nic_raw = be64_to_cpu(priv->nic_ts_report->nic_timestamp);
	WRITE_ONCE(priv->last_sync_nic_counter, nic_raw);

	return 0;
}

static int gve_ptp_gettimex64(struct ptp_clock_info *info,
			      struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	return -EOPNOTSUPP;
}

static int gve_ptp_settime64(struct ptp_clock_info *info,
			     const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static long gve_ptp_do_aux_work(struct ptp_clock_info *info)
{
	const struct gve_ptp *ptp = container_of(info, struct gve_ptp, info);
	struct gve_priv *priv = ptp->priv;
	int err;

	if (gve_get_reset_in_progress(priv) || !gve_get_admin_queue_ok(priv))
		goto out;

	err = gve_clock_nic_ts_read(priv);
	if (err && net_ratelimit())
		dev_err(&priv->pdev->dev,
			"%s read err %d\n", __func__, err);

out:
	return msecs_to_jiffies(GVE_NIC_TS_SYNC_INTERVAL_MS);
}

static const struct ptp_clock_info gve_ptp_caps = {
	.owner          = THIS_MODULE,
	.name		= "gve clock",
	.gettimex64	= gve_ptp_gettimex64,
	.settime64	= gve_ptp_settime64,
	.do_aux_work	= gve_ptp_do_aux_work,
};

static int gve_ptp_init(struct gve_priv *priv)
{
	struct gve_ptp *ptp;
	int err;

	if (!priv->nic_timestamp_supported) {
		dev_dbg(&priv->pdev->dev, "Device does not support PTP\n");
		return -EOPNOTSUPP;
	}

	priv->ptp = kzalloc(sizeof(*priv->ptp), GFP_KERNEL);
	if (!priv->ptp)
		return -ENOMEM;

	ptp = priv->ptp;
	ptp->info = gve_ptp_caps;
	ptp->clock = ptp_clock_register(&ptp->info, &priv->pdev->dev);

	if (IS_ERR(ptp->clock)) {
		dev_err(&priv->pdev->dev, "PTP clock registration failed\n");
		err  = PTR_ERR(ptp->clock);
		goto free_ptp;
	}

	ptp->priv = priv;
	return 0;

free_ptp:
	kfree(ptp);
	priv->ptp = NULL;
	return err;
}

static void gve_ptp_release(struct gve_priv *priv)
{
	struct gve_ptp *ptp = priv->ptp;

	if (!ptp)
		return;

	if (ptp->clock)
		ptp_clock_unregister(ptp->clock);

	kfree(ptp);
	priv->ptp = NULL;
}

int gve_init_clock(struct gve_priv *priv)
{
	int err;

	if (!priv->nic_timestamp_supported)
		return 0;

	err = gve_ptp_init(priv);
	if (err)
		return err;

	priv->nic_ts_report =
		dma_alloc_coherent(&priv->pdev->dev,
				   sizeof(struct gve_nic_ts_report),
				   &priv->nic_ts_report_bus,
				   GFP_KERNEL);
	if (!priv->nic_ts_report) {
		dev_err(&priv->pdev->dev, "%s dma alloc error\n", __func__);
		err = -ENOMEM;
		goto release_ptp;
	}

	return 0;

release_ptp:
	gve_ptp_release(priv);
	return err;
}

void gve_teardown_clock(struct gve_priv *priv)
{
	gve_ptp_release(priv);

	if (priv->nic_ts_report) {
		dma_free_coherent(&priv->pdev->dev,
				  sizeof(struct gve_nic_ts_report),
				  priv->nic_ts_report, priv->nic_ts_report_bus);
		priv->nic_ts_report = NULL;
	}
}
