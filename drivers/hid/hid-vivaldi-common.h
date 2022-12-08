/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _HID_VIVALDI_COMMON_H
#define _HID_VIVALDI_COMMON_H

struct hid_device;
struct hid_field;
struct hid_usage;

void vivaldi_feature_mapping(struct hid_device *hdev,
			     struct hid_field *field, struct hid_usage *usage);

extern const struct attribute_group *vivaldi_attribute_groups[];

#endif /* _HID_VIVALDI_COMMON_H */
