// SPDX-License-Identifier: MIT

#include <linux/device.h>
#include <linux/fb.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h> /* for firmware helpers */
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vgaarb.h>

#include <drm/drm_aperture.h>
#include <drm/drm_drv.h>
#include <drm/drm_print.h>

/**
 * DOC: overview
 *
 * A graphics device might be supported by different drivers, but only one
 * driver can be active at any given time. Many systems load a generic
 * graphics drivers, such as EFI-GOP or VESA, early during the boot process.
 * During later boot stages, they replace the generic driver with a dedicated,
 * hardware-specific driver. To take over the device the dedicated driver
 * first has to remove the generic driver. DRM aperture functions manage
 * ownership of DRM framebuffer memory and hand-over between drivers.
 *
 * DRM drivers should call drm_aperture_remove_conflicting_framebuffers()
 * at the top of their probe function. The function removes any generic
 * driver that is currently associated with the given framebuffer memory.
 * If the framebuffer is located at PCI BAR 0, the rsp code looks as in the
 * example given below.
 *
 * .. code-block:: c
 *
 *	static const struct drm_driver example_driver = {
 *		...
 *	};
 *
 *	static int remove_conflicting_framebuffers(struct pci_dev *pdev)
 *	{
 *		bool primary = false;
 *		resource_size_t base, size;
 *		int ret;
 *
 *		base = pci_resource_start(pdev, 0);
 *		size = pci_resource_len(pdev, 0);
 *	#ifdef CONFIG_X86
 *		primary = pdev->resource[PCI_ROM_RESOURCE].flags & IORESOURCE_ROM_SHADOW;
 *	#endif
 *
 *		return drm_aperture_remove_conflicting_framebuffers(base, size, primary,
 *		                                                    &example_driver);
 *	}
 *
 *	static int probe(struct pci_dev *pdev)
 *	{
 *		int ret;
 *
 *		// Remove any generic drivers...
 *		ret = remove_conflicting_framebuffers(pdev);
 *		if (ret)
 *			return ret;
 *
 *		// ... and initialize the hardware.
 *		...
 *
 *		drm_dev_register();
 *
 *		return 0;
 *	}
 *
 * PCI device drivers should call
 * drm_aperture_remove_conflicting_pci_framebuffers() and let it detect the
 * framebuffer apertures automatically. Device drivers without knowledge of
 * the framebuffer's location shall call drm_aperture_remove_framebuffers(),
 * which removes all drivers for known framebuffer.
 *
 * Drivers that are susceptible to being removed by other drivers, such as
 * generic EFI or VESA drivers, have to register themselves as owners of their
 * given framebuffer memory. Ownership of the framebuffer memory is achieved
 * by calling devm_aperture_acquire_from_firmware(). On success, the driver
 * is the owner of the framebuffer range. The function fails if the
 * framebuffer is already by another driver. See below for an example.
 *
 * .. code-block:: c
 *
 *	static int acquire_framebuffers(struct drm_device *dev, struct platform_device *pdev)
 *	{
 *		resource_size_t base, size;
 *
 *		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *		if (!mem)
 *			return -EINVAL;
 *		base = mem->start;
 *		size = resource_size(mem);
 *
 *		return devm_acquire_aperture_from_firmware(dev, base, size);
 *	}
 *
 *	static int probe(struct platform_device *pdev)
 *	{
 *		struct drm_device *dev;
 *		int ret;
 *
 *		// ... Initialize the device...
 *		dev = devm_drm_dev_alloc();
 *		...
 *
 *		// ... and acquire ownership of the framebuffer.
 *		ret = acquire_framebuffers(dev, pdev);
 *		if (ret)
 *			return ret;
 *
 *		drm_dev_register(dev, 0);
 *
 *		return 0;
 *	}
 *
 * The generic driver is now subject to forced removal by other drivers. This
 * only works for platform drivers that support hot unplug.
 * When a driver calls drm_aperture_remove_conflicting_framebuffers() et al
 * for the registered framebuffer range, the aperture helpers call
 * platform_device_unregister() and the generic driver unloads itself. It
 * may not access the device's registers, framebuffer memory, ROM, etc
 * afterwards.
 */

struct drm_aperture {
	struct drm_device *dev;
	resource_size_t base;
	resource_size_t size;
	struct list_head lh;
	void (*detach)(struct drm_device *dev);
};

static LIST_HEAD(drm_apertures);
static DEFINE_MUTEX(drm_apertures_lock);

static bool overlap(resource_size_t base1, resource_size_t end1,
		    resource_size_t base2, resource_size_t end2)
{
	return (base1 < end2) && (end1 > base2);
}

static void devm_aperture_acquire_release(void *data)
{
	struct drm_aperture *ap = data;
	bool detached = !ap->dev;

	if (detached)
		return;

	mutex_lock(&drm_apertures_lock);
	list_del(&ap->lh);
	mutex_unlock(&drm_apertures_lock);
}

static int devm_aperture_acquire(struct drm_device *dev,
				 resource_size_t base, resource_size_t size,
				 void (*detach)(struct drm_device *))
{
	size_t end = base + size;
	struct list_head *pos;
	struct drm_aperture *ap;

	mutex_lock(&drm_apertures_lock);

	list_for_each(pos, &drm_apertures) {
		ap = container_of(pos, struct drm_aperture, lh);
		if (overlap(base, end, ap->base, ap->base + ap->size)) {
			mutex_unlock(&drm_apertures_lock);
			return -EBUSY;
		}
	}

	ap = devm_kzalloc(dev->dev, sizeof(*ap), GFP_KERNEL);
	if (!ap) {
		mutex_unlock(&drm_apertures_lock);
		return -ENOMEM;
	}

	ap->dev = dev;
	ap->base = base;
	ap->size = size;
	ap->detach = detach;
	INIT_LIST_HEAD(&ap->lh);

	list_add(&ap->lh, &drm_apertures);

	mutex_unlock(&drm_apertures_lock);

	return devm_add_action_or_reset(dev->dev, devm_aperture_acquire_release, ap);
}

