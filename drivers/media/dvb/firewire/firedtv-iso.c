/*
 * FireSAT DVB driver
 *
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/spinlock.h>

#include <dvb_demux.h>

#include <dma.h>
#include <iso.h>
#include <nodemgr.h>

#include "firedtv.h"

static void rawiso_activity_cb(struct hpsb_iso *iso);

void tear_down_iso_channel(struct firedtv *fdtv)
{
	if (fdtv->iso_handle != NULL) {
		hpsb_iso_stop(fdtv->iso_handle);
		hpsb_iso_shutdown(fdtv->iso_handle);
	}
	fdtv->iso_handle = NULL;
}

int setup_iso_channel(struct firedtv *fdtv)
{
	int result;
	fdtv->iso_handle =
		hpsb_iso_recv_init(fdtv->ud->ne->host,
				   256 * 200, //data_buf_size,
				   256, //buf_packets,
				   fdtv->isochannel,
				   HPSB_ISO_DMA_DEFAULT, //dma_mode,
				   -1, //stat.config.irq_interval,
				   rawiso_activity_cb);
	if (fdtv->iso_handle == NULL) {
		printk(KERN_ERR "Cannot initialize iso receive.\n");
		return -EINVAL;
	}
	result = hpsb_iso_recv_start(fdtv->iso_handle, -1, -1, 0);
	if (result != 0) {
		printk(KERN_ERR "Cannot start iso receive.\n");
		return -EINVAL;
	}
	return 0;
}

static void rawiso_activity_cb(struct hpsb_iso *iso)
{
	unsigned int num;
	unsigned int i;
	unsigned int packet;
	unsigned long flags;
	struct firedtv *fdtv = NULL;
	struct firedtv *fdtv_iterator;

	spin_lock_irqsave(&fdtv_list_lock, flags);
	list_for_each_entry(fdtv_iterator, &fdtv_list, list) {
		if(fdtv_iterator->iso_handle == iso) {
			fdtv = fdtv_iterator;
			break;
		}
	}
	spin_unlock_irqrestore(&fdtv_list_lock, flags);

	if (fdtv) {
		packet = iso->first_packet;
		num = hpsb_iso_n_ready(iso);
		for (i = 0; i < num; i++,
			     packet = (packet + 1) % iso->buf_packets) {
			unsigned char *buf =
				dma_region_i(&iso->data_buf, unsigned char,
					     iso->infos[packet].offset +
					     sizeof(struct CIPHeader));
			int count = (iso->infos[packet].len -
				     sizeof(struct CIPHeader)) /
				(188 + sizeof(struct firewireheader));
			if (iso->infos[packet].len <= sizeof(struct CIPHeader))
				continue; // ignore empty packet

			while (count --) {
				if (buf[sizeof(struct firewireheader)] == 0x47)
					dvb_dmx_swfilter_packets(&fdtv->demux,
								 &buf[sizeof(struct firewireheader)], 1);
				else
					printk("%s: invalid packet, skipping\n", __func__);
				buf += 188 + sizeof(struct firewireheader);

			}

		}
		hpsb_iso_recv_release_packets(iso, num);
	}
	else {
		printk("%s: packets for unknown iso channel, skipping\n",
		       __func__);
		hpsb_iso_recv_release_packets(iso, hpsb_iso_n_ready(iso));
	}
}

