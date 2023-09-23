/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Ideas On Board Oy
 */

#ifndef _VCHIQ_DEVICE_H
#define _VCHIQ_DEVICE_H

#include <linux/device.h>

struct vchiq_device {
	struct device dev;
};

struct vchiq_driver {
	int		(*probe)(struct vchiq_device *device);
	void		(*remove)(struct vchiq_device *device);
	int		(*resume)(struct vchiq_device *device);
	int		(*suspend)(struct vchiq_device *device,
				   pm_message_t state);
	struct device_driver driver;
};

static inline struct vchiq_device *to_vchiq_device(struct device *d)
{
	return container_of(d, struct vchiq_device, dev);
}

static inline struct vchiq_driver *to_vchiq_driver(struct device_driver *d)
{
	return container_of(d, struct vchiq_driver, driver);
}

extern struct bus_type vchiq_bus_type;

struct vchiq_device *
vchiq_device_register(struct device *parent, const char *name);
void vchiq_device_unregister(struct vchiq_device *dev);

int vchiq_driver_register(struct vchiq_driver *vchiq_drv);
void vchiq_driver_unregister(struct vchiq_driver *vchiq_drv);

/**
 * module_vchiq_driver() - Helper macro for registering a vchiq driver
 * @__vchiq_driver: vchiq driver struct
 *
 * Helper macro for vchiq drivers which do not do anything special in
 * module init/exit. This eliminates a lot of boilerplate. Each module may only
 * use this macro once, and calling it replaces module_init() and module_exit()
 */
#define module_vchiq_driver(__vchiq_driver) \
	module_driver(__vchiq_driver, vchiq_driver_register, vchiq_driver_unregister)

#endif /* _VCHIQ_DEVICE_H */
