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
 * Backlight drivers can use acpi_video_get_backlight_type() to determine which
 * driver should handle the backlight. RAW/GPU-driver backlight drivers must
 * use the acpi_video_backlight_use_native() helper for this.
 *
 * If CONFIG_ACPI_VIDEO is neither set as "compiled in" (y) nor as a module (m)
 * this file will not be compiled and acpi_video_get_backlight_type() will
 * always return acpi_backlight_vendor.
 */

#include <linux/export.h>
#include <linux/acpi.h>
#include <linux/apple-gmux.h>
#include <linux/backlight.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/platform_data/x86/nvidia-wmi-ec-backlight.h>
#include <linux/types.h>
#include <linux/workqueue.h>
#include <acpi/video.h>

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
	struct acpi_device *acpi_dev = acpi_fetch_acpi_dev(handle);
	long *cap = context;
	struct pci_dev *dev;

	static const struct acpi_device_id video_ids[] = {
		{ACPI_VIDEO_HID, 0},
		{"", 0},
	};

	if (acpi_dev && !acpi_match_device_ids(acpi_dev, video_ids)) {
		dev = acpi_get_pci_dev(handle);
		if (!dev)
			return AE_OK;
		pci_dev_put(dev);
		*cap |= acpi_is_video_device(handle);
	}
	return AE_OK;
}

