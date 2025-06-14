// SPDX-License-Identifier: (GPL-2.0 OR MIT)
/* Google virtual Ethernet (gve) driver
 *
 * Copyright (C) 2025 Google LLC
 */

#include "gve.h"

static const struct ptp_clock_info gve_ptp_caps = {
	.owner          = THIS_MODULE,
	.name		= "gve clock",
};

static int __maybe_unused gve_ptp_init(struct gve_priv *priv)
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

static void __maybe_unused gve_ptp_release(struct gve_priv *priv)
{
	struct gve_ptp *ptp = priv->ptp;

	if (!ptp)
		return;

	if (ptp->clock)
		ptp_clock_unregister(ptp->clock);

	kfree(ptp);
	priv->ptp = NULL;
}
