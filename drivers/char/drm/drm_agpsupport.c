/**
 * \file drm_agpsupport.c
 * DRM support for AGP/GART backend
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "drmP.h"
#include <linux/module.h>

#if __OS_HAS_AGP

/**
 * Get AGP information.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a (output) drm_agp_info structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been initialized and acquired and fills in the
 * drm_agp_info structure with the information in drm_agp_head::agp_info.
 */
int drm_agp_info(drm_device_t * dev, drm_agp_info_t * info)
{
	DRM_AGP_KERN *kern;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	kern = &dev->agp->agp_info;
	info->agp_version_major = kern->version.major;
	info->agp_version_minor = kern->version.minor;
	info->mode = kern->mode;
	info->aperture_base = kern->aper_base;
	info->aperture_size = kern->aper_size * 1024 * 1024;
	info->memory_allowed = kern->max_memory << PAGE_SHIFT;
	info->memory_used = kern->current_memory << PAGE_SHIFT;
	info->id_vendor = kern->device->vendor;
	info->id_device = kern->device->device;

	return 0;
}

EXPORT_SYMBOL(drm_agp_info);

int drm_agp_info_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_info_t info;
	int err;

	err = drm_agp_info(dev, &info);
	if (err)
		return err;

	if (copy_to_user((drm_agp_info_t __user *) arg, &info, sizeof(info)))
		return -EFAULT;
	return 0;
}

/**
 * Acquire the AGP device.
 *
 * \param dev DRM device that is to acquire AGP.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device hasn't been acquired before and calls
 * \c agp_backend_acquire.
 */
int drm_agp_acquire(drm_device_t * dev)
{
	if (!dev->agp)
		return -ENODEV;
	if (dev->agp->acquired)
		return -EBUSY;
	if (!(dev->agp->bridge = agp_backend_acquire(dev->pdev)))
		return -ENODEV;
	dev->agp->acquired = 1;
	return 0;
}

EXPORT_SYMBOL(drm_agp_acquire);

/**
 * Acquire the AGP device (ioctl).
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg user argument.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device hasn't been acquired before and calls
 * \c agp_backend_acquire.
 */
int drm_agp_acquire_ioctl(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;

	return drm_agp_acquire((drm_device_t *) priv->head->dev);
}

/**
 * Release the AGP device.
 *
 * \param dev DRM device that is to release AGP.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been acquired and calls \c agp_backend_release.
 */
int drm_agp_release(drm_device_t * dev)
{
	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	agp_backend_release(dev->agp->bridge);
	dev->agp->acquired = 0;
	return 0;
}
EXPORT_SYMBOL(drm_agp_release);

int drm_agp_release_ioctl(struct inode *inode, struct file *filp,
			  unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;

	return drm_agp_release(dev);
}

/**
 * Enable the AGP bus.
 *
 * \param dev DRM device that has previously acquired AGP.
 * \param mode Requested AGP mode.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device has been acquired but not enabled, and calls
 * \c agp_enable.
 */
int drm_agp_enable(drm_device_t * dev, drm_agp_mode_t mode)
{
	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;

	dev->agp->mode = mode.mode;
	agp_enable(dev->agp->bridge, mode.mode);
	dev->agp->base = dev->agp->agp_info.aper_base;
	dev->agp->enabled = 1;
	return 0;
}

EXPORT_SYMBOL(drm_agp_enable);

int drm_agp_enable_ioctl(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_mode_t mode;

	if (copy_from_user(&mode, (drm_agp_mode_t __user *) arg, sizeof(mode)))
		return -EFAULT;

	return drm_agp_enable(dev, mode);
}

/**
 * Allocate AGP memory.
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_agp_buffer structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired, allocates the
 * memory via alloc_agp() and creates a drm_agp_mem entry for it.
 */
