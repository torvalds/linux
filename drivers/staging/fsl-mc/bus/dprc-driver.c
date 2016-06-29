/*
 * Freescale data path resource container (DPRC) driver
 *
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 * Author: German Rivera <German.Rivera@freescale.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include "../include/mc-private.h"
#include "../include/mc-sys.h"
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <linux/msi.h>
#include "dprc-cmd.h"

struct dprc_child_objs {
	int child_count;
	struct dprc_obj_desc *child_array;
};

static int __fsl_mc_device_remove_if_not_in_mc(struct device *dev, void *data)
{
	int i;
	struct dprc_child_objs *objs;
	struct fsl_mc_device *mc_dev;

	WARN_ON(!dev);
	WARN_ON(!data);
	mc_dev = to_fsl_mc_device(dev);
	objs = data;

	for (i = 0; i < objs->child_count; i++) {
		struct dprc_obj_desc *obj_desc = &objs->child_array[i];

		if (strlen(obj_desc->type) != 0 &&
		    FSL_MC_DEVICE_MATCH(mc_dev, obj_desc))
			break;
	}

	if (i == objs->child_count)
		fsl_mc_device_remove(mc_dev);

	return 0;
}

static int __fsl_mc_device_remove(struct device *dev, void *data)
{
	WARN_ON(!dev);
	WARN_ON(data);
	fsl_mc_device_remove(to_fsl_mc_device(dev));
	return 0;
}

/**
 * dprc_remove_devices - Removes devices for objects removed from a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 * @obj_desc_array: array of object descriptors for child objects currently
 * present in the DPRC in the MC.
 * @num_child_objects_in_mc: number of entries in obj_desc_array
 *
 * Synchronizes the state of the Linux bus driver with the actual state of
 * the MC by removing devices that represent MC objects that have
 * been dynamically removed in the physical DPRC.
 */
static void dprc_remove_devices(struct fsl_mc_device *mc_bus_dev,
				struct dprc_obj_desc *obj_desc_array,
				int num_child_objects_in_mc)
{
	if (num_child_objects_in_mc != 0) {
		/*
		 * Remove child objects that are in the DPRC in Linux,
		 * but not in the MC:
		 */
		struct dprc_child_objs objs;

		objs.child_count = num_child_objects_in_mc;
		objs.child_array = obj_desc_array;
		device_for_each_child(&mc_bus_dev->dev, &objs,
				      __fsl_mc_device_remove_if_not_in_mc);
	} else {
		/*
		 * There are no child objects for this DPRC in the MC.
		 * So, remove all the child devices from Linux:
		 */
		device_for_each_child(&mc_bus_dev->dev, NULL,
				      __fsl_mc_device_remove);
	}
}

static int __fsl_mc_device_match(struct device *dev, void *data)
{
	struct dprc_obj_desc *obj_desc = data;
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);

	return FSL_MC_DEVICE_MATCH(mc_dev, obj_desc);
}

static struct fsl_mc_device *fsl_mc_device_lookup(struct dprc_obj_desc
								*obj_desc,
						  struct fsl_mc_device
								*mc_bus_dev)
{
	struct device *dev;

	dev = device_find_child(&mc_bus_dev->dev, obj_desc,
				__fsl_mc_device_match);

	return dev ? to_fsl_mc_device(dev) : NULL;
}

/**
 * check_plugged_state_change - Check change in an MC object's plugged state
 *
 * @mc_dev: pointer to the fsl-mc device for a given MC object
 * @obj_desc: pointer to the MC object's descriptor in the MC
 *
 * If the plugged state has changed from unplugged to plugged, the fsl-mc
 * device is bound to the corresponding device driver.
 * If the plugged state has changed from plugged to unplugged, the fsl-mc
 * device is unbound from the corresponding device driver.
 */
