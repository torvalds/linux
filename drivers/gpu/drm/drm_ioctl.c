/**
 * \file drm_ioctl.c
 * IOCTL processing for DRM
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Gareth Hughes <gareth@valinux.com>
 */

/*
 * Created: Fri Jan  8 09:01:26 1999 by faith@valinux.com
 *
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
#include "drm_core.h"

#include "linux/pci.h"

/**
 * Get the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from drm_device::unique into user space.
 */
int drm_getunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;
	struct drm_master *master = file_priv->master;

	if (u->unique_len >= master->unique_len) {
		if (copy_to_user(u->unique, master->unique, master->unique_len))
			return -EFAULT;
	}
	u->unique_len = master->unique_len;

	return 0;
}

/**
 * Set the bus id.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_unique structure.
 * \return zero on success or a negative number on failure.
 *
 * Copies the bus id from userspace into drm_device::unique, and verifies that
 * it matches the device this DRM is attached to (EINVAL otherwise).  Deprecated
 * in interface version 1.1 and will return EBUSY when setversion has requested
 * version 1.1 or greater.
 */
int drm_setunique(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_unique *u = data;
	struct drm_master *master = file_priv->master;
	int domain, bus, slot, func, ret;

	if (master->unique_len || master->unique)
		return -EBUSY;

	if (!u->unique_len || u->unique_len > 1024)
		return -EINVAL;

	master->unique_len = u->unique_len;
	master->unique_size = u->unique_len + 1;
	master->unique = kmalloc(master->unique_size, GFP_KERNEL);
	if (!master->unique)
		return -ENOMEM;
	if (copy_from_user(master->unique, u->unique, master->unique_len))
		return -EFAULT;

	master->unique[master->unique_len] = '\0';

	dev->devname = kmalloc(strlen(dev->driver->pci_driver.name) +
			       strlen(master->unique) + 2, GFP_KERNEL);
	if (!dev->devname)
		return -ENOMEM;

	sprintf(dev->devname, "%s@%s", dev->driver->pci_driver.name,
		master->unique);

	/* Return error if the busid submitted doesn't match the device's actual
	 * busid.
	 */
	ret = sscanf(master->unique, "PCI:%d:%d:%d", &bus, &slot, &func);
	if (ret != 3)
		return -EINVAL;
	domain = bus >> 8;
	bus &= 0xff;

	if ((domain != drm_get_pci_domain(dev)) ||
	    (bus != dev->pdev->bus->number) ||
	    (slot != PCI_SLOT(dev->pdev->devfn)) ||
	    (func != PCI_FUNC(dev->pdev->devfn)))
		return -EINVAL;

	return 0;
}

static int drm_set_busid(struct drm_device *dev, struct drm_file *file_priv)
{
	struct drm_master *master = file_priv->master;
	int len;

	if (drm_core_check_feature(dev, DRIVER_USE_PLATFORM_DEVICE)) {
		master->unique_len = 10 + strlen(dev->platformdev->name);
		master->unique = kmalloc(master->unique_len + 1, GFP_KERNEL);

		if (master->unique == NULL)
			return -ENOMEM;

		len = snprintf(master->unique, master->unique_len,
			"platform:%s", dev->platformdev->name);

		if (len > master->unique_len)
			DRM_ERROR("Unique buffer overflowed\n");

		dev->devname =
			kmalloc(strlen(dev->platformdev->name) +
				master->unique_len + 2, GFP_KERNEL);

		if (dev->devname == NULL)
			return -ENOMEM;

		sprintf(dev->devname, "%s@%s", dev->platformdev->name,
			master->unique);

	} else {
		master->unique_len = 40;
		master->unique_size = master->unique_len;
		master->unique = kmalloc(master->unique_size, GFP_KERNEL);
		if (master->unique == NULL)
			return -ENOMEM;

		len = snprintf(master->unique, master->unique_len,
			"pci:%04x:%02x:%02x.%d",
			drm_get_pci_domain(dev),
			dev->pdev->bus->number,
			PCI_SLOT(dev->pdev->devfn),
			PCI_FUNC(dev->pdev->devfn));
		if (len >= master->unique_len)
			DRM_ERROR("buffer overflow");
		else
			master->unique_len = len;

		dev->devname =
			kmalloc(strlen(dev->driver->pci_driver.name) +
				master->unique_len + 2, GFP_KERNEL);

		if (dev->devname == NULL)
			return -ENOMEM;

		sprintf(dev->devname, "%s@%s", dev->driver->pci_driver.name,
			master->unique);
	}

	return 0;
}

/**
 * Get a mapping information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_map structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the mapping with the specified offset and copies its information
 * into userspace
 */
