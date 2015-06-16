/*
 *  Copyright (C) 2015       Red Hat Inc.
 *                           Hans de Goede <hdegoede@redhat.com>
 *  Copyright (C) 2008       SuSE Linux Products GmbH
 *                           Thomas Renninger <trenn@suse.de>
 *
 *  May be copied or modified under the terms of the GNU General Public License
 *
 * video_detect.c:
 * After PCI devices are glued with ACPI devices
 * acpi_get_pci_dev() can be called to identify ACPI graphics
 * devices for which a real graphics card is plugged in
 *
 * Depending on whether ACPI graphics extensions (cmp. ACPI spec Appendix B)
 * are available, video.ko should be used to handle the device.
 *
 * Otherwise vendor specific drivers like thinkpad_acpi, asus-laptop,
 * sony_acpi,... can take care about backlight brightness.
 *
 * Backlight drivers can use acpi_video_get_backlight_type() to determine
 * which driver should handle the backlight.
 *
 * If CONFIG_ACPI_VIDEO is neither set as "compiled in" (y) nor as a module (m)
 * this file will not be compiled and acpi_video_get_backlight_type() will
 * always return acpi_backlight_vendor.
 */

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/backlight.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/types.h>
#include <acpi/video.h>

ACPI_MODULE_NAME("video");
#define _COMPONENT		ACPI_VIDEO_COMPONENT

static enum acpi_backlight_type acpi_backlight_cmdline = acpi_backlight_undef;
static enum acpi_backlight_type acpi_backlight_dmi = acpi_backlight_undef;

static void acpi_video_parse_cmdline(void)
{
	if (!strcmp("vendor", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_vendor;
	if (!strcmp("video", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_video;
	if (!strcmp("native", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_native;
	if (!strcmp("none", acpi_video_backlight_string))
		acpi_backlight_cmdline = acpi_backlight_none;
}

static acpi_status
find_video(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	long *cap = context;
	struct pci_dev *dev;
	struct acpi_device *acpi_dev;

	static const struct acpi_device_id video_ids[] = {
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
	acpi_backlight_dmi = acpi_backlight_vendor;
	return 0;
}

static const struct dmi_system_id video_detect_dmi_table[] = {
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
	{
	.callback = video_detect_force_vendor,
	.ident = "Dell Inspiron 5737",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 5737"),
		},
	},
	{ },
};

/*
 * Determine which type of backlight interface to use on this system,
 * First check cmdline, then dmi quirks, then do autodetect.
 *
 * The autodetect order is:
 * 1) Is the acpi-video backlight interface supported ->
 *  no, use a vendor interface
 * 2) Is this a win8 "ready" BIOS and do we have a native interface ->
 *  yes, use a native interface
 * 3) Else use the acpi-video interface
 *
 * Arguably the native on win8 check should be done first, but that would
 * be a behavior change, which may causes issues.
 */
enum acpi_backlight_type acpi_video_get_backlight_type(void)
{
	static DEFINE_MUTEX(init_mutex);
	static bool init_done;
	static long video_caps;

	/* Parse cmdline, dmi and acpi only once */
	mutex_lock(&init_mutex);
	if (!init_done) {
		acpi_video_parse_cmdline();
		dmi_check_system(video_detect_dmi_table);
		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				    ACPI_UINT32_MAX, find_video, NULL,
				    &video_caps, NULL);
		init_done = true;
	}
	mutex_unlock(&init_mutex);

	if (acpi_backlight_cmdline != acpi_backlight_undef)
		return acpi_backlight_cmdline;

	if (acpi_backlight_dmi != acpi_backlight_undef)
		return acpi_backlight_dmi;

	if (!(video_caps & ACPI_VIDEO_BACKLIGHT))
		return acpi_backlight_vendor;

	if (acpi_osi_is_win8() && backlight_device_registered(BACKLIGHT_RAW))
		return acpi_backlight_native;

	return acpi_backlight_video;
}
EXPORT_SYMBOL(acpi_video_get_backlight_type);

/*
 * Set the preferred backlight interface type based on DMI info.
 * This function allows DMI blacklists to be implemented by external
 * platform drivers instead of putting a big blacklist in video_detect.c
 */
void acpi_video_set_dmi_backlight_type(enum acpi_backlight_type type)
{
	acpi_backlight_dmi = type;
}
EXPORT_SYMBOL(acpi_video_set_dmi_backlight_type);

/*
 * Compatiblity function, this is going away as soon as all drivers are
 * converted to acpi_video_set_dmi_backlight_type().
 *
 * Promote the vendor interface instead of the generic video module.
 * After calling this function you will probably want to call
 * acpi_video_unregister() to make sure the video module is not loaded
 */
void acpi_video_dmi_promote_vendor(void)
{
	acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);
}
EXPORT_SYMBOL(acpi_video_dmi_promote_vendor);

/*
 * Compatiblity function, this is going away as soon as all drivers are
 * converted to acpi_video_get_backlight_type().
 *
 * Returns true if video.ko can do backlight switching.
 */
int acpi_video_backlight_support(void)
{
	/*
	 * This is done this way since vendor drivers call this to see
	 * if they should load, and we do not want them to load for both
	 * the acpi_backlight_video and acpi_backlight_native cases.
	 */
	return acpi_video_get_backlight_type() != acpi_backlight_vendor;
}
EXPORT_SYMBOL(acpi_video_backlight_support);