static void check_plugged_state_change(struct fsl_mc_device *mc_dev,
				       struct dprc_obj_desc *obj_desc)
{
	int error;
	u32 plugged_flag_at_mc =
			obj_desc->state & DPRC_OBJ_STATE_PLUGGED;

	if (plugged_flag_at_mc !=
	    (mc_dev->obj_desc.state & DPRC_OBJ_STATE_PLUGGED)) {
		if (plugged_flag_at_mc) {
			mc_dev->obj_desc.state |= DPRC_OBJ_STATE_PLUGGED;
			error = device_attach(&mc_dev->dev);
			if (error < 0) {
				dev_err(&mc_dev->dev,
					"device_attach() failed: %d\n",
					error);
			}
		} else {
			mc_dev->obj_desc.state &= ~DPRC_OBJ_STATE_PLUGGED;
			device_release_driver(&mc_dev->dev);
		}
	}
}

/**
 * dprc_add_new_devices - Adds devices to the logical bus for a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 * @obj_desc_array: array of device descriptors for child devices currently
 * present in the physical DPRC.
 * @num_child_objects_in_mc: number of entries in obj_desc_array
 *
 * Synchronizes the state of the Linux bus driver with the actual
 * state of the MC by adding objects that have been newly discovered
 * in the physical DPRC.
 */
static void dprc_add_new_devices(struct fsl_mc_device *mc_bus_dev,
				 struct dprc_obj_desc *obj_desc_array,
				 int num_child_objects_in_mc)
{
	int error;
	int i;

	for (i = 0; i < num_child_objects_in_mc; i++) {
		struct fsl_mc_device *child_dev;
		struct dprc_obj_desc *obj_desc = &obj_desc_array[i];

		if (strlen(obj_desc->type) == 0)
			continue;

		/*
		 * Check if device is already known to Linux:
		 */
		child_dev = fsl_mc_device_lookup(obj_desc, mc_bus_dev);
		if (child_dev) {
			check_plugged_state_change(child_dev, obj_desc);
			continue;
		}

		error = fsl_mc_device_add(obj_desc, NULL, &mc_bus_dev->dev,
					  &child_dev);
		if (error < 0)
			continue;
	}
}

static void dprc_init_all_resource_pools(struct fsl_mc_device *mc_bus_dev)
{
	int pool_type;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);

	for (pool_type = 0; pool_type < FSL_MC_NUM_POOL_TYPES; pool_type++) {
		struct fsl_mc_resource_pool *res_pool =
		    &mc_bus->resource_pools[pool_type];

		res_pool->type = pool_type;
		res_pool->max_count = 0;
		res_pool->free_count = 0;
		res_pool->mc_bus = mc_bus;
		INIT_LIST_HEAD(&res_pool->free_list);
		mutex_init(&res_pool->mutex);
	}
}

static void dprc_cleanup_resource_pool(struct fsl_mc_device *mc_bus_dev,
				       enum fsl_mc_pool_type pool_type)
{
	struct fsl_mc_resource *resource;
	struct fsl_mc_resource *next;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);
	struct fsl_mc_resource_pool *res_pool =
					&mc_bus->resource_pools[pool_type];
	int free_count = 0;

	WARN_ON(res_pool->type != pool_type);
	WARN_ON(res_pool->free_count != res_pool->max_count);

	list_for_each_entry_safe(resource, next, &res_pool->free_list, node) {
		free_count++;
		WARN_ON(resource->type != res_pool->type);
		WARN_ON(resource->parent_pool != res_pool);
		devm_kfree(&mc_bus_dev->dev, resource);
	}

	WARN_ON(free_count != res_pool->free_count);
}

static void dprc_cleanup_all_resource_pools(struct fsl_mc_device *mc_bus_dev)
{
	int pool_type;

	for (pool_type = 0; pool_type < FSL_MC_NUM_POOL_TYPES; pool_type++)
		dprc_cleanup_resource_pool(mc_bus_dev, pool_type);
}

