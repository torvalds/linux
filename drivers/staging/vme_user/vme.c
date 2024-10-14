// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * VME Bridge Framework
 *
 * Author: Martyn Welch <martyn.welch@ge.com>
 * Copyright 2008 GE Intelligent Platforms Embedded Systems, Inc.
 *
 * Based on work by Tom Armistead and Ajit Prem
 * Copyright 2004 Motorola Inc.
 */

#include <linux/init.h>
#include <linux/export.h>
#include <linux/mm.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/poll.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/pagemap.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/syscalls.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include "vme.h"
#include "vme_bridge.h"

/* Bitmask and list of registered buses both protected by common mutex */
static unsigned int vme_bus_numbers;
static LIST_HEAD(vme_bus_list);
static DEFINE_MUTEX(vme_buses_lock);

static int __init vme_init(void);

static struct vme_dev *dev_to_vme_dev(struct device *dev)
{
	return container_of(dev, struct vme_dev, dev);
}

/*
 * Find the bridge that the resource is associated with.
 */
static struct vme_bridge *find_bridge(struct vme_resource *resource)
{
	/* Get list to search */
	switch (resource->type) {
	case VME_MASTER:
		return list_entry(resource->entry, struct vme_master_resource,
			list)->parent;
	case VME_SLAVE:
		return list_entry(resource->entry, struct vme_slave_resource,
			list)->parent;
	case VME_DMA:
		return list_entry(resource->entry, struct vme_dma_resource,
			list)->parent;
	case VME_LM:
		return list_entry(resource->entry, struct vme_lm_resource,
			list)->parent;
	default:
		return NULL;
	}
}

/**
 * vme_alloc_consistent - Allocate contiguous memory.
 * @resource: Pointer to VME resource.
 * @size: Size of allocation required.
 * @dma: Pointer to variable to store physical address of allocation.
 *
 * Allocate a contiguous block of memory for use by the driver. This is used to
 * create the buffers for the slave windows.
 *
 * Return: Virtual address of allocation on success, NULL on failure.
 */
void *vme_alloc_consistent(struct vme_resource *resource, size_t size,
			   dma_addr_t *dma)
{
	struct vme_bridge *bridge = find_bridge(resource);

	if (!bridge->alloc_consistent) {
		dev_err(bridge->parent,
			"alloc_consistent not supported by bridge %s\n",
			bridge->name);
		return NULL;
	}

	return bridge->alloc_consistent(bridge->parent, size, dma);
}
EXPORT_SYMBOL(vme_alloc_consistent);

/**
 * vme_free_consistent - Free previously allocated memory.
 * @resource: Pointer to VME resource.
 * @size: Size of allocation to free.
 * @vaddr: Virtual address of allocation.
 * @dma: Physical address of allocation.
 *
 * Free previously allocated block of contiguous memory.
 */
void vme_free_consistent(struct vme_resource *resource, size_t size,
			 void *vaddr, dma_addr_t dma)
{
	struct vme_bridge *bridge = find_bridge(resource);

	if (!bridge->free_consistent) {
		dev_err(bridge->parent,
			"free_consistent not supported by bridge %s\n",
			bridge->name);
		return;
	}

	bridge->free_consistent(bridge->parent, size, vaddr, dma);
}
EXPORT_SYMBOL(vme_free_consistent);

/**
 * vme_get_size - Helper function returning size of a VME window
 * @resource: Pointer to VME slave or master resource.
 *
 * Determine the size of the VME window provided. This is a helper
 * function, wrappering the call to vme_master_get or vme_slave_get
 * depending on the type of window resource handed to it.
 *
 * Return: Size of the window on success, zero on failure.
 */
size_t vme_get_size(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	int enabled, retval;
	unsigned long long base, size;
	dma_addr_t buf_base;
	u32 aspace, cycle, dwidth;

	switch (resource->type) {
	case VME_MASTER:
		retval = vme_master_get(resource, &enabled, &base, &size,
					&aspace, &cycle, &dwidth);
		if (retval)
			return 0;

		return size;
	case VME_SLAVE:
		retval = vme_slave_get(resource, &enabled, &base, &size,
				       &buf_base, &aspace, &cycle);
		if (retval)
			return 0;

		return size;
	case VME_DMA:
		return 0;
	default:
		dev_err(bridge->parent, "Unknown resource type\n");
		return 0;
	}
}
EXPORT_SYMBOL(vme_get_size);

int vme_check_window(struct vme_bridge *bridge, u32 aspace,
		     unsigned long long vme_base, unsigned long long size)
{
	int retval = 0;

	if (vme_base + size < size)
		return -EINVAL;

	switch (aspace) {
	case VME_A16:
		if (vme_base + size > VME_A16_MAX)
			retval = -EFAULT;
		break;
	case VME_A24:
		if (vme_base + size > VME_A24_MAX)
			retval = -EFAULT;
		break;
	case VME_A32:
		if (vme_base + size > VME_A32_MAX)
			retval = -EFAULT;
		break;
	case VME_A64:
		/* The VME_A64_MAX limit is actually U64_MAX + 1 */
		break;
	case VME_CRCSR:
		if (vme_base + size > VME_CRCSR_MAX)
			retval = -EFAULT;
		break;
	case VME_USER1:
	case VME_USER2:
	case VME_USER3:
	case VME_USER4:
		/* User Defined */
		break;
	default:
		dev_err(bridge->parent, "Invalid address space\n");
		retval = -EINVAL;
		break;
	}

	return retval;
}
EXPORT_SYMBOL(vme_check_window);

static u32 vme_get_aspace(int am)
{
	switch (am) {
	case 0x29:
	case 0x2D:
		return VME_A16;
	case 0x38:
	case 0x39:
	case 0x3A:
	case 0x3B:
	case 0x3C:
	case 0x3D:
	case 0x3E:
	case 0x3F:
		return VME_A24;
	case 0x8:
	case 0x9:
	case 0xA:
	case 0xB:
	case 0xC:
	case 0xD:
	case 0xE:
	case 0xF:
		return VME_A32;
	case 0x0:
	case 0x1:
	case 0x3:
		return VME_A64;
	}

	return 0;
}

/**
 * vme_slave_request - Request a VME slave window resource.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @address: Required VME address space.
 * @cycle: Required VME data transfer cycle type.
 *
 * Request use of a VME window resource capable of being set for the requested
 * address space and data transfer cycle.
 *
 * Return: Pointer to VME resource on success, NULL on failure.
 */
