/*
 *  Copyright (C) 2008       SuSE Linux Products GmbH
 *                           Thomas Renninger <trenn@suse.de>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 * video_detect.c:
 * Provides acpi_is_video_device() for early scanning of ACPI devices in scan.c
 * There a Linux specific (Spec does not provide a HID for video devices) is
 * assigned
 *
 * After PCI devices are glued with ACPI devices
 * acpi_get_pci_dev() can be called to identify ACPI graphics
 * devices for which a real graphics card is plugged in
 *
 * Now acpi_video_get_capabilities() can be called to check which
 * capabilities the graphics cards plugged in support. The check for general
 * video capabilities will be triggered by the first caller of
 * acpi_video_get_capabilities(NULL); which will happen when the first
 * backlight switching supporting driver calls:
 * acpi_video_backlight_support();
 *
 * Depending on whether ACPI graphics extensions (cmp. ACPI spec Appendix B)
 * are available, video.ko should be used to handle the device.
 *
 * Otherwise vendor specific drivers like thinkpad_acpi, asus-laptop,
 * sony_acpi,... can take care about backlight brightness.
 *
 * If CONFIG_ACPI_VIDEO is neither set as "compiled in" (y) nor as a module (m)
 * this file will not be compiled, acpi_video_get_capabilities() and
 * acpi_video_backlight_support() will always return 0 and vendor specific
 * drivers always can handle backlight.
 *
 */

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/pci.h>

#include "internal.h"

#define PREFIX "ACPI: "

ACPI_MODULE_NAME("video");
#define _COMPONENT		ACPI_VIDEO_COMPONENT

static long acpi_video_support;
static bool acpi_video_caps_checked;

static acpi_status
acpi_backlight_cap_match(acpi_handle handle, u32 level, void *context,
			  void **retyurn_value)
{
	long *cap = context;
	acpi_handle h_dummy;

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_BCM", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(handle, "_BCL", &h_dummy))) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Found generic backlight "
				  "support\n"));
		*cap |= ACPI_VIDEO_BACKLIGHT;
		if (ACPI_FAILURE(acpi_get_handle(handle, "_BQC", &h_dummy)))
			printk(KERN_WARNING FW_BUG PREFIX "No _BQC method, "
				"cannot determine initial brightness\n");
		/* We have backlight support, no need to scan further */
		return AE_CTRL_TERMINATE;
	}
	return 0;
}

/* Returns true if the ACPI object is a video device which can be
 * handled by video.ko.
 * The device will get a Linux specific CID added in scan.c to
 * identify the device as an ACPI graphics device
 * Be aware that the graphics device may not be physically present
 * Use acpi_video_get_capabilities() to detect general ACPI video
 * capabilities of present cards
 */
long acpi_is_video_device(acpi_handle handle)
{
	acpi_handle h_dummy;
	long video_caps = 0;

	/* Is this device able to support video switching ? */
	if (ACPI_SUCCESS(acpi_get_handle(handle, "_DOD", &h_dummy)) ||
	    ACPI_SUCCESS(acpi_get_handle(handle, "_DOS", &h_dummy)))
		video_caps |= ACPI_VIDEO_OUTPUT_SWITCHING;

	/* Is this device able to retrieve a video ROM ? */
	if (ACPI_SUCCESS(acpi_get_handle(handle, "_ROM", &h_dummy)))
		video_caps |= ACPI_VIDEO_ROM_AVAILABLE;

	/* Is this device able to configure which video head to be POSTed ? */
	if (ACPI_SUCCESS(acpi_get_handle(handle, "_VPO", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(handle, "_GPD", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(handle, "_SPD", &h_dummy)))
		video_caps |= ACPI_VIDEO_DEVICE_POSTING;

	/* Only check for backlight functionality if one of the above hit. */
	if (video_caps)
		acpi_walk_namespace(ACPI_TYPE_DEVICE, handle,
				    ACPI_UINT32_MAX, acpi_backlight_cap_match, NULL,
				    &video_caps, NULL);

	return video_caps;
}
EXPORT_SYMBOL(acpi_is_video_device);

static acpi_status
find_video(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	long *cap = context;
	struct pci_dev *dev;
	struct acpi_device *acpi_dev;

	const struct acpi_device_id video_ids[] = {
		{ACPI_VIDEO_HID, 0},
		{"", 0},
	};
	if (acpi_bus_get_device(handle, &acpi_dev))
		return AE_OK;

	if (!acpi_match_device_ids(acpi_dev, video_ids)) {
		dev = acpi_get_pci_dev(handle);
		if (!dev)
			return AE_OK;
		pci_dev_put(dev);
		*cap |= acpi_is_video_device(handle);
	}
	return AE_OK;
}

/* Force to use vendor driver when the ACPI device is known to be
 * buggy */
static int video_detect_force_vendor(const struct dmi_system_id *d)
{
	acpi_video_support |= ACPI_VIDEO_BACKLIGHT_DMI_VENDOR;
	return 0;
}

static struct dmi_system_id video_detect_dmi_table[] = {
	/* On Samsung X360, the BIOS will set a flag (VDRV) if generic
	 * ACPI backlight device is used. This flag will definitively break
	 * the backlight interface (even the vendor interface) untill next
	 * reboot. It's why we should prevent video.ko from being used here
	 * and we can't rely on a later call to acpi_video_unregister().
	 */
	{
	 .callback = video_detect_force_vendor,
	 .ident = "X360",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X360"),
		DMI_MATCH(DMI_BOARD_NAME, "X360"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	.ident = "Asus UL30VT",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30VT"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	.ident = "Asus UL30A",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30A"),
		},
	},
	{ },
};

/*
 * Returns the video capabilities of a specific ACPI graphics device
 *
 * if NULL is passed as argument all ACPI devices are enumerated and
 * all graphics capabilities of physically present devices are
 * summarized and returned. This is cached and done only once.
 */
long acpi_video_get_capabilities(acpi_handle graphics_handle)
{
	long caps = 0;
	struct acpi_device *tmp_dev;
	acpi_status status;

	if (acpi_video_caps_checked && graphics_handle == NULL)
		return acpi_video_support;

	if (!graphics_handle) {
		/* Only do the global walk through all graphics devices once */
		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				    ACPI_UINT32_MAX, find_video, NULL,
				    &caps, NULL);
		/* There might be boot param flags set already... */
		acpi_video_support |= caps;
		acpi_video_caps_checked = 1;
		/* Add blacklists here. Be careful to use the right *DMI* bits
		 * to still be able to override logic via boot params, e.g.:
		 *
		 *   if (dmi_name_in_vendors("XY")) {
		 *	acpi_video_support |=
		 *		ACPI_VIDEO_BACKLIGHT_DMI_VENDOR;
		 *}
		 */

		dmi_check_system(video_detect_dmi_table);
	} else {
		status = acpi_bus_get_device(graphics_handle, &tmp_dev);
		if (ACPI_FAILURE(status)) {
			ACPI_EXCEPTION((AE_INFO, status, "Invalid device"));
			return 0;
		}
		acpi_walk_namespace(ACPI_TYPE_DEVICE, graphics_handle,
				    ACPI_UINT32_MAX, find_video, NULL,
				    &caps, NULL);
	}
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "We have 0x%lX video support %s %s\n",
			  graphics_handle ? caps : acpi_video_support,
			  graphics_handle ? "on device " : "in general",
			  graphics_handle ? acpi_device_bid(tmp_dev) : ""));
	return caps;
}
EXPORT_SYMBOL(acpi_video_get_capabilities);

