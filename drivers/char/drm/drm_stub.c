/**
 * \file drm_stub.h
 * Stub support
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 */

/*
 * Created: Fri Jan 19 10:48:35 2001 by faith@acm.org
 *
 * Copyright 2001 VA Linux Systems, Inc., Sunnyvale, California.
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
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include "drmP.h"
#include "drm_core.h"

unsigned int drm_cards_limit = 16;	/* Enough for one machine */
unsigned int drm_debug = 0;		/* 1 to enable debug output */
EXPORT_SYMBOL(drm_debug);

MODULE_AUTHOR( CORE_AUTHOR );
MODULE_DESCRIPTION( CORE_DESC );
MODULE_LICENSE("GPL and additional rights");
MODULE_PARM_DESC(cards_limit, "Maximum number of graphics cards");
MODULE_PARM_DESC(debug, "Enable debug output");

module_param_named(cards_limit, drm_cards_limit, int, 0444);
module_param_named(debug, drm_debug, int, 0600);

drm_head_t **drm_heads;
struct drm_sysfs_class *drm_class;
struct proc_dir_entry *drm_proc_root;

static int drm_fill_in_dev(drm_device_t *dev, struct pci_dev *pdev, const struct pci_device_id *ent, struct drm_driver *driver)
{
	int retcode;

	spin_lock_init(&dev->count_lock);
	init_timer( &dev->timer );
	sema_init( &dev->struct_sem, 1 );
	sema_init( &dev->ctxlist_sem, 1 );

	dev->pdev   = pdev;

#ifdef __alpha__
	dev->hose   = pdev->sysdata;
	dev->pci_domain = dev->hose->bus->number;
#else
	dev->pci_domain = 0;
#endif
	dev->pci_bus = pdev->bus->number;
	dev->pci_slot = PCI_SLOT(pdev->devfn);
	dev->pci_func = PCI_FUNC(pdev->devfn);
	dev->irq = pdev->irq;

	dev->maplist = drm_calloc(1, sizeof(*dev->maplist), DRM_MEM_MAPS);
	if (dev->maplist == NULL)
		return -ENOMEM;
	INIT_LIST_HEAD(&dev->maplist->head);

	/* the DRM has 6 basic counters */
	dev->counters = 6;
	dev->types[0]  = _DRM_STAT_LOCK;
	dev->types[1]  = _DRM_STAT_OPENS;
	dev->types[2]  = _DRM_STAT_CLOSES;
	dev->types[3]  = _DRM_STAT_IOCTLS;
	dev->types[4]  = _DRM_STAT_LOCKS;
	dev->types[5]  = _DRM_STAT_UNLOCKS;

	dev->driver = driver;
	
	if (dev->driver->preinit)
		if ((retcode = dev->driver->preinit(dev, ent->driver_data)))
			goto error_out_unreg;

	if (drm_core_has_AGP(dev)) {
		if (drm_device_is_agp(dev))
			dev->agp = drm_agp_init(dev);
		if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP) && (dev->agp == NULL)) {
			DRM_ERROR( "Cannot initialize the agpgart module.\n" );
			retcode = -EINVAL;
			goto error_out_unreg;
		}
		if (drm_core_has_MTRR(dev)) {
			if (dev->agp)
				dev->agp->agp_mtrr = mtrr_add( dev->agp->agp_info.aper_base,
							       dev->agp->agp_info.aper_size*1024*1024,
							       MTRR_TYPE_WRCOMB,
							       1 );
		}
	}

	retcode = drm_ctxbitmap_init( dev );
	if( retcode ) {
		DRM_ERROR( "Cannot allocate memory for context bitmap.\n" );
		goto error_out_unreg;
	}

	return 0;
	
error_out_unreg:
	drm_takedown(dev);
	return retcode;
}

/**
 * File \c open operation.
 *
 * \param inode device inode.
 * \param filp file pointer.
 *
 * Puts the dev->fops corresponding to the device minor number into
 * \p filp, call the \c open method, and restore the file operations.
 */
int drm_stub_open(struct inode *inode, struct file *filp)
{
	drm_device_t *dev = NULL;
	int minor = iminor(inode);
	int err = -ENODEV;
	struct file_operations *old_fops;
	
	DRM_DEBUG("\n");

	if (!((minor >= 0) && (minor < drm_cards_limit)))
		return -ENODEV;

	if (!drm_heads[minor])
		return -ENODEV;
	
	if (!(dev = drm_heads[minor]->dev))
		return -ENODEV;

	old_fops = filp->f_op;
	filp->f_op = fops_get(&dev->driver->fops);
	if (filp->f_op->open && (err = filp->f_op->open(inode, filp))) {
		fops_put(filp->f_op);
		filp->f_op = fops_get(old_fops);
	}
	fops_put(old_fops);

	return err;
}