struct vme_resource *vme_slave_request(struct vme_dev *vdev, u32 address,
				       u32 cycle)
{
	struct vme_bridge *bridge;
	struct vme_slave_resource *allocated_image = NULL;
	struct vme_slave_resource *slave_image = NULL;
	struct vme_resource *resource = NULL;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		goto err_bus;
	}

	/* Loop through slave resources */
	list_for_each_entry(slave_image, &bridge->slave_resources, list) {
		if (!slave_image) {
			dev_err(bridge->parent,
				"Registered NULL Slave resource\n");
			continue;
		}

		/* Find an unlocked and compatible image */
		mutex_lock(&slave_image->mtx);
		if (((slave_image->address_attr & address) == address) &&
		    ((slave_image->cycle_attr & cycle) == cycle) &&
		    (slave_image->locked == 0)) {
			slave_image->locked = 1;
			mutex_unlock(&slave_image->mtx);
			allocated_image = slave_image;
			break;
		}
		mutex_unlock(&slave_image->mtx);
	}

	/* No free image */
	if (!allocated_image)
		goto err_image;

	resource = kmalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		goto err_alloc;

	resource->type = VME_SLAVE;
	resource->entry = &allocated_image->list;

	return resource;

err_alloc:
	/* Unlock image */
	mutex_lock(&slave_image->mtx);
	slave_image->locked = 0;
	mutex_unlock(&slave_image->mtx);
err_image:
err_bus:
	return NULL;
}
EXPORT_SYMBOL(vme_slave_request);

/**
 * vme_slave_set - Set VME slave window configuration.
 * @resource: Pointer to VME slave resource.
 * @enabled: State to which the window should be configured.
 * @vme_base: Base address for the window.
 * @size: Size of the VME window.
 * @buf_base: Based address of buffer used to provide VME slave window storage.
 * @aspace: VME address space for the VME window.
 * @cycle: VME data transfer cycle type for the VME window.
 *
 * Set configuration for provided VME slave window.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device, if an invalid resource has been provided or invalid
 *         attributes are provided. Hardware specific errors may also be
 *         returned.
 */
int vme_slave_set(struct vme_resource *resource, int enabled,
		  unsigned long long vme_base, unsigned long long size,
		  dma_addr_t buf_base, u32 aspace, u32 cycle)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_slave_resource *image;
	int retval;

	if (resource->type != VME_SLAVE) {
		dev_err(bridge->parent, "Not a slave resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_slave_resource, list);

	if (!bridge->slave_set) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	if (!(((image->address_attr & aspace) == aspace) &&
	      ((image->cycle_attr & cycle) == cycle))) {
		dev_err(bridge->parent, "Invalid attributes\n");
		return -EINVAL;
	}

	retval = vme_check_window(bridge, aspace, vme_base, size);
	if (retval)
		return retval;

	return bridge->slave_set(image, enabled, vme_base, size, buf_base,
		aspace, cycle);
}
EXPORT_SYMBOL(vme_slave_set);

/**
 * vme_slave_get - Retrieve VME slave window configuration.
 * @resource: Pointer to VME slave resource.
 * @enabled: Pointer to variable for storing state.
 * @vme_base: Pointer to variable for storing window base address.
 * @size: Pointer to variable for storing window size.
 * @buf_base: Pointer to variable for storing slave buffer base address.
 * @aspace: Pointer to variable for storing VME address space.
 * @cycle: Pointer to variable for storing VME data transfer cycle type.
 *
 * Return configuration for provided VME slave window.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device or if an invalid resource has been provided.
 */
int vme_slave_get(struct vme_resource *resource, int *enabled,
		  unsigned long long *vme_base, unsigned long long *size,
		  dma_addr_t *buf_base, u32 *aspace, u32 *cycle)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_slave_resource *image;

	if (resource->type != VME_SLAVE) {
		dev_err(bridge->parent, "Not a slave resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_slave_resource, list);

	if (!bridge->slave_get) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->slave_get(image, enabled, vme_base, size, buf_base,
		aspace, cycle);
}
EXPORT_SYMBOL(vme_slave_get);

/**
 * vme_slave_free - Free VME slave window
 * @resource: Pointer to VME slave resource.
 *
 * Free the provided slave resource so that it may be reallocated.
 */
void vme_slave_free(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_slave_resource *slave_image;

	if (resource->type != VME_SLAVE) {
		dev_err(bridge->parent, "Not a slave resource\n");
		return;
	}

	slave_image = list_entry(resource->entry, struct vme_slave_resource,
				 list);

	/* Unlock image */
	mutex_lock(&slave_image->mtx);
	if (slave_image->locked == 0)
		dev_err(bridge->parent, "Image is already free\n");

	slave_image->locked = 0;
	mutex_unlock(&slave_image->mtx);

	/* Free up resource memory */
	kfree(resource);
}
EXPORT_SYMBOL(vme_slave_free);

/**
 * vme_master_request - Request a VME master window resource.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @address: Required VME address space.
 * @cycle: Required VME data transfer cycle type.
 * @dwidth: Required VME data transfer width.
 *
 * Request use of a VME window resource capable of being set for the requested
 * address space, data transfer cycle and width.
 *
 * Return: Pointer to VME resource on success, NULL on failure.
 */
struct vme_resource *vme_master_request(struct vme_dev *vdev, u32 address,
					u32 cycle, u32 dwidth)
{
	struct vme_bridge *bridge;
	struct vme_master_resource *allocated_image = NULL;
	struct vme_master_resource *master_image = NULL;
	struct vme_resource *resource = NULL;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		goto err_bus;
	}

	/* Loop through master resources */
	list_for_each_entry(master_image, &bridge->master_resources, list) {
		if (!master_image) {
			dev_warn(bridge->parent,
				 "Registered NULL master resource\n");
			continue;
		}

		/* Find an unlocked and compatible image */
		spin_lock(&master_image->lock);
		if (((master_image->address_attr & address) == address) &&
		    ((master_image->cycle_attr & cycle) == cycle) &&
		    ((master_image->width_attr & dwidth) == dwidth) &&
		    (master_image->locked == 0)) {
			master_image->locked = 1;
			spin_unlock(&master_image->lock);
			allocated_image = master_image;
			break;
		}
		spin_unlock(&master_image->lock);
	}

	/* Check to see if we found a resource */
	if (!allocated_image) {
		dev_err(&vdev->dev, "Can't find a suitable resource\n");
		goto err_image;
	}

	resource = kmalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		goto err_alloc;

	resource->type = VME_MASTER;
	resource->entry = &allocated_image->list;

	return resource;

err_alloc:
	/* Unlock image */
	spin_lock(&master_image->lock);
	master_image->locked = 0;
	spin_unlock(&master_image->lock);
err_image:
err_bus:
	return NULL;
}
EXPORT_SYMBOL(vme_master_request);

/**
 * vme_master_set - Set VME master window configuration.
 * @resource: Pointer to VME master resource.
 * @enabled: State to which the window should be configured.
 * @vme_base: Base address for the window.
 * @size: Size of the VME window.
 * @aspace: VME address space for the VME window.
 * @cycle: VME data transfer cycle type for the VME window.
 * @dwidth: VME data transfer width for the VME window.
 *
 * Set configuration for provided VME master window.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device, if an invalid resource has been provided or invalid
 *         attributes are provided. Hardware specific errors may also be
 *         returned.
 */
int vme_master_set(struct vme_resource *resource, int enabled,
		   unsigned long long vme_base, unsigned long long size,
		   u32 aspace, u32 cycle, u32 dwidth)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;
	int retval;

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);

	if (!bridge->master_set) {
		dev_warn(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	if (!(((image->address_attr & aspace) == aspace) &&
	      ((image->cycle_attr & cycle) == cycle) &&
	      ((image->width_attr & dwidth) == dwidth))) {
		dev_warn(bridge->parent, "Invalid attributes\n");
		return -EINVAL;
	}

	retval = vme_check_window(bridge, aspace, vme_base, size);
	if (retval)
		return retval;

	return bridge->master_set(image, enabled, vme_base, size, aspace,
		cycle, dwidth);
}
EXPORT_SYMBOL(vme_master_set);

/**
 * vme_master_get - Retrieve VME master window configuration.
 * @resource: Pointer to VME master resource.
 * @enabled: Pointer to variable for storing state.
 * @vme_base: Pointer to variable for storing window base address.
 * @size: Pointer to variable for storing window size.
 * @aspace: Pointer to variable for storing VME address space.
 * @cycle: Pointer to variable for storing VME data transfer cycle type.
 * @dwidth: Pointer to variable for storing VME data transfer width.
 *
 * Return configuration for provided VME master window.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device or if an invalid resource has been provided.
 */
int vme_master_get(struct vme_resource *resource, int *enabled,
		   unsigned long long *vme_base, unsigned long long *size,
		   u32 *aspace, u32 *cycle, u32 *dwidth)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);

	if (!bridge->master_get) {
		dev_warn(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->master_get(image, enabled, vme_base, size, aspace,
		cycle, dwidth);
}
EXPORT_SYMBOL(vme_master_get);

/**
 * vme_master_read - Read data from VME space into a buffer.
 * @resource: Pointer to VME master resource.
 * @buf: Pointer to buffer where data should be transferred.
 * @count: Number of bytes to transfer.
 * @offset: Offset into VME master window at which to start transfer.
 *
 * Perform read of count bytes of data from location on VME bus which maps into
 * the VME master window at offset to buf.
 *
 * Return: Number of bytes read, -EINVAL if resource is not a VME master
 *         resource or read operation is not supported. -EFAULT returned if
 *         invalid offset is provided. Hardware specific errors may also be
 *         returned.
 */
ssize_t vme_master_read(struct vme_resource *resource, void *buf, size_t count,
			loff_t offset)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;
	size_t length;

	if (!bridge->master_read) {
		dev_warn(bridge->parent,
			 "Reading from resource not supported\n");
		return -EINVAL;
	}

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);

	length = vme_get_size(resource);

	if (offset > length) {
		dev_warn(bridge->parent, "Invalid Offset\n");
		return -EFAULT;
	}

	if ((offset + count) > length)
		count = length - offset;

	return bridge->master_read(image, buf, count, offset);
}
EXPORT_SYMBOL(vme_master_read);

