/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef VFIO_CDX_PRIVATE_H
#define VFIO_CDX_PRIVATE_H

#define VFIO_CDX_OFFSET_SHIFT    40

static inline u64 vfio_cdx_index_to_offset(u32 index)
{
	return ((u64)(index) << VFIO_CDX_OFFSET_SHIFT);
}

struct vfio_cdx_region {
	u32			flags;
	u32			type;
	u64			addr;
	resource_size_t		size;
};

struct vfio_cdx_device {
	struct vfio_device	vdev;
	struct vfio_cdx_region	*regions;
};

#endif /* VFIO_CDX_PRIVATE_H */
