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
#include <linux/mii.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <net/dst.h>

#include <asm/octeon/octeon.h>

#include "octeon-ethernet.h"
#include "ethernet-defines.h"

#include "cvmx-helper.h"
#include "cvmx-pip.h"

static unsigned long long cvm_oct_stats_read_switch(struct net_device *dev,
						    int phy_id, int offset)
{
	struct octeon_ethernet *priv = netdev_priv(dev);

	priv->mii_info.mdio_write(dev, phy_id, 0x1d, 0xcc00 | offset);
	return ((uint64_t) priv->mii_info.
		mdio_read(dev, phy_id,
			  0x1e) << 16) | (uint64_t) priv->mii_info.
	    mdio_read(dev, phy_id, 0x1f);
}

static int cvm_oct_stats_switch_show(struct seq_file *m, void *v)
{
	static const int ports[] = { 0, 1, 2, 3, 9, -1 };
	struct net_device *dev = cvm_oct_device[0];
	int index = 0;

	while (ports[index] != -1) {

		/* Latch port */
		struct octeon_ethernet *priv = netdev_priv(dev);

		priv->mii_info.mdio_write(dev, 0x1b, 0x1d,
					  0xdc00 | ports[index]);
		seq_printf(m, "\nSwitch Port %d\n", ports[index]);
		seq_printf(m, "InGoodOctets:   %12llu\t"
			   "OutOctets:      %12llu\t"
			   "64 Octets:      %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b,
						     0x00) |
			   (cvm_oct_stats_read_switch(dev, 0x1b, 0x01) << 32),
			   cvm_oct_stats_read_switch(dev, 0x1b,
						     0x0E) |
			   (cvm_oct_stats_read_switch(dev, 0x1b, 0x0F) << 32),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x08));

		seq_printf(m, "InBadOctets:    %12llu\t"
			   "OutUnicast:     %12llu\t"
			   "65-127 Octets:  %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x02),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x10),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x09));

		seq_printf(m, "InUnicast:      %12llu\t"
			   "OutBroadcasts:  %12llu\t"
			   "128-255 Octets: %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x04),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x13),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x0A));

		seq_printf(m, "InBroadcasts:   %12llu\t"
			   "OutMulticasts:  %12llu\t"
			   "256-511 Octets: %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x06),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x12),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x0B));

		seq_printf(m, "InMulticasts:   %12llu\t"
			   "OutPause:       %12llu\t"
			   "512-1023 Octets:%12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x07),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x15),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x0C));

		seq_printf(m, "InPause:        %12llu\t"
			   "Excessive:      %12llu\t"
			   "1024-Max Octets:%12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x16),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x11),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x0D));

		seq_printf(m, "InUndersize:    %12llu\t"
			   "Collisions:     %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x18),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1E));

		seq_printf(m, "InFragments:    %12llu\t"
			   "Deferred:       %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x19),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x05));

		seq_printf(m, "InOversize:     %12llu\t"
			   "Single:         %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1A),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x14));

		seq_printf(m, "InJabber:       %12llu\t"
			   "Multiple:       %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1B),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x17));

		seq_printf(m, "In RxErr:       %12llu\t"
			   "OutFCSErr:      %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1C),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x03));

		seq_printf(m, "InFCSErr:       %12llu\t"
			   "Late:           %12llu\n",
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1D),
			   cvm_oct_stats_read_switch(dev, 0x1b, 0x1F));
		index++;
	}
	return 0;
}

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

	if (cvm_oct_device[0]) {
		priv = netdev_priv(cvm_oct_device[0]);
		if (priv->imode == CVMX_HELPER_INTERFACE_MODE_GMII)
			cvm_oct_stats_switch_show(m, v);
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
