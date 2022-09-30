// SPDX-License-Identifier: GPL-2.0-only
/*
 * X86 ACPI Utility Functions
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2013-2015 Intel Corporation. All rights reserved.
 */

#define pr_fmt(fmt) "ACPI: " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/platform_device.h>
#include <asm/cpu_device_id.h>
#include <asm/intel-family.h>
#include "../internal.h"

/*
 * Some ACPI devices are hidden (status == 0x0) in recent BIOS-es because
 * some recent Windows drivers bind to one device but poke at multiple
 * devices at the same time, so the others get hidden.
 *
 * Some BIOS-es (temporarily) hide specific APCI devices to work around Windows
 * driver bugs. We use DMI matching to match known cases of this.
 *
 * Likewise sometimes some not-actually present devices are sometimes
 * reported as present, which may cause issues.
 *
 * We work around this by using the below quirk list to override the status
 * reported by the _STA method with a fixed value (ACPI_STA_DEFAULT or 0).
 * Note this MUST only be done for devices where this is safe.
 *
 * This status overriding is limited to specific CPU (SoC) models both to
 * avoid potentially causing trouble on other models and because some HIDs
 * are re-used on different SoCs for completely different devices.
 */
struct override_status_id {
	struct acpi_device_id hid[2];
	struct x86_cpu_id cpu_ids[2];
	struct dmi_system_id dmi_ids[2]; /* Optional */
	const char *uid;
	const char *path;
	unsigned long long status;
};

#define ENTRY(status, hid, uid, path, cpu_model, dmi...) {		\
	{ { hid, }, {} },						\
	{ X86_MATCH_INTEL_FAM6_MODEL(cpu_model, NULL), {} },		\
	{ { .matches = dmi }, {} },					\
	uid,								\
	path,								\
	status,								\
}

#define PRESENT_ENTRY_HID(hid, uid, cpu_model, dmi...) \
	ENTRY(ACPI_STA_DEFAULT, hid, uid, NULL, cpu_model, dmi)

#define NOT_PRESENT_ENTRY_HID(hid, uid, cpu_model, dmi...) \
	ENTRY(0, hid, uid, NULL, cpu_model, dmi)

#define PRESENT_ENTRY_PATH(path, cpu_model, dmi...) \
	ENTRY(ACPI_STA_DEFAULT, "", NULL, path, cpu_model, dmi)

#define NOT_PRESENT_ENTRY_PATH(path, cpu_model, dmi...) \
	ENTRY(0, "", NULL, path, cpu_model, dmi)

static const struct override_status_id override_status_ids[] = {
	/*
	 * Bay / Cherry Trail PWM directly poked by GPU driver in win10,
	 * but Linux uses a separate PWM driver, harmless if not used.
	 */
	PRESENT_ENTRY_HID("80860F09", "1", ATOM_SILVERMONT, {}),
	PRESENT_ENTRY_HID("80862288", "1", ATOM_AIRMONT, {}),

	/* The Xiaomi Mi Pad 2 uses PWM2 for touchkeys backlight control */
	PRESENT_ENTRY_HID("80862289", "2", ATOM_AIRMONT, {
		DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
	      }),

	/*
	 * The INT0002 device is necessary to clear wakeup interrupt sources
	 * on Cherry Trail devices, without it we get nobody cared IRQ msgs.
	 */
	PRESENT_ENTRY_HID("INT0002", "1", ATOM_AIRMONT, {}),
	/*
	 * On the Dell Venue 11 Pro 7130 and 7139, the DSDT hides
	 * the touchscreen ACPI device until a certain time
	 * after _SB.PCI0.GFX0.LCD.LCD1._ON gets called has passed
	 * *and* _STA has been called at least 3 times since.
	 */
	PRESENT_ENTRY_HID("SYNA7500", "1", HASWELL_L, {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Venue 11 Pro 7130"),
	      }),
	PRESENT_ENTRY_HID("SYNA7500", "1", HASWELL_L, {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Venue 11 Pro 7139"),
	      }),

	/*
	 * The GPD win BIOS dated 20170221 has disabled the accelerometer, the
	 * drivers sometimes cause crashes under Windows and this is how the
	 * manufacturer has solved this :|  The DMI match may not seem unique,
	 * but it is. In the 67000+ DMI decode dumps from linux-hardware.org
	 * only 116 have board_vendor set to "AMI Corporation" and of those 116
	 * only the GPD win and pocket entries' board_name is "Default string".
	 *
	 * Unfortunately the GPD pocket also uses these strings and its BIOS
	 * was copy-pasted from the GPD win, so it has a disabled KIOX000A
	 * node which we should not enable, thus we also check the BIOS date.
	 */
	PRESENT_ENTRY_HID("KIOX000A", "1", ATOM_AIRMONT, {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "02/21/2017")
	      }),
	PRESENT_ENTRY_HID("KIOX000A", "1", ATOM_AIRMONT, {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "03/20/2017")
	      }),
	PRESENT_ENTRY_HID("KIOX000A", "1", ATOM_AIRMONT, {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "05/25/2017")
	      }),

	/*
	 * The GPD win/pocket have a PCI wifi card, but its DSDT has the SDIO
	 * mmc controller enabled and that has a child-device which _PS3
	 * method sets a GPIO causing the PCI wifi card to turn off.
	 * See above remark about uniqueness of the DMI match.
	 */
	NOT_PRESENT_ENTRY_PATH("\\_SB_.PCI0.SDHB.BRC1", ATOM_AIRMONT, {
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
	      }),
};