/**
 * dprc_scan_objects - Discover objects in a DPRC
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 * @total_irq_count: total number of IRQs needed by objects in the DPRC.
 *
 * Detects objects added and removed from a DPRC and synchronizes the
 * state of the Linux bus driver, MC by adding and removing
 * devices accordingly.
 * Two types of devices can be found in a DPRC: allocatable objects (e.g.,
 * dpbp, dpmcp) and non-allocatable devices (e.g., dprc, dpni).
 * All allocatable devices needed to be probed before all non-allocatable
 * devices, to ensure that device drivers for non-allocatable
 * devices can allocate any type of allocatable devices.
 * That is, we need to ensure that the corresponding resource pools are
 * populated before they can get allocation requests from probe callbacks
 * of the device drivers for the non-allocatable devices.
 */
int dprc_scan_objects(struct fsl_mc_device *mc_bus_dev,
		      unsigned int *total_irq_count)
{
	int num_child_objects;
	int dprc_get_obj_failures;
	int error;
	unsigned int irq_count = mc_bus_dev->obj_desc.irq_count;
	struct dprc_obj_desc *child_obj_desc_array = NULL;

	error = dprc_get_obj_count(mc_bus_dev->mc_io,
				   0,
				   mc_bus_dev->mc_handle,
				   &num_child_objects);
	if (error < 0) {
		dev_err(&mc_bus_dev->dev, "dprc_get_obj_count() failed: %d\n",
			error);
		return error;
	}

	if (num_child_objects != 0) {
		int i;

		child_obj_desc_array =
		    devm_kmalloc_array(&mc_bus_dev->dev, num_child_objects,
				       sizeof(*child_obj_desc_array),
				       GFP_KERNEL);
		if (!child_obj_desc_array)
			return -ENOMEM;

		/*
		 * Discover objects currently present in the physical DPRC:
		 */
		dprc_get_obj_failures = 0;
		for (i = 0; i < num_child_objects; i++) {
			struct dprc_obj_desc *obj_desc =
			    &child_obj_desc_array[i];

			error = dprc_get_obj(mc_bus_dev->mc_io,
					     0,
					     mc_bus_dev->mc_handle,
					     i, obj_desc);
			if (error < 0) {
				dev_err(&mc_bus_dev->dev,
					"dprc_get_obj(i=%d) failed: %d\n",
					i, error);
				/*
				 * Mark the obj entry as "invalid", by using the
				 * empty string as obj type:
				 */
				obj_desc->type[0] = '\0';
				obj_desc->id = error;
				dprc_get_obj_failures++;
				continue;
			}

			/*
			 * add a quirk for all versions of dpsec < 4.0...none
			 * are coherent regardless of what the MC reports.
			 */
			if ((strcmp(obj_desc->type, "dpseci") == 0) &&
			    (obj_desc->ver_major < 4))
				obj_desc->flags |=
					DPRC_OBJ_FLAG_NO_MEM_SHAREABILITY;

			irq_count += obj_desc->irq_count;
			dev_dbg(&mc_bus_dev->dev,
				"Discovered object: type %s, id %d\n",
				obj_desc->type, obj_desc->id);
		}

		if (dprc_get_obj_failures != 0) {
			dev_err(&mc_bus_dev->dev,
				"%d out of %d devices could not be retrieved\n",
				dprc_get_obj_failures, num_child_objects);
		}
	}

	*total_irq_count = irq_count;
	dprc_remove_devices(mc_bus_dev, child_obj_desc_array,
			    num_child_objects);

	dprc_add_new_devices(mc_bus_dev, child_obj_desc_array,
			     num_child_objects);

	if (child_obj_desc_array)
		devm_kfree(&mc_bus_dev->dev, child_obj_desc_array);

	return 0;
}
EXPORT_SYMBOL_GPL(dprc_scan_objects);