/**
 * Get a secondary minor number.
 *
 * \param dev device data structure
 * \param sec-minor structure to hold the assigned minor
 * \return negative number on failure.
 *
 * Search an empty entry and initialize it to the given parameters, and
 * create the proc init entry via proc_init(). This routines assigns
 * minor numbers to secondary heads of multi-headed cards
 */
static int drm_get_head(drm_device_t *dev, drm_head_t *head)
{
	drm_head_t **heads = drm_heads;
	int ret;
	int minor;

	DRM_DEBUG("\n");

	for (minor = 0; minor < drm_cards_limit; minor++, heads++) {
		if (!*heads) {
			
			*head = (drm_head_t) {
				.dev = dev,
				.device = MKDEV(DRM_MAJOR, minor),
				.minor = minor,
			};
			
			if ((ret = drm_proc_init(dev, minor, drm_proc_root, &head->dev_root))) {
				printk (KERN_ERR "DRM: Failed to initialize /proc/dri.\n");
				goto err_g1;
			}

			
			head->dev_class = drm_sysfs_device_add(drm_class,
							       MKDEV(DRM_MAJOR,
								     minor),
							       &dev->pdev->dev,
							       "card%d", minor);
			if (IS_ERR(head->dev_class)) {
				printk(KERN_ERR "DRM: Error sysfs_device_add.\n");
				ret = PTR_ERR(head->dev_class);
				goto err_g2;
			}
			*heads = head;

			DRM_DEBUG("new minor assigned %d\n", minor);
			return 0;
		}
	}
	DRM_ERROR("out of minors\n");
	return -ENOMEM;
err_g2:
	drm_proc_cleanup(minor, drm_proc_root, head->dev_root);
err_g1:
	*head = (drm_head_t) {.dev = NULL};
	return ret;
}
		
/**
 * Register.
 *
 * \param pdev - PCI device structure
 * \param ent entry from the PCI ID table with device type flags
 * \return zero on success or a negative number on failure.
 *
 * Attempt to gets inter module "drm" information. If we are first
 * then register the character device and inter module information.
 * Try and register, if we fail to register, backout previous work.
 */
int drm_get_dev(struct pci_dev *pdev, const struct pci_device_id *ent,
	      struct drm_driver *driver)
{
	drm_device_t *dev;
	int ret;

	DRM_DEBUG("\n");

	dev = drm_calloc(1, sizeof(*dev), DRM_MEM_STUB);
	if (!dev)
		return -ENOMEM;

	pci_enable_device(pdev);

	if ((ret = drm_fill_in_dev(dev, pdev, ent, driver))) {
		printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
		goto err_g1;
	}
	if ((ret = drm_get_head(dev, &dev->primary)))
		goto err_g1;

	/* postinit is a required function to display the signon banner */
	/* drivers add secondary heads here if needed */
	if ((ret = dev->driver->postinit(dev, ent->driver_data)))
		goto err_g1;

	return 0;

err_g1:
	drm_free(dev, sizeof(*dev), DRM_MEM_STUB);
	return ret;
}
EXPORT_SYMBOL(drm_get_dev);

/**
 * Put a device minor number.
 *
 * \param dev device data structure
 * \return always zero
 *
 * Cleans up the proc resources. If it is the last minor then release the foreign
 * "drm" data, otherwise unregisters the "drm" data, frees the dev list and
 * unregisters the character device.
 */
int drm_put_dev(drm_device_t * dev)
{
	DRM_DEBUG("release primary %s\n", dev->driver->pci_driver.name);

	if (dev->unique) {
		drm_free(dev->unique, strlen(dev->unique) + 1, DRM_MEM_DRIVER);
		dev->unique = NULL;
		dev->unique_len = 0;
	}
	if (dev->devname) {
		drm_free(dev->devname, strlen(dev->devname) + 1,
			 DRM_MEM_DRIVER);
		dev->devname = NULL;
	}
	drm_free(dev, sizeof(*dev), DRM_MEM_STUB);
	return 0;
}

/**
 * Put a secondary minor number.
 *
 * \param sec_minor - structure to be released
 * \return always zero
 *
 * Cleans up the proc resources. Not legal for this to be the
 * last minor released.
 *
 */
int drm_put_head(drm_head_t *head)
{
	int minor = head->minor;
	
	DRM_DEBUG("release secondary minor %d\n", minor);
	
	drm_proc_cleanup(minor, drm_proc_root, head->dev_root);
	drm_sysfs_device_remove(MKDEV(DRM_MAJOR, head->minor));
	
	*head = (drm_head_t){.dev = NULL};

	drm_heads[minor] = NULL;
	
	return 0;
}

