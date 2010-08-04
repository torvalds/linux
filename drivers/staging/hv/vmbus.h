/*
 *
 * Copyright (c) 2009, Microsoft Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307 USA.
 *
 * Authors:
 *   Haiyang Zhang <haiyangz@microsoft.com>
 *   Hank Janssen  <hjanssen@microsoft.com>
 *
 */


#ifndef _VMBUS_H_
#define _VMBUS_H_

#include <linux/device.h>
#include "vmbus_api.h"

struct driver_context {
	struct hv_guid class_id;

	struct device_driver driver;

	/*
	 * Use these methods instead of the struct device_driver so 2.6 kernel
	 * stops complaining
	 * TODO - fix this!
	 */
	int (*probe)(struct device *);
	int (*remove)(struct device *);
	void (*shutdown)(struct device *);
};

struct vm_device {
	struct work_struct probe_failed_work_item;
	struct hv_guid class_id;
	struct hv_guid device_id;
	int probe_error;
	struct hv_device device_obj;
	struct device device;
};

static inline struct vm_device *to_vm_device(struct hv_device *d)
{
	return container_of(d, struct vm_device, device_obj);
}

static inline struct vm_device *device_to_vm_device(struct device *d)
{
	return container_of(d, struct vm_device, device);
}

static inline struct driver_context *driver_to_driver_context(struct device_driver *d)
{
	return container_of(d, struct driver_context, driver);
}


/* Vmbus interface */

int vmbus_child_driver_register(struct driver_context *driver_ctx);
void vmbus_child_driver_unregister(struct driver_context *driver_ctx);
void vmbus_get_interface(struct vmbus_channel_interface *interface);

extern struct completion hv_channel_ready;

#endif /* _VMBUS_H_ */