int drm_agp_alloc(drm_device_t *dev, drm_agp_buffer_t *request)
{
	drm_agp_mem_t *entry;
	DRM_AGP_MEM *memory;
	unsigned long pages;
	u32 type;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	if (!(entry = drm_alloc(sizeof(*entry), DRM_MEM_AGPLISTS)))
		return -ENOMEM;

	memset(entry, 0, sizeof(*entry));

	pages = (request->size + PAGE_SIZE - 1) / PAGE_SIZE;
	type = (u32) request->type;
	if (!(memory = drm_alloc_agp(dev, pages, type))) {
		drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -ENOMEM;
	}

	entry->handle = (unsigned long)memory->key + 1;
	entry->memory = memory;
	entry->bound = 0;
	entry->pages = pages;
	entry->prev = NULL;
	entry->next = dev->agp->memory;
	if (dev->agp->memory)
		dev->agp->memory->prev = entry;
	dev->agp->memory = entry;

	request->handle = entry->handle;
	request->physical = memory->physical;

	return 0;
}
EXPORT_SYMBOL(drm_agp_alloc);

int drm_agp_alloc_ioctl(struct inode *inode, struct file *filp,
			unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_buffer_t request;
	drm_agp_buffer_t __user *argp = (void __user *)arg;
	int err;

	if (copy_from_user(&request, argp, sizeof(request)))
		return -EFAULT;

	err = drm_agp_alloc(dev, &request);
	if (err)
		return err;

	if (copy_to_user(argp, &request, sizeof(request))) {
		drm_agp_mem_t *entry = dev->agp->memory;

		dev->agp->memory = entry->next;
		dev->agp->memory->prev = NULL;
		drm_free_agp(entry->memory, entry->pages);
		drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
		return -EFAULT;
	}

	return 0;
}

/**
 * Search for the AGP memory entry associated with a handle.
 *
 * \param dev DRM device structure.
 * \param handle AGP memory handle.
 * \return pointer to the drm_agp_mem structure associated with \p handle.
 *
 * Walks through drm_agp_head::memory until finding a matching handle.
 */
static drm_agp_mem_t *drm_agp_lookup_entry(drm_device_t * dev,
					   unsigned long handle)
{
	drm_agp_mem_t *entry;

	for (entry = dev->agp->memory; entry; entry = entry->next) {
		if (entry->handle == handle)
			return entry;
	}
	return NULL;
}

/**
 * Unbind AGP memory from the GATT (ioctl).
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_agp_binding structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and acquired, looks-up the AGP memory
 * entry and passes it to the unbind_agp() function.
 */
int drm_agp_unbind(drm_device_t *dev, drm_agp_binding_t *request)
{
	drm_agp_mem_t *entry;
	int ret;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	if (!(entry = drm_agp_lookup_entry(dev, request->handle)))
		return -EINVAL;
	if (!entry->bound)
		return -EINVAL;
	ret = drm_unbind_agp(entry->memory);
	if (ret == 0)
		entry->bound = 0;
	return ret;
}
EXPORT_SYMBOL(drm_agp_unbind);

int drm_agp_unbind_ioctl(struct inode *inode, struct file *filp,
			 unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_binding_t request;

	if (copy_from_user
	    (&request, (drm_agp_binding_t __user *) arg, sizeof(request)))
		return -EFAULT;

	return drm_agp_unbind(dev, &request);
}

/**
 * Bind AGP memory into the GATT (ioctl)
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_agp_binding structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired and that no memory
 * is currently bound into the GATT. Looks-up the AGP memory entry and passes
 * it to bind_agp() function.
 */
int drm_agp_bind(drm_device_t *dev, drm_agp_binding_t *request)
{
	drm_agp_mem_t *entry;
	int retcode;
	int page;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	if (!(entry = drm_agp_lookup_entry(dev, request->handle)))
		return -EINVAL;
	if (entry->bound)
		return -EINVAL;
	page = (request->offset + PAGE_SIZE - 1) / PAGE_SIZE;
	if ((retcode = drm_bind_agp(entry->memory, page)))
		return retcode;
	entry->bound = dev->agp->base + (page << PAGE_SHIFT);
	DRM_DEBUG("base = 0x%lx entry->bound = 0x%lx\n",
		  dev->agp->base, entry->bound);
	return 0;
}
EXPORT_SYMBOL(drm_agp_bind);

int drm_agp_bind_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_binding_t request;

	if (copy_from_user
	    (&request, (drm_agp_binding_t __user *) arg, sizeof(request)))
		return -EFAULT;

	return drm_agp_bind(dev, &request);
}