/**
 * dprc_scan_container - Scans a physical DPRC and synchronizes Linux bus state
 *
 * @mc_bus_dev: pointer to the fsl-mc device that represents a DPRC object
 *
 * Scans the physical DPRC and synchronizes the state of the Linux
 * bus driver with the actual state of the MC by adding and removing
 * devices as appropriate.
 */
int dprc_scan_container(struct fsl_mc_device *mc_bus_dev)
{
	int error;
	unsigned int irq_count;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_bus_dev);

	dprc_init_all_resource_pools(mc_bus_dev);

	/*
	 * Discover objects in the DPRC:
	 */
	mutex_lock(&mc_bus->scan_mutex);
	error = dprc_scan_objects(mc_bus_dev, &irq_count);
	mutex_unlock(&mc_bus->scan_mutex);
	if (error < 0)
		goto error;

	if (dev_get_msi_domain(&mc_bus_dev->dev) && !mc_bus->irq_resources) {
		if (irq_count > FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS) {
			dev_warn(&mc_bus_dev->dev,
				 "IRQs needed (%u) exceed IRQs preallocated (%u)\n",
				 irq_count, FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS);
		}

		error = fsl_mc_populate_irq_pool(
				mc_bus,
				FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS);
		if (error < 0)
			goto error;
	}

	return 0;
error:
	dprc_cleanup_all_resource_pools(mc_bus_dev);
	return error;
}
EXPORT_SYMBOL_GPL(dprc_scan_container);

/**
 * dprc_irq0_handler - Regular ISR for DPRC interrupt 0
 *
 * @irq: IRQ number of the interrupt being handled
 * @arg: Pointer to device structure
 */
static irqreturn_t dprc_irq0_handler(int irq_num, void *arg)
{
	return IRQ_WAKE_THREAD;
}

/**
 * dprc_irq0_handler_thread - Handler thread function for DPRC interrupt 0
 *
 * @irq: IRQ number of the interrupt being handled
 * @arg: Pointer to device structure
 */
static irqreturn_t dprc_irq0_handler_thread(int irq_num, void *arg)
{
	int error;
	u32 status;
	struct device *dev = arg;
	struct fsl_mc_device *mc_dev = to_fsl_mc_device(dev);
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_dev);
	struct fsl_mc_io *mc_io = mc_dev->mc_io;
	struct msi_desc *msi_desc = mc_dev->irqs[0]->msi_desc;

	dev_dbg(dev, "DPRC IRQ %d triggered on CPU %u\n",
		irq_num, smp_processor_id());

	if (WARN_ON(!(mc_dev->flags & FSL_MC_IS_DPRC)))
		return IRQ_HANDLED;

	mutex_lock(&mc_bus->scan_mutex);
	if (WARN_ON(!msi_desc || msi_desc->irq != (u32)irq_num))
		goto out;

	status = 0;
	error = dprc_get_irq_status(mc_io, 0, mc_dev->mc_handle, 0,
				    &status);
	if (error < 0) {
		dev_err(dev,
			"dprc_get_irq_status() failed: %d\n", error);
		goto out;
	}

	error = dprc_clear_irq_status(mc_io, 0, mc_dev->mc_handle, 0,
				      status);
	if (error < 0) {
		dev_err(dev,
			"dprc_clear_irq_status() failed: %d\n", error);
		goto out;
	}

	if (status & (DPRC_IRQ_EVENT_OBJ_ADDED |
		      DPRC_IRQ_EVENT_OBJ_REMOVED |
		      DPRC_IRQ_EVENT_CONTAINER_DESTROYED |
		      DPRC_IRQ_EVENT_OBJ_DESTROYED |
		      DPRC_IRQ_EVENT_OBJ_CREATED)) {
		unsigned int irq_count;

		error = dprc_scan_objects(mc_dev, &irq_count);
		if (error < 0) {
			/*
			 * If the error is -ENXIO, we ignore it, as it indicates
			 * that the object scan was aborted, as we detected that
			 * an object was removed from the DPRC in the MC, while
			 * we were scanning the DPRC.
			 */
			if (error != -ENXIO) {
				dev_err(dev, "dprc_scan_objects() failed: %d\n",
					error);
			}

			goto out;
		}

		if (irq_count > FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS) {
			dev_warn(dev,
				 "IRQs needed (%u) exceed IRQs preallocated (%u)\n",
				 irq_count, FSL_MC_IRQ_POOL_MAX_TOTAL_IRQS);
		}
	}