/**
 * vme_master_write - Write data out to VME space from a buffer.
 * @resource: Pointer to VME master resource.
 * @buf: Pointer to buffer holding data to transfer.
 * @count: Number of bytes to transfer.
 * @offset: Offset into VME master window at which to start transfer.
 *
 * Perform write of count bytes of data from buf to location on VME bus which
 * maps into the VME master window at offset.
 *
 * Return: Number of bytes written, -EINVAL if resource is not a VME master
 *         resource or write operation is not supported. -EFAULT returned if
 *         invalid offset is provided. Hardware specific errors may also be
 *         returned.
 */
ssize_t vme_master_write(struct vme_resource *resource, void *buf,
			 size_t count, loff_t offset)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;
	size_t length;

	if (!bridge->master_write) {
		dev_warn(bridge->parent, "Writing to resource not supported\n");
		return -EINVAL;
	}

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);

	length = vme_get_size(resource);

	if (offset > length) {
		dev_warn(bridge->parent, "Invalid Offset\n");
		return -EFAULT;
	}

	if ((offset + count) > length)
		count = length - offset;

	return bridge->master_write(image, buf, count, offset);
}
EXPORT_SYMBOL(vme_master_write);

/**
 * vme_master_rmw - Perform read-modify-write cycle.
 * @resource: Pointer to VME master resource.
 * @mask: Bits to be compared and swapped in operation.
 * @compare: Bits to be compared with data read from offset.
 * @swap: Bits to be swapped in data read from offset.
 * @offset: Offset into VME master window at which to perform operation.
 *
 * Perform read-modify-write cycle on provided location:
 * - Location on VME bus is read.
 * - Bits selected by mask are compared with compare.
 * - Where a selected bit matches that in compare and are selected in swap,
 * the bit is swapped.
 * - Result written back to location on VME bus.
 *
 * Return: Bytes written on success, -EINVAL if resource is not a VME master
 *         resource or RMW operation is not supported. Hardware specific
 *         errors may also be returned.
 */
unsigned int vme_master_rmw(struct vme_resource *resource, unsigned int mask,
			    unsigned int compare, unsigned int swap, loff_t offset)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;

	if (!bridge->master_rmw) {
		dev_warn(bridge->parent, "Writing to resource not supported\n");
		return -EINVAL;
	}

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);

	return bridge->master_rmw(image, mask, compare, swap, offset);
}
EXPORT_SYMBOL(vme_master_rmw);

/**
 * vme_master_mmap - Mmap region of VME master window.
 * @resource: Pointer to VME master resource.
 * @vma: Pointer to definition of user mapping.
 *
 * Memory map a region of the VME master window into user space.
 *
 * Return: Zero on success, -EINVAL if resource is not a VME master
 *         resource or -EFAULT if map exceeds window size. Other generic mmap
 *         errors may also be returned.
 */
int vme_master_mmap(struct vme_resource *resource, struct vm_area_struct *vma)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *image;
	phys_addr_t phys_addr;
	unsigned long vma_size;

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return -EINVAL;
	}

	image = list_entry(resource->entry, struct vme_master_resource, list);
	phys_addr = image->bus_resource.start + (vma->vm_pgoff << PAGE_SHIFT);
	vma_size = vma->vm_end - vma->vm_start;

	if (phys_addr + vma_size > image->bus_resource.end + 1) {
		dev_err(bridge->parent, "Map size cannot exceed the window size\n");
		return -EFAULT;
	}

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	return vm_iomap_memory(vma, phys_addr, vma->vm_end - vma->vm_start);
}
EXPORT_SYMBOL(vme_master_mmap);