static void acpi_video_caps_check(void)
{
	/*
	 * We must check whether the ACPI graphics device is physically plugged
	 * in. Therefore this must be called after binding PCI and ACPI devices
	 */
	if (!acpi_video_caps_checked)
		acpi_video_get_capabilities(NULL);
}

bool acpi_video_backlight_quirks(void)
{
	return acpi_gbl_osi_data >= ACPI_OSI_WIN_8;
}
EXPORT_SYMBOL(acpi_video_backlight_quirks);

/* Promote the vendor interface instead of the generic video module.
 * This function allow DMI blacklists to be implemented by externals
 * platform drivers instead of putting a big blacklist in video_detect.c
 * After calling this function you will probably want to call
 * acpi_video_unregister() to make sure the video module is not loaded
 */
void acpi_video_dmi_promote_vendor(void)
{
	acpi_video_caps_check();
	acpi_video_support |= ACPI_VIDEO_BACKLIGHT_DMI_VENDOR;
}
EXPORT_SYMBOL(acpi_video_dmi_promote_vendor);

/* To be called when a driver who previously promoted the vendor
 * interface */
void acpi_video_dmi_demote_vendor(void)
{
	acpi_video_caps_check();
	acpi_video_support &= ~ACPI_VIDEO_BACKLIGHT_DMI_VENDOR;
}
EXPORT_SYMBOL(acpi_video_dmi_demote_vendor);

/* Returns true if video.ko can do backlight switching */
int acpi_video_backlight_support(void)
{
	acpi_video_caps_check();

	/* First check for boot param -> highest prio */
	if (acpi_video_support & ACPI_VIDEO_BACKLIGHT_FORCE_VENDOR)
		return 0;
	else if (acpi_video_support & ACPI_VIDEO_BACKLIGHT_FORCE_VIDEO)
		return 1;

	/* Then check for DMI blacklist -> second highest prio */
	if (acpi_video_support & ACPI_VIDEO_BACKLIGHT_DMI_VENDOR)
		return 0;
	else if (acpi_video_support & ACPI_VIDEO_BACKLIGHT_DMI_VIDEO)
		return 1;

	/* Then go the default way */
	return acpi_video_support & ACPI_VIDEO_BACKLIGHT;
}
EXPORT_SYMBOL(acpi_video_backlight_support);

/*
 * Use acpi_backlight=vendor/video to force that backlight switching
 * is processed by vendor specific acpi drivers or video.ko driver.
 */
static int __init acpi_backlight(char *str)
{
	if (str == NULL || *str == '\0')
		return 1;
	else {
		if (!strcmp("vendor", str))
			acpi_video_support |=
				ACPI_VIDEO_BACKLIGHT_FORCE_VENDOR;
		if (!strcmp("video", str))
			acpi_video_support |=
				ACPI_VIDEO_BACKLIGHT_FORCE_VIDEO;
	}
	return 1;
}
__setup("acpi_backlight=", acpi_backlight);