out:
	mutex_unlock(&mc_bus->scan_mutex);
	return IRQ_HANDLED;
}

/*
 * Disable and clear interrupt for a given DPRC object
 */
static int disable_dprc_irq(struct fsl_mc_device *mc_dev)
{
	int error;
	struct fsl_mc_io *mc_io = mc_dev->mc_io;

	WARN_ON(mc_dev->obj_desc.irq_count != 1);

	/*
	 * Disable generation of interrupt, while we configure it:
	 */
	error = dprc_set_irq_enable(mc_io, 0, mc_dev->mc_handle, 0, 0);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"Disabling DPRC IRQ failed: dprc_set_irq_enable() failed: %d\n",
			error);
		return error;
	}

	/*
	 * Disable all interrupt causes for the interrupt:
	 */
	error = dprc_set_irq_mask(mc_io, 0, mc_dev->mc_handle, 0, 0x0);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"Disabling DPRC IRQ failed: dprc_set_irq_mask() failed: %d\n",
			error);
		return error;
	}

	/*
	 * Clear any leftover interrupts:
	 */
	error = dprc_clear_irq_status(mc_io, 0, mc_dev->mc_handle, 0, ~0x0U);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"Disabling DPRC IRQ failed: dprc_clear_irq_status() failed: %d\n",
			error);
		return error;
	}

	return 0;
}

static int register_dprc_irq_handler(struct fsl_mc_device *mc_dev)
{
	int error;
	struct fsl_mc_device_irq *irq = mc_dev->irqs[0];

	WARN_ON(mc_dev->obj_desc.irq_count != 1);

	/*
	 * NOTE: devm_request_threaded_irq() invokes the device-specific
	 * function that programs the MSI physically in the device
	 */
	error = devm_request_threaded_irq(&mc_dev->dev,
					  irq->msi_desc->irq,
					  dprc_irq0_handler,
					  dprc_irq0_handler_thread,
					  IRQF_NO_SUSPEND | IRQF_ONESHOT,
					  "FSL MC DPRC irq0",
					  &mc_dev->dev);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"devm_request_threaded_irq() failed: %d\n",
			error);
		return error;
	}

	return 0;
}

static int enable_dprc_irq(struct fsl_mc_device *mc_dev)
{
	int error;

	/*
	 * Enable all interrupt causes for the interrupt:
	 */
	error = dprc_set_irq_mask(mc_dev->mc_io, 0, mc_dev->mc_handle, 0,
				  ~0x0u);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"Enabling DPRC IRQ failed: dprc_set_irq_mask() failed: %d\n",
			error);

		return error;
	}

	/*
	 * Enable generation of the interrupt:
	 */
	error = dprc_set_irq_enable(mc_dev->mc_io, 0, mc_dev->mc_handle, 0, 1);
	if (error < 0) {
		dev_err(&mc_dev->dev,
			"Enabling DPRC IRQ failed: dprc_set_irq_enable() failed: %d\n",
			error);

		return error;
	}

	return 0;
}

/*
 * Setup interrupt for a given DPRC device
 */
static int dprc_setup_irq(struct fsl_mc_device *mc_dev)
{
	int error;

	error = fsl_mc_allocate_irqs(mc_dev);
	if (error < 0)
		return error;

	error = disable_dprc_irq(mc_dev);
	if (error < 0)
		goto error_free_irqs;

	error = register_dprc_irq_handler(mc_dev);
	if (error < 0)
		goto error_free_irqs;

	error = enable_dprc_irq(mc_dev);
	if (error < 0)
		goto error_free_irqs;

	return 0;

error_free_irqs:
	fsl_mc_free_irqs(mc_dev);
	return error;
}

