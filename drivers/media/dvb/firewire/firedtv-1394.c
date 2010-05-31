/*
 * FireDTV driver -- ieee1394 I/O backend
 *
 * Copyright (C) 2004 Andreas Monitzer <andy@monitzer.com>
 * Copyright (C) 2007-2008 Ben Backx <ben@bbackx.com>
 * Copyright (C) 2008 Henrik Kurelid <henrik@kurelid.se>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <dma.h>
#include <csr1212.h>
#include <highlevel.h>
#include <hosts.h>
#include <ieee1394.h>
#include <iso.h>
#include <nodemgr.h>

#include <dvb_demux.h>

#include "firedtv.h"

static LIST_HEAD(node_list);
static DEFINE_SPINLOCK(node_list_lock);

#define CIP_HEADER_SIZE			8
#define MPEG2_TS_HEADER_SIZE		4
#define MPEG2_TS_SOURCE_PACKET_SIZE	(4 + 188)

static void rawiso_activity_cb(struct hpsb_iso *iso)
{
	struct firedtv *f, *fdtv = NULL;
	unsigned int i, num, packet;
	unsigned char *buf;
	unsigned long flags;
	int count;

	spin_lock_irqsave(&node_list_lock, flags);
	list_for_each_entry(f, &node_list, list)
		if (f->backend_data == iso) {
			fdtv = f;
			break;
		}
	spin_unlock_irqrestore(&node_list_lock, flags);

	packet = iso->first_packet;
	num = hpsb_iso_n_ready(iso);

	if (!fdtv) {
		dev_err(fdtv->device, "received at unknown iso channel\n");
		goto out;
	}

	for (i = 0; i < num; i++, packet = (packet + 1) % iso->buf_packets) {
		buf = dma_region_i(&iso->data_buf, unsigned char,
			iso->infos[packet].offset + CIP_HEADER_SIZE);
		count = (iso->infos[packet].len - CIP_HEADER_SIZE) /
			MPEG2_TS_SOURCE_PACKET_SIZE;

		/* ignore empty packet */
		if (iso->infos[packet].len <= CIP_HEADER_SIZE)
			continue;

		while (count--) {
			if (buf[MPEG2_TS_HEADER_SIZE] == 0x47)
				dvb_dmx_swfilter_packets(&fdtv->demux,
						&buf[MPEG2_TS_HEADER_SIZE], 1);
			else
				dev_err(fdtv->device,
					"skipping invalid packet\n");
			buf += MPEG2_TS_SOURCE_PACKET_SIZE;
		}
	}
out:
	hpsb_iso_recv_release_packets(iso, num);
}

static inline struct node_entry *node_of(struct firedtv *fdtv)
{
	return container_of(fdtv->device, struct unit_directory, device)->ne;
}

static int node_lock(struct firedtv *fdtv, u64 addr, void *data)
{
	quadlet_t *d = data;
	int ret;

	ret = hpsb_node_lock(node_of(fdtv), addr,
			     EXTCODE_COMPARE_SWAP, &d[1], d[0]);
	d[0] = d[1];

	return ret;
}

static int node_read(struct firedtv *fdtv, u64 addr, void *data)
{
	return hpsb_node_read(node_of(fdtv), addr, data, 4);
}

static int node_write(struct firedtv *fdtv, u64 addr, void *data, size_t len)
{
	return hpsb_node_write(node_of(fdtv), addr, data, len);
}

#define FDTV_ISO_BUFFER_PACKETS 256
#define FDTV_ISO_BUFFER_SIZE (FDTV_ISO_BUFFER_PACKETS * 200)

static int start_iso(struct firedtv *fdtv)
{
	struct hpsb_iso *iso_handle;
	int ret;

	iso_handle = hpsb_iso_recv_init(node_of(fdtv)->host,
				FDTV_ISO_BUFFER_SIZE, FDTV_ISO_BUFFER_PACKETS,
				fdtv->isochannel, HPSB_ISO_DMA_DEFAULT,
				-1, /* stat.config.irq_interval */
				rawiso_activity_cb);
	if (iso_handle == NULL) {
		dev_err(fdtv->device, "cannot initialize iso receive\n");
		return -ENOMEM;
	}
	fdtv->backend_data = iso_handle;

	ret = hpsb_iso_recv_start(iso_handle, -1, -1, 0);
	if (ret != 0) {
		dev_err(fdtv->device, "cannot start iso receive\n");
		hpsb_iso_shutdown(iso_handle);
		fdtv->backend_data = NULL;
	}
	return ret;
}

