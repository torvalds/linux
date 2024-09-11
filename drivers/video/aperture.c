// SPDX-License-Identifier: MIT

#include <linux/aperture.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/sysfb.h>
#include <linux/types.h>
#include <linux/vgaarb.h>

#include <video/vga.h>

/**
 * DOC: overview
 *
 * A graphics device might be supported by different drivers, but only one
 * driver can be active at any given time. Many systems load a generic
 * graphics drivers, such as EFI-GOP or VESA, early during the boot process.
 * During later boot stages, they replace the generic driver with a dedicated,
 * hardware-specific driver. To take over the device, the dedicated driver
 * first has to remove the generic driver. Aperture functions manage
 * ownership of framebuffer memory and hand-over between drivers.
 *
 * Graphics drivers should call aperture_remove_conflicting_devices()
 * at the top of their probe function. The function removes any generic
 * driver that is currently associated with the given framebuffer memory.
 * An example for a graphics device on the platform bus is shown below.
 *
 * .. code-block:: c
 *
 *	static int example_probe(struct platform_device *pdev)
 *	{
 *		struct resource *mem;
 *		resource_size_t base, size;
 *		int ret;
 *
 *		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *		if (!mem)
 *			return -ENODEV;
 *		base = mem->start;
 *		size = resource_size(mem);
 *
 *		ret = aperture_remove_conflicting_devices(base, size, "example");
 *		if (ret)
 *			return ret;
 *
 *		// Initialize the hardware
 *		...
 *
 *		return 0;
 *	}
 *
 *	static const struct platform_driver example_driver = {
 *		.probe = example_probe,
 *		...
 *	};
 *
 * The given example reads the platform device's I/O-memory range from the
 * device instance. An active framebuffer will be located within this range.
 * The call to aperture_remove_conflicting_devices() releases drivers that
 * have previously claimed ownership of the range and are currently driving
 * output on the framebuffer. If successful, the new driver can take over
 * the device.
 *
 * While the given example uses a platform device, the aperture helpers work
 * with every bus that has an addressable framebuffer. In the case of PCI,
 * device drivers can also call aperture_remove_conflicting_pci_devices() and
 * let the function detect the apertures automatically. Device drivers without
 * knowledge of the framebuffer's location can call
 * aperture_remove_all_conflicting_devices(), which removes all known devices.
 *
 * Drivers that are susceptible to being removed by other drivers, such as
 * generic EFI or VESA drivers, have to register themselves as owners of their
 * framebuffer apertures. Ownership of the framebuffer memory is achieved
 * by calling devm_aperture_acquire_for_platform_device(). If successful, the
 * driver is the owner of the framebuffer range. The function fails if the
 * framebuffer is already owned by another driver. See below for an example.
 *
 * .. code-block:: c
 *
 *	static int generic_probe(struct platform_device *pdev)
 *	{
 *		struct resource *mem;
 *		resource_size_t base, size;
 *
 *		mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
 *		if (!mem)
 *			return -ENODEV;
 *		base = mem->start;
 *		size = resource_size(mem);
 *
 *		ret = devm_aperture_acquire_for_platform_device(pdev, base, size);
 *		if (ret)
 *			return ret;
 *
 *		// Initialize the hardware
 *		...
 *
 *		return 0;
 *	}
 *
 *	static int generic_remove(struct platform_device *)
 *	{
 *		// Hot-unplug the device
 *		...
 *
 *		return 0;
 *	}
 *
 *	static const struct platform_driver generic_driver = {
 *		.probe = generic_probe,
 *		.remove = generic_remove,
 *		...
 *	};
 *
 * The similar to the previous example, the generic driver claims ownership
 * of the framebuffer memory from its probe function. This will fail if the
 * memory range, or parts of it, is already owned by another driver.
 *
 * If successful, the generic driver is now subject to forced removal by
 * another driver. This only works for platform drivers that support hot
 * unplugging. When a driver calls aperture_remove_conflicting_devices()
 * et al for the registered framebuffer range, the aperture helpers call
 * platform_device_unregister() and the generic driver unloads itself. The
 * generic driver also has to provide a remove function to make this work.
 * Once hot unplugged from hardware, it may not access the device's
 * registers, framebuffer memory, ROM, etc afterwards.
 */

struct aperture_range {
	struct device *dev;
	resource_size_t base;
	resource_size_t size;
	struct list_head lh;
	void (*detach)(struct device *dev);
};

static LIST_HEAD(apertures);
static DEFINE_MUTEX(apertures_lock);

static bool overlap(resource_size_t base1, resource_size_t end1,
		    resource_size_t base2, resource_size_t end2)
{
	return (base1 < end2) && (end1 > base2);
}

static void devm_aperture_acquire_release(void *data)
{
	struct aperture_range *ap = data;
	bool detached = !ap->dev;

	if (detached)
		return;

	mutex_lock(&apertures_lock);
	list_del(&ap->lh);
	mutex_unlock(&apertures_lock);
}

static int devm_aperture_acquire(struct device *dev,
				 resource_size_t base, resource_size_t size,
				 void (*detach)(struct device *))
{
	size_t end = base + size;
	struct list_head *pos;
	struct aperture_range *ap;

	mutex_lock(&apertures_lock);

	list_for_each(pos, &apertures) {
		ap = container_of(pos, struct aperture_range, lh);
		if (overlap(base, end, ap->base, ap->base + ap->size)) {
			mutex_unlock(&apertures_lock);
			return -EBUSY;
		}
	}

	ap = devm_kzalloc(dev, sizeof(*ap), GFP_KERNEL);
	if (!ap) {
		mutex_unlock(&apertures_lock);
		return -ENOMEM;
	}

	ap->dev = dev;
	ap->base = base;
	ap->size = size;
	ap->detach = detach;
	INIT_LIST_HEAD(&ap->lh);

	list_add(&ap->lh, &apertures);

	mutex_unlock(&apertures_lock);

	return devm_add_action_or_reset(dev, devm_aperture_acquire_release, ap);
}

