/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  HID Haptic support for Linux
 *
 *  Copyright (c) 2021 Angela Czubak <acz@semihalf.com>
 */

#include <linux/hid.h>

#define HID_HAPTIC_ORDINAL_WAVEFORMNONE 1
#define HID_HAPTIC_ORDINAL_WAVEFORMSTOP 2

#define HID_HAPTIC_MODE_DEVICE 0
#define HID_HAPTIC_MODE_HOST 1

struct hid_haptic_effect {
	u8 *report_buf;
	struct input_dev *input_dev;
	struct work_struct work;
	struct list_head control;
	struct mutex control_mutex;
};

struct hid_haptic_effect_node {
	struct list_head node;
	struct file *file;
};

struct hid_haptic_device {
	struct input_dev *input_dev;
	struct hid_device *hdev;
	struct hid_report *auto_trigger_report;
	struct mutex auto_trigger_mutex;
	struct workqueue_struct *wq;
	struct hid_report *manual_trigger_report;
	struct mutex manual_trigger_mutex;
	size_t manual_trigger_report_len;
	int pressed_state;
	s32 pressure_sum;
	s32 force_logical_minimum;
	s32 force_physical_minimum;
	s32 force_resolution;
	u32 mode;
	u32 default_auto_trigger;
	u32 vendor_page;
	u32 vendor_id;
	u32 max_waveform_id;
	u32 max_duration_id;
	u16 *hid_usage_map;
	u32 *duration_map;
	u16 press_ordinal;
	u16 release_ordinal;
	struct hid_haptic_effect *effect;
	struct hid_haptic_effect stop_effect;
};

#if IS_ENABLED(CONFIG_HID_HAPTIC)
void hid_haptic_feature_mapping(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_field *field, struct hid_usage
				*usage);
bool hid_haptic_check_pressure_unit(struct hid_haptic_device *haptic,
				    struct hid_input *hi, struct hid_field *field);
int hid_haptic_input_mapping(struct hid_device *hdev,
			     struct hid_haptic_device *haptic,
			     struct hid_input *hi,
			     struct hid_field *field, struct hid_usage *usage,
			     unsigned long **bit, int *max);
int hid_haptic_input_configured(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_input *hi);
int hid_haptic_init(struct hid_device *hdev, struct hid_haptic_device **haptic_ptr);
void hid_haptic_handle_press_release(struct hid_haptic_device *haptic);
void hid_haptic_pressure_reset(struct hid_haptic_device *haptic);
void hid_haptic_pressure_increase(struct hid_haptic_device *haptic,
				  __s32 pressure);
#else
static inline
void hid_haptic_feature_mapping(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_field *field, struct hid_usage
				*usage)
{}
static inline
bool hid_haptic_check_pressure_unit(struct hid_haptic_device *haptic,
				    struct hid_input *hi, struct hid_field *field)
{
	return false;
}
static inline
int hid_haptic_input_mapping(struct hid_device *hdev,
			     struct hid_haptic_device *haptic,
			     struct hid_input *hi,
			     struct hid_field *field, struct hid_usage *usage,
			     unsigned long **bit, int *max)
{
	return 0;
}
static inline
int hid_haptic_input_configured(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_input *hi)
{
	return 0;
}
static inline
void hid_haptic_reset(struct hid_device *hdev, struct hid_haptic_device *haptic)
{}
static inline
int hid_haptic_init(struct hid_device *hdev, struct hid_haptic_device **haptic_ptr)
{
	return 0;
}
static inline
void hid_haptic_handle_press_release(struct hid_haptic_device *haptic) {}
static inline
bool hid_haptic_handle_input(struct hid_haptic_device *haptic)
{
	return false;
}
static inline
void hid_haptic_pressure_reset(struct hid_haptic_device *haptic) {}
static inline
void hid_haptic_pressure_increase(struct hid_haptic_device *haptic,
				  __s32 pressure)
{}
#endif
