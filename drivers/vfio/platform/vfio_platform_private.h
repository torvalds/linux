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

struct vfio_platform_device {
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

#endif /* VFIO_PLATFORM_PRIVATE_H */