bool acpi_device_override_status(struct acpi_device *adev, unsigned long long *status)
{
	bool ret = false;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(override_status_ids); i++) {
		if (!x86_match_cpu(override_status_ids[i].cpu_ids))
			continue;

		if (override_status_ids[i].dmi_ids[0].matches[0].slot &&
		    !dmi_check_system(override_status_ids[i].dmi_ids))
			continue;

		if (override_status_ids[i].path) {
			struct acpi_buffer path = { ACPI_ALLOCATE_BUFFER, NULL };
			bool match;

			if (acpi_get_name(adev->handle, ACPI_FULL_PATHNAME, &path))
				continue;

			match = strcmp((char *)path.pointer, override_status_ids[i].path) == 0;
			kfree(path.pointer);

			if (!match)
				continue;
		} else {
			if (acpi_match_device_ids(adev, override_status_ids[i].hid))
				continue;

			if (!adev->pnp.unique_id ||
			    strcmp(adev->pnp.unique_id, override_status_ids[i].uid))
				continue;
		}

		*status = override_status_ids[i].status;
		ret = true;
		break;
	}

	return ret;
}

/*
 * AMD systems from Renoir and Lucienne *require* that the NVME controller
 * is put into D3 over a Modern Standby / suspend-to-idle cycle.
 *
 * This is "typically" accomplished using the `StorageD3Enable`
 * property in the _DSD that is checked via the `acpi_storage_d3` function
 * but this property was introduced after many of these systems launched
 * and most OEM systems don't have it in their BIOS.
 *
 * The Microsoft documentation for StorageD3Enable mentioned that Windows has
 * a hardcoded allowlist for D3 support, which was used for these platforms.
 *
 * This allows quirking on Linux in a similar fashion.
 */
static const struct x86_cpu_id storage_d3_cpu_ids[] = {
	X86_MATCH_VENDOR_FAM_MODEL(AMD, 23, 96, NULL),	/* Renoir */
	X86_MATCH_VENDOR_FAM_MODEL(AMD, 23, 104, NULL),	/* Lucienne */
	{}
};