/**
 * vme_master_free - Free VME master window
 * @resource: Pointer to VME master resource.
 *
 * Free the provided master resource so that it may be reallocated.
 */
void vme_master_free(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_master_resource *master_image;

	if (resource->type != VME_MASTER) {
		dev_err(bridge->parent, "Not a master resource\n");
		return;
	}

	master_image = list_entry(resource->entry, struct vme_master_resource,
				  list);

	/* Unlock image */
	spin_lock(&master_image->lock);
	if (master_image->locked == 0)
		dev_err(bridge->parent, "Image is already free\n");

	master_image->locked = 0;
	spin_unlock(&master_image->lock);

	/* Free up resource memory */
	kfree(resource);
}
EXPORT_SYMBOL(vme_master_free);

/**
 * vme_dma_request - Request a DMA controller.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @route: Required src/destination combination.
 *
 * Request a VME DMA controller with capability to perform transfers bewteen
 * requested source/destination combination.
 *
 * Return: Pointer to VME DMA resource on success, NULL on failure.
 */
struct vme_resource *vme_dma_request(struct vme_dev *vdev, u32 route)
{
	struct vme_bridge *bridge;
	struct vme_dma_resource *allocated_ctrlr = NULL;
	struct vme_dma_resource *dma_ctrlr = NULL;
	struct vme_resource *resource = NULL;

	/* XXX Not checking resource attributes */
	dev_err(&vdev->dev, "No VME resource Attribute tests done\n");

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		goto err_bus;
	}

	/* Loop through DMA resources */
	list_for_each_entry(dma_ctrlr, &bridge->dma_resources, list) {
		if (!dma_ctrlr) {
			dev_err(bridge->parent,
				"Registered NULL DMA resource\n");
			continue;
		}

		/* Find an unlocked and compatible controller */
		mutex_lock(&dma_ctrlr->mtx);
		if (((dma_ctrlr->route_attr & route) == route) &&
		    (dma_ctrlr->locked == 0)) {
			dma_ctrlr->locked = 1;
			mutex_unlock(&dma_ctrlr->mtx);
			allocated_ctrlr = dma_ctrlr;
			break;
		}
		mutex_unlock(&dma_ctrlr->mtx);
	}

	/* Check to see if we found a resource */
	if (!allocated_ctrlr)
		goto err_ctrlr;

	resource = kmalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		goto err_alloc;

	resource->type = VME_DMA;
	resource->entry = &allocated_ctrlr->list;

	return resource;

err_alloc:
	/* Unlock image */
	mutex_lock(&dma_ctrlr->mtx);
	dma_ctrlr->locked = 0;
	mutex_unlock(&dma_ctrlr->mtx);
err_ctrlr:
err_bus:
	return NULL;
}
EXPORT_SYMBOL(vme_dma_request);

/**
 * vme_new_dma_list - Create new VME DMA list.
 * @resource: Pointer to VME DMA resource.
 *
 * Create a new VME DMA list. It is the responsibility of the user to free
 * the list once it is no longer required with vme_dma_list_free().
 *
 * Return: Pointer to new VME DMA list, NULL on allocation failure or invalid
 *         VME DMA resource.
 */
struct vme_dma_list *vme_new_dma_list(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_dma_list *dma_list;

	if (resource->type != VME_DMA) {
		dev_err(bridge->parent, "Not a DMA resource\n");
		return NULL;
	}

	dma_list = kmalloc(sizeof(*dma_list), GFP_KERNEL);
	if (!dma_list)
		return NULL;

	INIT_LIST_HEAD(&dma_list->entries);
	dma_list->parent = list_entry(resource->entry,
				      struct vme_dma_resource,
				      list);
	mutex_init(&dma_list->mtx);

	return dma_list;
}
EXPORT_SYMBOL(vme_new_dma_list);

/**
 * vme_dma_pattern_attribute - Create "Pattern" type VME DMA list attribute.
 * @pattern: Value to use used as pattern
 * @type: Type of pattern to be written.
 *
 * Create VME DMA list attribute for pattern generation. It is the
 * responsibility of the user to free used attributes using
 * vme_dma_free_attribute().
 *
 * Return: Pointer to VME DMA attribute, NULL on failure.
 */
struct vme_dma_attr *vme_dma_pattern_attribute(u32 pattern, u32 type)
{
	struct vme_dma_attr *attributes;
	struct vme_dma_pattern *pattern_attr;

	attributes = kmalloc(sizeof(*attributes), GFP_KERNEL);
	if (!attributes)
		goto err_attr;

	pattern_attr = kmalloc(sizeof(*pattern_attr), GFP_KERNEL);
	if (!pattern_attr)
		goto err_pat;

	attributes->type = VME_DMA_PATTERN;
	attributes->private = (void *)pattern_attr;

	pattern_attr->pattern = pattern;
	pattern_attr->type = type;

	return attributes;

err_pat:
	kfree(attributes);
err_attr:
	return NULL;
}
EXPORT_SYMBOL(vme_dma_pattern_attribute);

/**
 * vme_dma_pci_attribute - Create "PCI" type VME DMA list attribute.
 * @address: PCI base address for DMA transfer.
 *
 * Create VME DMA list attribute pointing to a location on PCI for DMA
 * transfers. It is the responsibility of the user to free used attributes
 * using vme_dma_free_attribute().
 *
 * Return: Pointer to VME DMA attribute, NULL on failure.
 */
struct vme_dma_attr *vme_dma_pci_attribute(dma_addr_t address)
{
	struct vme_dma_attr *attributes;
	struct vme_dma_pci *pci_attr;

	/* XXX Run some sanity checks here */

	attributes = kmalloc(sizeof(*attributes), GFP_KERNEL);
	if (!attributes)
		goto err_attr;

	pci_attr = kmalloc(sizeof(*pci_attr), GFP_KERNEL);
	if (!pci_attr)
		goto err_pci;

	attributes->type = VME_DMA_PCI;
	attributes->private = (void *)pci_attr;

	pci_attr->address = address;

	return attributes;

err_pci:
	kfree(attributes);
err_attr:
	return NULL;
}
EXPORT_SYMBOL(vme_dma_pci_attribute);

/**
 * vme_dma_vme_attribute - Create "VME" type VME DMA list attribute.
 * @address: VME base address for DMA transfer.
 * @aspace: VME address space to use for DMA transfer.
 * @cycle: VME bus cycle to use for DMA transfer.
 * @dwidth: VME data width to use for DMA transfer.
 *
 * Create VME DMA list attribute pointing to a location on the VME bus for DMA
 * transfers. It is the responsibility of the user to free used attributes
 * using vme_dma_free_attribute().
 *
 * Return: Pointer to VME DMA attribute, NULL on failure.
 */
