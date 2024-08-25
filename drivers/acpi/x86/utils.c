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

#define ENTRY(status, hid, uid, path, cpu_vfm, dmi...) {		\
	{ { hid, }, {} },						\
	{ X86_MATCH_VFM(cpu_vfm, NULL), {} },				\
	{ { .matches = dmi }, {} },					\
	uid,								\
	path,								\
	status,								\
}

#define PRESENT_ENTRY_HID(hid, uid, cpu_vfm, dmi...) \
	ENTRY(ACPI_STA_DEFAULT, hid, uid, NULL, cpu_vfm, dmi)

#define NOT_PRESENT_ENTRY_HID(hid, uid, cpu_vfm, dmi...) \
	ENTRY(0, hid, uid, NULL, cpu_vfm, dmi)

#define PRESENT_ENTRY_PATH(path, cpu_vfm, dmi...) \
	ENTRY(ACPI_STA_DEFAULT, "", NULL, path, cpu_vfm, dmi)

#define NOT_PRESENT_ENTRY_PATH(path, cpu_vfm, dmi...) \
	ENTRY(0, "", NULL, path, cpu_vfm, dmi)

static const struct override_status_id override_status_ids[] = {
	/*
	 * Bay / Cherry Trail PWM directly poked by GPU driver in win10,
	 * but Linux uses a separate PWM driver, harmless if not used.
	 */
	PRESENT_ENTRY_HID("80860F09", "1", INTEL_ATOM_SILVERMONT, {}),
	PRESENT_ENTRY_HID("80862288", "1", INTEL_ATOM_AIRMONT, {}),

	/* The Xiaomi Mi Pad 2 uses PWM2 for touchkeys backlight control */
	PRESENT_ENTRY_HID("80862289", "2", INTEL_ATOM_AIRMONT, {
		DMI_MATCH(DMI_SYS_VENDOR, "Xiaomi Inc"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Mipad2"),
	      }),

	/*
	 * The INT0002 device is necessary to clear wakeup interrupt sources
	 * on Cherry Trail devices, without it we get nobody cared IRQ msgs.
	 */
	PRESENT_ENTRY_HID("INT0002", "1", INTEL_ATOM_AIRMONT, {}),
	/*
	 * On the Dell Venue 11 Pro 7130 and 7139, the DSDT hides
	 * the touchscreen ACPI device until a certain time
	 * after _SB.PCI0.GFX0.LCD.LCD1._ON gets called has passed
	 * *and* _STA has been called at least 3 times since.
	 */
	PRESENT_ENTRY_HID("SYNA7500", "1", INTEL_HASWELL_L, {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Venue 11 Pro 7130"),
	      }),
	PRESENT_ENTRY_HID("SYNA7500", "1", INTEL_HASWELL_L, {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Venue 11 Pro 7139"),
	      }),

	/*
	 * The Dell XPS 15 9550 has a SMO8110 accelerometer /
	 * HDD freefall sensor which is wrongly marked as not present.
	 */
	PRESENT_ENTRY_HID("SMO8810", "1", INTEL_SKYLAKE, {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "XPS 15 9550"),
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
	PRESENT_ENTRY_HID("KIOX000A", "1", INTEL_ATOM_AIRMONT, {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "02/21/2017")
	      }),
	PRESENT_ENTRY_HID("KIOX000A", "1", INTEL_ATOM_AIRMONT, {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "03/20/2017")
	      }),
	PRESENT_ENTRY_HID("KIOX000A", "1", INTEL_ATOM_AIRMONT, {
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
	NOT_PRESENT_ENTRY_PATH("\\_SB_.PCI0.SDHB.BRC1", INTEL_ATOM_AIRMONT, {
		DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_EXACT_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_EXACT_MATCH(DMI_BOARD_SERIAL, "Default string"),
		DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Default string"),
	      }),

	/*
	 * The LSM303D on the Lenovo Yoga Tablet 2 series is present
	 * as both ACCL0001 and MAGN0001. As we can only ever register an
	 * i2c client for one of them, ignore MAGN0001.
	 */
	NOT_PRESENT_ENTRY_HID("MAGN0001", "1", INTEL_ATOM_SILVERMONT, {
		DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
		DMI_MATCH(DMI_PRODUCT_FAMILY, "YOGATablet2"),
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

			if (!acpi_dev_uid_match(adev, override_status_ids[i].uid))
				continue;
		}

		*status = override_status_ids[i].status;
		ret = true;
		break;
	}

	return ret;
}

/*
 * AMD systems from Renoir onwards *require* that the NVME controller
 * is put into D3 over a Modern Standby / suspend-to-idle cycle.
 *
 * This is "typically" accomplished using the `StorageD3Enable`
 * property in the _DSD that is checked via the `acpi_storage_d3` function
 * but some OEM systems still don't have it in their BIOS.
 *
 * The Microsoft documentation for StorageD3Enable mentioned that Windows has
 * a hardcoded allowlist for D3 support as well as a registry key to override
 * the BIOS, which has been used for these cases.
 *
 * This allows quirking on Linux in a similar fashion.
 *
 * Cezanne systems shouldn't *normally* need this as the BIOS includes
 * StorageD3Enable.  But for two reasons we have added it.
 * 1) The BIOS on a number of Dell systems have ambiguity
 *    between the same value used for _ADR on ACPI nodes GPP1.DEV0 and GPP1.NVME.
 *    GPP1.NVME is needed to get StorageD3Enable node set properly.
 *    https://bugzilla.kernel.org/show_bug.cgi?id=216440
 *    https://bugzilla.kernel.org/show_bug.cgi?id=216773
 *    https://bugzilla.kernel.org/show_bug.cgi?id=217003
 * 2) On at least one HP system StorageD3Enable is missing on the second NVME
 *    disk in the system.
 * 3) On at least one HP Rembrandt system StorageD3Enable is missing on the only
 *    NVME device.
 */
bool force_storage_d3(void)
{
	if (!cpu_feature_enabled(X86_FEATURE_ZEN))
		return false;
	return acpi_gbl_FADT.flags & ACPI_FADT_LOW_POWER_S0;
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
#define ACPI_QUIRK_UART1_SKIP					BIT(1)
#define ACPI_QUIRK_UART1_TTY_UART2_SKIP				BIT(2)
#define ACPI_QUIRK_PNP_UART1_SKIP				BIT(3)
#define ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY			BIT(4)
#define ACPI_QUIRK_USE_ACPI_AC_AND_BATTERY			BIT(5)
#define ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS			BIT(6)

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
		/* Acer Iconia One 7 B1-750 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "VESPA2"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY |
					ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS),
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "ME176C"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_UART1_TTY_UART2_SKIP |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY |
					ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS),
	},
	{
		/* Lenovo Yoga Book X90F/L */
		.matches = {
			DMI_EXACT_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "CHERRYVIEW D1 PLATFORM"),
			DMI_EXACT_MATCH(DMI_PRODUCT_VERSION, "YETI-11"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_UART1_SKIP |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY |
					ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS),
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "ASUSTeK COMPUTER INC."),
			DMI_MATCH(DMI_PRODUCT_NAME, "TF103C"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY |
					ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS),
	},
	{
		/* Lenovo Yoga Tablet 2 1050F/L */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corp."),
			DMI_MATCH(DMI_PRODUCT_NAME, "VALLEYVIEW C0 PLATFORM"),
			DMI_MATCH(DMI_BOARD_NAME, "BYT-T FFD8"),
			/* Partial match on beginning of BIOS version */
			DMI_MATCH(DMI_BIOS_VERSION, "BLADE_21"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_PNP_UART1_SKIP |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Lenovo Yoga Tab 3 Pro X90F */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Intel Corporation"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Blade3-10A-001"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Medion Lifetab S10346 */
		.matches = {
			DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
			DMI_MATCH(DMI_BOARD_NAME, "Aptio CRB"),
			/* Way too generic, also match on BIOS data */
			DMI_MATCH(DMI_BIOS_DATE, "10/22/2015"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY),
	},
	{
		/* Nextbook Ares 8 (BYT version)*/
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "M890BAP"),
		},
		.driver_data = (void *)(ACPI_QUIRK_SKIP_I2C_CLIENTS |
					ACPI_QUIRK_SKIP_ACPI_AC_AND_BATTERY |
					ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS),
	},
	{
		/* Nextbook Ares 8A (CHT version)*/
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Insyde"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CherryTrail"),
			DMI_MATCH(DMI_BIOS_VERSION, "M882"),
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
	{ "10EC5651", 0 }, /* RealTek ALC5651 audio codec */
	{ "INT33F4", 0 },  /* X-Powers AXP288 PMIC */
	{ "INT33FD", 0 },  /* Intel Crystal Cove PMIC */
	{ "INT34D3", 0 },  /* Intel Whiskey Cove PMIC */
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

static int acpi_dmi_skip_serdev_enumeration(struct device *controller_parent, bool *skip)
{
	struct acpi_device *adev = ACPI_COMPANION(controller_parent);
	const struct dmi_system_id *dmi_id;
	long quirks = 0;
	u64 uid;
	int ret;

	ret = acpi_dev_uid_to_integer(adev, &uid);
	if (ret)
		return 0;

	dmi_id = dmi_first_match(acpi_quirk_skip_dmi_ids);
	if (dmi_id)
		quirks = (unsigned long)dmi_id->driver_data;

	if (!dev_is_platform(controller_parent)) {
		/* PNP enumerated UARTs */
		if ((quirks & ACPI_QUIRK_PNP_UART1_SKIP) && uid == 1)
			*skip = true;

		return 0;
	}

	if ((quirks & ACPI_QUIRK_UART1_SKIP) && uid == 1)
		*skip = true;

	if (quirks & ACPI_QUIRK_UART1_TTY_UART2_SKIP) {
		if (uid == 1)
			return -ENODEV; /* Create tty cdev instead of serdev */

		if (uid == 2)
			*skip = true;
	}

	return 0;
}

bool acpi_quirk_skip_gpio_event_handlers(void)
{
	const struct dmi_system_id *dmi_id;
	long quirks;

	dmi_id = dmi_first_match(acpi_quirk_skip_dmi_ids);
	if (!dmi_id)
		return false;

	quirks = (unsigned long)dmi_id->driver_data;
	return (quirks & ACPI_QUIRK_SKIP_GPIO_EVENT_HANDLERS);
}
EXPORT_SYMBOL_GPL(acpi_quirk_skip_gpio_event_handlers);
#else
static int acpi_dmi_skip_serdev_enumeration(struct device *controller_parent, bool *skip)
{
	return 0;
}
#endif

int acpi_quirk_skip_serdev_enumeration(struct device *controller_parent, bool *skip)
{
	struct acpi_device *adev = ACPI_COMPANION(controller_parent);

	*skip = false;

	/*
	 * The DELL0501 ACPI HID represents an UART (CID is set to PNP0501) with
	 * a backlight-controller attached. There is no separate ACPI device with
	 * an UartSerialBusV2() resource to model the backlight-controller.
	 * Set skip to true so that the tty core creates a serdev ctrl device.
	 * The backlight driver will manually create the serdev client device.
	 */
	if (acpi_dev_hid_match(adev, "DELL0501")) {
		*skip = true;
		/*
		 * Create a platform dev for dell-uart-backlight to bind to.
		 * This is a static device, so no need to store the result.
		 */
		platform_device_register_simple("dell-uart-backlight", PLATFORM_DEVID_NONE,
						NULL, 0);
		return 0;
	}

	return acpi_dmi_skip_serdev_enumeration(controller_parent, skip);
}
EXPORT_SYMBOL_GPL(acpi_quirk_skip_serdev_enumeration);

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

/* This section provides a workaround for a specific x86 system
 * which requires disabling of mwait to work correctly.
 */
static int __init acpi_proc_quirk_set_no_mwait(const struct dmi_system_id *id)
{
	pr_notice("%s detected - disabling mwait for CPU C-states\n",
		  id->ident);
	boot_option_idle_override = IDLE_NOMWAIT;
	return 0;
}

static const struct dmi_system_id acpi_proc_quirk_mwait_dmi_table[] __initconst = {
	{
		.callback = acpi_proc_quirk_set_no_mwait,
		.ident = "Extensa 5220",
		.matches =  {
			DMI_MATCH(DMI_BIOS_VENDOR, "Phoenix Technologies LTD"),
			DMI_MATCH(DMI_SYS_VENDOR, "Acer"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "0100"),
			DMI_MATCH(DMI_BOARD_NAME, "Columbia"),
		},
		.driver_data = NULL,
	},
	{}
};

void __init acpi_proc_quirk_mwait_check(void)
{
	/*
	 * Check whether the system is DMI table. If yes, OSPM
	 * should not use mwait for CPU-states.
	 */
	dmi_check_system(acpi_proc_quirk_mwait_dmi_table);
}
