/*
 * X86 ACPI Utility Functions
 *
 * Copyright (C) 2017 Hans de Goede <hdegoede@redhat.com>
 *
 * Based on various non upstream patches to support the CHT Whiskey Cove PMIC:
 * Copyright (C) 2013-2015 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
 * We work around this by always reporting ACPI_STA_DEFAULT for these
 * devices. Note this MUST only be done for devices where this is safe.
 *
 * This forcing of devices to be present is limited to specific CPU (SoC)
 * models both to avoid potentially causing trouble on other models and
 * because some HIDs are re-used on different SoCs for completely
 * different devices.
 */
struct always_present_id {
	struct acpi_device_id hid[2];
	struct x86_cpu_id cpu_ids[2];
	struct dmi_system_id dmi_ids[2]; /* Optional */
	const char *uid;
};

#define ICPU(model)	{ X86_VENDOR_INTEL, 6, model, X86_FEATURE_ANY, }

#define ENTRY(hid, uid, cpu_models, dmi...) {				\
	{ { hid, }, {} },						\
	{ cpu_models, {} },						\
	{ { .matches = dmi }, {} },					\
	uid,								\
}

static const struct always_present_id always_present_ids[] = {
	/*
	 * Bay / Cherry Trail PWM directly poked by GPU driver in win10,
	 * but Linux uses a separate PWM driver, harmless if not used.
	 */
	ENTRY("80860F09", "1", ICPU(INTEL_FAM6_ATOM_SILVERMONT1), {}),
	ENTRY("80862288", "1", ICPU(INTEL_FAM6_ATOM_AIRMONT), {}),
	/*
	 * The INT0002 device is necessary to clear wakeup interrupt sources
	 * on Cherry Trail devices, without it we get nobody cared IRQ msgs.
	 */
	ENTRY("INT0002", "1", ICPU(INTEL_FAM6_ATOM_AIRMONT), {}),
	/*
	 * On the Dell Venue 11 Pro 7130 the DSDT hides the touchscreen ACPI
	 * device until a certain time after _SB.PCI0.GFX0.LCD.LCD1._ON gets
	 * called has passed *and* _STA has been called at least 3 times since.
	 */
	ENTRY("SYNA7500", "1", ICPU(INTEL_FAM6_HASWELL_ULT), {
		DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		DMI_MATCH(DMI_PRODUCT_NAME, "Venue 11 Pro 7130"),
	      }),
	/*
	 * The GPD win BIOS dated 20170320 has disabled the accelerometer, the
	 * drivers sometimes cause crashes under Windows and this is how the
	 * manufacturer has solved this :| Note that the the DMI data is less
	 * generic then it seems, a board_vendor of "AMI Corporation" is quite
	 * rare and a board_name of "Default String" also is rare.
	 */
	ENTRY("KIOX000A", "1", ICPU(INTEL_FAM6_ATOM_AIRMONT), {
		DMI_MATCH(DMI_BOARD_VENDOR, "AMI Corporation"),
		DMI_MATCH(DMI_BOARD_NAME, "Default string"),
		DMI_MATCH(DMI_PRODUCT_NAME, "Default string"),
		DMI_MATCH(DMI_BIOS_DATE, "03/20/2017")
	      }),
};

bool acpi_device_always_present(struct acpi_device *adev)
{
	u32 *status = (u32 *)&adev->status;
	u32 old_status = *status;
	bool ret = false;
	unsigned int i;

	/* acpi_match_device_ids checks status, so set it to default */
	*status = ACPI_STA_DEFAULT;
	for (i = 0; i < ARRAY_SIZE(always_present_ids); i++) {
		if (acpi_match_device_ids(adev, always_present_ids[i].hid))
			continue;

		if (!adev->pnp.unique_id ||
		    strcmp(adev->pnp.unique_id, always_present_ids[i].uid))
			continue;

		if (!x86_match_cpu(always_present_ids[i].cpu_ids))
			continue;

		if (always_present_ids[i].dmi_ids[0].matches[0].slot &&
		    !dmi_check_system(always_present_ids[i].dmi_ids))
			continue;

		if (old_status != ACPI_STA_DEFAULT) /* Log only once */
			dev_info(&adev->dev,
				 "Device [%s] is in always present list\n",
				 adev->pnp.bus_id);

		ret = true;
		break;
	}
	*status = old_status;

	return ret;
}