struct vme_dma_attr *vme_dma_vme_attribute(unsigned long long address,
					   u32 aspace, u32 cycle, u32 dwidth)
{
	struct vme_dma_attr *attributes;
	struct vme_dma_vme *vme_attr;

	attributes = kmalloc(sizeof(*attributes), GFP_KERNEL);
	if (!attributes)
		goto err_attr;

	vme_attr = kmalloc(sizeof(*vme_attr), GFP_KERNEL);
	if (!vme_attr)
		goto err_vme;

	attributes->type = VME_DMA_VME;
	attributes->private = (void *)vme_attr;

	vme_attr->address = address;
	vme_attr->aspace = aspace;
	vme_attr->cycle = cycle;
	vme_attr->dwidth = dwidth;

	return attributes;

err_vme:
	kfree(attributes);
err_attr:
	return NULL;
}
EXPORT_SYMBOL(vme_dma_vme_attribute);

/**
 * vme_dma_free_attribute - Free DMA list attribute.
 * @attributes: Pointer to DMA list attribute.
 *
 * Free VME DMA list attribute. VME DMA list attributes can be safely freed
 * once vme_dma_list_add() has returned.
 */
void vme_dma_free_attribute(struct vme_dma_attr *attributes)
{
	kfree(attributes->private);
	kfree(attributes);
}
EXPORT_SYMBOL(vme_dma_free_attribute);

/**
 * vme_dma_list_add - Add enty to a VME DMA list.
 * @list: Pointer to VME list.
 * @src: Pointer to DMA list attribute to use as source.
 * @dest: Pointer to DMA list attribute to use as destination.
 * @count: Number of bytes to transfer.
 *
 * Add an entry to the provided VME DMA list. Entry requires pointers to source
 * and destination DMA attributes and a count.
 *
 * Please note, the attributes supported as source and destinations for
 * transfers are hardware dependent.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device or if the link list has already been submitted for execution.
 *         Hardware specific errors also possible.
 */
int vme_dma_list_add(struct vme_dma_list *list, struct vme_dma_attr *src,
		     struct vme_dma_attr *dest, size_t count)
{
	struct vme_bridge *bridge = list->parent->parent;
	int retval;

	if (!bridge->dma_list_add) {
		dev_warn(bridge->parent,
			 "Link List DMA generation not supported\n");
		return -EINVAL;
	}

	if (!mutex_trylock(&list->mtx)) {
		dev_err(bridge->parent, "Link List already submitted\n");
		return -EINVAL;
	}

	retval = bridge->dma_list_add(list, src, dest, count);

	mutex_unlock(&list->mtx);

	return retval;
}
EXPORT_SYMBOL(vme_dma_list_add);

/**
 * vme_dma_list_exec - Queue a VME DMA list for execution.
 * @list: Pointer to VME list.
 *
 * Queue the provided VME DMA list for execution. The call will return once the
 * list has been executed.
 *
 * Return: Zero on success, -EINVAL if operation is not supported on this
 *         device. Hardware specific errors also possible.
 */
int vme_dma_list_exec(struct vme_dma_list *list)
{
	struct vme_bridge *bridge = list->parent->parent;
	int retval;

	if (!bridge->dma_list_exec) {
		dev_err(bridge->parent,
			"Link List DMA execution not supported\n");
		return -EINVAL;
	}

	mutex_lock(&list->mtx);

	retval = bridge->dma_list_exec(list);

	mutex_unlock(&list->mtx);

	return retval;
}
EXPORT_SYMBOL(vme_dma_list_exec);

/**
 * vme_dma_list_free - Free a VME DMA list.
 * @list: Pointer to VME list.
 *
 * Free the provided DMA list and all its entries.
 *
 * Return: Zero on success, -EINVAL on invalid VME resource, -EBUSY if resource
 *         is still in use. Hardware specific errors also possible.
 */
int vme_dma_list_free(struct vme_dma_list *list)
{
	struct vme_bridge *bridge = list->parent->parent;
	int retval;

	if (!bridge->dma_list_empty) {
		dev_warn(bridge->parent,
			 "Emptying of Link Lists not supported\n");
		return -EINVAL;
	}

	if (!mutex_trylock(&list->mtx)) {
		dev_err(bridge->parent, "Link List in use\n");
		return -EBUSY;
	}

	/*
	 * Empty out all of the entries from the DMA list. We need to go to the
	 * low level driver as DMA entries are driver specific.
	 */
	retval = bridge->dma_list_empty(list);
	if (retval) {
		dev_err(bridge->parent, "Unable to empty link-list entries\n");
		mutex_unlock(&list->mtx);
		return retval;
	}
	mutex_unlock(&list->mtx);
	kfree(list);

	return retval;
}
EXPORT_SYMBOL(vme_dma_list_free);

/**
 * vme_dma_free - Free a VME DMA resource.
 * @resource: Pointer to VME DMA resource.
 *
 * Free the provided DMA resource so that it may be reallocated.
 *
 * Return: Zero on success, -EINVAL on invalid VME resource, -EBUSY if resource
 *         is still active.
 */
int vme_dma_free(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_dma_resource *ctrlr;

	if (resource->type != VME_DMA) {
		dev_err(bridge->parent, "Not a DMA resource\n");
		return -EINVAL;
	}

	ctrlr = list_entry(resource->entry, struct vme_dma_resource, list);

	if (!mutex_trylock(&ctrlr->mtx)) {
		dev_err(bridge->parent, "Resource busy, can't free\n");
		return -EBUSY;
	}

	if (!(list_empty(&ctrlr->pending) && list_empty(&ctrlr->running))) {
		dev_warn(bridge->parent,
			 "Resource still processing transfers\n");
		mutex_unlock(&ctrlr->mtx);
		return -EBUSY;
	}

	ctrlr->locked = 0;

	mutex_unlock(&ctrlr->mtx);

	kfree(resource);

	return 0;
}
EXPORT_SYMBOL(vme_dma_free);

void vme_bus_error_handler(struct vme_bridge *bridge,
			   unsigned long long address, int am)
{
	struct vme_error_handler *handler;
	int handler_triggered = 0;
	u32 aspace = vme_get_aspace(am);

	list_for_each_entry(handler, &bridge->vme_error_handlers, list) {
		if ((aspace == handler->aspace) &&
		    (address >= handler->start) &&
		    (address < handler->end)) {
			if (!handler->num_errors)
				handler->first_error = address;
			if (handler->num_errors != UINT_MAX)
				handler->num_errors++;
			handler_triggered = 1;
		}
	}

	if (!handler_triggered)
		dev_err(bridge->parent,
			"Unhandled VME access error at address 0x%llx\n",
			address);
}
EXPORT_SYMBOL(vme_bus_error_handler);

