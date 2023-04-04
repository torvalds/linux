/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HAB_VIRTIO_H
#define __HAB_VIRTIO_H

#include "hab.h"

enum {
	HAB_PCHAN_TX_VQ = 0, /* receive data from gvm */
	HAB_PCHAN_RX_VQ, /* send data to gvm */
	HAB_PCHAN_VQ_MAX,
};

/* cross link between probe and comm-dev-alloc */
struct vq_pchan {
	uint32_t mmid;
	struct physical_channel *pchan;
	struct virtio_hab *vhab;

	unsigned int index[HAB_PCHAN_VQ_MAX]; /* vring index */
	struct virtqueue *vq[HAB_PCHAN_VQ_MAX];
	spinlock_t lock[HAB_PCHAN_VQ_MAX]; /* per pchan lock */

	wait_queue_head_t out_wq;
	struct tasklet_struct task; /* for rxq only */

	void *s_pool;
	struct list_head s_list; /* small buffer available out list */
	int s_cnt;

	void *m_pool;
	struct list_head m_list; /* medium buffer available out list */
	int m_cnt;

	void *l_pool;
	struct list_head l_list; /* large buffer available out list */
	int l_cnt;

	void *in_pool;
	struct list_head in_list; /* only used for init then stored in vqs */
	int in_cnt;

	void *read_data; /* recved buf should be one of the in bufs */
	size_t read_size;
	int read_offset;

	bool pchan_ready;
};

typedef void (*vq_callback)(struct virtqueue *);

struct virtio_hab {
	struct virtio_device *vdev; /* the actual virtio device probed */

	uint32_t mmid_start; /* starting mmid for this virthab */
	int mmid_range; /* total mmid used in this virthab, it might cross mmid groups */

	/* in case vqs are not start from zero to support all the needs of one
	 * virtio device, and it always starts after "other" vqs
	 */
	int vqs_offset;
	struct virtqueue **vqs; /* holds total # of vqs for all the pchans. 2 vqs per pchan */
	vq_callback_t **cbs; /* each vqs callback */
	char **names; /* each vqs' names */

	struct vq_pchan *vqpchans; /* total # of pchans */

	spinlock_t mlock; /* master lock for all the pchans */
	bool ready; /* overall device ready flag */

	struct list_head node; /* list of all probed virtio hab */
};

/*
 * this commdev has two parts, the pchan for hab driver created in commdev alloc,
 * and, virtio dev and vqs created during virtio probe.
 * commdev might happen earlier than virtio probe
 * one kind of hab driver for one kind of virtio device. within this one pair
 * there is one list/array of pchans/commdevs
 */
struct virtio_pchan_link {
	uint32_t mmid;
	struct physical_channel *pchan; /* link back to hab driver */
	struct vq_pchan *vpc; /* link back to the virtio probe result */
	struct virtio_hab *vhab; /* this is initialized during virtio probe */
};

#ifdef CONFIG_MSM_VIRTIO_HAB
int virthab_queue_inbufs(struct virtio_hab *vh, int alloc);

int virthab_alloc(struct virtio_device *vdev, struct virtio_hab **pvh,
			uint32_t mmid_start, int mmid_range);

int virthab_init_vqs_pre(struct virtio_hab *vh);

int virthab_init_vqs_post(struct virtio_hab *vh);

struct virtio_device *virthab_get_vdev(int32_t mmid);
#else
int virthab_queue_inbufs(struct virtio_hab *vh, int alloc)
{
	return -ENODEV;
}

int virthab_alloc(struct virtio_device *vdev, struct virtio_hab **pvh,
			uint32_t mmid_start, int mmid_range)
{
	return -ENODEV;
}

int virthab_init_vqs_pre(struct virtio_hab *vh)
{
	return -ENODEV;
}

int virthab_init_vqs_post(struct virtio_hab *vh)
{
	return -ENODEV;
}

struct virtio_device *virthab_get_vdev(int32_t mmid)
{
	return NULL;
}
#endif

#endif /* __HAB_VIRTIO_H */
