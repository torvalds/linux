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
#include <uapi/linux/vdpa.h>

#include "vdpa_sim.h"

#define DRV_VERSION  "0.1"
#define DRV_AUTHOR   "Jason Wang <jasowang@redhat.com>"
#define DRV_DESC     "vDPA Device Simulator for networking device"
#define DRV_LICENSE  "GPL v2"

#define VDPASIM_NET_FEATURES	(VDPASIM_FEATURES | \
				 (1ULL << VIRTIO_NET_F_MAC) | \
				 (1ULL << VIRTIO_NET_F_MTU) | \
				 (1ULL << VIRTIO_NET_F_CTRL_VQ) | \
				 (1ULL << VIRTIO_NET_F_CTRL_MAC_ADDR))

/* 3 virtqueues, 2 address spaces, 2 virtqueue groups */
#define VDPASIM_NET_VQ_NUM	3
#define VDPASIM_NET_AS_NUM	2
#define VDPASIM_NET_GROUP_NUM	2

static void vdpasim_net_complete(struct vdpasim_virtqueue *vq, size_t len)
{
	/* Make sure data is wrote before advancing index */
	smp_wmb();

	vringh_complete_iotlb(&vq->vring, vq->head, len);

	/* Make sure used is visible before rasing the interrupt. */
	smp_wmb();

	local_bh_disable();
	if (vringh_need_notify_iotlb(&vq->vring) > 0)
		vringh_notify(&vq->vring);
	local_bh_enable();
}

static bool receive_filter(struct vdpasim *vdpasim, size_t len)
{
	bool modern = vdpasim->features & (1ULL << VIRTIO_F_VERSION_1);
	size_t hdr_len = modern ? sizeof(struct virtio_net_hdr_v1) :
				  sizeof(struct virtio_net_hdr);
	struct virtio_net_config *vio_config = vdpasim->config;

	if (len < ETH_ALEN + hdr_len)
		return false;

	if (is_broadcast_ether_addr(vdpasim->buffer + hdr_len) ||
	    is_multicast_ether_addr(vdpasim->buffer + hdr_len))
		return true;
	if (!strncmp(vdpasim->buffer + hdr_len, vio_config->mac, ETH_ALEN))
		return true;

	return false;
}

static virtio_net_ctrl_ack vdpasim_handle_ctrl_mac(struct vdpasim *vdpasim,
						   u8 cmd)
{
	struct virtio_net_config *vio_config = vdpasim->config;
	struct vdpasim_virtqueue *cvq = &vdpasim->vqs[2];
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	size_t read;

	switch (cmd) {
	case VIRTIO_NET_CTRL_MAC_ADDR_SET:
		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->in_iov,
					     vio_config->mac, ETH_ALEN);
		if (read == ETH_ALEN)
			status = VIRTIO_NET_OK;
		break;
	default:
		break;
	}

	return status;
}

static void vdpasim_handle_cvq(struct vdpasim *vdpasim)
{
	struct vdpasim_virtqueue *cvq = &vdpasim->vqs[2];
	virtio_net_ctrl_ack status = VIRTIO_NET_ERR;
	struct virtio_net_ctrl_hdr ctrl;
	size_t read, write;
	int err;

	if (!(vdpasim->features & (1ULL << VIRTIO_NET_F_CTRL_VQ)))
		return;

	if (!cvq->ready)
		return;

	while (true) {
		err = vringh_getdesc_iotlb(&cvq->vring, &cvq->in_iov,
					   &cvq->out_iov,
					   &cvq->head, GFP_ATOMIC);
		if (err <= 0)
			break;

		read = vringh_iov_pull_iotlb(&cvq->vring, &cvq->in_iov, &ctrl,
					     sizeof(ctrl));
		if (read != sizeof(ctrl))
			break;

		switch (ctrl.class) {
		case VIRTIO_NET_CTRL_MAC:
			status = vdpasim_handle_ctrl_mac(vdpasim, ctrl.cmd);
			break;
		default:
			break;
		}

		/* Make sure data is wrote before advancing index */
		smp_wmb();

		write = vringh_iov_push_iotlb(&cvq->vring, &cvq->out_iov,
					      &status, sizeof(status));
		vringh_complete_iotlb(&cvq->vring, cvq->head, write);
		vringh_kiov_cleanup(&cvq->in_iov);
		vringh_kiov_cleanup(&cvq->out_iov);

		/* Make sure used is visible before rasing the interrupt. */
		smp_wmb();

		local_bh_disable();
		if (cvq->cb)
			cvq->cb(cvq->private);
		local_bh_enable();
	}
}