static void drm_aperture_detach_firmware(struct drm_device *dev)
{
	struct platform_device *pdev = to_platform_device(dev->dev);

	/*
	 * Remove the device from the device hierarchy. This is the right thing
	 * to do for firmware-based DRM drivers, such as EFI, VESA or VGA. After
	 * the new driver takes over the hardware, the firmware device's state
	 * will be lost.
	 *
	 * For non-platform devices, a new callback would be required.
	 *
	 * If the aperture helpers ever need to handle native drivers, this call
	 * would only have to unplug the DRM device, so that the hardware device
	 * stays around after detachment.
	 */
	platform_device_unregister(pdev);
}

/**
 * devm_aperture_acquire_from_firmware - Acquires ownership of a firmware framebuffer
 *                                       on behalf of a DRM driver.
 * @dev:	the DRM device to own the framebuffer memory
 * @base:	the framebuffer's byte offset in physical memory
 * @size:	the framebuffer size in bytes
 *
 * Installs the given device as the new owner of the framebuffer. The function
 * expects the framebuffer to be provided by a platform device that has been
 * set up by firmware. Firmware can be any generic interface, such as EFI,
 * VESA, VGA, etc. If the native hardware driver takes over ownership of the
 * framebuffer range, the firmware state gets lost. Aperture helpers will then
 * unregister the platform device automatically. Acquired apertures are
 * released automatically if the underlying device goes away.
 *
 * The function fails if the framebuffer range, or parts of it, is currently
 * owned by another driver. To evict current owners, callers should use
 * drm_aperture_remove_conflicting_framebuffers() et al. before calling this
 * function. The function also fails if the given device is not a platform
 * device.
 *
 * Returns:
 * 0 on success, or a negative errno value otherwise.
 */
int devm_aperture_acquire_from_firmware(struct drm_device *dev, resource_size_t base,
					resource_size_t size)
{
	if (drm_WARN_ON(dev, !dev_is_platform(dev->dev)))
		return -EINVAL;

	return devm_aperture_acquire(dev, base, size, drm_aperture_detach_firmware);
}
EXPORT_SYMBOL(devm_aperture_acquire_from_firmware);

static void drm_aperture_detach_drivers(resource_size_t base, resource_size_t size)
{
	resource_size_t end = base + size;
	struct list_head *pos, *n;

	mutex_lock(&drm_apertures_lock);

	list_for_each_safe(pos, n, &drm_apertures) {
		struct drm_aperture *ap =
			container_of(pos, struct drm_aperture, lh);
		struct drm_device *dev = ap->dev;

		if (WARN_ON_ONCE(!dev))
			continue;

		if (!overlap(base, end, ap->base, ap->base + ap->size))
			continue;

		ap->dev = NULL; /* detach from device */
		list_del(&ap->lh);

		ap->detach(dev);
	}

	mutex_unlock(&drm_apertures_lock);
}

/**
 * drm_aperture_remove_conflicting_framebuffers - remove existing framebuffers in the given range
 * @base: the aperture's base address in physical memory
 * @size: aperture size in bytes
 * @primary: also kick vga16fb if present
 * @req_driver: requesting DRM driver
 *
 * This function removes graphics device drivers which use memory range described by
 * @base and @size.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_framebuffers(resource_size_t base, resource_size_t size,
						 bool primary, const struct drm_driver *req_driver)
{
#if IS_REACHABLE(CONFIG_FB)
	struct apertures_struct *a;
	int ret;

	a = alloc_apertures(1);
	if (!a)
		return -ENOMEM;

	a->ranges[0].base = base;
	a->ranges[0].size = size;

	ret = remove_conflicting_framebuffers(a, req_driver->name, primary);
	kfree(a);

	if (ret)
		return ret;
#endif

	drm_aperture_detach_drivers(base, size);

	return 0;
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_framebuffers);

/**
 * drm_aperture_remove_conflicting_pci_framebuffers - remove existing framebuffers for PCI devices
 * @pdev: PCI device
 * @req_driver: requesting DRM driver
 *
 * This function removes graphics device drivers using memory range configured
 * for any of @pdev's memory bars. The function assumes that PCI device with
 * shadowed ROM drives a primary display and so kicks out vga16fb.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_pci_framebuffers(struct pci_dev *pdev,
						     const struct drm_driver *req_driver)
{
	resource_size_t base, size;
	int bar, ret;

	/*
	 * WARNING: Apparently we must kick fbdev drivers before vgacon,
	 * otherwise the vga fbdev driver falls over.
	 */
#if IS_REACHABLE(CONFIG_FB)
	ret = remove_conflicting_pci_framebuffers(pdev, req_driver->name);
	if (ret)
		return ret;
#endif
	ret = vga_remove_vgacon(pdev);
	if (ret)
		return ret;

	for (bar = 0; bar < PCI_STD_NUM_BARS; ++bar) {
		if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
			continue;
		base = pci_resource_start(pdev, bar);
		size = pci_resource_len(pdev, bar);
		drm_aperture_detach_drivers(base, size);
	}

	return 0;
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_pci_framebuffers);
