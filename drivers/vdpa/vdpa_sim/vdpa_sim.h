/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2020, Red Hat Inc. All rights reserved.
 */

#ifndef _VDPA_SIM_H
#define _VDPA_SIM_H

#include <linux/iova.h>
#include <linux/vringh.h>
#include <linux/vdpa.h>
#include <linux/virtio_byteorder.h>
#include <linux/vhost_iotlb.h>
#include <uapi/linux/virtio_config.h>

#define VDPASIM_FEATURES	((1ULL << VIRTIO_F_ANY_LAYOUT) | \
				 (1ULL << VIRTIO_F_VERSION_1)  | \
				 (1ULL << VIRTIO_F_ACCESS_PLATFORM))

struct vdpasim;

struct vdpasim_virtqueue {
	struct vringh vring;
	struct vringh_kiov in_iov;
	struct vringh_kiov out_iov;
	unsigned short head;
	bool ready;
	u64 desc_addr;
	u64 device_addr;
	u64 driver_addr;
	u32 num;
	void *private;
	irqreturn_t (*cb)(void *data);
};

struct vdpasim_dev_attr {
	struct vdpa_mgmt_dev *mgmt_dev;
	const char *name;
	u64 supported_features;
	size_t alloc_size;
	size_t config_size;
	size_t buffer_size;
	int nvqs;
	u32 id;
	u32 ngroups;
	u32 nas;

	work_func_t work_fn;
	void (*get_config)(struct vdpasim *vdpasim, void *config);
	void (*set_config)(struct vdpasim *vdpasim, const void *config);
	int (*get_stats)(struct vdpasim *vdpasim, u16 idx,
			 struct sk_buff *msg,
			 struct netlink_ext_ack *extack);
};

/* State of each vdpasim device */
struct vdpasim {
	struct vdpa_device vdpa;
	struct vdpasim_virtqueue *vqs;
	struct work_struct work;
	struct vdpasim_dev_attr dev_attr;
	/* spinlock to synchronize virtqueue state */
	spinlock_t lock;
	/* virtio config according to device type */
	void *config;
	struct vhost_iotlb *iommu;
	bool *iommu_pt;
	void *buffer;
	u32 status;
	u32 generation;
	u64 features;
	u32 groups;
	bool running;
	bool pending_kick;
	/* spinlock to synchronize iommu table */
	spinlock_t iommu_lock;
};

struct vdpasim *vdpasim_create(struct vdpasim_dev_attr *attr,
			       const struct vdpa_dev_set_config *config);

/* TODO: cross-endian support */
static inline bool vdpasim_is_little_endian(struct vdpasim *vdpasim)
{
	return virtio_legacy_is_little_endian() ||
		(vdpasim->features & (1ULL << VIRTIO_F_VERSION_1));
}

static inline u16 vdpasim16_to_cpu(struct vdpasim *vdpasim, __virtio16 val)
{
	return __virtio16_to_cpu(vdpasim_is_little_endian(vdpasim), val);
}

static inline __virtio16 cpu_to_vdpasim16(struct vdpasim *vdpasim, u16 val)
{
	return __cpu_to_virtio16(vdpasim_is_little_endian(vdpasim), val);
}

static inline u32 vdpasim32_to_cpu(struct vdpasim *vdpasim, __virtio32 val)
{
	return __virtio32_to_cpu(vdpasim_is_little_endian(vdpasim), val);
}

static inline __virtio32 cpu_to_vdpasim32(struct vdpasim *vdpasim, u32 val)
{
	return __cpu_to_virtio32(vdpasim_is_little_endian(vdpasim), val);
}

static inline u64 vdpasim64_to_cpu(struct vdpasim *vdpasim, __virtio64 val)
{
	return __virtio64_to_cpu(vdpasim_is_little_endian(vdpasim), val);
}

static inline __virtio64 cpu_to_vdpasim64(struct vdpasim *vdpasim, u64 val)
{
	return __cpu_to_virtio64(vdpasim_is_little_endian(vdpasim), val);
}

#endif