/**
 * dprc_probe - callback invoked when a DPRC is being bound to this driver
 *
 * @mc_dev: Pointer to fsl-mc device representing a DPRC
 *
 * It opens the physical DPRC in the MC.
 * It scans the DPRC to discover the MC objects contained in it.
 * It creates the interrupt pool for the MC bus associated with the DPRC.
 * It configures the interrupts for the DPRC device itself.
 */
static int dprc_probe(struct fsl_mc_device *mc_dev)
{
	int error;
	size_t region_size;
	struct device *parent_dev = mc_dev->dev.parent;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_dev);
	bool mc_io_created = false;
	bool msi_domain_set = false;

	if (WARN_ON(strcmp(mc_dev->obj_desc.type, "dprc") != 0))
		return -EINVAL;

	if (WARN_ON(dev_get_msi_domain(&mc_dev->dev)))
		return -EINVAL;

	if (!mc_dev->mc_io) {
		/*
		 * This is a child DPRC:
		 */
		if (WARN_ON(!dev_is_fsl_mc(parent_dev)))
			return -EINVAL;

		if (WARN_ON(mc_dev->obj_desc.region_count == 0))
			return -EINVAL;

		region_size = mc_dev->regions[0].end -
			      mc_dev->regions[0].start + 1;

		error = fsl_create_mc_io(&mc_dev->dev,
					 mc_dev->regions[0].start,
					 region_size,
					 NULL,
					 FSL_MC_IO_ATOMIC_CONTEXT_PORTAL,
					 &mc_dev->mc_io);
		if (error < 0)
			return error;

		mc_io_created = true;

		/*
		 * Inherit parent MSI domain:
		 */
		dev_set_msi_domain(&mc_dev->dev,
				   dev_get_msi_domain(parent_dev));
		msi_domain_set = true;
	} else {
		/*
		 * This is a root DPRC
		 */
		struct irq_domain *mc_msi_domain;

		if (WARN_ON(dev_is_fsl_mc(parent_dev)))
			return -EINVAL;

		error = fsl_mc_find_msi_domain(parent_dev,
					       &mc_msi_domain);
		if (error < 0) {
			dev_warn(&mc_dev->dev,
				 "WARNING: MC bus without interrupt support\n");
		} else {
			dev_set_msi_domain(&mc_dev->dev, mc_msi_domain);
			msi_domain_set = true;
		}
	}

	error = dprc_open(mc_dev->mc_io, 0, mc_dev->obj_desc.id,
			  &mc_dev->mc_handle);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dprc_open() failed: %d\n", error);
		goto error_cleanup_msi_domain;
	}

	error = dprc_get_attributes(mc_dev->mc_io, 0, mc_dev->mc_handle,
				    &mc_bus->dprc_attr);
	if (error < 0) {
		dev_err(&mc_dev->dev, "dprc_get_attributes() failed: %d\n",
			error);
		goto error_cleanup_open;
	}

	if (mc_bus->dprc_attr.version.major < DPRC_MIN_VER_MAJOR ||
	   (mc_bus->dprc_attr.version.major == DPRC_MIN_VER_MAJOR &&
	    mc_bus->dprc_attr.version.minor < DPRC_MIN_VER_MINOR)) {
		dev_err(&mc_dev->dev,
			"ERROR: DPRC version %d.%d not supported\n",
			mc_bus->dprc_attr.version.major,
			mc_bus->dprc_attr.version.minor);
		error = -ENOTSUPP;
		goto error_cleanup_open;
	}

	mutex_init(&mc_bus->scan_mutex);

	/*
	 * Discover MC objects in DPRC object:
	 */
	error = dprc_scan_container(mc_dev);
	if (error < 0)
		goto error_cleanup_open;

	/*
	 * Configure interrupt for the DPRC object associated with this MC bus:
	 */
	error = dprc_setup_irq(mc_dev);
	if (error < 0)
		goto error_cleanup_open;

	dev_info(&mc_dev->dev, "DPRC device bound to driver");
	return 0;

