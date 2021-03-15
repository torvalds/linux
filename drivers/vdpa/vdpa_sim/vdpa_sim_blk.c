// SPDX-License-Identifier: GPL-2.0-only
/*
 * VDPA simulator for block device.
 *
 * Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/blkdev.h>
#include <linux/vringh.h>
#include <linux/vdpa.h>
#include <uapi/linux/virtio_blk.h>

#include "vdpa_sim.h"

#define DRV_VERSION  "0.1"
#define DRV_AUTHOR   "Max Gurtovoy <mgurtovoy@nvidia.com>"
#define DRV_DESC     "vDPA Device Simulator for block device"
#define DRV_LICENSE  "GPL v2"

#define VDPASIM_BLK_FEATURES	(VDPASIM_FEATURES | \
				 (1ULL << VIRTIO_BLK_F_SIZE_MAX) | \
				 (1ULL << VIRTIO_BLK_F_SEG_MAX)  | \
				 (1ULL << VIRTIO_BLK_F_BLK_SIZE) | \
				 (1ULL << VIRTIO_BLK_F_TOPOLOGY) | \
				 (1ULL << VIRTIO_BLK_F_MQ))

#define VDPASIM_BLK_CAPACITY	0x40000
#define VDPASIM_BLK_SIZE_MAX	0x1000
#define VDPASIM_BLK_SEG_MAX	32
#define VDPASIM_BLK_VQ_NUM	1

static struct vdpasim *vdpasim_blk_dev;

static void vdpasim_blk_work(struct work_struct *work)
{
	struct vdpasim *vdpasim = container_of(work, struct vdpasim, work);
	u8 status = VIRTIO_BLK_S_OK;
	int i;

	spin_lock(&vdpasim->lock);

	if (!(vdpasim->status & VIRTIO_CONFIG_S_DRIVER_OK))
		goto out;

	for (i = 0; i < VDPASIM_BLK_VQ_NUM; i++) {
		struct vdpasim_virtqueue *vq = &vdpasim->vqs[i];

		if (!vq->ready)
			continue;

		while (vringh_getdesc_iotlb(&vq->vring, &vq->out_iov,
					    &vq->in_iov, &vq->head,
					    GFP_ATOMIC) > 0) {
			int write;

			vq->in_iov.i = vq->in_iov.used - 1;
			write = vringh_iov_push_iotlb(&vq->vring, &vq->in_iov,
						      &status, 1);
			if (write <= 0)
				break;

			/* Make sure data is wrote before advancing index */
			smp_wmb();

			vringh_complete_iotlb(&vq->vring, vq->head, write);

			/* Make sure used is visible before rasing the interrupt. */
			smp_wmb();

			local_bh_disable();
			if (vringh_need_notify_iotlb(&vq->vring) > 0)
				vringh_notify(&vq->vring);
			local_bh_enable();
		}
	}
out:
	spin_unlock(&vdpasim->lock);
}

static void vdpasim_blk_get_config(struct vdpasim *vdpasim, void *config)
{
	struct virtio_blk_config *blk_config = config;

	memset(config, 0, sizeof(struct virtio_blk_config));

	blk_config->capacity = cpu_to_vdpasim64(vdpasim, VDPASIM_BLK_CAPACITY);
	blk_config->size_max = cpu_to_vdpasim32(vdpasim, VDPASIM_BLK_SIZE_MAX);
	blk_config->seg_max = cpu_to_vdpasim32(vdpasim, VDPASIM_BLK_SEG_MAX);
	blk_config->num_queues = cpu_to_vdpasim16(vdpasim, VDPASIM_BLK_VQ_NUM);
	blk_config->min_io_size = cpu_to_vdpasim16(vdpasim, 1);
	blk_config->opt_io_size = cpu_to_vdpasim32(vdpasim, 1);
	blk_config->blk_size = cpu_to_vdpasim32(vdpasim, SECTOR_SIZE);
}

static int __init vdpasim_blk_init(void)
{
	struct vdpasim_dev_attr dev_attr = {};
	int ret;

	dev_attr.id = VIRTIO_ID_BLOCK;
	dev_attr.supported_features = VDPASIM_BLK_FEATURES;
	dev_attr.nvqs = VDPASIM_BLK_VQ_NUM;
	dev_attr.config_size = sizeof(struct virtio_blk_config);
	dev_attr.get_config = vdpasim_blk_get_config;
	dev_attr.work_fn = vdpasim_blk_work;
	dev_attr.buffer_size = PAGE_SIZE;

	vdpasim_blk_dev = vdpasim_create(&dev_attr);
	if (IS_ERR(vdpasim_blk_dev)) {
		ret = PTR_ERR(vdpasim_blk_dev);
		goto out;
	}

	ret = vdpa_register_device(&vdpasim_blk_dev->vdpa, VDPASIM_BLK_VQ_NUM);
	if (ret)
		goto put_dev;

	return 0;

put_dev:
	put_device(&vdpasim_blk_dev->vdpa.dev);
out:
	return ret;
}

static void __exit vdpasim_blk_exit(void)
{
	struct vdpa_device *vdpa = &vdpasim_blk_dev->vdpa;

	vdpa_unregister_device(vdpa);
}

module_init(vdpasim_blk_init)
module_exit(vdpasim_blk_exit)

MODULE_VERSION(DRV_VERSION);
MODULE_LICENSE(DRV_LICENSE);
MODULE_AUTHOR(DRV_AUTHOR);
MODULE_DESCRIPTION(DRV_DESC);
