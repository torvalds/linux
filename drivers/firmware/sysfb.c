// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Generic System Framebuffers
 * Copyright (c) 2012-2013 David Herrmann <dh.herrmann@gmail.com>
 */

/*
 * Simple-Framebuffer support
 * Create a platform-device for any available boot framebuffer. The
 * simple-framebuffer platform device is already available on DT systems, so
 * this module parses the global "screen_info" object and creates a suitable
 * platform device compatible with the "simple-framebuffer" DT object. If
 * the framebuffer is incompatible, we instead create a legacy
 * "vesa-framebuffer", "efi-framebuffer" or "platform-framebuffer" device and
 * pass the screen_info as platform_data. This allows legacy drivers
 * to pick these devices up without messing with simple-framebuffer drivers.
 * The global "screen_info" is still valid at all times.
 *
 * If CONFIG_SYSFB_SIMPLEFB is not selected, never register "simple-framebuffer"
 * platform devices, but only use legacy framebuffer devices for
 * backwards compatibility.
 *
 * TODO: We set the dev_id field of all platform-devices to 0. This allows
 * other OF/DT parsers to create such devices, too. However, they must
 * start at offset 1 for this to work.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/platform_data/simplefb.h>
#include <linux/platform_device.h>
#include <linux/screen_info.h>
#include <linux/sysfb.h>

static struct platform_device *pd;
static DEFINE_MUTEX(disable_lock);
static bool disabled;

static bool sysfb_unregister(void)
{
	if (IS_ERR_OR_NULL(pd))
		return false;

	platform_device_unregister(pd);
	pd = NULL;

	return true;
}

/**
 * sysfb_disable() - disable the Generic System Framebuffers support
 *
 * This disables the registration of system framebuffer devices that match the
 * generic drivers that make use of the system framebuffer set up by firmware.
 *
 * It also unregisters a device if this was already registered by sysfb_init().
 *
 * Context: The function can sleep. A @disable_lock mutex is acquired to serialize
 *          against sysfb_init(), that registers a system framebuffer device.
 */
void sysfb_disable(void)
{
	mutex_lock(&disable_lock);
	sysfb_unregister();
	disabled = true;
	mutex_unlock(&disable_lock);
}
EXPORT_SYMBOL_GPL(sysfb_disable);

#if defined(CONFIG_PCI)
static __init bool sysfb_pci_dev_is_enabled(struct pci_dev *pdev)
{
	/*
	 * TODO: Try to integrate this code into the PCI subsystem
	 */
	int ret;
	u16 command;

	ret = pci_read_config_word(pdev, PCI_COMMAND, &command);
	if (ret != PCIBIOS_SUCCESSFUL)
		return false;
	if (!(command & PCI_COMMAND_MEMORY))
		return false;
	return true;
}
#else
static __init bool sysfb_pci_dev_is_enabled(struct pci_dev *pdev)
{
	return false;
}
#endif

static __init struct device *sysfb_parent_dev(const struct screen_info *si)
{
	struct pci_dev *pdev;

	pdev = screen_info_pci_dev(si);
	if (IS_ERR(pdev)) {
		return ERR_CAST(pdev);
	} else if (pdev) {
		if (!sysfb_pci_dev_is_enabled(pdev))
			return ERR_PTR(-ENODEV);
		return &pdev->dev;
	}

	return NULL;
}

static __init int sysfb_init(void)
{
	struct screen_info *si = &screen_info;
	struct device *parent;
	struct simplefb_platform_data mode;
	const char *name;
	bool compatible;
	int ret = 0;

	screen_info_apply_fixups();

	mutex_lock(&disable_lock);
	if (disabled)
		goto unlock_mutex;

	sysfb_apply_efi_quirks();

	parent = sysfb_parent_dev(si);
	if (IS_ERR(parent))
		goto unlock_mutex;

	/* try to create a simple-framebuffer device */
	compatible = sysfb_parse_mode(si, &mode);
	if (compatible) {
		pd = sysfb_create_simplefb(si, &mode, parent);
		if (!IS_ERR(pd))
			goto unlock_mutex;
	}

	/* if the FB is incompatible, create a legacy framebuffer device */
	if (si->orig_video_isVGA == VIDEO_TYPE_EFI)
		name = "efi-framebuffer";
	else if (si->orig_video_isVGA == VIDEO_TYPE_VLFB)
		name = "vesa-framebuffer";
	else if (si->orig_video_isVGA == VIDEO_TYPE_VGAC)
		name = "vga-framebuffer";
	else if (si->orig_video_isVGA == VIDEO_TYPE_EGAC)
		name = "ega-framebuffer";
	else
		name = "platform-framebuffer";

	pd = platform_device_alloc(name, 0);
	if (!pd) {
		ret = -ENOMEM;
		goto unlock_mutex;
	}

	pd->dev.parent = parent;

	sysfb_set_efifb_fwnode(pd);

	ret = platform_device_add_data(pd, si, sizeof(*si));
	if (ret)
		goto err;

	ret = platform_device_add(pd);
	if (ret)
		goto err;

	goto unlock_mutex;
err:
	platform_device_put(pd);
unlock_mutex:
	mutex_unlock(&disable_lock);
	return ret;
}

/* must execute after PCI subsystem for EFI quirks */
device_initcall(sysfb_init);