struct vme_error_handler *vme_register_error_handler(struct vme_bridge *bridge, u32 aspace,
						     unsigned long long address, size_t len)
{
	struct vme_error_handler *handler;

	handler = kmalloc(sizeof(*handler), GFP_ATOMIC);
	if (!handler)
		return NULL;

	handler->aspace = aspace;
	handler->start = address;
	handler->end = address + len;
	handler->num_errors = 0;
	handler->first_error = 0;
	list_add_tail(&handler->list, &bridge->vme_error_handlers);

	return handler;
}
EXPORT_SYMBOL(vme_register_error_handler);

void vme_unregister_error_handler(struct vme_error_handler *handler)
{
	list_del(&handler->list);
	kfree(handler);
}
EXPORT_SYMBOL(vme_unregister_error_handler);

void vme_irq_handler(struct vme_bridge *bridge, int level, int statid)
{
	void (*call)(int level, int statid, void *priv_data);
	void *priv_data;

	call = bridge->irq[level - 1].callback[statid].func;
	priv_data = bridge->irq[level - 1].callback[statid].priv_data;
	if (call)
		call(level, statid, priv_data);
	else
		dev_warn(bridge->parent,
			 "Spurious VME interrupt, level:%x, vector:%x\n", level,
			 statid);
}
EXPORT_SYMBOL(vme_irq_handler);

/**
 * vme_irq_request - Request a specific VME interrupt.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @level: Interrupt priority being requested.
 * @statid: Interrupt vector being requested.
 * @callback: Pointer to callback function called when VME interrupt/vector
 *            received.
 * @priv_data: Generic pointer that will be passed to the callback function.
 *
 * Request callback to be attached as a handler for VME interrupts with provided
 * level and statid.
 *
 * Return: Zero on success, -EINVAL on invalid vme device, level or if the
 *         function is not supported, -EBUSY if the level/statid combination is
 *         already in use. Hardware specific errors also possible.
 */
int vme_irq_request(struct vme_dev *vdev, int level, int statid,
		    void (*callback)(int, int, void *),
		    void *priv_data)
{
	struct vme_bridge *bridge;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		return -EINVAL;
	}

	if ((level < 1) || (level > 7)) {
		dev_err(bridge->parent, "Invalid interrupt level\n");
		return -EINVAL;
	}

	if (!bridge->irq_set) {
		dev_err(bridge->parent,
			"Configuring interrupts not supported\n");
		return -EINVAL;
	}

	mutex_lock(&bridge->irq_mtx);

	if (bridge->irq[level - 1].callback[statid].func) {
		mutex_unlock(&bridge->irq_mtx);
		dev_warn(bridge->parent, "VME Interrupt already taken\n");
		return -EBUSY;
	}

	bridge->irq[level - 1].count++;
	bridge->irq[level - 1].callback[statid].priv_data = priv_data;
	bridge->irq[level - 1].callback[statid].func = callback;

	/* Enable IRQ level */
	bridge->irq_set(bridge, level, 1, 1);

	mutex_unlock(&bridge->irq_mtx);

	return 0;
}
EXPORT_SYMBOL(vme_irq_request);

/**
 * vme_irq_free - Free a VME interrupt.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @level: Interrupt priority of interrupt being freed.
 * @statid: Interrupt vector of interrupt being freed.
 *
 * Remove previously attached callback from VME interrupt priority/vector.
 */
void vme_irq_free(struct vme_dev *vdev, int level, int statid)
{
	struct vme_bridge *bridge;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		return;
	}

	if ((level < 1) || (level > 7)) {
		dev_err(bridge->parent, "Invalid interrupt level\n");
		return;
	}

	if (!bridge->irq_set) {
		dev_err(bridge->parent,
			"Configuring interrupts not supported\n");
		return;
	}

	mutex_lock(&bridge->irq_mtx);

	bridge->irq[level - 1].count--;

	/* Disable IRQ level if no more interrupts attached at this level*/
	if (bridge->irq[level - 1].count == 0)
		bridge->irq_set(bridge, level, 0, 1);

	bridge->irq[level - 1].callback[statid].func = NULL;
	bridge->irq[level - 1].callback[statid].priv_data = NULL;

	mutex_unlock(&bridge->irq_mtx);
}
EXPORT_SYMBOL(vme_irq_free);

/**
 * vme_irq_generate - Generate VME interrupt.
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 * @level: Interrupt priority at which to assert the interrupt.
 * @statid: Interrupt vector to associate with the interrupt.
 *
 * Generate a VME interrupt of the provided level and with the provided
 * statid.
 *
 * Return: Zero on success, -EINVAL on invalid vme device, level or if the
 *         function is not supported. Hardware specific errors also possible.
 */
int vme_irq_generate(struct vme_dev *vdev, int level, int statid)
{
	struct vme_bridge *bridge;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		return -EINVAL;
	}

	if ((level < 1) || (level > 7)) {
		dev_warn(bridge->parent, "Invalid interrupt level\n");
		return -EINVAL;
	}

	if (!bridge->irq_generate) {
		dev_warn(bridge->parent,
			 "Interrupt generation not supported\n");
		return -EINVAL;
	}

	return bridge->irq_generate(bridge, level, statid);
}
EXPORT_SYMBOL(vme_irq_generate);

/**
 * vme_lm_request - Request a VME location monitor
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 *
 * Allocate a location monitor resource to the driver. A location monitor
 * allows the driver to monitor accesses to a contiguous number of
 * addresses on the VME bus.
 *
 * Return: Pointer to a VME resource on success or NULL on failure.
 */
struct vme_resource *vme_lm_request(struct vme_dev *vdev)
{
	struct vme_bridge *bridge;
	struct vme_lm_resource *allocated_lm = NULL;
	struct vme_lm_resource *lm = NULL;
	struct vme_resource *resource = NULL;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		goto err_bus;
	}

	/* Loop through LM resources */
	list_for_each_entry(lm, &bridge->lm_resources, list) {
		if (!lm) {
			dev_err(bridge->parent,
				"Registered NULL Location Monitor resource\n");
			continue;
		}

		/* Find an unlocked controller */
		mutex_lock(&lm->mtx);
		if (lm->locked == 0) {
			lm->locked = 1;
			mutex_unlock(&lm->mtx);
			allocated_lm = lm;
			break;
		}
		mutex_unlock(&lm->mtx);
	}

	/* Check to see if we found a resource */
	if (!allocated_lm)
		goto err_lm;

	resource = kmalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		goto err_alloc;

	resource->type = VME_LM;
	resource->entry = &allocated_lm->list;

	return resource;

