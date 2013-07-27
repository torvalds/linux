/* drm_pci.h -- PCI DMA memory management wrappers for DRM -*- linux-c -*- */
/**
 * \file drm_pci.c
 * \brief Functions and ioctls to manage PCI memory
 *
 * \warning These interfaces aren't stable yet.
 *
 * \todo Implement the remaining ioctl's for the PCI pools.
 * \todo The wrappers here are so thin that they would be better off inlined..
 *
 * \author José Fonseca <jrfonseca@tungstengraphics.com>
 * \author Leif Delgass <ldelgass@retinalburn.net>
 */

/*
 * Copyright 2003 José Fonseca.
 * Copyright 2003 Leif Delgass.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <drm/drmP.h>

/**********************************************************************/
/** \name PCI memory */
/*@{*/

/**
 * \brief Allocate a PCI consistent memory block, for DMA.
 */
drm_dma_handle_t *drm_pci_alloc(struct drm_device * dev, size_t size, size_t align)
{
	drm_dma_handle_t *dmah;
#if 1
	unsigned long addr;
	size_t sz;
#endif

	/* pci_alloc_consistent only guarantees alignment to the smallest
	 * PAGE_SIZE order which is greater than or equal to the requested size.
	 * Return NULL here for now to make sure nobody tries for larger alignment
	 */
	if (align > size)
		return NULL;

	dmah = kmalloc(sizeof(drm_dma_handle_t), GFP_KERNEL);
	if (!dmah)
		return NULL;

	dmah->size = size;
	dmah->vaddr = dma_alloc_coherent(&dev->pdev->dev, size, &dmah->busaddr, GFP_KERNEL | __GFP_COMP);

	if (dmah->vaddr == NULL) {
		kfree(dmah);
		return NULL;
	}

	memset(dmah->vaddr, 0, size);

	/* XXX - Is virt_to_page() legal for consistent mem? */
	/* Reserve */
	for (addr = (unsigned long)dmah->vaddr, sz = size;
	     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		SetPageReserved(virt_to_page(addr));
	}

	return dmah;
}

EXPORT_SYMBOL(drm_pci_alloc);

/**
 * \brief Free a PCI consistent memory block without freeing its descriptor.
 *
 * This function is for internal use in the Linux-specific DRM core code.
 */
void __drm_pci_free(struct drm_device * dev, drm_dma_handle_t * dmah)
{
#if 1
	unsigned long addr;
	size_t sz;
#endif

	if (dmah->vaddr) {
		/* XXX - Is virt_to_page() legal for consistent mem? */
		/* Unreserve */
		for (addr = (unsigned long)dmah->vaddr, sz = dmah->size;
		     sz > 0; addr += PAGE_SIZE, sz -= PAGE_SIZE) {
			ClearPageReserved(virt_to_page(addr));
		}
		dma_free_coherent(&dev->pdev->dev, dmah->size, dmah->vaddr,
				  dmah->busaddr);
	}
}

/**
 * \brief Free a PCI consistent memory block
 */
void drm_pci_free(struct drm_device * dev, drm_dma_handle_t * dmah)
{
	__drm_pci_free(dev, dmah);
	kfree(dmah);
}

EXPORT_SYMBOL(drm_pci_free);

#ifdef CONFIG_PCI

static int drm_get_pci_domain(struct drm_device *dev)
{
#ifndef __alpha__
	/* For historical reasons, drm_get_pci_domain() is busticated
	 * on most archs and has to remain so for userspace interface
	 * < 1.4, except on alpha which was right from the beginning
	 */
	if (dev->if_version < 0x10004)
		return 0;
#endif /* __alpha__ */

	return pci_domain_nr(dev->pdev->bus);
}

static int drm_pci_get_irq(struct drm_device *dev)
{
	return dev->pdev->irq;
}

static const char *drm_pci_get_name(struct drm_device *dev)
{
	struct pci_driver *pdriver = dev->driver->kdriver.pci;
	return pdriver->name;
}

static int drm_pci_set_busid(struct drm_device *dev, struct drm_master *master)
{
	int len, ret;
	struct pci_driver *pdriver = dev->driver->kdriver.pci;
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

	if (len >= master->unique_len) {
		DRM_ERROR("buffer overflow");
		ret = -EINVAL;
		goto err;
	} else
		master->unique_len = len;

	dev->devname =
		kmalloc(strlen(pdriver->name) +
			master->unique_len + 2, GFP_KERNEL);

	if (dev->devname == NULL) {
		ret = -ENOMEM;
		goto err;
	}

	sprintf(dev->devname, "%s@%s", pdriver->name,
		master->unique);

	return 0;
err:
	return ret;
}

static int drm_pci_set_unique(struct drm_device *dev,
			      struct drm_master *master,
			      struct drm_unique *u)
{
	int domain, bus, slot, func, ret;
	const char *bus_name;