static void aperture_detach_platform_device(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);

	/*
	 * Remove the device from the device hierarchy. This is the right thing
	 * to do for firmware-based fb drivers, such as EFI, VESA or VGA. After
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
 * devm_aperture_acquire_for_platform_device - Acquires ownership of an aperture
 *                                             on behalf of a platform device.
 * @pdev:	the platform device to own the aperture
 * @base:	the aperture's byte offset in physical memory
 * @size:	the aperture size in bytes
 *
 * Installs the given device as the new owner of the aperture. The function
 * expects the aperture to be provided by a platform device. If another
 * driver takes over ownership of the aperture, aperture helpers will then
 * unregister the platform device automatically. All acquired apertures are
 * released automatically when the underlying device goes away.
 *
 * The function fails if the aperture, or parts of it, is currently
 * owned by another device. To evict current owners, callers should use
 * remove_conflicting_devices() et al. before calling this function.
 *
 * Returns:
 * 0 on success, or a negative errno value otherwise.
 */
int devm_aperture_acquire_for_platform_device(struct platform_device *pdev,
					      resource_size_t base,
					      resource_size_t size)
{
	return devm_aperture_acquire(&pdev->dev, base, size, aperture_detach_platform_device);
}
EXPORT_SYMBOL(devm_aperture_acquire_for_platform_device);

static void aperture_detach_devices(resource_size_t base, resource_size_t size)
{
	resource_size_t end = base + size;
	struct list_head *pos, *n;

	mutex_lock(&apertures_lock);

	list_for_each_safe(pos, n, &apertures) {
		struct aperture_range *ap = container_of(pos, struct aperture_range, lh);
		struct device *dev = ap->dev;

		if (WARN_ON_ONCE(!dev))
			continue;

		if (!overlap(base, end, ap->base, ap->base + ap->size))
			continue;

		ap->dev = NULL; /* detach from device */
		list_del(&ap->lh);

		ap->detach(dev);
	}

	mutex_unlock(&apertures_lock);
}

/**
 * aperture_remove_conflicting_devices - remove devices in the given range
 * @base: the aperture's base address in physical memory
 * @size: aperture size in bytes
 * @name: a descriptive name of the requesting driver
 *
 * This function removes devices that own apertures within @base and @size.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int aperture_remove_conflicting_devices(resource_size_t base, resource_size_t size,
					const char *name)
{
	/*
	 * If a driver asked to unregister a platform device registered by
	 * sysfb, then can be assumed that this is a driver for a display
	 * that is set up by the system firmware and has a generic driver.
	 *
	 * Drivers for devices that don't have a generic driver will never
	 * ask for this, so let's assume that a real driver for the display
	 * was already probed and prevent sysfb to register devices later.
	 */
	sysfb_disable(NULL);

	aperture_detach_devices(base, size);

	return 0;
}
EXPORT_SYMBOL(aperture_remove_conflicting_devices);

/**
 * __aperture_remove_legacy_vga_devices - remove legacy VGA devices of a PCI devices
 * @pdev: PCI device
 *
 * This function removes VGA devices provided by @pdev, such as a VGA
 * framebuffer or a console. This is useful if you have a VGA-compatible
 * PCI graphics device with framebuffers in non-BAR locations. Drivers
 * should acquire ownership of those memory areas and afterwards call
 * this helper to release remaining VGA devices.
 *
 * If your hardware has its framebuffers accessible via PCI BARS, use
 * aperture_remove_conflicting_pci_devices() instead. The function will
 * release any VGA devices automatically.
 *
 * WARNING: Apparently we must remove graphics drivers before calling
 *          this helper. Otherwise the vga fbdev driver falls over if
 *          we have vgacon configured.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int __aperture_remove_legacy_vga_devices(struct pci_dev *pdev)
{
	/* VGA framebuffer */
	aperture_detach_devices(VGA_FB_PHYS_BASE, VGA_FB_PHYS_SIZE);

	/* VGA textmode console */
	return vga_remove_vgacon(pdev);
}
EXPORT_SYMBOL(__aperture_remove_legacy_vga_devices);

/**
 * aperture_remove_conflicting_pci_devices - remove existing framebuffers for PCI devices
 * @pdev: PCI device
 * @name: a descriptive name of the requesting driver
 *
 * This function removes devices that own apertures within any of @pdev's
 * memory bars. The function assumes that PCI device with shadowed ROM
 * drives a primary display and therefore kicks out vga16fb as well.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int aperture_remove_conflicting_pci_devices(struct pci_dev *pdev, const char *name)
{
	resource_size_t base, size;
	int bar, ret = 0;

	sysfb_disable(&pdev->dev);

	for (bar = 0; bar < PCI_STD_NUM_BARS; ++bar) {
		if (!(pci_resource_flags(pdev, bar) & IORESOURCE_MEM))
			continue;

		base = pci_resource_start(pdev, bar);
		size = pci_resource_len(pdev, bar);
		aperture_detach_devices(base, size);
	}

	/*
	 * If this is the primary adapter, there could be a VGA device
	 * that consumes the VGA framebuffer I/O range. Remove this
	 * device as well.
	 */
	if (pdev == vga_default_device())
		ret = __aperture_remove_legacy_vga_devices(pdev);

	return ret;

}
EXPORT_SYMBOL(aperture_remove_conflicting_pci_devices);
