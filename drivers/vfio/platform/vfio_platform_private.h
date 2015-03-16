/*
 * Copyright (C) 2013 - Virtual Open Systems
 * Author: Antonios Motakis <a.motakis@virtualopensystems.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef VFIO_PLATFORM_PRIVATE_H
#define VFIO_PLATFORM_PRIVATE_H

#include <linux/types.h>
#include <linux/interrupt.h>

#define VFIO_PLATFORM_OFFSET_SHIFT   40
#define VFIO_PLATFORM_OFFSET_MASK (((u64)(1) << VFIO_PLATFORM_OFFSET_SHIFT) - 1)

#define VFIO_PLATFORM_OFFSET_TO_INDEX(off)	\
	(off >> VFIO_PLATFORM_OFFSET_SHIFT)

#define VFIO_PLATFORM_INDEX_TO_OFFSET(index)	\
	((u64)(index) << VFIO_PLATFORM_OFFSET_SHIFT)

struct vfio_platform_irq {
	u32			flags;
	u32			count;
	int			hwirq;
	char			*name;
	struct eventfd_ctx	*trigger;
	bool			masked;
	spinlock_t		lock;
};

struct vfio_platform_region {
	u64			addr;
	resource_size_t		size;
	u32			flags;
	u32			type;
#define VFIO_PLATFORM_REGION_TYPE_MMIO	1
#define VFIO_PLATFORM_REGION_TYPE_PIO	2
	void __iomem		*ioaddr;
};

struct vfio_platform_device {
	struct vfio_platform_region	*regions;
	u32				num_regions;
	struct vfio_platform_irq	*irqs;
	u32				num_irqs;
	int				refcnt;
	struct mutex			igate;

	/*
	 * These fields should be filled by the bus specific binder
	 */
	void		*opaque;
	const char	*name;
	uint32_t	flags;
	/* callbacks to discover device resources */
	struct resource*
		(*get_resource)(struct vfio_platform_device *vdev, int i);
	int	(*get_irq)(struct vfio_platform_device *vdev, int i);
};

extern int vfio_platform_probe_common(struct vfio_platform_device *vdev,
				      struct device *dev);
extern struct vfio_platform_device *vfio_platform_remove_common
				     (struct device *dev);

extern int vfio_platform_irq_init(struct vfio_platform_device *vdev);
extern void vfio_platform_irq_cleanup(struct vfio_platform_device *vdev);

extern int vfio_platform_set_irqs_ioctl(struct vfio_platform_device *vdev,
					uint32_t flags, unsigned index,
					unsigned start, unsigned count,
					void *data);

#endif /* VFIO_PLATFORM_PRIVATE_H */
