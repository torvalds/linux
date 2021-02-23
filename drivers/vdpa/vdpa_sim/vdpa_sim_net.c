// SPDX-License-Identifier: GPL-2.0-only
/*
 * VDPA simulator for networking device.
 *
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 *     Author: Jason Wang <jasowang@redhat.com>
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/etherdevice.h>
#include <linux/vringh.h>
#include <linux/vdpa.h>
#include <uapi/linux/virtio_net.h>

#include "vdpa_sim.h"

#define DRV_VERSION  "0.1"
#define DRV_AUTHOR   "Jason Wang <jasowang@redhat.com>"
#define DRV_DESC     "vDPA Device Simulator for networking device"
#define DRV_LICENSE  "GPL v2"

#define VDPASIM_NET_FEATURES	(VDPASIM_FEATURES | \
				 (1ULL << VIRTIO_NET_F_MAC))

#define VDPASIM_NET_VQ_NUM	2

static char *macaddr;
module_param(macaddr, charp, 0);
MODULE_PARM_DESC(macaddr, "Ethernet MAC address");

u8 macaddr_buf[ETH_ALEN];

static struct vdpasim *vdpasim_net_dev;

static void vdpasim_net_work(struct work_struct *work)
{
	struct vdpasim *vdpasim = container_of(work, struct vdpasim, work);
	struct vdpasim_virtqueue *txq = &vdpasim->vqs[1];
	struct vdpasim_virtqueue *rxq = &vdpasim->vqs[0];
	ssize_t read, write;
	size_t total_write;
	int pkts = 0;
	int err;

	spin_lock(&vdpasim->lock);

	if (!(vdpasim->status & VIRTIO_CONFIG_S_DRIVER_OK))
		goto out;

	if (!txq->ready || !rxq->ready)
		goto out;

	while (true) {
		total_write = 0;
		err = vringh_getdesc_iotlb(&txq->vring, &txq->out_iov, NULL,
					   &txq->head, GFP_ATOMIC);
		if (err <= 0)
			break;

		err = vringh_getdesc_iotlb(&rxq->vring, NULL, &rxq->in_iov,
					   &rxq->head, GFP_ATOMIC);
		if (err <= 0) {
			vringh_complete_iotlb(&txq->vring, txq->head, 0);
			break;
		}

		while (true) {
			read = vringh_iov_pull_iotlb(&txq->vring, &txq->out_iov,
						     vdpasim->buffer,
						     PAGE_SIZE);
			if (read <= 0)
				break;

			write = vringh_iov_push_iotlb(&rxq->vring, &rxq->in_iov,
						      vdpasim->buffer, read);
			if (write <= 0)
				break;

			total_write += write;
		}

		/* Make sure data is wrote before advancing index */
		smp_wmb();

		vringh_complete_iotlb(&txq->vring, txq->head, 0);
		vringh_complete_iotlb(&rxq->vring, rxq->head, total_write);

		/* Make sure used is visible before rasing the interrupt. */
		smp_wmb();

		local_bh_disable();
		if (vringh_need_notify_iotlb(&txq->vring) > 0)
			vringh_notify(&txq->vring);
		if (vringh_need_notify_iotlb(&rxq->vring) > 0)
			vringh_notify(&rxq->vring);
		local_bh_enable();

		if (++pkts > 4) {
			schedule_work(&vdpasim->work);
			goto out;
		}
	}

out:
	spin_unlock(&vdpasim->lock);
}

static void vdpasim_net_get_config(struct vdpasim *vdpasim, void *config)
{
	struct virtio_net_config *net_config =
		(struct virtio_net_config *)config;

	net_config->mtu = cpu_to_vdpasim16(vdpasim, 1500);
	net_config->status = cpu_to_vdpasim16(vdpasim, VIRTIO_NET_S_LINK_UP);
	memcpy(net_config->mac, macaddr_buf, ETH_ALEN);
}

static int __init vdpasim_net_init(void)
{
	struct vdpasim_dev_attr dev_attr = {};
	int ret;

	if (macaddr) {
		mac_pton(macaddr, macaddr_buf);
		if (!is_valid_ether_addr(macaddr_buf)) {
			ret = -EADDRNOTAVAIL;
			goto out;
		}
	} else {
		eth_random_addr(macaddr_buf);
	}

	dev_attr.id = VIRTIO_ID_NET;
	dev_attr.supported_features = VDPASIM_NET_FEATURES;
	dev_attr.nvqs = VDPASIM_NET_VQ_NUM;
	dev_attr.config_size = sizeof(struct virtio_net_config);
	dev_attr.get_config = vdpasim_net_get_config;
	dev_attr.work_fn = vdpasim_net_work;
	dev_attr.buffer_size = PAGE_SIZE;

	vdpasim_net_dev = vdpasim_create(&dev_attr);
	if (IS_ERR(vdpasim_net_dev)) {
		ret = PTR_ERR(vdpasim_net_dev);
		goto out;
	}

	ret = vdpa_register_device(&vdpasim_net_dev->vdpa);
	if (ret)
		goto put_dev;

	return 0;

put_dev:
	put_device(&vdpasim_net_dev->vdpa.dev);
out:
	return ret;
}

static void __exit vdpasim_net_exit(void)
{
	struct vdpa_device *vdpa = &vdpasim_net_dev->vdpa;

	vdpa_unregister_device(vdpa);
}

module_init(vdpasim_net_init);
module_exit(vdpasim_net_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
