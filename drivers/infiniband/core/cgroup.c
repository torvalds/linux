/*
 * Copyright (C) 2016 Parav Pandit <pandit.parav@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include "core_priv.h"

/**
 * ib_device_register_rdmacg - register with rdma cgroup.
 * @device: device to register to participate in resource
 *          accounting by rdma cgroup.
 *
 * Register with the rdma cgroup. Should be called before
 * exposing rdma device to user space applications to avoid
 * resource accounting leak.
 */
void ib_device_register_rdmacg(struct ib_device *device)
{
	device->cg_device.name = device->name;
	rdmacg_register_device(&device->cg_device);
}

/**
 * ib_device_unregister_rdmacg - unregister with rdma cgroup.
 * @device: device to unregister.
 *
 * Unregister with the rdma cgroup. Should be called after
 * all the resources are deallocated, and after a stage when any
 * other resource allocation by user application cannot be done
 * for this device to avoid any leak in accounting.
 */
void ib_device_unregister_rdmacg(struct ib_device *device)
{
	rdmacg_unregister_device(&device->cg_device);
}

int ib_rdmacg_try_charge(struct ib_rdmacg_object *cg_obj,
			 struct ib_device *device,
			 enum rdmacg_resource_type resource_index)
{
	return rdmacg_try_charge(&cg_obj->cg, &device->cg_device,
				 resource_index);
}
EXPORT_SYMBOL(ib_rdmacg_try_charge);

void ib_rdmacg_uncharge(struct ib_rdmacg_object *cg_obj,
			struct ib_device *device,
			enum rdmacg_resource_type resource_index)
{
	rdmacg_uncharge(cg_obj->cg, &device->cg_device,
			resource_index);
}
EXPORT_SYMBOL(ib_rdmacg_uncharge);