/* This depends on ACPI_WMI which is X86 only */
#ifdef CONFIG_X86
static bool nvidia_wmi_ec_supported(void)
{
	struct wmi_brightness_args args = {
		.mode = WMI_BRIGHTNESS_MODE_GET,
		.val = 0,
		.ret = 0,
	};
	struct acpi_buffer buf = { (acpi_size)sizeof(args), &args };
	acpi_status status;

	status = wmi_evaluate_method(WMI_BRIGHTNESS_GUID, 0,
				     WMI_BRIGHTNESS_METHOD_SOURCE, &buf, &buf);
	if (ACPI_FAILURE(status))
		return false;

	/*
	 * If brightness is handled by the EC then nvidia-wmi-ec-backlight
	 * should be used, else the GPU driver(s) should be used.
	 */
	return args.ret == WMI_BRIGHTNESS_SOURCE_EC;
}
#else
static bool nvidia_wmi_ec_supported(void)
{
	return false;
}
#endif

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
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1128309 */
	 .callback = video_detect_force_vendor,
	 /* Acer KAV80 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "KAV80"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus UL30VT */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30VT"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus UL30A */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK Computer Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UL30A"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus X55U */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X55U"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus X101CH */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X101CH"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus X401U */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X401U"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus X501U */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "X501U"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Asus 1015CX */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "1015CX"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* GIGABYTE GB-BXBT-2807 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "GIGABYTE"),
		DMI_MATCH(DMI_PRODUCT_NAME, "GB-BXBT-2807"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Samsung N150/N210/N220 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N150/N210/N220"),
		DMI_MATCH(DMI_BOARD_NAME, "N150/N210/N220"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Samsung NF110/NF210/NF310 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "NF110/NF210/NF310"),
		DMI_MATCH(DMI_BOARD_NAME, "NF110/NF210/NF310"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Samsung NC210 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "NC210/NC110"),
		DMI_MATCH(DMI_BOARD_NAME, "NC210/NC110"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Sony VPCEH3U1E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Sony Corporation"),
		DMI_MATCH(DMI_PRODUCT_NAME, "VPCEH3U1E"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 /* Xiaomi Mi Pad 2 */
	 .matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
		},
	},

	/*
	 * Toshiba models with Transflective display, these need to use
	 * the toshiba_acpi vendor driver for proper Transflective handling.
	 */
	{
	 .callback = video_detect_force_vendor,
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE R500"),
		},
	},
	{
	 .callback = video_detect_force_vendor,
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE R600"),
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
	 /* ThinkPad T420 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T420"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad T520 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad T520"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad X201s */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201s"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* ThinkPad X201T */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_VERSION, "ThinkPad X201T"),
		},
	},

	/* The native backlight controls do not work on some older machines */
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=81515 */
	 .callback = video_detect_force_video,
	 /* HP ENVY 15 Notebook */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP ENVY 15 Notebook PC"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* SAMSUNG 870Z5E/880Z5E/680Z5E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "870Z5E/880Z5E/680Z5E"),
		},
	},
	{
	 .callback = video_detect_force_video,
	 /* SAMSUNG 370R4E/370R4V/370R5E/3570RE/370R5V */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "370R4E/370R4V/370R5E/3570RE/370R5V"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1186097 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 3570R/370R/470R/450R/510R/4450RV */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "3570R/370R/470R/450R/510R/4450RV"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1557060 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 670Z5E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "670Z5E"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1094948 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 730U3E/740U3E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "730U3E/740U3E"),
		},
	},
	{
	 /* https://bugs.freedesktop.org/show_bug.cgi?id=87286 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 900X3C/900X3D/900X3E/900X4C/900X4D */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME,
			  "900X3C/900X3D/900X3E/900X4C/900X4D"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1272633 */
	 .callback = video_detect_force_video,
	 /* Dell XPS14 L421X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L421X"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1163574 */
	 .callback = video_detect_force_video,
	 /* Dell XPS15 L521X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS L521X"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=108971 */
	 .callback = video_detect_force_video,
	 /* SAMSUNG 530U4E/540U4E */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "530U4E/540U4E"),
		},
	},
	/* https://bugs.launchpad.net/bugs/1894667 */
	{
	 .callback = video_detect_force_video,
	 /* HP 635 Notebook */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Hewlett-Packard"),
		DMI_MATCH(DMI_PRODUCT_NAME, "HP 635 Notebook PC"),
		},
	},

	/* Non win8 machines which need native backlight nevertheless */
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1201530 */
	 .callback = video_detect_force_native,
	 /* Lenovo Ideapad S405 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_BOARD_NAME, "Lenovo IdeaPad S405"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1187004 */
	 .callback = video_detect_force_native,
	 /* Lenovo Ideapad Z570 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "102434U"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo E41-25 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "81FS"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Lenovo E41-45 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_NAME, "82BK"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1217249 */
	 .callback = video_detect_force_native,
	 /* Apple MacBook Pro 12,1 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Apple Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "MacBookPro12,1"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Inspiron N4010 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron N4010"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Vostro V131 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Vostro V131"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1123661 */
	 .callback = video_detect_force_native,
	 /* Dell XPS 17 L702X */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Dell System XPS L702X"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Dell Precision 7510 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Precision 7510"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Acer Aspire 5738z */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5738"),
		DMI_MATCH(DMI_BOARD_NAME, "JV50"),
		},
	},
	{
	 /* https://bugzilla.redhat.com/show_bug.cgi?id=1012674 */
	 .callback = video_detect_force_native,
	 /* Acer Aspire 5741 */
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5741"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=42993 */
	 .callback = video_detect_force_native,
	 /* Acer Aspire 5750 */
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Aspire 5750"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=42833 */
	 .callback = video_detect_force_native,
	 /* Acer Extensa 5235 */
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Extensa 5235"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Acer TravelMate 4750 */
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 4750"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=207835 */
	 .callback = video_detect_force_native,
	 /* Acer TravelMate 5735Z */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5735Z"),
		DMI_MATCH(DMI_BOARD_NAME, "BA51_MV"),
		},
	},
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=36322 */
	 .callback = video_detect_force_native,
	 /* Acer TravelMate 5760 */
	 .matches = {
		DMI_MATCH(DMI_BOARD_VENDOR, "Acer"),
		DMI_MATCH(DMI_PRODUCT_NAME, "TravelMate 5760"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* ASUSTeK COMPUTER INC. GA401 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA401"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* ASUSTeK COMPUTER INC. GA502 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA502"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* ASUSTeK COMPUTER INC. GA503 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "GA503"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Asus UX303UB */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
		DMI_MATCH(DMI_PRODUCT_NAME, "UX303UB"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Samsung N150P */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N150P"),
		DMI_MATCH(DMI_BOARD_NAME, "N150P"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Samsung N145P/N250P/N260P */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N145P/N250P/N260P"),
		DMI_MATCH(DMI_BOARD_NAME, "N145P/N250P/N260P"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Samsung N250P */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "SAMSUNG ELECTRONICS CO., LTD."),
		DMI_MATCH(DMI_PRODUCT_NAME, "N250P"),
		DMI_MATCH(DMI_BOARD_NAME, "N250P"),
		},
	},

	/*
	 * These Toshibas have a broken acpi-video interface for brightness
	 * control. They also have an issue where the panel is off after
	 * suspend until a special firmware call is made to turn it back
	 * on. This is handled by the toshiba_acpi kernel module, so that
	 * module must be enabled for these models to work correctly.
	 */
	{
	 /* https://bugzilla.kernel.org/show_bug.cgi?id=21012 */
	 .callback = video_detect_force_native,
	 /* Toshiba Portégé R700 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE R700"),
		},
	},
	{
	 /* Portégé: https://bugs.freedesktop.org/show_bug.cgi?id=82634 */
	 /* Satellite: https://bugzilla.kernel.org/show_bug.cgi?id=21012 */
	 .callback = video_detect_force_native,
	 /* Toshiba Satellite/Portégé R830 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "R830"),
		},
	},
	{
	 .callback = video_detect_force_native,
	 /* Toshiba Satellite/Portégé Z830 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Z830"),
		},
	},

	/*
	 * Desktops which falsely report a backlight and which our heuristics
	 * for this do not catch.
	 */
	{
	 .callback = video_detect_force_none,
	 /* Dell OptiPlex 9020M */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "OptiPlex 9020M"),
		},
	},
	{
	 .callback = video_detect_force_none,
	 /* MSI MS-7721 */
	 .matches = {
		DMI_MATCH(DMI_SYS_VENDOR, "MSI"),
		DMI_MATCH(DMI_PRODUCT_NAME, "MS-7721"),
		},
	},
	{ },
};

