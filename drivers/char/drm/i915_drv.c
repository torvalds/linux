/* i915_drv.c -- i830,i845,i855,i865,i915 driver -*- linux-c -*-
 */

/**************************************************************************
 * 
 * Copyright 2003 Tungsten Graphics, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 * 
 **************************************************************************/

#include "drmP.h"
#include "drm.h"
#include "i915_drm.h"
#include "i915_drv.h"

#include "drm_pciids.h"

int postinit( struct drm_device *dev, unsigned long flags )
{
	dev->counters += 4;
	dev->types[6] = _DRM_STAT_IRQ;
	dev->types[7] = _DRM_STAT_PRIMARY;
	dev->types[8] = _DRM_STAT_SECONDARY;
	dev->types[9] = _DRM_STAT_DMA;
	
	DRM_INFO( "Initialized %s %d.%d.%d %s on minor %d: %s\n",
		DRIVER_NAME,
		DRIVER_MAJOR,
		DRIVER_MINOR,
		DRIVER_PATCHLEVEL,
		DRIVER_DATE,
		dev->primary.minor,
		pci_pretty_name(dev->pdev)
		);
	return 0;
}

static int version( drm_version_t *version )
{
	int len;

	version->version_major = DRIVER_MAJOR;
	version->version_minor = DRIVER_MINOR;
	version->version_patchlevel = DRIVER_PATCHLEVEL;
	DRM_COPY( version->name, DRIVER_NAME );
	DRM_COPY( version->date, DRIVER_DATE );
	DRM_COPY( version->desc, DRIVER_DESC );
	return 0;
}

static struct pci_device_id pciidlist[] = {
	i915_PCI_IDS
};

extern drm_ioctl_desc_t i915_ioctls[];
extern int i915_max_ioctl;

static struct drm_driver driver = {
	.driver_features = DRIVER_USE_AGP | DRIVER_REQUIRE_AGP | DRIVER_USE_MTRR |
				DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED,
	.pretakedown = i915_driver_pretakedown,
	.prerelease = i915_driver_prerelease,
	.irq_preinstall = i915_driver_irq_preinstall,
	.irq_postinstall = i915_driver_irq_postinstall,
	.irq_uninstall = i915_driver_irq_uninstall,
	.irq_handler = i915_driver_irq_handler,
	.reclaim_buffers = drm_core_reclaim_buffers,
	.get_map_ofs = drm_core_get_map_ofs,
	.get_reg_ofs = drm_core_get_reg_ofs,
	.postinit = postinit,
	.version = version,
	.ioctls = i915_ioctls,
	.fops = {
		.owner = THIS_MODULE,
		.open = drm_open,
		.release = drm_release,
		.ioctl = drm_ioctl,
		.mmap = drm_mmap,
		.poll = drm_poll,
		.fasync = drm_fasync,
	},
	.pci_driver = {
		.name          = DRIVER_NAME,
		.id_table      = pciidlist,
	}
};

static int __init i915_init(void)
{
	driver.num_ioctls = i915_max_ioctl;
	return drm_init(&driver);
}

static void __exit i915_exit(void)
{
	drm_exit(&driver);
}

module_init(i915_init);
module_exit(i915_exit);

MODULE_AUTHOR( DRIVER_AUTHOR );
MODULE_DESCRIPTION( DRIVER_DESC );
MODULE_LICENSE("GPL and additional rights");