	master->unique_len = u->unique_len;
	master->unique_size = u->unique_len + 1;
	master->unique = kmalloc(master->unique_size, GFP_KERNEL);
	if (!master->unique) {
		ret = -ENOMEM;
		goto err;
	}

	if (copy_from_user(master->unique, u->unique, master->unique_len)) {
		ret = -EFAULT;
		goto err;
	}

	master->unique[master->unique_len] = '\0';

	bus_name = dev->driver->bus->get_name(dev);
	dev->devname = kmalloc(strlen(bus_name) +
			       strlen(master->unique) + 2, GFP_KERNEL);
	if (!dev->devname) {
		ret = -ENOMEM;
		goto err;
	}

	sprintf(dev->devname, "%s@%s", bus_name,
		master->unique);

	/* Return error if the busid submitted doesn't match the device's actual
	 * busid.
	 */
	ret = sscanf(master->unique, "PCI:%d:%d:%d", &bus, &slot, &func);
	if (ret != 3) {
		ret = -EINVAL;
		goto err;
	}

	domain = bus >> 8;
	bus &= 0xff;

	if ((domain != drm_get_pci_domain(dev)) ||
	    (bus != dev->pdev->bus->number) ||
	    (slot != PCI_SLOT(dev->pdev->devfn)) ||
	    (func != PCI_FUNC(dev->pdev->devfn))) {
		ret = -EINVAL;
		goto err;
	}
	return 0;
err:
	return ret;
}


static int drm_pci_irq_by_busid(struct drm_device *dev, struct drm_irq_busid *p)
{
	if ((p->busnum >> 8) != drm_get_pci_domain(dev) ||
	    (p->busnum & 0xff) != dev->pdev->bus->number ||
	    p->devnum != PCI_SLOT(dev->pdev->devfn) || p->funcnum != PCI_FUNC(dev->pdev->devfn))
		return -EINVAL;

	p->irq = dev->pdev->irq;

	DRM_DEBUG("%d:%d:%d => IRQ %d\n", p->busnum, p->devnum, p->funcnum,
		  p->irq);
	return 0;
}

static int drm_pci_agp_init(struct drm_device *dev)
{
	if (drm_core_has_AGP(dev)) {
		if (drm_pci_device_is_agp(dev))
			dev->agp = drm_agp_init(dev);
		if (drm_core_check_feature(dev, DRIVER_REQUIRE_AGP)
		    && (dev->agp == NULL)) {
			DRM_ERROR("Cannot initialize the agpgart module.\n");
			return -EINVAL;
		}
		if (drm_core_has_MTRR(dev)) {
			if (dev->agp)
				dev->agp->agp_mtrr = arch_phys_wc_add(
					dev->agp->agp_info.aper_base,
					dev->agp->agp_info.aper_size *
					1024 * 1024);
		}
	}
	return 0;
}

static void drm_pci_agp_destroy(struct drm_device *dev)
{
	if (drm_core_has_AGP(dev) && dev->agp) {
		if (drm_core_has_MTRR(dev))
			arch_phys_wc_del(dev->agp->agp_mtrr);
		drm_agp_clear(dev);
		drm_agp_destroy(dev->agp);
		dev->agp = NULL;
	}
}

static struct drm_bus drm_pci_bus = {
	.bus_type = DRIVER_BUS_PCI,
	.get_irq = drm_pci_get_irq,
	.get_name = drm_pci_get_name,
	.set_busid = drm_pci_set_busid,
	.set_unique = drm_pci_set_unique,
	.irq_by_busid = drm_pci_irq_by_busid,
	.agp_init = drm_pci_agp_init,
	.agp_destroy = drm_pci_agp_destroy,
};

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
int drm_get_pci_dev(struct pci_dev *pdev, const struct pci_device_id *ent,
		    struct drm_driver *driver)
{
	struct drm_device *dev;
	int ret;

	DRM_DEBUG("\n");

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	ret = pci_enable_device(pdev);
	if (ret)
		goto err_g1;

	dev->pdev = pdev;
	dev->dev = &pdev->dev;

	dev->pci_device = pdev->device;
	dev->pci_vendor = pdev->vendor;

#ifdef __alpha__
	dev->hose = pdev->sysdata;
#endif

	mutex_lock(&drm_global_mutex);

	if ((ret = drm_fill_in_dev(dev, ent, driver))) {
		printk(KERN_ERR "DRM: Fill_in_dev failed.\n");
		goto err_g2;
	}

	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		pci_set_drvdata(pdev, dev);
		ret = drm_get_minor(dev, &dev->control, DRM_MINOR_CONTROL);
		if (ret)
			goto err_g2;
	}

	if ((ret = drm_get_minor(dev, &dev->primary, DRM_MINOR_LEGACY)))
		goto err_g3;

	if (dev->driver->load) {
		ret = dev->driver->load(dev, ent->driver_data);
		if (ret)
			goto err_g4;
	}

	/* setup the grouping for the legacy output */
	if (drm_core_check_feature(dev, DRIVER_MODESET)) {
		ret = drm_mode_group_init_legacy_group(dev,
						&dev->primary->mode_group);
		if (ret)
			goto err_g4;
	}

	list_add_tail(&dev->driver_item, &driver->device_list);

	DRM_INFO("Initialized %s %d.%d.%d %s for %s on minor %d\n",
		 driver->name, driver->major, driver->minor, driver->patchlevel,
		 driver->date, pci_name(pdev), dev->primary->index);

	mutex_unlock(&drm_global_mutex);
	return 0;