static bool google_cros_ec_present(void)
{
	return acpi_dev_found("GOOG0004");
}

/*
 * Determine which type of backlight interface to use on this system,
 * First check cmdline, then dmi quirks, then do autodetect.
 */
static enum acpi_backlight_type __acpi_video_get_backlight_type(bool native)
{
	static DEFINE_MUTEX(init_mutex);
	static bool nvidia_wmi_ec_present;
	static bool native_available;
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
		nvidia_wmi_ec_present = nvidia_wmi_ec_supported();
		init_done = true;
	}
	if (native)
		native_available = true;
	mutex_unlock(&init_mutex);

	/*
	 * The below heuristics / detection steps are in order of descending
	 * presedence. The commandline takes presedence over anything else.
	 */
	if (acpi_backlight_cmdline != acpi_backlight_undef)
		return acpi_backlight_cmdline;

	/* DMI quirks override any autodetection. */
	if (acpi_backlight_dmi != acpi_backlight_undef)
		return acpi_backlight_dmi;

	/* Special cases such as nvidia_wmi_ec and apple gmux. */
	if (nvidia_wmi_ec_present)
		return acpi_backlight_nvidia_wmi_ec;

	if (apple_gmux_present())
		return acpi_backlight_apple_gmux;

	/* On systems with ACPI video use either native or ACPI video. */
	if (video_caps & ACPI_VIDEO_BACKLIGHT) {
		/*
		 * Windows 8 and newer no longer use the ACPI video interface,
		 * so it often does not work. If the ACPI tables are written
		 * for win8 and native brightness ctl is available, use that.
		 *
		 * The native check deliberately is inside the if acpi-video
		 * block on older devices without acpi-video support native
		 * is usually not the best choice.
		 */
		if (acpi_osi_is_win8() && native_available)
			return acpi_backlight_native;
		else
			return acpi_backlight_video;
	}

	/*
	 * Chromebooks that don't have backlight handle in ACPI table
	 * are supposed to use native backlight if it's available.
	 */
	if (google_cros_ec_present() && native_available)
		return acpi_backlight_native;

	/* No ACPI video (old hw), use vendor specific fw methods. */
	return acpi_backlight_vendor;
}

enum acpi_backlight_type acpi_video_get_backlight_type(void)
{
	return __acpi_video_get_backlight_type(false);
}
EXPORT_SYMBOL(acpi_video_get_backlight_type);

bool acpi_video_backlight_use_native(void)
{
	return __acpi_video_get_backlight_type(true) == acpi_backlight_native;
}
EXPORT_SYMBOL(acpi_video_backlight_use_native);