static void vdpasim_net_work(struct work_struct *work)
{
	struct vdpasim *vdpasim = container_of(work, struct vdpasim, work);
	struct vdpasim_virtqueue *txq = &vdpasim->vqs[1];
	struct vdpasim_virtqueue *rxq = &vdpasim->vqs[0];
	ssize_t read, write;
	int pkts = 0;
	int err;

	spin_lock(&vdpasim->lock);

	if (!vdpasim->running)
		goto out;

	if (!(vdpasim->status & VIRTIO_CONFIG_S_DRIVER_OK))
		goto out;

	vdpasim_handle_cvq(vdpasim);

	if (!txq->ready || !rxq->ready)
		goto out;

	while (true) {
		err = vringh_getdesc_iotlb(&txq->vring, &txq->out_iov, NULL,
					   &txq->head, GFP_ATOMIC);
		if (err <= 0)
			break;

		read = vringh_iov_pull_iotlb(&txq->vring, &txq->out_iov,
					     vdpasim->buffer,
					     PAGE_SIZE);

		if (!receive_filter(vdpasim, read)) {
			vdpasim_net_complete(txq, 0);
			continue;
		}

		err = vringh_getdesc_iotlb(&rxq->vring, NULL, &rxq->in_iov,
					   &rxq->head, GFP_ATOMIC);
		if (err <= 0) {
			vdpasim_net_complete(txq, 0);
			break;
		}

		write = vringh_iov_push_iotlb(&rxq->vring, &rxq->in_iov,
					      vdpasim->buffer, read);
		if (write <= 0)
			break;

		vdpasim_net_complete(txq, 0);
		vdpasim_net_complete(rxq, write);

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
	struct virtio_net_config *net_config = config;

	net_config->status = cpu_to_vdpasim16(vdpasim, VIRTIO_NET_S_LINK_UP);
}

static void vdpasim_net_setup_config(struct vdpasim *vdpasim,
				     const struct vdpa_dev_set_config *config)
{
	struct virtio_net_config *vio_config = vdpasim->config;

	if (config->mask & (1 << VDPA_ATTR_DEV_NET_CFG_MACADDR))
		memcpy(vio_config->mac, config->net.mac, ETH_ALEN);
	if (config->mask & (1 << VDPA_ATTR_DEV_NET_CFG_MTU))
		vio_config->mtu = cpu_to_vdpasim16(vdpasim, config->net.mtu);
	else
		/* Setup default MTU to be 1500 */
		vio_config->mtu = cpu_to_vdpasim16(vdpasim, 1500);
}

static void vdpasim_net_mgmtdev_release(struct device *dev)
{
}

static struct device vdpasim_net_mgmtdev = {
	.init_name = "vdpasim_net",
	.release = vdpasim_net_mgmtdev_release,
};

static int vdpasim_net_dev_add(struct vdpa_mgmt_dev *mdev, const char *name,
			       const struct vdpa_dev_set_config *config)
{
	struct vdpasim_dev_attr dev_attr = {};
	struct vdpasim *simdev;
	int ret;

	dev_attr.mgmt_dev = mdev;
	dev_attr.name = name;
	dev_attr.id = VIRTIO_ID_NET;
	dev_attr.supported_features = VDPASIM_NET_FEATURES;
	dev_attr.nvqs = VDPASIM_NET_VQ_NUM;
	dev_attr.ngroups = VDPASIM_NET_GROUP_NUM;
	dev_attr.nas = VDPASIM_NET_AS_NUM;
	dev_attr.config_size = sizeof(struct virtio_net_config);
	dev_attr.get_config = vdpasim_net_get_config;
	dev_attr.work_fn = vdpasim_net_work;
	dev_attr.buffer_size = PAGE_SIZE;

	simdev = vdpasim_create(&dev_attr, config);
	if (IS_ERR(simdev))
		return PTR_ERR(simdev);

	vdpasim_net_setup_config(simdev, config);

	ret = _vdpa_register_device(&simdev->vdpa, VDPASIM_NET_VQ_NUM);
	if (ret)
		goto reg_err;

	return 0;

reg_err:
	put_device(&simdev->vdpa.dev);
	return ret;
}

static void vdpasim_net_dev_del(struct vdpa_mgmt_dev *mdev,
				struct vdpa_device *dev)
{
	struct vdpasim *simdev = container_of(dev, struct vdpasim, vdpa);

	_vdpa_unregister_device(&simdev->vdpa);
}

static const struct vdpa_mgmtdev_ops vdpasim_net_mgmtdev_ops = {
	.dev_add = vdpasim_net_dev_add,
	.dev_del = vdpasim_net_dev_del
};

static struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_NET, VIRTIO_DEV_ANY_ID },
	{ 0 },
};

static struct vdpa_mgmt_dev mgmt_dev = {
	.device = &vdpasim_net_mgmtdev,
	.id_table = id_table,
	.ops = &vdpasim_net_mgmtdev_ops,
	.config_attr_mask = (1 << VDPA_ATTR_DEV_NET_CFG_MACADDR |
			     1 << VDPA_ATTR_DEV_NET_CFG_MTU |
		             1 << VDPA_ATTR_DEV_FEATURES),
	.max_supported_vqs = VDPASIM_NET_VQ_NUM,
	.supported_features = VDPASIM_NET_FEATURES,
};

static int __init vdpasim_net_init(void)
{
	int ret;

	ret = device_register(&vdpasim_net_mgmtdev);
	if (ret) {
		put_device(&vdpasim_net_mgmtdev);
		return ret;
	}

	ret = vdpa_mgmtdev_register(&mgmt_dev);
	if (ret)
		goto parent_err;
	return 0;

parent_err:
	device_unregister(&vdpasim_net_mgmtdev);
	return ret;
}

static void __exit vdpasim_net_exit(void)
{
	vdpa_mgmtdev_unregister(&mgmt_dev);
	device_unregister(&vdpasim_net_mgmtdev);
}

module_init(vdpasim_net_init);
module_exit(vdpasim_net_exit);

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