err_alloc:
	/* Unlock image */
	mutex_lock(&lm->mtx);
	lm->locked = 0;
	mutex_unlock(&lm->mtx);
err_lm:
err_bus:
	return NULL;
}
EXPORT_SYMBOL(vme_lm_request);

/**
 * vme_lm_count - Determine number of VME Addresses monitored
 * @resource: Pointer to VME location monitor resource.
 *
 * The number of contiguous addresses monitored is hardware dependent.
 * Return the number of contiguous addresses monitored by the
 * location monitor.
 *
 * Return: Count of addresses monitored or -EINVAL when provided with an
 *	   invalid location monitor resource.
 */
int vme_lm_count(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return -EINVAL;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	return lm->monitors;
}
EXPORT_SYMBOL(vme_lm_count);

/**
 * vme_lm_set - Configure location monitor
 * @resource: Pointer to VME location monitor resource.
 * @lm_base: Base address to monitor.
 * @aspace: VME address space to monitor.
 * @cycle: VME bus cycle type to monitor.
 *
 * Set the base address, address space and cycle type of accesses to be
 * monitored by the location monitor.
 *
 * Return: Zero on success, -EINVAL when provided with an invalid location
 *	   monitor resource or function is not supported. Hardware specific
 *	   errors may also be returned.
 */
int vme_lm_set(struct vme_resource *resource, unsigned long long lm_base,
	       u32 aspace, u32 cycle)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return -EINVAL;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	if (!bridge->lm_set) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->lm_set(lm, lm_base, aspace, cycle);
}
EXPORT_SYMBOL(vme_lm_set);

/**
 * vme_lm_get - Retrieve location monitor settings
 * @resource: Pointer to VME location monitor resource.
 * @lm_base: Pointer used to output the base address monitored.
 * @aspace: Pointer used to output the address space monitored.
 * @cycle: Pointer used to output the VME bus cycle type monitored.
 *
 * Retrieve the base address, address space and cycle type of accesses to
 * be monitored by the location monitor.
 *
 * Return: Zero on success, -EINVAL when provided with an invalid location
 *	   monitor resource or function is not supported. Hardware specific
 *	   errors may also be returned.
 */
int vme_lm_get(struct vme_resource *resource, unsigned long long *lm_base,
	       u32 *aspace, u32 *cycle)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return -EINVAL;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	if (!bridge->lm_get) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->lm_get(lm, lm_base, aspace, cycle);
}
EXPORT_SYMBOL(vme_lm_get);

/**
 * vme_lm_attach - Provide callback for location monitor address
 * @resource: Pointer to VME location monitor resource.
 * @monitor: Offset to which callback should be attached.
 * @callback: Pointer to callback function called when triggered.
 * @data: Generic pointer that will be passed to the callback function.
 *
 * Attach a callback to the specified offset into the location monitors
 * monitored addresses. A generic pointer is provided to allow data to be
 * passed to the callback when called.
 *
 * Return: Zero on success, -EINVAL when provided with an invalid location
 *	   monitor resource or function is not supported. Hardware specific
 *	   errors may also be returned.
 */
int vme_lm_attach(struct vme_resource *resource, int monitor,
		  void (*callback)(void *), void *data)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return -EINVAL;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	if (!bridge->lm_attach) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->lm_attach(lm, monitor, callback, data);
}
EXPORT_SYMBOL(vme_lm_attach);

/**
 * vme_lm_detach - Remove callback for location monitor address
 * @resource: Pointer to VME location monitor resource.
 * @monitor: Offset to which callback should be removed.
 *
 * Remove the callback associated with the specified offset into the
 * location monitors monitored addresses.
 *
 * Return: Zero on success, -EINVAL when provided with an invalid location
 *	   monitor resource or function is not supported. Hardware specific
 *	   errors may also be returned.
 */
int vme_lm_detach(struct vme_resource *resource, int monitor)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return -EINVAL;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	if (!bridge->lm_detach) {
		dev_err(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->lm_detach(lm, monitor);
}
EXPORT_SYMBOL(vme_lm_detach);

/**
 * vme_lm_free - Free allocated VME location monitor
 * @resource: Pointer to VME location monitor resource.
 *
 * Free allocation of a VME location monitor.
 *
 * WARNING: This function currently expects that any callbacks that have
 *          been attached to the location monitor have been removed.
 *
 * Return: Zero on success, -EINVAL when provided with an invalid location
 *	   monitor resource.
 */
void vme_lm_free(struct vme_resource *resource)
{
	struct vme_bridge *bridge = find_bridge(resource);
	struct vme_lm_resource *lm;

	if (resource->type != VME_LM) {
		dev_err(bridge->parent, "Not a Location Monitor resource\n");
		return;
	}

	lm = list_entry(resource->entry, struct vme_lm_resource, list);

	mutex_lock(&lm->mtx);

	/* XXX
	 * Check to see that there aren't any callbacks still attached, if
	 * there are we should probably be detaching them!
	 */

	lm->locked = 0;

	mutex_unlock(&lm->mtx);

	kfree(resource);
}
EXPORT_SYMBOL(vme_lm_free);

/**
 * vme_slot_num - Retrieve slot ID
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 *
 * Retrieve the slot ID associated with the provided VME device.
 *
 * Return: The slot ID on success, -EINVAL if VME bridge cannot be determined
 *         or the function is not supported. Hardware specific errors may also
 *         be returned.
 */
int vme_slot_num(struct vme_dev *vdev)
{
	struct vme_bridge *bridge;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		return -EINVAL;
	}

	if (!bridge->slot_get) {
		dev_warn(bridge->parent, "%s not supported\n", __func__);
		return -EINVAL;
	}

	return bridge->slot_get(bridge);
}
EXPORT_SYMBOL(vme_slot_num);

/**
 * vme_bus_num - Retrieve bus number
 * @vdev: Pointer to VME device struct vme_dev assigned to driver instance.
 *
 * Retrieve the bus enumeration associated with the provided VME device.
 *
 * Return: The bus number on success, -EINVAL if VME bridge cannot be
 *         determined.
 */
int vme_bus_num(struct vme_dev *vdev)
{
	struct vme_bridge *bridge;

	bridge = vdev->bridge;
	if (!bridge) {
		dev_err(&vdev->dev, "Can't find VME bus\n");
		return -EINVAL;
	}

	return bridge->num;
}
EXPORT_SYMBOL(vme_bus_num);

/* - Bridge Registration --------------------------------------------------- */

static void vme_dev_release(struct device *dev)
{
	kfree(dev_to_vme_dev(dev));
}