error_cleanup_open:
	(void)dprc_close(mc_dev->mc_io, 0, mc_dev->mc_handle);

error_cleanup_msi_domain:
	if (msi_domain_set)
		dev_set_msi_domain(&mc_dev->dev, NULL);

	if (mc_io_created) {
		fsl_destroy_mc_io(mc_dev->mc_io);
		mc_dev->mc_io = NULL;
	}

	return error;
}

/*
 * Tear down interrupt for a given DPRC object
 */
static void dprc_teardown_irq(struct fsl_mc_device *mc_dev)
{
	struct fsl_mc_device_irq *irq = mc_dev->irqs[0];

	(void)disable_dprc_irq(mc_dev);

	devm_free_irq(&mc_dev->dev, irq->msi_desc->irq, &mc_dev->dev);

	fsl_mc_free_irqs(mc_dev);
}

/**
 * dprc_remove - callback invoked when a DPRC is being unbound from this driver
 *
 * @mc_dev: Pointer to fsl-mc device representing the DPRC
 *
 * It removes the DPRC's child objects from Linux (not from the MC) and
 * closes the DPRC device in the MC.
 * It tears down the interrupts that were configured for the DPRC device.
 * It destroys the interrupt pool associated with this MC bus.
 */
static int dprc_remove(struct fsl_mc_device *mc_dev)
{
	int error;
	struct fsl_mc_bus *mc_bus = to_fsl_mc_bus(mc_dev);

	if (WARN_ON(strcmp(mc_dev->obj_desc.type, "dprc") != 0))
		return -EINVAL;
	if (WARN_ON(!mc_dev->mc_io))
		return -EINVAL;

	if (WARN_ON(!mc_bus->irq_resources))
		return -EINVAL;

	if (dev_get_msi_domain(&mc_dev->dev))
		dprc_teardown_irq(mc_dev);

	device_for_each_child(&mc_dev->dev, NULL, __fsl_mc_device_remove);

	if (dev_get_msi_domain(&mc_dev->dev)) {
		fsl_mc_cleanup_irq_pool(mc_bus);
		dev_set_msi_domain(&mc_dev->dev, NULL);
	}

	dprc_cleanup_all_resource_pools(mc_dev);

	error = dprc_close(mc_dev->mc_io, 0, mc_dev->mc_handle);
	if (error < 0)
		dev_err(&mc_dev->dev, "dprc_close() failed: %d\n", error);

	if (!fsl_mc_is_root_dprc(&mc_dev->dev)) {
		fsl_destroy_mc_io(mc_dev->mc_io);
		mc_dev->mc_io = NULL;
	}

	dev_info(&mc_dev->dev, "DPRC device unbound from driver");
	return 0;
}

static const struct fsl_mc_device_id match_id_table[] = {
	{
	 .vendor = FSL_MC_VENDOR_FREESCALE,
	 .obj_type = "dprc"},
	{.vendor = 0x0},
};

static struct fsl_mc_driver dprc_driver = {
	.driver = {
		   .name = FSL_MC_DPRC_DRIVER_NAME,
		   .owner = THIS_MODULE,
		   .pm = NULL,
		   },
	.match_id_table = match_id_table,
	.probe = dprc_probe,
	.remove = dprc_remove,
};

int __init dprc_driver_init(void)
{
	return fsl_mc_driver_register(&dprc_driver);
}

void dprc_driver_exit(void)
{
	fsl_mc_driver_unregister(&dprc_driver);
}
