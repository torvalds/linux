// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Driver for Dell laptop extras
 *
 *  Copyright (c) Lyndon Sanche <lsanche@lyndeno.ca>
 *
 *  Based on documentation in the libsmbios package:
 *  Copyright (C) 2005-2014 Dell Inc.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bits.h>
#include <linux/device/faux.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_profile.h>
#include <linux/slab.h>

#include "dell-smbios.h"

static struct faux_device *dell_pc_fdev;
static int supported_modes;

static const struct dmi_system_id dell_device_table[] __initconst = {
	{
		.ident = "Dell Inc.",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Inc."),
		},
	},
	{
		.ident = "Dell Computer Corporation",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "Dell Computer Corporation"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, dell_device_table);

/* Derived from smbios-thermal-ctl
 *
 * cbClass 17
 * cbSelect 19
 * User Selectable Thermal Tables(USTT)
 * cbArg1 determines the function to be performed
 * cbArg1 0x0 = Get Thermal Information
 *  cbRES1         Standard return codes (0, -1, -2)
 *  cbRES2, byte 0  Bitmap of supported thermal modes. A mode is supported if
 *                  its bit is set to 1
 *     Bit 0 Balanced
 *     Bit 1 Cool Bottom
 *     Bit 2 Quiet
 *     Bit 3 Performance
 *  cbRES2, byte 1 Bitmap of supported Active Acoustic Controller (AAC) modes.
 *                 Each mode corresponds to the supported thermal modes in
 *                  byte 0. A mode is supported if its bit is set to 1.
 *     Bit 0 AAC (Balanced)
 *     Bit 1 AAC (Cool Bottom
 *     Bit 2 AAC (Quiet)
 *     Bit 3 AAC (Performance)
 *  cbRes3, byte 0 Current Thermal Mode
 *     Bit 0 Balanced
 *     Bit 1 Cool Bottom
 *     Bit 2 Quiet
 *     Bit 3 Performanc
 *  cbRes3, byte 1  AAC Configuration type
 *          0       Global (AAC enable/disable applies to all supported USTT modes)
 *          1       USTT mode specific
 *  cbRes3, byte 2  Current Active Acoustic Controller (AAC) Mode
 *     If AAC Configuration Type is Global,
 *          0       AAC mode disabled
 *          1       AAC mode enabled
 *     If AAC Configuration Type is USTT mode specific (multiple bits may be set),
 *          Bit 0 AAC (Balanced)
 *          Bit 1 AAC (Cool Bottom
 *          Bit 2 AAC (Quiet)
 *          Bit 3 AAC (Performance)
 *  cbRes3, byte 3  Current Fan Failure Mode
 *     Bit 0 Minimal Fan Failure (at least one fan has failed, one fan working)
 *     Bit 1 Catastrophic Fan Failure (all fans have failed)
 *
 * cbArg1 0x1   (Set Thermal Information), both desired thermal mode and
 *               desired AAC mode shall be applied
 * cbArg2, byte 0  Desired Thermal Mode to set
 *                  (only one bit may be set for this parameter)
 *     Bit 0 Balanced
 *     Bit 1 Cool Bottom
 *     Bit 2 Quiet
 *     Bit 3 Performance
 * cbArg2, byte 1  Desired Active Acoustic Controller (AAC) Mode to set
 *     If AAC Configuration Type is Global,
 *         0  AAC mode disabled
 *         1  AAC mode enabled
 *     If AAC Configuration Type is USTT mode specific
 *     (multiple bits may be set for this parameter),
 *         Bit 0 AAC (Balanced)
 *         Bit 1 AAC (Cool Bottom
 *         Bit 2 AAC (Quiet)
 *         Bit 3 AAC (Performance)
 */

#define DELL_ACC_GET_FIELD	GENMASK(19, 16)
#define DELL_ACC_SET_FIELD	GENMASK(11, 8)
#define DELL_THERMAL_SUPPORTED	GENMASK(3, 0)

enum thermal_mode_bits {
	DELL_BALANCED    = BIT(0),
	DELL_COOL_BOTTOM = BIT(1),
	DELL_QUIET       = BIT(2),
	DELL_PERFORMANCE = BIT(3),
};

static int thermal_get_mode(void)
{
	struct calling_interface_buffer buffer;
	int state;
	int ret;

	dell_fill_request(&buffer, 0x0, 0, 0, 0);
	ret = dell_send_request(&buffer, CLASS_INFO, SELECT_THERMAL_MANAGEMENT);
	if (ret)
		return ret;
	state = buffer.output[2];
	if (state & DELL_BALANCED)
		return DELL_BALANCED;
	else if (state & DELL_COOL_BOTTOM)
		return DELL_COOL_BOTTOM;
	else if (state & DELL_QUIET)
		return DELL_QUIET;
	else if (state & DELL_PERFORMANCE)
		return DELL_PERFORMANCE;
	else
		return -ENXIO;
}