/* Common bridge initialization */
struct vme_bridge *vme_init_bridge(struct vme_bridge *bridge)
{
	INIT_LIST_HEAD(&bridge->vme_error_handlers);
	INIT_LIST_HEAD(&bridge->master_resources);
	INIT_LIST_HEAD(&bridge->slave_resources);
	INIT_LIST_HEAD(&bridge->dma_resources);
	INIT_LIST_HEAD(&bridge->lm_resources);
	mutex_init(&bridge->irq_mtx);

	return bridge;
}
EXPORT_SYMBOL(vme_init_bridge);

int vme_register_bridge(struct vme_bridge *bridge)
{
	int i;
	int ret = -1;

	mutex_lock(&vme_buses_lock);
	for (i = 0; i < sizeof(vme_bus_numbers) * 8; i++) {
		if ((vme_bus_numbers & (1 << i)) == 0) {
			vme_bus_numbers |= (1 << i);
			bridge->num = i;
			INIT_LIST_HEAD(&bridge->devices);
			list_add_tail(&bridge->bus_list, &vme_bus_list);
			ret = 0;
			break;
		}
	}
	mutex_unlock(&vme_buses_lock);

	return ret;
}
EXPORT_SYMBOL(vme_register_bridge);

void vme_unregister_bridge(struct vme_bridge *bridge)
{
	struct vme_dev *vdev;
	struct vme_dev *tmp;

	mutex_lock(&vme_buses_lock);
	vme_bus_numbers &= ~(1 << bridge->num);
	list_for_each_entry_safe(vdev, tmp, &bridge->devices, bridge_list) {
		list_del(&vdev->drv_list);
		list_del(&vdev->bridge_list);
		device_unregister(&vdev->dev);
	}
	list_del(&bridge->bus_list);
	mutex_unlock(&vme_buses_lock);
}
EXPORT_SYMBOL(vme_unregister_bridge);

/* - Driver Registration --------------------------------------------------- */

static int __vme_register_driver_bus(struct vme_driver *drv,
				     struct vme_bridge *bridge,
				     unsigned int ndevs)
{
	int err;
	unsigned int i;
	struct vme_dev *vdev;
	struct vme_dev *tmp;

	for (i = 0; i < ndevs; i++) {
		vdev = kzalloc(sizeof(*vdev), GFP_KERNEL);
		if (!vdev) {
			err = -ENOMEM;
			goto err_devalloc;
		}
		vdev->num = i;
		vdev->bridge = bridge;
		vdev->dev.platform_data = drv;
		vdev->dev.release = vme_dev_release;
		vdev->dev.parent = bridge->parent;
		vdev->dev.bus = &vme_bus_type;
		dev_set_name(&vdev->dev, "%s.%u-%u", drv->name, bridge->num,
			     vdev->num);

		err = device_register(&vdev->dev);
		if (err)
			goto err_reg;

		if (vdev->dev.platform_data) {
			list_add_tail(&vdev->drv_list, &drv->devices);
			list_add_tail(&vdev->bridge_list, &bridge->devices);
		} else {
			device_unregister(&vdev->dev);
		}
	}
	return 0;

err_reg:
	put_device(&vdev->dev);
err_devalloc:
	list_for_each_entry_safe(vdev, tmp, &drv->devices, drv_list) {
		list_del(&vdev->drv_list);
		list_del(&vdev->bridge_list);
		device_unregister(&vdev->dev);
	}
	return err;
}

static int __vme_register_driver(struct vme_driver *drv, unsigned int ndevs)
{
	struct vme_bridge *bridge;
	int err = 0;

	mutex_lock(&vme_buses_lock);
	list_for_each_entry(bridge, &vme_bus_list, bus_list) {
		/*
		 * This cannot cause trouble as we already have vme_buses_lock
		 * and if the bridge is removed, it will have to go through
		 * vme_unregister_bridge() to do it (which calls remove() on
		 * the bridge which in turn tries to acquire vme_buses_lock and
		 * will have to wait).
		 */
		err = __vme_register_driver_bus(drv, bridge, ndevs);
		if (err)
			break;
	}
	mutex_unlock(&vme_buses_lock);
	return err;
}

/**
 * vme_register_driver - Register a VME driver
 * @drv: Pointer to VME driver structure to register.
 * @ndevs: Maximum number of devices to allow to be enumerated.
 *
 * Register a VME device driver with the VME subsystem.
 *
 * Return: Zero on success, error value on registration failure.
 */
int vme_register_driver(struct vme_driver *drv, unsigned int ndevs)
{
	int err;

	drv->driver.name = drv->name;
	drv->driver.bus = &vme_bus_type;
	INIT_LIST_HEAD(&drv->devices);

	err = driver_register(&drv->driver);
	if (err)
		return err;

	err = __vme_register_driver(drv, ndevs);
	if (err)
		driver_unregister(&drv->driver);

	return err;
}
EXPORT_SYMBOL(vme_register_driver);

/**
 * vme_unregister_driver - Unregister a VME driver
 * @drv: Pointer to VME driver structure to unregister.
 *
 * Unregister a VME device driver from the VME subsystem.
 */
void vme_unregister_driver(struct vme_driver *drv)
{
	struct vme_dev *dev, *dev_tmp;

	mutex_lock(&vme_buses_lock);
	list_for_each_entry_safe(dev, dev_tmp, &drv->devices, drv_list) {
		list_del(&dev->drv_list);
		list_del(&dev->bridge_list);
		device_unregister(&dev->dev);
	}
	mutex_unlock(&vme_buses_lock);

	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL(vme_unregister_driver);

/* - Bus Registration ------------------------------------------------------ */

static int vme_bus_match(struct device *dev, const struct device_driver *drv)
{
	struct vme_driver *vme_drv;

	vme_drv = container_of(drv, struct vme_driver, driver);

	if (dev->platform_data == vme_drv) {
		struct vme_dev *vdev = dev_to_vme_dev(dev);

		if (vme_drv->match && vme_drv->match(vdev))
			return 1;

		dev->platform_data = NULL;
	}
	return 0;
}

static int vme_bus_probe(struct device *dev)
{
	struct vme_driver *driver;
	struct vme_dev *vdev = dev_to_vme_dev(dev);

	driver = dev->platform_data;
	if (driver->probe)
		return driver->probe(vdev);

	return -ENODEV;
}

static void vme_bus_remove(struct device *dev)
{
	struct vme_driver *driver;
	struct vme_dev *vdev = dev_to_vme_dev(dev);

	driver = dev->platform_data;
	if (driver->remove)
		driver->remove(vdev);
}

const struct bus_type vme_bus_type = {
	.name = "vme",
	.match = vme_bus_match,
	.probe = vme_bus_probe,
	.remove = vme_bus_remove,
};
EXPORT_SYMBOL(vme_bus_type);

static int __init vme_init(void)
{
	return bus_register(&vme_bus_type);
}
subsys_initcall(vme_init);