int drm_getmap(struct drm_device *dev, void *data,
	       struct drm_file *file_priv)
{
	struct drm_map *map = data;
	struct drm_map_list *r_list = NULL;
	struct list_head *list;
	int idx;
	int i;

	idx = map->offset;

	mutex_lock(&dev->struct_mutex);
	if (idx < 0) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	i = 0;
	list_for_each(list, &dev->maplist) {
		if (i == idx) {
			r_list = list_entry(list, struct drm_map_list, head);
			break;
		}
		i++;
	}
	if (!r_list || !r_list->map) {
		mutex_unlock(&dev->struct_mutex);
		return -EINVAL;
	}

	map->offset = r_list->map->offset;
	map->size = r_list->map->size;
	map->type = r_list->map->type;
	map->flags = r_list->map->flags;
	map->handle = (void *)(unsigned long) r_list->user_token;
	map->mtrr = r_list->map->mtrr;
	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Get client information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_client structure.
 *
 * \return zero on success or a negative number on failure.
 *
 * Searches for the client with the specified index and copies its information
 * into userspace
 */
int drm_getclient(struct drm_device *dev, void *data,
		  struct drm_file *file_priv)
{
	struct drm_client *client = data;
	struct drm_file *pt;
	int idx;
	int i;

	idx = client->idx;
	mutex_lock(&dev->struct_mutex);

	i = 0;
	list_for_each_entry(pt, &dev->filelist, lhead) {
		if (i++ >= idx) {
			client->auth = pt->authenticated;
			client->pid = pt->pid;
			client->uid = pt->uid;
			client->magic = pt->magic;
			client->iocs = pt->ioctl_count;
			mutex_unlock(&dev->struct_mutex);

			return 0;
		}
	}
	mutex_unlock(&dev->struct_mutex);

	return -EINVAL;
}

/**
 * Get statistics information.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_stats structure.
 *
 * \return zero on success or a negative number on failure.
 */
int drm_getstats(struct drm_device *dev, void *data,
		 struct drm_file *file_priv)
{
	struct drm_stats *stats = data;
	int i;

	memset(stats, 0, sizeof(*stats));

	mutex_lock(&dev->struct_mutex);

	for (i = 0; i < dev->counters; i++) {
		if (dev->types[i] == _DRM_STAT_LOCK)
			stats->data[i].value =
			    (file_priv->master->lock.hw_lock ? file_priv->master->lock.hw_lock->lock : 0);
		else
			stats->data[i].value = atomic_read(&dev->counts[i]);
		stats->data[i].type = dev->types[i];
	}

	stats->count = dev->counters;

	mutex_unlock(&dev->struct_mutex);

	return 0;
}

/**
 * Setversion ioctl.
 *
 * \param inode device inode.
 * \param file_priv DRM file private.
 * \param cmd command.
 * \param arg user argument, pointing to a drm_lock structure.
 * \return zero on success or negative number on failure.
 *
 * Sets the requested interface version
 */
int drm_setversion(struct drm_device *dev, void *data, struct drm_file *file_priv)
{
	struct drm_set_version *sv = data;
	int if_version, retcode = 0;

	if (sv->drm_di_major != -1) {
		if (sv->drm_di_major != DRM_IF_MAJOR ||
		    sv->drm_di_minor < 0 || sv->drm_di_minor > DRM_IF_MINOR) {
			retcode = -EINVAL;
			goto done;
		}
		if_version = DRM_IF_VERSION(sv->drm_di_major,
					    sv->drm_di_minor);
		dev->if_version = max(if_version, dev->if_version);
		if (sv->drm_di_minor >= 1) {
			/*
			 * Version 1.1 includes tying of DRM to specific device
			 */
			drm_set_busid(dev, file_priv);
		}
	}

	if (sv->drm_dd_major != -1) {
		if (sv->drm_dd_major != dev->driver->major ||
		    sv->drm_dd_minor < 0 || sv->drm_dd_minor >
		    dev->driver->minor) {
			retcode = -EINVAL;
			goto done;
		}

		if (dev->driver->set_version)
			dev->driver->set_version(dev, sv);
	}

done:
	sv->drm_di_major = DRM_IF_MAJOR;
	sv->drm_di_minor = DRM_IF_MINOR;
	sv->drm_dd_major = dev->driver->major;
	sv->drm_dd_minor = dev->driver->minor;

	return retcode;
}

/** No-op ioctl. */
int drm_noop(struct drm_device *dev, void *data,
	     struct drm_file *file_priv)
{
	DRM_DEBUG("\n");
	return 0;
}