static int thermal_get_supported_modes(int *supported_bits)
{
	struct calling_interface_buffer buffer;
	int ret;

	dell_fill_request(&buffer, 0x0, 0, 0, 0);
	ret = dell_send_request(&buffer, CLASS_INFO, SELECT_THERMAL_MANAGEMENT);
	if (ret)
		return ret;
	*supported_bits = FIELD_GET(DELL_THERMAL_SUPPORTED, buffer.output[1]);
	return 0;
}

static int thermal_get_acc_mode(int *acc_mode)
{
	struct calling_interface_buffer buffer;
	int ret;

	dell_fill_request(&buffer, 0x0, 0, 0, 0);
	ret = dell_send_request(&buffer, CLASS_INFO, SELECT_THERMAL_MANAGEMENT);
	if (ret)
		return ret;
	*acc_mode = FIELD_GET(DELL_ACC_GET_FIELD, buffer.output[3]);
	return 0;
}

static int thermal_set_mode(enum thermal_mode_bits state)
{
	struct calling_interface_buffer buffer;
	int ret;
	int acc_mode;

	ret = thermal_get_acc_mode(&acc_mode);
	if (ret)
		return ret;

	dell_fill_request(&buffer, 0x1, FIELD_PREP(DELL_ACC_SET_FIELD, acc_mode) | state, 0, 0);
	return dell_send_request(&buffer, CLASS_INFO, SELECT_THERMAL_MANAGEMENT);
}

static int thermal_platform_profile_set(struct device *dev,
					enum platform_profile_option profile)
{
	switch (profile) {
	case PLATFORM_PROFILE_BALANCED:
		return thermal_set_mode(DELL_BALANCED);
	case PLATFORM_PROFILE_PERFORMANCE:
		return thermal_set_mode(DELL_PERFORMANCE);
	case PLATFORM_PROFILE_QUIET:
		return thermal_set_mode(DELL_QUIET);
	case PLATFORM_PROFILE_COOL:
		return thermal_set_mode(DELL_COOL_BOTTOM);
	default:
		return -EOPNOTSUPP;
	}
}

static int thermal_platform_profile_get(struct device *dev,
					enum platform_profile_option *profile)
{
	int ret;

	ret = thermal_get_mode();
	if (ret < 0)
		return ret;

	switch (ret) {
	case DELL_BALANCED:
		*profile = PLATFORM_PROFILE_BALANCED;
		break;
	case DELL_PERFORMANCE:
		*profile = PLATFORM_PROFILE_PERFORMANCE;
		break;
	case DELL_COOL_BOTTOM:
		*profile = PLATFORM_PROFILE_COOL;
		break;
	case DELL_QUIET:
		*profile = PLATFORM_PROFILE_QUIET;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int thermal_platform_profile_probe(void *drvdata, unsigned long *choices)
{
	int current_mode;

	if (supported_modes & DELL_QUIET)
		__set_bit(PLATFORM_PROFILE_QUIET, choices);
	if (supported_modes & DELL_COOL_BOTTOM)
		__set_bit(PLATFORM_PROFILE_COOL, choices);
	if (supported_modes & DELL_BALANCED)
		__set_bit(PLATFORM_PROFILE_BALANCED, choices);
	if (supported_modes & DELL_PERFORMANCE)
		__set_bit(PLATFORM_PROFILE_PERFORMANCE, choices);

	/* Make sure that ACPI is in sync with the profile set by USTT */
	current_mode = thermal_get_mode();
	if (current_mode < 0)
		return current_mode;

	thermal_set_mode(current_mode);

	return 0;
}

static const struct platform_profile_ops dell_pc_platform_profile_ops = {
	.probe = thermal_platform_profile_probe,
	.profile_get = thermal_platform_profile_get,
	.profile_set = thermal_platform_profile_set,
};

static int dell_pc_faux_probe(struct faux_device *fdev)
{
	struct device *ppdev;
	int ret;

	if (!dell_smbios_class_is_supported(CLASS_INFO))
		return -ENODEV;

	ret = thermal_get_supported_modes(&supported_modes);
	if (ret < 0)
		return ret;

	ppdev = devm_platform_profile_register(&fdev->dev, "dell-pc", NULL,
					       &dell_pc_platform_profile_ops);

	return PTR_ERR_OR_ZERO(ppdev);
}

static const struct faux_device_ops dell_pc_faux_ops = {
	.probe = dell_pc_faux_probe,
};

static int __init dell_init(void)
{
	if (!dmi_check_system(dell_device_table))
		return -ENODEV;

	dell_pc_fdev = faux_device_create("dell-pc", NULL, &dell_pc_faux_ops);
	if (!dell_pc_fdev)
		return -ENODEV;

	return 0;
}

static void __exit dell_exit(void)
{
	faux_device_destroy(dell_pc_fdev);
}

module_init(dell_init);
module_exit(dell_exit);

MODULE_AUTHOR("Lyndon Sanche <lsanche@lyndeno.ca>");
MODULE_DESCRIPTION("Dell PC driver");
MODULE_LICENSE("GPL");
