// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID Haptic support for Linux
 *
 *  Copyright (c) 2021 Angela Czubak <acz@semihalf.com>
 */

#include "hid-haptic.h"

void hid_haptic_feature_mapping(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_field *field, struct hid_usage *usage)
{
	if (usage->hid == HID_HP_AUTOTRIGGER) {
		if (usage->usage_index >= field->report_count) {
			dev_err(&hdev->dev,
				"HID_HP_AUTOTRIGGER out of range\n");
			return;
		}

		hid_device_io_start(hdev);
		hid_hw_request(hdev, field->report, HID_REQ_GET_REPORT);
		hid_hw_wait(hdev);
		hid_device_io_stop(hdev);
		haptic->default_auto_trigger =
			field->value[usage->usage_index];
		haptic->auto_trigger_report = field->report;
	}
}
EXPORT_SYMBOL_GPL(hid_haptic_feature_mapping);

bool hid_haptic_check_pressure_unit(struct hid_haptic_device *haptic,
				    struct hid_input *hi, struct hid_field *field)
{
	if (field->unit == HID_UNIT_GRAM || field->unit == HID_UNIT_NEWTON)
		return true;
	return false;
}
EXPORT_SYMBOL_GPL(hid_haptic_check_pressure_unit);

int hid_haptic_input_mapping(struct hid_device *hdev,
			     struct hid_haptic_device *haptic,
			     struct hid_input *hi,
			     struct hid_field *field, struct hid_usage *usage,
			     unsigned long **bit, int *max)
{
	if (usage->hid == HID_HP_MANUALTRIGGER) {
		haptic->manual_trigger_report = field->report;
		/* we don't really want to map these fields */
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hid_haptic_input_mapping);

int hid_haptic_input_configured(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_input *hi)
{

	if (hi->application == HID_DG_TOUCHPAD) {
		if (haptic->auto_trigger_report &&
		    haptic->manual_trigger_report) {
			__set_bit(INPUT_PROP_HAPTIC_TOUCHPAD, hi->input->propbit);
			return 1;
		}
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(hid_haptic_input_configured);