err_g4:
	drm_put_minor(&dev->primary);
err_g3:
	if (drm_core_check_feature(dev, DRIVER_MODESET))
		drm_put_minor(&dev->control);
err_g2:
	pci_disable_device(pdev);
err_g1:
	kfree(dev);
	mutex_unlock(&drm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(drm_get_pci_dev);

/**
 * PCI device initialization. Called direct from modules at load time.
 *
 * \return zero on success or a negative number on failure.
 *
 * Initializes a drm_device structures,registering the
 * stubs and initializing the AGP device.
 *
 * Expands the \c DRIVER_PREINIT and \c DRIVER_POST_INIT macros before and
 * after the initialization for driver customization.
 */
int drm_pci_init(struct drm_driver *driver, struct pci_driver *pdriver)
{
	struct pci_dev *pdev = NULL;
	const struct pci_device_id *pid;
	int i;

	DRM_DEBUG("\n");

	INIT_LIST_HEAD(&driver->device_list);
	driver->kdriver.pci = pdriver;
	driver->bus = &drm_pci_bus;

	if (driver->driver_features & DRIVER_MODESET)
		return pci_register_driver(pdriver);

	/* If not using KMS, fall back to stealth mode manual scanning. */
	for (i = 0; pdriver->id_table[i].vendor != 0; i++) {
		pid = &pdriver->id_table[i];

		/* Loop around setting up a DRM device for each PCI device
		 * matching our ID and device class.  If we had the internal
		 * function that pci_get_subsys and pci_get_class used, we'd
		 * be able to just pass pid in instead of doing a two-stage
		 * thing.
		 */
		pdev = NULL;
		while ((pdev =
			pci_get_subsys(pid->vendor, pid->device, pid->subvendor,
				       pid->subdevice, pdev)) != NULL) {
			if ((pdev->class & pid->class_mask) != pid->class)
				continue;

			/* stealth mode requires a manual probe */
			pci_dev_get(pdev);
			drm_get_pci_dev(pdev, pid, driver);
		}
	}
	return 0;
}

int drm_pcie_get_speed_cap_mask(struct drm_device *dev, u32 *mask)
{
	struct pci_dev *root;
	u32 lnkcap, lnkcap2;

	*mask = 0;
	if (!dev->pdev)
		return -EINVAL;

	root = dev->pdev->bus->self;

	/* we've been informed via and serverworks don't make the cut */
	if (root->vendor == PCI_VENDOR_ID_VIA ||
	    root->vendor == PCI_VENDOR_ID_SERVERWORKS)
		return -EINVAL;

	pcie_capability_read_dword(root, PCI_EXP_LNKCAP, &lnkcap);
	pcie_capability_read_dword(root, PCI_EXP_LNKCAP2, &lnkcap2);

	if (lnkcap2) {	/* PCIe r3.0-compliant */
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_2_5GB)
			*mask |= DRM_PCIE_SPEED_25;
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_5_0GB)
			*mask |= DRM_PCIE_SPEED_50;
		if (lnkcap2 & PCI_EXP_LNKCAP2_SLS_8_0GB)
			*mask |= DRM_PCIE_SPEED_80;
	} else {	/* pre-r3.0 */
		if (lnkcap & PCI_EXP_LNKCAP_SLS_2_5GB)
			*mask |= DRM_PCIE_SPEED_25;
		if (lnkcap & PCI_EXP_LNKCAP_SLS_5_0GB)
			*mask |= (DRM_PCIE_SPEED_25 | DRM_PCIE_SPEED_50);
	}

	DRM_INFO("probing gen 2 caps for device %x:%x = %x/%x\n", root->vendor, root->device, lnkcap, lnkcap2);
	return 0;
}
EXPORT_SYMBOL(drm_pcie_get_speed_cap_mask);

#else

int drm_pci_init(struct drm_driver *driver, struct pci_driver *pdriver)
{
	return -1;
}

#endif

EXPORT_SYMBOL(drm_pci_init);

/*@}*/
void drm_pci_exit(struct drm_driver *driver, struct pci_driver *pdriver)
{
	struct drm_device *dev, *tmp;
	DRM_DEBUG("\n");

	if (driver->driver_features & DRIVER_MODESET) {
		pci_unregister_driver(pdriver);
	} else {
		list_for_each_entry_safe(dev, tmp, &driver->device_list, driver_item)
			drm_put_dev(dev);
	}
	DRM_INFO("Module unloaded\n");
}
EXPORT_SYMBOL(drm_pci_exit);