static void stop_iso(struct firedtv *fdtv)
{
	struct hpsb_iso *iso_handle = fdtv->backend_data;

	if (iso_handle != NULL) {
		hpsb_iso_stop(iso_handle);
		hpsb_iso_shutdown(iso_handle);
	}
	fdtv->backend_data = NULL;
}

static const struct firedtv_backend fdtv_1394_backend = {
	.lock		= node_lock,
	.read		= node_read,
	.write		= node_write,
	.start_iso	= start_iso,
	.stop_iso	= stop_iso,
};

static void fcp_request(struct hpsb_host *host, int nodeid, int direction,
			int cts, u8 *data, size_t length)
{
	struct firedtv *f, *fdtv = NULL;
	unsigned long flags;
	int su;

	if (length == 0 || (data[0] & 0xf0) != 0)
		return;

	su = data[1] & 0x7;

	spin_lock_irqsave(&node_list_lock, flags);
	list_for_each_entry(f, &node_list, list)
		if (node_of(f)->host == host &&
		    node_of(f)->nodeid == nodeid &&
		    (f->subunit == su || (f->subunit == 0 && su == 0x7))) {
			fdtv = f;
			break;
		}
	spin_unlock_irqrestore(&node_list_lock, flags);

	if (fdtv)
		avc_recv(fdtv, data, length);
}

static int node_probe(struct device *dev)
{
	struct unit_directory *ud =
			container_of(dev, struct unit_directory, device);
	struct firedtv *fdtv;
	int kv_len, err;
	void *kv_str;

	if (ud->model_name_kv) {
		kv_len = (ud->model_name_kv->value.leaf.len - 2) * 4;
		kv_str = CSR1212_TEXTUAL_DESCRIPTOR_LEAF_DATA(ud->model_name_kv);
	} else {
		kv_len = 0;
		kv_str = NULL;
	}
	fdtv = fdtv_alloc(dev, &fdtv_1394_backend, kv_str, kv_len);
	if (!fdtv)
		return -ENOMEM;

	/*
	 * Work around a bug in udev's path_id script:  Use the fw-host's dev
	 * instead of the unit directory's dev as parent of the input device.
	 */
	err = fdtv_register_rc(fdtv, dev->parent->parent);
	if (err)
		goto fail_free;

	spin_lock_irq(&node_list_lock);
	list_add_tail(&fdtv->list, &node_list);
	spin_unlock_irq(&node_list_lock);

	err = avc_identify_subunit(fdtv);
	if (err)
		goto fail;

	err = fdtv_dvb_register(fdtv);
	if (err)
		goto fail;

	avc_register_remote_control(fdtv);

	return 0;
fail:
	spin_lock_irq(&node_list_lock);
	list_del(&fdtv->list);
	spin_unlock_irq(&node_list_lock);
	fdtv_unregister_rc(fdtv);
fail_free:
	kfree(fdtv);

	return err;
}

static int node_remove(struct device *dev)
{
	struct firedtv *fdtv = dev_get_drvdata(dev);

	fdtv_dvb_unregister(fdtv);

	spin_lock_irq(&node_list_lock);
	list_del(&fdtv->list);
	spin_unlock_irq(&node_list_lock);

	fdtv_unregister_rc(fdtv);
	kfree(fdtv);

	return 0;
}

static int node_update(struct unit_directory *ud)
{
	struct firedtv *fdtv = dev_get_drvdata(&ud->device);

	if (fdtv->isochannel >= 0)
		cmp_establish_pp_connection(fdtv, fdtv->subunit,
					    fdtv->isochannel);
	return 0;
}

static struct hpsb_protocol_driver fdtv_driver = {
	.name		= "firedtv",
	.id_table	= fdtv_id_table,
	.update		= node_update,
	.driver         = {
		.probe  = node_probe,
		.remove = node_remove,
	},
};

static struct hpsb_highlevel fdtv_highlevel = {
	.name		= "firedtv",
	.fcp_request	= fcp_request,
};

int __init fdtv_1394_init(void)
{
	int ret;

	hpsb_register_highlevel(&fdtv_highlevel);
	ret = hpsb_register_protocol(&fdtv_driver);
	if (ret) {
		printk(KERN_ERR "firedtv: failed to register protocol\n");
		hpsb_unregister_highlevel(&fdtv_highlevel);
	}
	return ret;
}

void __exit fdtv_1394_exit(void)
{
	hpsb_unregister_protocol(&fdtv_driver);
	hpsb_unregister_highlevel(&fdtv_highlevel);
}
