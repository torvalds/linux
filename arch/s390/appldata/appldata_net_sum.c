/*
 * arch/s390/appldata/appldata_net_sum.c
 *
 * Data gathering module for Linux-VM Monitor Stream, Stage 1.
 * Collects accumulated network statistics (Packets received/transmitted,
 * dropped, errors, ...).
 *
 * Copyright (C) 2003,2006 IBM Corporation, IBM Deutschland Entwicklung GmbH.
 *
 * Author: Gerald Schaefer <gerald.schaefer@de.ibm.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/kernel_stat.h>
#include <linux/netdevice.h>
#include <net/net_namespace.h>

#include "appldata.h"


/*
 * Network data
 *
 * This is accessed as binary data by z/VM. If changes to it can't be avoided,
 * the structure version (product ID, see appldata_base.c) needs to be changed
 * as well and all documentation and z/VM applications using it must be updated.
 *
 * The record layout is documented in the Linux for zSeries Device Drivers
 * book:
 * http://oss.software.ibm.com/developerworks/opensource/linux390/index.shtml
 */
static struct appldata_net_sum_data {
	u64 timestamp;
	u32 sync_count_1;	/* after VM collected the record data, */
	u32 sync_count_2;	/* sync_count_1 and sync_count_2 should be the
				   same. If not, the record has been updated on
				   the Linux side while VM was collecting the
				   (possibly corrupt) data */

	u32 nr_interfaces;	/* nr. of network interfaces being monitored */

	u32 padding;		/* next value is 64-bit aligned, so these */
				/* 4 byte would be padded out by compiler */

	u64 rx_packets;		/* total packets received        */
	u64 tx_packets;		/* total packets transmitted     */
	u64 rx_bytes;		/* total bytes received          */
	u64 tx_bytes;		/* total bytes transmitted       */
	u64 rx_errors;		/* bad packets received          */
	u64 tx_errors;		/* packet transmit problems      */
	u64 rx_dropped;		/* no space in linux buffers     */
	u64 tx_dropped;		/* no space available in linux   */
	u64 collisions;		/* collisions while transmitting */
} __attribute__((packed)) appldata_net_sum_data;


/*
 * appldata_get_net_sum_data()
 *
 * gather accumulated network statistics
 */
static void appldata_get_net_sum_data(void *data)
{
	int i;
	struct appldata_net_sum_data *net_data;
	struct net_device *dev;
	unsigned long rx_packets, tx_packets, rx_bytes, tx_bytes, rx_errors,
			tx_errors, rx_dropped, tx_dropped, collisions;

	net_data = data;
	net_data->sync_count_1++;

	i = 0;
	rx_packets = 0;
	tx_packets = 0;
	rx_bytes   = 0;
	tx_bytes   = 0;
	rx_errors  = 0;
	tx_errors  = 0;
	rx_dropped = 0;
	tx_dropped = 0;
	collisions = 0;

	rcu_read_lock();
	for_each_netdev_rcu(&init_net, dev) {
		const struct net_device_stats *stats = dev_get_stats(dev);

		rx_packets += stats->rx_packets;
		tx_packets += stats->tx_packets;
		rx_bytes   += stats->rx_bytes;
		tx_bytes   += stats->tx_bytes;
		rx_errors  += stats->rx_errors;
		tx_errors  += stats->tx_errors;
		rx_dropped += stats->rx_dropped;
		tx_dropped += stats->tx_dropped;
		collisions += stats->collisions;
		i++;
	}
	rcu_read_unlock();

	net_data->nr_interfaces = i;
	net_data->rx_packets = rx_packets;
	net_data->tx_packets = tx_packets;
	net_data->rx_bytes   = rx_bytes;
	net_data->tx_bytes   = tx_bytes;
	net_data->rx_errors  = rx_errors;
	net_data->tx_errors  = tx_errors;
	net_data->rx_dropped = rx_dropped;
	net_data->tx_dropped = tx_dropped;
	net_data->collisions = collisions;

	net_data->timestamp = get_clock();
	net_data->sync_count_2++;
}


static struct appldata_ops ops = {
	.name	   = "net_sum",
	.record_nr = APPLDATA_RECORD_NET_SUM_ID,
	.size	   = sizeof(struct appldata_net_sum_data),
	.callback  = &appldata_get_net_sum_data,
	.data      = &appldata_net_sum_data,
	.owner     = THIS_MODULE,
	.mod_lvl   = {0xF0, 0xF0},		/* EBCDIC "00" */
};


/*
 * appldata_net_init()
 *
 * init data, register ops
 */
static int __init appldata_net_init(void)
{
	return appldata_register_ops(&ops);
}

/*
 * appldata_net_exit()
 *
 * unregister ops
 */
static void __exit appldata_net_exit(void)
{
	appldata_unregister_ops(&ops);
}


module_init(appldata_net_init);
module_exit(appldata_net_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Gerald Schaefer");
MODULE_DESCRIPTION("Linux-VM Monitor Stream, accumulated network statistics");
