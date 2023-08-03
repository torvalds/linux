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
#include <linux/workqueue.h>
#include <acpi/video.h>

void acpi_video_unregister_backlight(void);

static bool backlight_notifier_registered;
static struct notifier_block backlight_nb;
static struct work_struct backlight_notify_work;

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

static int video_detect_force_video(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_video;
	return 0;
}

static int video_detect_force_native(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_native;
	return 0;
}

static int video_detect_force_none(const struct dmi_system_id *d)
{
	acpi_backlight_dmi = acpi_backlight_none;
	return 0;
}

static const struct dmi_system_id video_detect_dmi_table[] = {
	/* On Samsung X360, the BIOS will set a flag (VDRV) if generic
	 * ACPI backlight device is used. This flag will definitively break
	 * the backlight interface (even the vendor interface) until next
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
	.ident = "GIGABYTE GB-BXBT-2807",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
		DMI_MATCH(DMI_PRODUCT_NAME, "GB-BXBT-2807"),
		},
	},
	{
	.callback = video_detect_force_vendor,
	.ident = "Sony VPCEH3U1E",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEH3U1E"),
		},
	},

	/*
	 * These models have a working acpi_video backlight control, and using
	 * native backlight causes a regression where backlight does not work
	 * when userspace is not handling brightness key events. Disable
	 * native_backlight on these to fix this:
	 * https://bugzilla.kernel.org/show_bug.cgi?id=81691
	 */
	{
	 .callback = video_detect_force_video,
	 .ident = "ThinkPad T420",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T420"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 .ident = "ThinkPad T520",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T520"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 .ident = "ThinkPad X201s",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201s"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 .ident = "ThinkPad X201T",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201T"),
		},
	},

	/* The native backlight controls do not work on some older machines */
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=81515 */
	 .callback = video_detect_force_video,
	 .ident = "HP ENVY 15 Notebook",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP ENVY 15 Notebook PC"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 870Z5E/880Z5E/680Z5E",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "870Z5E/880Z5E/680Z5E"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 370R4E/370R4V/370R5E/3570RE/370R5V",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "370R4E/370R4V/370R5E/3570RE/370R5V"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1186097 */
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 3570R/370R/470R/450R/510R/4450RV",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "3570R/370R/470R/450R/510R/4450RV"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1557060 */
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 670Z5E",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "670Z5E"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1094948 */
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 730U3E/740U3E",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "730U3E/740U3E"),
		},
	},
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=87286 */
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 900X3C/900X3D/900X3E/900X4C/900X4D",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "900X3C/900X3D/900X3E/900X4C/900X4D"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1272633 */
	 .callback = video_detect_force_video,
	 .ident = "Dell XPS14 L421X",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L421X"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1163574 */
	 .callback = video_detect_force_video,
	 .ident = "Dell XPS15 L521X",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L521X"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=108971 */
	 .callback = video_detect_force_video,
	 .ident = "SAMSUNG 530U4E/540U4E",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "530U4E/540U4E"),
		},
	},
	/* https://bugs.launchpad.net/bugs/1894667 */
	{
	 .callback = video_detect_force_video,
	 .ident = "HP 635 Notebook",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP 635 Notebook PC"),
		},
	},

	/* Non win8 machines which need native backlight nevertheless */
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1201530 */
	 .callback = video_detect_force_native,
	 .ident = "Lenovo Ideapad S405",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_BOARD_NAME, "Lenovo IdeaPad S405"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1187004 */
	 .callback = video_detect_force_native,
	 .ident = "Lenovo Ideapad Z570",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "Ideapad Z570"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .ident = "Lenovo E41-25",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "81FS"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .ident = "Lenovo E41-45",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "82BK"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo ThinkPad X131e (3371 AMD version) */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "3371"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Apple iMac11,3 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "iMac11,3"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1217249 */
	 .callback = video_detect_force_native,
	 .ident = "Apple MacBook Pro 12,1",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro12,1"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .ident = "Dell Vostro V131",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V131"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1123661 */
	 .callback = video_detect_force_native,
	 .ident = "Dell XPS 17 L702X",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Dell System XPS L702X"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .ident = "Dell Precision 7510",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Precision 7510"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 .ident = "Acer Aspire 5738z",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5738"),
		DMI_MATCH(DMI_BOARD_NAME, "JV50"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=207835 */
	 .callback = video_detect_force_native,
	 .ident = "Acer TravelMate 5735Z",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5735Z"),
		DMI_MATCH(DMI_BOARD_NAME, "BA51_MV"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "ASUSTeK COMPUTER INC. GA401",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA401"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "ASUSTeK COMPUTER INC. GA502",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA502"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "ASUSTeK COMPUTER INC. GA503",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA503"),
		},
	},
	/*
	 * Clevo NL5xRU and NL5xNU/TUXEDO Aura 15 Gen1 and Gen2 have both a
	 * working native and video interface. However the default detection
	 * mechanism first registers the video interface before unregistering
	 * it again and switching to the native interface during boot. This
	 * results in a dangling SBIOS request for backlight change for some
	 * reason, causing the backlight to switch to ~2% once per boot on the
	 * first power cord connect or disconnect event. Setting the native
	 * interface explicitly circumvents this buggy behaviour, by avoiding
	 * the unregistering process.
	 */
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "NL5xRU"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "AURA1501"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xRU",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "EDUBOOK1502"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "Clevo NL5xNU",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "NL5xNU"),
		},
	},
	/*
	 * The TongFang PF5PU1G, PF4NU1F, PF5NU1G, and PF5LUXG/TUXEDO BA15 Gen10,
	 * Pulse 14/15 Gen1, and Pulse 15 Gen2 have the same problem as the Clevo
	 * NL5xRU and NL5xNU/TUXEDO Aura 15 Gen1 and Gen2. See the description
	 * above.
	 */
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5PU1G",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5PU1G"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF4NU1F",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF4NU1F"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF4NU1F",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "PULSE1401"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5NU1G",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5NU1G"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5NU1G",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "PULSE1501"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang PF5LUXG",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "PF5LUXG"),
		},
	},
	/*
	 * More Tongfang devices with the same issue as the Clevo NL5xRU and
	 * NL5xNU/TUXEDO Aura 15 Gen1 and Gen2. See the description above.
	 */
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GKxNRxx",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "GKxNRxx"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GKxNRxx",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "POLARIS1501A1650TI"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GKxNRxx",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "POLARIS1501A2060"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GKxNRxx",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "POLARIS1701A1650TI"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GKxNRxx",
	.matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TUXEDO"),
		DMI_MATCH(DMI_BOARD_NAME, "POLARIS1701A2060"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GMxNGxx",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "GMxNGxx"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GMxZGxx",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "GMxZGxx"),
		},
	},
	{
	.callback = video_detect_force_native,
	.ident = "TongFang GMxRGxx",
	.matches = {
		DMI_MATCH(DMI_BOARD_NAME, "GMxRGxx"),
		},
	},
	/*
	 * Desktops which falsely report a backlight and which our heuristics
	 * for this do not catch.
	 */
	{
	 .callback = video_detect_force_none,
	 .ident = "Dell OptiPlex 9020M",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 9020M"),
		},
	},
	{
	 .callback = video_detect_force_none,
	 .ident = "MSI MS-7721",
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "MSI"),
		DMI_MATCH(DMI_PRODUCT_NAME, "MS-7721"),
		},
	},
	{ },
};