/**
 * Free AGP memory (ioctl).
 *
 * \param inode device inode.
 * \param filp file pointer.
 * \param cmd command.
 * \param arg pointer to a drm_agp_buffer structure.
 * \return zero on success or a negative number on failure.
 *
 * Verifies the AGP device is present and has been acquired and looks up the
 * AGP memory entry. If the memory it's currently bound, unbind it via
 * unbind_agp(). Frees it via free_agp() as well as the entry itself
 * and unlinks from the doubly linked list it's inserted in.
 */
int drm_agp_free(drm_device_t *dev, drm_agp_buffer_t *request)
{
	drm_agp_mem_t *entry;

	if (!dev->agp || !dev->agp->acquired)
		return -EINVAL;
	if (!(entry = drm_agp_lookup_entry(dev, request->handle)))
		return -EINVAL;
	if (entry->bound)
		drm_unbind_agp(entry->memory);

	if (entry->prev)
		entry->prev->next = entry->next;
	else
		dev->agp->memory = entry->next;

	if (entry->next)
		entry->next->prev = entry->prev;

	drm_free_agp(entry->memory, entry->pages);
	drm_free(entry, sizeof(*entry), DRM_MEM_AGPLISTS);
	return 0;
}
EXPORT_SYMBOL(drm_agp_free);

int drm_agp_free_ioctl(struct inode *inode, struct file *filp,
		       unsigned int cmd, unsigned long arg)
{
	drm_file_t *priv = filp->private_data;
	drm_device_t *dev = priv->head->dev;
	drm_agp_buffer_t request;

	if (copy_from_user
	    (&request, (drm_agp_buffer_t __user *) arg, sizeof(request)))
		return -EFAULT;

	return drm_agp_free(dev, &request);
}

/**
 * Initialize the AGP resources.
 *
 * \return pointer to a drm_agp_head structure.
 *
 * Gets the drm_agp_t structure which is made available by the agpgart module
 * via the inter_module_* functions. Creates and initializes a drm_agp_head
 * structure.
 */
drm_agp_head_t *drm_agp_init(drm_device_t * dev)
{
	drm_agp_head_t *head = NULL;

	if (!(head = drm_alloc(sizeof(*head), DRM_MEM_AGPLISTS)))
		return NULL;
	memset((void *)head, 0, sizeof(*head));
	head->bridge = agp_find_bridge(dev->pdev);
	if (!head->bridge) {
		if (!(head->bridge = agp_backend_acquire(dev->pdev))) {
			drm_free(head, sizeof(*head), DRM_MEM_AGPLISTS);
			return NULL;
		}
		agp_copy_info(head->bridge, &head->agp_info);
		agp_backend_release(head->bridge);
	} else {
		agp_copy_info(head->bridge, &head->agp_info);
	}
	if (head->agp_info.chipset == NOT_SUPPORTED) {
		drm_free(head, sizeof(*head), DRM_MEM_AGPLISTS);
		return NULL;
	}
	head->memory = NULL;
	head->cant_use_aperture = head->agp_info.cant_use_aperture;
	head->page_mask = head->agp_info.page_mask;

	return head;
}

/** Calls agp_allocate_memory() */
DRM_AGP_MEM *drm_agp_allocate_memory(struct agp_bridge_data * bridge,
				     size_t pages, u32 type)
{
	return agp_allocate_memory(bridge, pages, type);
}

/** Calls agp_free_memory() */
int drm_agp_free_memory(DRM_AGP_MEM * handle)
{
	if (!handle)
		return 0;
	agp_free_memory(handle);
	return 1;
}

/** Calls agp_bind_memory() */
int drm_agp_bind_memory(DRM_AGP_MEM * handle, off_t start)
{
	if (!handle)
		return -EINVAL;
	return agp_bind_memory(handle, start);
}

/** Calls agp_unbind_memory() */
int drm_agp_unbind_memory(DRM_AGP_MEM * handle)
{
	if (!handle)
		return -EINVAL;
	return agp_unbind_memory(handle);
}

#endif				/* __OS_HAS_AGP */
