// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2018 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include "net_driver.h"
#include <linux/module.h>
#include <linux/netdevice.h>
#include "efx_common.h"
#include "efx_channels.h"
#include "efx.h"
#include "mcdi.h"
#include "selftest.h"
#include "rx_common.h"
#include "tx_common.h"
#include "nic.h"
#include "io.h"
#include "mcdi_pcol.h"

/* Reset workqueue. If any NIC has a hardware failure then a reset will be
 * queued onto this work queue. This is not a per-nic work queue, because
 * efx_reset_work() acquires the rtnl lock, so resets are naturally serialised.
 */
static struct workqueue_struct *reset_workqueue;

int efx_create_reset_workqueue(void)
{
	reset_workqueue = create_singlethread_workqueue("sfc_reset");
	if (!reset_workqueue) {
		printk(KERN_ERR "Failed to create reset workqueue\n");
		return -ENOMEM;
	}

	return 0;
}

void efx_queue_reset_work(struct efx_nic *efx)
{
	queue_work(reset_workqueue, &efx->reset_work);
}

void efx_flush_reset_workqueue(struct efx_nic *efx)
{
	cancel_work_sync(&efx->reset_work);
}

void efx_destroy_reset_workqueue(void)
{
	if (reset_workqueue) {
		destroy_workqueue(reset_workqueue);
		reset_workqueue = NULL;
	}
}