/* This uses a workqueue to avoid various locking ordering issues */
static void acpi_video_backlight_notify_work(struct work_struct *work)
{
	if (acpi_video_get_backlight_type() != acpi_backlight_video)
		acpi_video_unregister_backlight();
}

static int acpi_video_backlight_notify(struct notifier_block *nb,
				       unsigned long val, void *bd)
{
	struct backlight_device *backlight = bd;

	/* A raw bl registering may change video -> native */
	if (backlight->props.type == BACKLIGHT_RAW &&
	    val == BACKLIGHT_REGISTERED)
		schedule_work(&backlight_notify_work);

	return NOTIFY_OK;
}

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
		INIT_WORK(&backlight_notify_work,
			  acpi_video_backlight_notify_work);
		backlight_nb.notifier_call = acpi_video_backlight_notify;
		backlight_nb.priority = 0;
		if (backlight_register_notifier(&backlight_nb) == 0)
			backlight_notifier_registered = true;
		init_done = true;
	}
	mutex_unlock(&init_mutex);

	if (acpi_backlight_cmdline != acpi_backlight_undef)
		return acpi_backlight_cmdline;

	if (acpi_backlight_dmi != acpi_backlight_undef)
		return acpi_backlight_dmi;

	if (!(video_caps & ACPI_VIDEO_BACKLIGHT))
		return acpi_backlight_vendor;

	if (acpi_osi_is_win8() && backlight_device_get_by_type(BACKLIGHT_RAW))
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
	/* Remove acpi-video backlight interface if it is no longer desired */
	if (acpi_video_get_backlight_type() != acpi_backlight_video)
		acpi_video_unregister_backlight();
}
EXPORT_SYMBOL(acpi_video_set_dmi_backlight_type);

void __exit acpi_video_detect_exit(void)
{
	if (backlight_notifier_registered)
		backlight_unregister_notifier(&backlight_nb);
}