static const struct dmi_system_id force_storage_d3_dmi[] = {
	{
		/*
		 * _ADR is ambiguous between GPP1.DEV0 and GPP1.NVME
		 * but .NVME is needed to get StorageD3Enable node
		 * https://bugzilla.kernel.org/show_bug.cgi?id=216440
		 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
			DMI_MATCH(DMI_PRODUCT_NAME, "Inspiron 14 7425 2-in-1"),
		}
	},
	{}
};

bool force_storage_d3(void)
{
	const struct dmi_system_id *dmi_id = dmi_first_match(force_storage_d3_dmi);

	return dmi_id || x86_match_cpu(storage_d3_cpu_ids);
}

/*
 * x86 ACPI boards which ship with only Android as their factory image usually
 * declare a whole bunch of bogus I2C devices in their ACPI tables and sometimes
 * there are issues with serdev devices on these boards too, e.g. the resource
 * points to the wrong serdev_controller.
 *
 * Instantiating I2C / serdev devs for these bogus devs causes various issues,
 * e.g. GPIO/IRQ resource conflicts because sometimes drivers do bind to them.
 * The Android x86 kernel fork shipped on these devices has some special code
 * to remove the bogus I2C clients (and AFAICT serdevs are ignored completely).
 *
 * The acpi_quirk_skip_*_enumeration() functions below are used by the I2C or
 * serdev code to skip instantiating any I2C or serdev devs on broken boards.
 *
 * In case of I2C an exception is made for HIDs on the i2c_acpi_known_good_ids
 * list. These are known to always be correct (and in case of the audio-codecs
 * the drivers heavily rely on the codec being enumerated through ACPI).
 *
 * Note these boards typically do actually have I2C and serdev devices,
 * just different ones then the ones described in their DSDT. The devices
 * which are actually present are manually instantiated by the
 * drivers/platform/x86/x86-android-tablets.c kernel module.
 */
#define ACPI_QUIRK_SKIP_I2C_CLIENTS				BIT(0)
#define ACPI_QUIRK_UART1_TTY_UART2_SKIP				BIT(1)
#define ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY			BIT(2)
#define ACPI_QUIRK_USE_ACPI_AC_AND_BATTERY			BIT(3)

static const struct dmi_system_id acpi_quirk_skip_dmi_ids[] = {
	/*
	 * 1. Devices with only the skip / don't-skip AC and battery quirks,
	 *    sorted alphabetically.
	 */
	{
		/* ECS EF20EA, AXP288 PMIC but uses separate fuel-gauge */
		.matches = {
			DMI_MATCH(DMI_PRODUCT_NAME, "EF20EA"),
		},
		.driver_data = (void *)ACPI_QUIRK_USE_ACPI_AC_AND_BATTERY
	},
	{
		/* Lenovo Ideapad Miix 320, AXP288 PMIC, separate fuel-gauge */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "80XF"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Lenovo MIIX 320-10ICR"),
		},
		.driver_data = (void *)ACPI_QUIRK_USE_ACPI_AC_AND_BATTERY
	},

	/*
	 * 2. Devices which also have the skip i2c/serdev quirks and which
	 *    need the x86-android-tablets module to properly work.
	 */
#if IS_ENABLED(CONFIG_X86_ANDROID_TABLETS)
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ME176C"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_UART1_TTY_UART2_SKIP |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "TF103C"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Lenovo Yoga Tablet 1050F/L */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
			/* Partial match on beginning of BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Nextbook Ares 8 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "M890BAP"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Whitelabel (sold as various brands) TM800A550L */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			/* Above strings are too generic, also match on BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "ZY-8-BI-PX4S70VTR400-X423B-005-D"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
#endif
	{}
};

#if IS_ENABLED(CONFIG_X86_ANDROID_TABLETS)
static const struct acpi_device_id i2c_acpi_known_good_ids[] = {
	{ "10EC5640", 0 }, /* RealTek ALC5640 audio codec */
	{ "INT33F4", 0 },  /* X-Powers AXP288 PMIC */
	{ "INT33FD", 0 },  /* Intel Crystal Cove PMIC */
	{ "NPCE69A", 0 },  /* Asus Transformer keyboard dock */
	{}
};

bool acpi_quirk_skip_i2c_client_enumeration(struct acpi_device *adev)
{
	const struct dmi_system_id *dmi_id;
	long quirks;

	dmi_id = dmi_first_match(acpi_quirk_skip_dmi_ids);
	if (!dmi_id)
		return false;

	quirks = (unsigned long)dmi_id->driver_data;
	if (!(quirks & ACPI_QUIRK_SKIP_I2C_CLIENTS))
		return false;

	return acpi_match_device_ids(adev, i2c_acpi_known_good_ids);
}
EXPORT_SYMBOL_GPL(acpi_quirk_skip_i2c_client_enumeration);

int acpi_quirk_skip_serdev_enumeration(struct device *controller_parent, bool *skip)
{
	struct acpi_device *adev = ACPI_COMPANION(controller_parent);
	const struct dmi_system_id *dmi_id;
	long quirks = 0;

	*skip = false;

	/* !dev_is_platform() to not match on PNP enumerated debug UARTs */
	if (!adev || !adev->pnp.unique_id || !dev_is_platform(controller_parent))
		return 0;

	dmi_id = dmi_first_match(acpi_quirk_skip_dmi_ids);
	if (dmi_id)
		quirks = (unsigned long)dmi_id->driver_data;

	if (quirks & ACPI_QUIRK_UART1_TTY_UART2_SKIP) {
		if (!strcmp(adev->pnp.unique_id, "1"))
			return -ENODEV; /* Create tty cdev instead of serdev */

		if (!strcmp(adev->pnp.unique_id, "2"))
			*skip = true;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_quirk_skip_serdev_enumeration);
#endif

/* Lists of PMIC ACPI HIDs with an (often better) native charger driver */
static const struct {
	const char *hid;
	int hrv;
} acpi_skip_ac_and_battery_pmic_ids[] = {
	{ "INT33F4", -1 }, /* X-Powers AXP288 PMIC */
	{ "INT34D3",  3 }, /* Intel Cherrytrail Whiskey Cove PMIC */
};

bool acpi_quirk_skip_acpi_ac_and_battery(void)
{
	const struct dmi_system_id *dmi_id;
	long quirks = 0;
	int i;

	dmi_id = dmi_first_match(acpi_quirk_skip_dmi_ids);
	if (dmi_id)
		quirks = (unsigned long)dmi_id->driver_data;

	if (quirks & ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY)
		return true;

	if (quirks & ACPI_QUIRK_USE_ACPI_AC_AND_BATTERY)
		return false;

	for (i = 0; i < ARRAY_SIZE(acpi_skip_ac_and_battery_pmic_ids); i++) {
		if (acpi_dev_present(acpi_skip_ac_and_battery_pmic_ids[i].hid, "1",
				     acpi_skip_ac_and_battery_pmic_ids[i].hrv)) {
			pr_info_once("found native %s PMIC, skipping ACPI AC and battery devices\n",
				     acpi_skip_ac_and_battery_pmic_ids[i].hid);
			return true;
		}
	}

	return false;
}
EXPORT_SYMBOL_GPL(acpi_quirk_skip_acpi_ac_and_battery);
