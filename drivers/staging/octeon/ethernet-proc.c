/**********************************************************************
 * Author: Cavium Networks
 *
 * Contact: support@caviumnetworks.com
 * This file is part of the OCTEON SDK
 *
 * Copyright (c) 2003-2007 Cavium Networks
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this file; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 * or visit http://www.gnu.org/licenses/.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium Networks for more information
**********************************************************************/
#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "octeon-ethernet.h"
#include "ethernet-defines.h"

#include "cvmx-helper.h"
#include "cvmx-pip.h"

/**
 * User is reading /proc/octeon_ethernet_stats
 *
 * @m:
 * @v:
 * Returns
 */
static int cvm_oct_stats_show(struct seq_file *m, void *v)
{
	struct octeon_ethernet *priv;
	int port;

	for (port = 0; port < TOTAL_NUMBER_OF_PORTS; port++) {

		if (cvm_oct_device[port]) {
			priv = netdev_priv(cvm_oct_device[port]);

			seq_printf(m, "\nOcteon Port %d (%s)\n", port,
				   cvm_oct_device[port]->name);
			seq_printf(m,
				   "rx_packets:             %12lu\t"
				   "tx_packets:             %12lu\n",
				   priv->stats.rx_packets,
				   priv->stats.tx_packets);
			seq_printf(m,
				   "rx_bytes:               %12lu\t"
				   "tx_bytes:               %12lu\n",
				   priv->stats.rx_bytes, priv->stats.tx_bytes);
			seq_printf(m,
				   "rx_errors:              %12lu\t"
				   "tx_errors:              %12lu\n",
				   priv->stats.rx_errors,
				   priv->stats.tx_errors);
			seq_printf(m,
				   "rx_dropped:             %12lu\t"
				   "tx_dropped:             %12lu\n",
				   priv->stats.rx_dropped,
				   priv->stats.tx_dropped);
			seq_printf(m,
				   "rx_length_errors:       %12lu\t"
				   "tx_aborted_errors:      %12lu\n",
				   priv->stats.rx_length_errors,
				   priv->stats.tx_aborted_errors);
			seq_printf(m,
				   "rx_over_errors:         %12lu\t"
				   "tx_carrier_errors:      %12lu\n",
				   priv->stats.rx_over_errors,
				   priv->stats.tx_carrier_errors);
			seq_printf(m,
				   "rx_crc_errors:          %12lu\t"
				   "tx_fifo_errors:         %12lu\n",
				   priv->stats.rx_crc_errors,
				   priv->stats.tx_fifo_errors);
			seq_printf(m,
				   "rx_frame_errors:        %12lu\t"
				   "tx_heartbeat_errors:    %12lu\n",
				   priv->stats.rx_frame_errors,
				   priv->stats.tx_heartbeat_errors);
			seq_printf(m,
				   "rx_fifo_errors:         %12lu\t"
				   "tx_window_errors:       %12lu\n",
				   priv->stats.rx_fifo_errors,
				   priv->stats.tx_window_errors);
			seq_printf(m,
				   "rx_missed_errors:       %12lu\t"
				   "multicast:              %12lu\n",
				   priv->stats.rx_missed_errors,
				   priv->stats.multicast);
		}
	}

	return 0;
}

/**
 * /proc/octeon_ethernet_stats was openned. Use the single_open iterator
 *
 * @inode:
 * @file:
 * Returns
 */
static int cvm_oct_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, cvm_oct_stats_show, NULL);
}

static const struct file_operations cvm_oct_stats_operations = {
	.open = cvm_oct_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void cvm_oct_proc_initialize(void)
{
	struct proc_dir_entry *entry =
	    create_proc_entry("octeon_ethernet_stats", 0, NULL);
	if (entry)
		entry->proc_fops = &cvm_oct_stats_operations;
}

void cvm_oct_proc_shutdown(void)
{
	remove_proc_entry("octeon_ethernet_stats", NULL);
}
