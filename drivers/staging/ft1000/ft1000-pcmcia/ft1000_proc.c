/*---------------------------------------------------------------------------
   FT1000 driver for Flarion Flash OFDM NIC Device

   Copyright (C) 2006 Patrik Ostrihon, All rights reserved.
   Copyright (C) 2006 ProWeb Consulting, a.s, All rights reserved.

   This program is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 2 of the License, or (at your option) any
   later version. This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details. You should have received a copy of the GNU General Public
   License along with this program; if not, write to the
   Free Software Foundation, Inc., 59 Temple Place -
   Suite 330, Boston, MA 02111-1307, USA.
-----------------------------------------------------------------------------*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include "ft1000.h"

#define FT1000_PROC "ft1000"
#define MAX_FILE_LEN 255

#define seq_putx(m, message, size, var) \
	seq_printf(m, message);	\
	for(i = 0; i < (size - 1); i++) { \
		seq_printf(m, "%02x:", var[i]); \
	} \
	seq_printf(m, "%02x\n", var[i])

#define seq_putd(m, message, size, var) \
	seq_printf(m, message); \
	for(i = 0; i < (size - 1); i++) { \
		seq_printf(m, "%d.", var[i]); \
	} \
	seq_printf(m, "%d\n", var[i])

static int ft1000ReadProc(struct seq_file *m, void *v)
{
	static const char *status[] = {
		"Idle (Disconnect)", "Searching", "Active (Connected)",
		"Waiting for L2", "Sleep", "No Coverage", "", ""
	};
	static const char *signal[] = { "", "*", "**", "***", "****" };

	struct net_device *dev = m->private;
	struct ft1000_info *info = netdev_priv(dev);
	int i;
	int strength;
	int quality;
	struct timeval tv;
	time_t delta;

	if (info->AsicID == ELECTRABUZZ_ID) {
		if (info->ProgConStat != 0xFF) {
			info->LedStat =
				ft1000_read_dpram(dev, FT1000_DSP_LED);
			info->ConStat =
				ft1000_read_dpram(dev, FT1000_DSP_CON_STATE);
		} else {
			info->ConStat = 0xf;
		}
	} else {
		if (info->ProgConStat != 0xFF) {
			info->LedStat =
				ntohs(ft1000_read_dpram_mag_16(
					      dev, FT1000_MAG_DSP_LED,
					      FT1000_MAG_DSP_LED_INDX));
			info->ConStat =
				ntohs(ft1000_read_dpram_mag_16(
					      dev, FT1000_MAG_DSP_CON_STATE,
					      FT1000_MAG_DSP_CON_STATE_INDX));
		} else {
			info->ConStat = 0xf;
		}
	}

	i = (info->LedStat) & 0xf;
	switch (i) {
	case 0x1:
		strength = 1;
		break;
	case 0x3:
		strength = 2;
		break;
	case 0x7:
		strength = 3;
		break;
	case 0xf:
		strength = 4;
		break;
	default:
		strength = 0;
	}

	i = (info->LedStat >> 8) & 0xf;
	switch (i) {
	case 0x1:
		quality = 1;
		break;
	case 0x3:
		quality = 2;
		break;
	case 0x7:
		quality = 3;
		break;
	case 0xf:
		quality = 4;
		break;
	default:
		quality = 0;
	}

	do_gettimeofday(&tv);
	delta = tv.tv_sec - info->ConTm;
	seq_printf(m, "Connection Time: %02ld:%02ld:%02ld\n",
			 ((delta / 3600) % 24), ((delta / 60) % 60), (delta % 60));
	seq_printf(m, "Connection Time[s]: %ld\n", delta);
	seq_printf(m, "Asic ID: %s\n",
			 info->AsicID ==
			 ELECTRABUZZ_ID ? "ELECTRABUZZ ASIC" : "MAGNEMITE ASIC");
	seq_putx(m, "SKU: ", SKUSZ, info->Sku);
	seq_putx(m, "EUI64: ", EUISZ, info->eui64);
	seq_putd(m, "DSP version number: ", DSPVERSZ, info->DspVer);
	seq_putx(m, "Hardware Serial Number: ", HWSERNUMSZ, info->HwSerNum);
	seq_putx(m, "Caliberation Version: ", CALVERSZ, info->RfCalVer);
	seq_putd(m, "Caliberation Date: ", CALDATESZ, info->RfCalDate);
	seq_printf(m, "Media State: %s\n",
			 (info->mediastate) ? "link" : "no link");
	seq_printf(m, "Connection Status: %s\n", status[info->ConStat & 0x7]);
	seq_printf(m, "RX packets: %ld\n", info->stats.rx_packets);
	seq_printf(m, "TX packets: %ld\n", info->stats.tx_packets);
	seq_printf(m, "RX bytes: %ld\n", info->stats.rx_bytes);
	seq_printf(m, "TX bytes: %ld\n", info->stats.tx_bytes);
	seq_printf(m, "Signal Strength: %s\n", signal[strength]);
	seq_printf(m, "Signal Quality: %s\n", signal[quality]);
	return 0;
}

/*
 * seq_file wrappers for procfile show routines.
 */
static int ft1000_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ft1000ReadProc, PDE_DATA(inode));
}

static const struct file_operations ft1000_proc_fops = {
	.open		= ft1000_proc_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int ft1000NotifyProc(struct notifier_block *this, unsigned long event,
				void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct ft1000_info *info;

	info = netdev_priv(dev);

	switch (event) {
	case NETDEV_CHANGENAME:
		remove_proc_entry(info->netdevname, info->ft1000_proc_dir);
		proc_create_data(dev->name, 0644, info->ft1000_proc_dir,
				 &ft1000_proc_fops, dev);
		snprintf(info->netdevname, IFNAMSIZ, "%s", dev->name);
		break;
	}
	return NOTIFY_DONE;
}

static struct notifier_block ft1000_netdev_notifier = {
	.notifier_call = ft1000NotifyProc
};

void ft1000InitProc(struct net_device *dev)
{
	struct ft1000_info *info;

	info = netdev_priv(dev);

	info->ft1000_proc_dir = proc_mkdir(FT1000_PROC, init_net.proc_net);

	proc_create_data(dev->name, 0644, info->ft1000_proc_dir,
			 &ft1000_proc_fops, dev);

	snprintf(info->netdevname, IFNAMSIZ, "%s", dev->name);
	register_netdevice_notifier(&ft1000_netdev_notifier);
}

void ft1000CleanupProc(struct net_device *dev)
{
	struct ft1000_info *info;

	info = netdev_priv(dev);

	remove_proc_entry(dev->name, info->ft1000_proc_dir);
	remove_proc_entry(FT1000_PROC, init_net.proc_net);
	unregister_netdevice_notifier(&ft1000_netdev_notifier);
}
