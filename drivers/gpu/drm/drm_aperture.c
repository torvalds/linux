// SPDX-License-Identifier: MIT

#include <drm/drm_aperture.h>
#include <drm/drm_fb_helper.h>

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
 *		                                                    "example driver");
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
 */

/**
 * drm_aperture_remove_conflicting_framebuffers - remove existing framebuffers in the given range
 * @base: the aperture's base address in physical memory
 * @size: aperture size in bytes
 * @primary: also kick vga16fb if present
 * @name: requesting driver name
 *
 * This function removes graphics device drivers which use memory range described by
 * @base and @size.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_framebuffers(resource_size_t base, resource_size_t size,
						 bool primary, const char *name)
{
	struct apertures_struct *a;
	int ret;

	a = alloc_apertures(1);
	if (!a)
		return -ENOMEM;

	a->ranges[0].base = base;
	a->ranges[0].size = size;

	ret = drm_fb_helper_remove_conflicting_framebuffers(a, name, primary);
	kfree(a);

	return ret;
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_framebuffers);

/**
 * drm_aperture_remove_conflicting_pci_framebuffers - remove existing framebuffers for PCI devices
 * @pdev: PCI device
 * @name: requesting driver name
 *
 * This function removes graphics device drivers using memory range configured
 * for any of @pdev's memory bars. The function assumes that PCI device with
 * shadowed ROM drives a primary display and so kicks out vga16fb.
 *
 * Returns:
 * 0 on success, or a negative errno code otherwise
 */
int drm_aperture_remove_conflicting_pci_framebuffers(struct pci_dev *pdev, const char *name)
{
	return drm_fb_helper_remove_conflicting_pci_framebuffers(pdev, name);
}
EXPORT_SYMBOL(drm_aperture_remove_conflicting_pci_framebuffers);
