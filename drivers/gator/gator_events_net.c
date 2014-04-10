/**
 * Copyright (C) ARM Limited 2010-2014. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include "gator.h"
#include <linux/netdevice.h>
#include <linux/hardirq.h>

#define NETRX		0
#define NETTX		1
#define TOTALNET	2

static ulong netrx_enabled;
static ulong nettx_enabled;
static ulong netrx_key;
static ulong nettx_key;
static int rx_total, tx_total;
static ulong netPrev[TOTALNET];
static int netGet[TOTALNET * 4];

static struct timer_list net_wake_up_timer;

// Must be run in process context as the kernel function dev_get_stats() can sleep
static void get_network_stats(struct work_struct *wsptr)
{
	int rx = 0, tx = 0;
	struct net_device *dev;

	for_each_netdev(&init_net, dev) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
		const struct net_device_stats *stats = dev_get_stats(dev);
#else
		struct rtnl_link_stats64 temp;
		const struct rtnl_link_stats64 *stats = dev_get_stats(dev, &temp);
#endif
		rx += stats->rx_bytes;
		tx += stats->tx_bytes;
	}
	rx_total = rx;
	tx_total = tx;
}

DECLARE_WORK(wq_get_stats, get_network_stats);

static void net_wake_up_handler(unsigned long unused_data)
{
	// had to delay scheduling work as attempting to schedule work during the context switch is illegal in kernel versions 3.5 and greater
	schedule_work(&wq_get_stats);
}

static void calculate_delta(int *rx, int *tx)
{
	int rx_calc, tx_calc;

	rx_calc = (int)(rx_total - netPrev[NETRX]);
	if (rx_calc < 0)
		rx_calc = 0;
	netPrev[NETRX] += rx_calc;

	tx_calc = (int)(tx_total - netPrev[NETTX]);
	if (tx_calc < 0)
		tx_calc = 0;
	netPrev[NETTX] += tx_calc;

	*rx = rx_calc;
	*tx = tx_calc;
}

static int gator_events_net_create_files(struct super_block *sb, struct dentry *root)
{
	// Network counters are not currently supported in RT-Preempt full because mod_timer is used
#ifndef CONFIG_PREEMPT_RT_FULL
	struct dentry *dir;

	dir = gatorfs_mkdir(sb, root, "Linux_net_rx");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &netrx_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &netrx_key);

	dir = gatorfs_mkdir(sb, root, "Linux_net_tx");
	if (!dir) {
		return -1;
	}
	gatorfs_create_ulong(sb, dir, "enabled", &nettx_enabled);
	gatorfs_create_ro_ulong(sb, dir, "key", &nettx_key);
#endif

	return 0;
}

static int gator_events_net_start(void)
{
	get_network_stats(0);
	netPrev[NETRX] = rx_total;
	netPrev[NETTX] = tx_total;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 36)
	setup_timer(&net_wake_up_timer, net_wake_up_handler, 0);
#else
	setup_deferrable_timer_on_stack(&net_wake_up_timer, net_wake_up_handler, 0);
#endif
	return 0;
}

static void gator_events_net_stop(void)
{
	del_timer_sync(&net_wake_up_timer);
	netrx_enabled = 0;
	nettx_enabled = 0;
}

static int gator_events_net_read(int **buffer)
{
	int len, rx_delta, tx_delta;
	static int last_rx_delta = 0, last_tx_delta = 0;

	if (!on_primary_core())
		return 0;

	if (!netrx_enabled && !nettx_enabled)
		return 0;

	mod_timer(&net_wake_up_timer, jiffies + 1);

	calculate_delta(&rx_delta, &tx_delta);

	len = 0;
	if (netrx_enabled && last_rx_delta != rx_delta) {
		last_rx_delta = rx_delta;
		netGet[len++] = netrx_key;
		netGet[len++] = 0;	// indicates to Streamline that rx_delta bytes were transmitted now, not since the last message
		netGet[len++] = netrx_key;
		netGet[len++] = rx_delta;
	}

	if (nettx_enabled && last_tx_delta != tx_delta) {
		last_tx_delta = tx_delta;
		netGet[len++] = nettx_key;
		netGet[len++] = 0;	// indicates to Streamline that tx_delta bytes were transmitted now, not since the last message
		netGet[len++] = nettx_key;
		netGet[len++] = tx_delta;
	}

	if (buffer)
		*buffer = netGet;

	return len;
}

static struct gator_interface gator_events_net_interface = {
	.create_files = gator_events_net_create_files,
	.start = gator_events_net_start,
	.stop = gator_events_net_stop,
	.read = gator_events_net_read,
};

int gator_events_net_init(void)
{
	netrx_key = gator_events_get_key();
	nettx_key = gator_events_get_key();

	netrx_enabled = 0;
	nettx_enabled = 0;

	return gator_events_install(&gator_events_net_interface);
}
