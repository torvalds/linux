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
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <asm/uaccess.h>
#include "ft1000.h"

#define FT1000_PROC "ft1000"
#define MAX_FILE_LEN 255

#define PUTM_TO_PAGE(len,page,args...) \
	len += snprintf(page+len, PAGE_SIZE - len, args)

#define PUTX_TO_PAGE(len,page,message,size,var) \
	len += snprintf(page+len, PAGE_SIZE - len, message); \
	for(i = 0; i < (size - 1); i++) \
	{ \
		len += snprintf(page+len, PAGE_SIZE - len, "%02x:", var[i]); \
	} \
	len += snprintf(page+len, PAGE_SIZE - len, "%02x\n", var[i])

#define PUTD_TO_PAGE(len,page,message,size,var) \
	len += snprintf(page+len, PAGE_SIZE - len, message); \
	for(i = 0; i < (size - 1); i++) \
	{ \
		len += snprintf(page+len, PAGE_SIZE - len, "%d.", var[i]); \
	} \
	len += snprintf(page+len, PAGE_SIZE - len, "%d\n", var[i])

int ft1000ReadProc(char *page, char **start, off_t off,
		   int count, int *eof, void *data)
{
	struct net_device *dev;
	int len;
	int i;
	FT1000_INFO *info;
	char *status[] =
		{ "Idle (Disconnect)", "Searching", "Active (Connected)",
		"Waiting for L2", "Sleep", "No Coverage", "", ""
	};
	char *signal[] = { "", "*", "**", "***", "****" };
	int strength;
	int quality;
	struct timeval tv;
	time_t delta;

	dev = (struct net_device *)data;
	info = netdev_priv(dev);

	if (off > 0) {
		*eof = 1;
		return 0;
	}

	/* Wrap-around */

	if (info->AsicID == ELECTRABUZZ_ID) {
		if (info->DspHibernateFlag == 0) {
			if (info->ProgConStat != 0xFF) {
				info->LedStat =
					ft1000_read_dpram(dev, FT1000_DSP_LED);
				info->ConStat =
					ft1000_read_dpram(dev,
							  FT1000_DSP_CON_STATE);
			} else {
				info->ConStat = 0xf;
			}
		}
	} else {
		if (info->ProgConStat != 0xFF) {
			info->LedStat =
				ntohs(ft1000_read_dpram_mag_16
				  (dev, FT1000_MAG_DSP_LED,
				   FT1000_MAG_DSP_LED_INDX));
			info->ConStat =
				ntohs(ft1000_read_dpram_mag_16
				  (dev, FT1000_MAG_DSP_CON_STATE,
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
	delta = (tv.tv_sec - info->ConTm);
	len = 0;
	PUTM_TO_PAGE(len, page, "Connection Time: %02ld:%02ld:%02ld\n",
			 ((delta / 3600) % 24), ((delta / 60) % 60), (delta % 60));
	PUTM_TO_PAGE(len, page, "Connection Time[s]: %ld\n", delta);
	PUTM_TO_PAGE(len, page, "Asic ID: %s\n",
			 (info->AsicID) ==
			 ELECTRABUZZ_ID ? "ELECTRABUZZ ASIC" : "MAGNEMITE ASIC");
	PUTX_TO_PAGE(len, page, "SKU: ", SKUSZ, info->Sku);
	PUTX_TO_PAGE(len, page, "EUI64: ", EUISZ, info->eui64);
	PUTD_TO_PAGE(len, page, "DSP version number: ", DSPVERSZ, info->DspVer);
	PUTX_TO_PAGE(len, page, "Hardware Serial Number: ", HWSERNUMSZ,
			 info->HwSerNum);
	PUTX_TO_PAGE(len, page, "Caliberation Version: ", CALVERSZ,
			 info->RfCalVer);
	PUTD_TO_PAGE(len, page, "Caliberation Date: ", CALDATESZ,
			 info->RfCalDate);
	PUTM_TO_PAGE(len, page, "Media State: %s\n",
			 (info->mediastate) ? "link" : "no link");
	PUTM_TO_PAGE(len, page, "Connection Status: %s\n",
			 status[((info->ConStat) & 0x7)]);
	PUTM_TO_PAGE(len, page, "RX packets: %ld\n", info->stats.rx_packets);
	PUTM_TO_PAGE(len, page, "TX packets: %ld\n", info->stats.tx_packets);
	PUTM_TO_PAGE(len, page, "RX bytes: %ld\n", info->stats.rx_bytes);
	PUTM_TO_PAGE(len, page, "TX bytes: %ld\n", info->stats.tx_bytes);
	PUTM_TO_PAGE(len, page, "Signal Strength: %s\n", signal[strength]);
	PUTM_TO_PAGE(len, page, "Signal Quality: %s\n", signal[quality]);
	return len;
}

static int ft1000NotifyProc(struct notifier_block *this, unsigned long event,
				void *ptr)
{
	struct net_device *dev = ptr;
	FT1000_INFO *info;

	info = netdev_priv(dev);

	switch (event) {
	case NETDEV_CHANGENAME:
		remove_proc_entry(info->netdevname, info->proc_ft1000);
		create_proc_read_entry(dev->name, 0644, info->proc_ft1000,
					   ft1000ReadProc, dev);
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
	FT1000_INFO *info;

	info = netdev_priv(dev);

	info->proc_ft1000 = proc_mkdir(FT1000_PROC, init_net.proc_net);
	create_proc_read_entry(dev->name, 0644, info->proc_ft1000,
				   ft1000ReadProc, dev);
	snprintf(info->netdevname, IFNAMSIZ, "%s", dev->name);
	register_netdevice_notifier(&ft1000_netdev_notifier);
}

void ft1000CleanupProc(struct net_device *dev)
{
	FT1000_INFO *info;

	info = netdev_priv(dev);

	remove_proc_entry(dev->name, info->proc_ft1000);
	remove_proc_entry(FT1000_PROC, init_net.proc_net);
	unregister_netdevice_notifier(&ft1000_netdev_notifier);
}

EXPORT_SYMBOL(ft1000InitProc);
EXPORT_SYMBOL(ft1000CleanupProc);
