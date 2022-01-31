// SPDX-License-Identifier: GPL-2.0-only
/*
 * X86 ACPI Utility Functions
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2013-2015 Intel Corporation. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
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

bool force_storage_d3(void)
{
	return x86_match_cpu(storage_d3_cpu_ids);
}
