// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright 2015-2022 Amazon.com, Inc. or its affiliates. All rights reserved.
 */

#include <linux/pci.h>
#include "ena_netdev.h"
#include "ena_phc.h"
#include "ena_devlink.h"

static int ena_phc_adjtime(struct ptp_clock_info *clock_info, s64 delta)
{
	return -EOPNOTSUPP;
}

static int ena_phc_adjfine(struct ptp_clock_info *clock_info, long scaled_ppm)
{
	return -EOPNOTSUPP;
}

static int ena_phc_feature_enable(struct ptp_clock_info *clock_info,
				  struct ptp_clock_request *rq,
				  int on)
{
	return -EOPNOTSUPP;
}

static int ena_phc_gettimex64(struct ptp_clock_info *clock_info,
			      struct timespec64 *ts,
			      struct ptp_system_timestamp *sts)
{
	struct ena_phc_info *phc_info =
		container_of(clock_info, struct ena_phc_info, clock_info);
	unsigned long flags;
	u64 timestamp_nsec;
	int rc;

	spin_lock_irqsave(&phc_info->lock, flags);

	ptp_read_system_prets(sts);

	rc = ena_com_phc_get_timestamp(phc_info->adapter->ena_dev,
				       &timestamp_nsec);

	ptp_read_system_postts(sts);

	spin_unlock_irqrestore(&phc_info->lock, flags);

	*ts = ns_to_timespec64(timestamp_nsec);

	return rc;
}

static int ena_phc_settime64(struct ptp_clock_info *clock_info,
			     const struct timespec64 *ts)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info ena_ptp_clock_info = {
	.owner		= THIS_MODULE,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.pps		= 0,
	.adjtime	= ena_phc_adjtime,
	.adjfine	= ena_phc_adjfine,
	.gettimex64	= ena_phc_gettimex64,
	.settime64	= ena_phc_settime64,
	.enable		= ena_phc_feature_enable,
};

/* Enable/Disable PHC by the kernel, affects on the next init flow */
void ena_phc_enable(struct ena_adapter *adapter, bool enable)
{
	struct ena_phc_info *phc_info = adapter->phc_info;

	if (!phc_info) {
		netdev_err(adapter->netdev, "phc_info is not allocated\n");
		return;
	}

	phc_info->enabled = enable;
}

/* Check if PHC is enabled by the kernel */
bool ena_phc_is_enabled(struct ena_adapter *adapter)
{
	struct ena_phc_info *phc_info = adapter->phc_info;

	return (phc_info && phc_info->enabled);
}

/* PHC is activated if ptp clock is registered in the kernel */
bool ena_phc_is_active(struct ena_adapter *adapter)
{
	struct ena_phc_info *phc_info = adapter->phc_info;

	return (phc_info && phc_info->clock);
}

static int ena_phc_register(struct ena_adapter *adapter)
{
	struct pci_dev *pdev = adapter->pdev;
	struct ptp_clock_info *clock_info;
	struct ena_phc_info *phc_info;
	int rc = 0;

	phc_info = adapter->phc_info;
	clock_info = &phc_info->clock_info;

	/* PHC may already be registered in case of a reset */
	if (ena_phc_is_active(adapter))
		return 0;

	phc_info->adapter = adapter;

	spin_lock_init(&phc_info->lock);

	/* Fill the ptp_clock_info struct and register PTP clock */
	*clock_info = ena_ptp_clock_info;
	snprintf(clock_info->name,
		 sizeof(clock_info->name),
		 "ena-ptp-%02x",
		 PCI_SLOT(pdev->devfn));

	phc_info->clock = ptp_clock_register(clock_info, &pdev->dev);
	if (IS_ERR(phc_info->clock)) {
		rc = PTR_ERR(phc_info->clock);
		netdev_err(adapter->netdev, "Failed registering ptp clock, error: %d\n",
			   rc);
		phc_info->clock = NULL;
	}

	return rc;
}

static void ena_phc_unregister(struct ena_adapter *adapter)
{
	struct ena_phc_info *phc_info = adapter->phc_info;

	/* During reset flow, PHC must stay registered
	 * to keep kernel's PHC index
	 */
	if (ena_phc_is_active(adapter) &&
	    !test_bit(ENA_FLAG_TRIGGER_RESET, &adapter->flags)) {
		ptp_clock_unregister(phc_info->clock);
		phc_info->clock = NULL;
	}
}

int ena_phc_alloc(struct ena_adapter *adapter)
{
	/* Allocate driver specific PHC info */
	adapter->phc_info = vzalloc(sizeof(*adapter->phc_info));
	if (unlikely(!adapter->phc_info)) {
		netdev_err(adapter->netdev, "Failed to alloc phc_info\n");
		return -ENOMEM;
	}

	return 0;
}

void ena_phc_free(struct ena_adapter *adapter)
{
	if (adapter->phc_info) {
		vfree(adapter->phc_info);
		adapter->phc_info = NULL;
	}
}

int ena_phc_init(struct ena_adapter *adapter)
{
	struct ena_com_dev *ena_dev = adapter->ena_dev;
	struct net_device *netdev = adapter->netdev;
	int rc = -EOPNOTSUPP;

	/* Validate PHC feature is supported in the device */
	if (!ena_com_phc_supported(ena_dev)) {
		netdev_dbg(netdev, "PHC feature is not supported by the device\n");
		goto err_ena_com_phc_init;
	}

	/* Validate PHC feature is enabled by the kernel */
	if (!ena_phc_is_enabled(adapter)) {
		netdev_dbg(netdev, "PHC feature is not enabled by the kernel\n");
		goto err_ena_com_phc_init;
	}

	/* Initialize device specific PHC info */
	rc = ena_com_phc_init(ena_dev);
	if (unlikely(rc)) {
		netdev_err(netdev, "Failed to init phc, error: %d\n", rc);
		goto err_ena_com_phc_init;
	}

	/* Configure PHC feature in driver and device */
	rc = ena_com_phc_config(ena_dev);
	if (unlikely(rc)) {
		netdev_err(netdev, "Failed to config phc, error: %d\n", rc);
		goto err_ena_com_phc_config;
	}

	/* Register to PTP class driver */
	rc = ena_phc_register(adapter);
	if (unlikely(rc)) {
		netdev_err(netdev, "Failed to register phc, error: %d\n", rc);
		goto err_ena_com_phc_config;
	}

	return 0;

err_ena_com_phc_config:
	ena_com_phc_destroy(ena_dev);
err_ena_com_phc_init:
	ena_phc_enable(adapter, false);
	ena_devlink_disable_phc_param(adapter->devlink);
	return rc;
}

void ena_phc_destroy(struct ena_adapter *adapter)
{
	ena_phc_unregister(adapter);
	ena_com_phc_destroy(adapter->ena_dev);
}

int ena_phc_get_index(struct ena_adapter *adapter)
{
	if (ena_phc_is_active(adapter))
		return ptp_clock_index(adapter->phc_info->clock);

	return -1;
}
