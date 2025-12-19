// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  HID Haptic support for Linux
 *
 *  Copyright (c) 2021 Angela Czubak <acz@semihalf.com>
 */

#include <linux/input/mt.h>
#include <linux/module.h>

#include "hid-haptic.h"

void hid_haptic_feature_mapping(struct hid_device *hdev,
				struct hid_haptic_device *haptic,
				struct hid_field *field, struct hid_usage *usage)
{
	u16 usage_hid;

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
	} else if ((usage->hid & HID_USAGE_PAGE) == HID_UP_ORDINAL) {
		usage_hid = usage->hid & HID_USAGE;
		switch (field->logical) {
		case HID_HP_WAVEFORMLIST:
			if (usage_hid > haptic->max_waveform_id)
				haptic->max_waveform_id = usage_hid;
			break;
		case HID_HP_DURATIONLIST:
			if (usage_hid > haptic->max_duration_id)
				haptic->max_duration_id = usage_hid;
			break;
		default:
			break;
		}
	}
}
EXPORT_SYMBOL_GPL(hid_haptic_feature_mapping);

bool hid_haptic_check_pressure_unit(struct hid_haptic_device *haptic,
				    struct hid_input *hi, struct hid_field *field)
{
	if (field->unit == HID_UNIT_GRAM || field->unit == HID_UNIT_NEWTON) {
		haptic->force_logical_minimum = field->logical_minimum;
		haptic->force_physical_minimum = field->physical_minimum;
		haptic->force_resolution = input_abs_get_res(hi->input,
							     ABS_MT_PRESSURE);
		return true;
	}
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
			__set_bit(INPUT_PROP_PRESSUREPAD, hi->input->propbit);
			return 1;
		}
		return 0;
	}
	return -1;
}
EXPORT_SYMBOL_GPL(hid_haptic_input_configured);

static void parse_auto_trigger_field(struct hid_haptic_device *haptic,
				     struct hid_field *field)
{
	int count = field->report_count;
	int n;
	u16 usage_hid;

	for (n = 0; n < count; n++) {
		switch (field->usage[n].hid & HID_USAGE_PAGE) {
		case HID_UP_ORDINAL:
			usage_hid = field->usage[n].hid & HID_USAGE;
			switch (field->logical) {
			case HID_HP_WAVEFORMLIST:
				haptic->hid_usage_map[usage_hid] = field->value[n];
				if (field->value[n] ==
				    (HID_HP_WAVEFORMPRESS & HID_USAGE)) {
					haptic->press_ordinal = usage_hid;
				} else if (field->value[n] ==
					   (HID_HP_WAVEFORMRELEASE & HID_USAGE)) {
					haptic->release_ordinal = usage_hid;
				}
				break;
			case HID_HP_DURATIONLIST:
				haptic->duration_map[usage_hid] =
					field->value[n];
				break;
			default:
				break;
			}
			break;
		case HID_UP_HAPTIC:
			switch (field->usage[n].hid) {
			case HID_HP_WAVEFORMVENDORID:
				haptic->vendor_id = field->value[n];
				break;
			case HID_HP_WAVEFORMVENDORPAGE:
				haptic->vendor_page = field->value[n];
				break;
			default:
				break;
			}
			break;
		default:
			/* Should not really happen */
			break;
		}
	}
}

static void fill_effect_buf(struct hid_haptic_device *haptic,
			    struct ff_haptic_effect *effect,
			    struct hid_haptic_effect *haptic_effect,
			    int waveform_ordinal)
{
	struct hid_report *rep = haptic->manual_trigger_report;
	struct hid_usage *usage;
	struct hid_field *field;
	s32 value;
	int i, j;
	u8 *buf = haptic_effect->report_buf;

	mutex_lock(&haptic->manual_trigger_mutex);
	for (i = 0; i < rep->maxfield; i++) {
		field = rep->field[i];
		/* Ignore if report count is out of bounds. */
		if (field->report_count < 1)
			continue;

		for (j = 0; j < field->maxusage; j++) {
			usage = &field->usage[j];

			switch (usage->hid) {
			case HID_HP_INTENSITY:
				if (effect->intensity > 100) {
					value = field->logical_maximum;
				} else {
					value = field->logical_minimum +
						effect->intensity *
						(field->logical_maximum -
						 field->logical_minimum) / 100;
				}
				break;
			case HID_HP_REPEATCOUNT:
				value = effect->repeat_count;
				break;
			case HID_HP_RETRIGGERPERIOD:
				value = effect->retrigger_period;
				break;
			case HID_HP_MANUALTRIGGER:
				value = waveform_ordinal;
				break;
			default:
				break;
			}

			field->value[j] = value;
		}
	}

	hid_output_report(rep, buf);
	mutex_unlock(&haptic->manual_trigger_mutex);
}

static void switch_mode(struct hid_device *hdev, struct hid_haptic_device *haptic,
			int mode)
{
	struct hid_report *rep = haptic->auto_trigger_report;
	struct hid_field *field;
	s32 value;
	int i, j;

	if (mode == HID_HAPTIC_MODE_HOST)
		value = HID_HAPTIC_ORDINAL_WAVEFORMSTOP;
	else
		value = haptic->default_auto_trigger;

	mutex_lock(&haptic->auto_trigger_mutex);
	for (i = 0; i < rep->maxfield; i++) {
		field = rep->field[i];
		/* Ignore if report count is out of bounds. */
		if (field->report_count < 1)
			continue;

		for (j = 0; j < field->maxusage; j++) {
			if (field->usage[j].hid == HID_HP_AUTOTRIGGER)
				field->value[j] = value;
		}
	}

	/* send the report */
	hid_hw_request(hdev, rep, HID_REQ_SET_REPORT);
	mutex_unlock(&haptic->auto_trigger_mutex);
	haptic->mode = mode;
}

static int hid_haptic_upload_effect(struct input_dev *dev, struct ff_effect *effect,
				    struct ff_effect *old)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct ff_device *ff = dev->ff;
	struct hid_haptic_device *haptic = ff->private;
	int i, ordinal = 0;
	bool switch_modes = false;

	/* If vendor range, check vendor id and page */
	if (effect->u.haptic.hid_usage >= (HID_HP_VENDORWAVEFORMMIN & HID_USAGE) &&
	    effect->u.haptic.hid_usage <= (HID_HP_VENDORWAVEFORMMAX & HID_USAGE) &&
	    (effect->u.haptic.vendor_id != haptic->vendor_id ||
	     effect->u.haptic.vendor_waveform_page != haptic->vendor_page))
		return -EINVAL;

	/* Check hid_usage */
	for (i = 1; i <= haptic->max_waveform_id; i++) {
		if (haptic->hid_usage_map[i] == effect->u.haptic.hid_usage) {
			ordinal = i;
			break;
		}
	}
	if (ordinal < 1)
		return -EINVAL;

	/* Fill the buffer for the effect id */
	fill_effect_buf(haptic, &effect->u.haptic, &haptic->effect[effect->id],
			ordinal);

	if (effect->u.haptic.hid_usage == (HID_HP_WAVEFORMPRESS & HID_USAGE) ||
			effect->u.haptic.hid_usage == (HID_HP_WAVEFORMRELEASE & HID_USAGE))
		switch_modes = true;

	/* If device is in autonomous mode, and the uploaded effect signals userspace
	 * wants control of the device, change modes
	 */
	if (switch_modes && haptic->mode == HID_HAPTIC_MODE_DEVICE)
		switch_mode(hdev, haptic, HID_HAPTIC_MODE_HOST);

	return 0;
}

static int play_effect(struct hid_device *hdev, struct hid_haptic_device *haptic,
		       struct hid_haptic_effect *effect)
{
	int ret;

	ret = hid_hw_output_report(hdev, effect->report_buf,
				   haptic->manual_trigger_report_len);
	if (ret < 0) {
		ret = hid_hw_raw_request(hdev,
					 haptic->manual_trigger_report->id,
					 effect->report_buf,
					 haptic->manual_trigger_report_len,
					 HID_OUTPUT_REPORT, HID_REQ_SET_REPORT);
	}

	return ret;
}

static void haptic_work_handler(struct work_struct *work)
{

	struct hid_haptic_effect *effect = container_of(work,
							struct hid_haptic_effect,
							work);
	struct input_dev *dev = effect->input_dev;
	struct hid_device *hdev = input_get_drvdata(dev);
	struct hid_haptic_device *haptic = dev->ff->private;

	mutex_lock(&haptic->manual_trigger_mutex);
	if (effect != &haptic->stop_effect)
		play_effect(hdev, haptic, &haptic->stop_effect);

	play_effect(hdev, haptic, effect);
	mutex_unlock(&haptic->manual_trigger_mutex);

}

static int hid_haptic_playback(struct input_dev *dev, int effect_id, int value)
{
	struct hid_haptic_device *haptic = dev->ff->private;

	if (value)
		queue_work(haptic->wq, &haptic->effect[effect_id].work);
	else
		queue_work(haptic->wq, &haptic->stop_effect.work);

	return 0;
}

static void effect_set_default(struct ff_effect *effect)
{
	effect->type = FF_HAPTIC;
	effect->id = -1;
	effect->u.haptic.hid_usage = HID_HP_WAVEFORMNONE & HID_USAGE;
	effect->u.haptic.intensity = 100;
	effect->u.haptic.retrigger_period = 0;
	effect->u.haptic.repeat_count = 0;
}

static int hid_haptic_erase(struct input_dev *dev, int effect_id)
{
	struct hid_haptic_device *haptic = dev->ff->private;
	struct hid_device *hdev = input_get_drvdata(dev);
	struct ff_effect effect;
	int ordinal;

	effect_set_default(&effect);

	if (effect.u.haptic.hid_usage == (HID_HP_WAVEFORMRELEASE & HID_USAGE)) {
		ordinal = haptic->release_ordinal;
		if (!ordinal) {
			ordinal = HID_HAPTIC_ORDINAL_WAVEFORMNONE;
			if (haptic->mode == HID_HAPTIC_MODE_HOST)
				switch_mode(hdev, haptic, HID_HAPTIC_MODE_DEVICE);
		} else
			effect.u.haptic.hid_usage = HID_HP_WAVEFORMRELEASE & HID_USAGE;

		fill_effect_buf(haptic, &effect.u.haptic, &haptic->effect[effect_id],
				ordinal);
	} else if (effect.u.haptic.hid_usage == (HID_HP_WAVEFORMPRESS & HID_USAGE)) {
		ordinal = haptic->press_ordinal;
		if (!ordinal) {
			ordinal = HID_HAPTIC_ORDINAL_WAVEFORMNONE;
			if (haptic->mode == HID_HAPTIC_MODE_HOST)
				switch_mode(hdev, haptic, HID_HAPTIC_MODE_DEVICE);
		}
		else
			effect.u.haptic.hid_usage = HID_HP_WAVEFORMPRESS & HID_USAGE;

		fill_effect_buf(haptic, &effect.u.haptic, &haptic->effect[effect_id],
				ordinal);
	}

	return 0;
}

static void hid_haptic_destroy(struct ff_device *ff)
{
	struct hid_haptic_device *haptic = ff->private;
	struct hid_device *hdev = haptic->hdev;
	int r;

	if (hdev)
		put_device(&hdev->dev);

	kfree(haptic->stop_effect.report_buf);
	haptic->stop_effect.report_buf = NULL;

	if (haptic->effect) {
		for (r = 0; r < ff->max_effects; r++)
			kfree(haptic->effect[r].report_buf);
		kfree(haptic->effect);
	}
	haptic->effect = NULL;

	destroy_workqueue(haptic->wq);
	haptic->wq = NULL;

	kfree(haptic->duration_map);
	haptic->duration_map = NULL;

	kfree(haptic->hid_usage_map);
	haptic->hid_usage_map = NULL;

	module_put(THIS_MODULE);
}

int hid_haptic_init(struct hid_device *hdev,
		    struct hid_haptic_device **haptic_ptr)
{
	struct hid_haptic_device *haptic = *haptic_ptr;
	struct input_dev *dev = NULL;
	struct hid_input *hidinput;
	struct ff_device *ff;
	int ret = 0, r;
	struct ff_haptic_effect stop_effect = {
		.hid_usage = HID_HP_WAVEFORMSTOP & HID_USAGE,
	};
	const char *prefix = "hid-haptic";
	char *name;
	int (*flush)(struct input_dev *dev, struct file *file);
	int (*event)(struct input_dev *dev, unsigned int type, unsigned int code, int value);

	haptic->hdev = hdev;
	haptic->max_waveform_id = max(2u, haptic->max_waveform_id);
	haptic->max_duration_id = max(2u, haptic->max_duration_id);

	haptic->hid_usage_map = kcalloc(haptic->max_waveform_id + 1,
					sizeof(u16), GFP_KERNEL);
	if (!haptic->hid_usage_map) {
		ret = -ENOMEM;
		goto exit;
	}
	haptic->duration_map = kcalloc(haptic->max_duration_id + 1,
				       sizeof(u32), GFP_KERNEL);
	if (!haptic->duration_map) {
		ret = -ENOMEM;
		goto usage_map;
	}

	if (haptic->max_waveform_id != haptic->max_duration_id)
		dev_warn(&hdev->dev,
			 "Haptic duration and waveform lists have different max id (%u and %u).\n",
			 haptic->max_duration_id, haptic->max_waveform_id);

	haptic->hid_usage_map[HID_HAPTIC_ORDINAL_WAVEFORMNONE] =
		HID_HP_WAVEFORMNONE & HID_USAGE;
	haptic->hid_usage_map[HID_HAPTIC_ORDINAL_WAVEFORMSTOP] =
		HID_HP_WAVEFORMSTOP & HID_USAGE;

	mutex_init(&haptic->auto_trigger_mutex);
	for (r = 0; r < haptic->auto_trigger_report->maxfield; r++)
		parse_auto_trigger_field(haptic, haptic->auto_trigger_report->field[r]);

	list_for_each_entry(hidinput, &hdev->inputs, list) {
		if (hidinput->application == HID_DG_TOUCHPAD) {
			dev = hidinput->input;
			break;
		}
	}

	if (!dev) {
		dev_err(&hdev->dev, "Failed to find the input device\n");
		ret = -ENODEV;
		goto duration_map;
	}

	haptic->input_dev = dev;
	haptic->manual_trigger_report_len =
		hid_report_len(haptic->manual_trigger_report);
	mutex_init(&haptic->manual_trigger_mutex);
	name = kmalloc(strlen(prefix) + strlen(hdev->name) + 2, GFP_KERNEL);
	if (name) {
		sprintf(name, "%s %s", prefix, hdev->name);
		haptic->wq = create_singlethread_workqueue(name);
		kfree(name);
	}
	if (!haptic->wq) {
		ret = -ENOMEM;
		goto duration_map;
	}
	haptic->effect = kcalloc(FF_MAX_EFFECTS,
				 sizeof(struct hid_haptic_effect), GFP_KERNEL);
	if (!haptic->effect) {
		ret = -ENOMEM;
		goto output_queue;
	}
	for (r = 0; r < FF_MAX_EFFECTS; r++) {
		haptic->effect[r].report_buf =
			hid_alloc_report_buf(haptic->manual_trigger_report,
					     GFP_KERNEL);
		if (!haptic->effect[r].report_buf) {
			dev_err(&hdev->dev,
				"Failed to allocate a buffer for an effect.\n");
			ret = -ENOMEM;
			goto buffer_free;
		}
		haptic->effect[r].input_dev = dev;
		INIT_WORK(&haptic->effect[r].work, haptic_work_handler);
	}
	haptic->stop_effect.report_buf =
		hid_alloc_report_buf(haptic->manual_trigger_report,
				     GFP_KERNEL);
	if (!haptic->stop_effect.report_buf) {
		dev_err(&hdev->dev,
			"Failed to allocate a buffer for stop effect.\n");
		ret = -ENOMEM;
		goto buffer_free;
	}
	haptic->stop_effect.input_dev = dev;
	INIT_WORK(&haptic->stop_effect.work, haptic_work_handler);
	fill_effect_buf(haptic, &stop_effect, &haptic->stop_effect,
			HID_HAPTIC_ORDINAL_WAVEFORMSTOP);

	input_set_capability(dev, EV_FF, FF_HAPTIC);

	flush = dev->flush;
	event = dev->event;
	ret = input_ff_create(dev, FF_MAX_EFFECTS);
	if (ret) {
		dev_err(&hdev->dev, "Failed to create ff device.\n");
		goto stop_buffer_free;
	}

	ff = dev->ff;
	ff->private = haptic;
	ff->upload = hid_haptic_upload_effect;
	ff->playback = hid_haptic_playback;
	ff->erase = hid_haptic_erase;
	ff->destroy = hid_haptic_destroy;
	if (!try_module_get(THIS_MODULE)) {
		dev_err(&hdev->dev, "Failed to increase module count.\n");
		goto input_free;
	}
	if (!get_device(&hdev->dev)) {
		dev_err(&hdev->dev, "Failed to get hdev device.\n");
		module_put(THIS_MODULE);
		goto input_free;
	}
	return 0;

input_free:
	input_ff_destroy(dev);
	/* Do not let double free happen, input_ff_destroy will call
	 * hid_haptic_destroy.
	 */
	*haptic_ptr = NULL;
	/* Restore dev flush and event */
	dev->flush = flush;
	dev->event = event;
	return ret;
stop_buffer_free:
	kfree(haptic->stop_effect.report_buf);
	haptic->stop_effect.report_buf = NULL;
buffer_free:
	while (--r >= 0)
		kfree(haptic->effect[r].report_buf);
	kfree(haptic->effect);
	haptic->effect = NULL;
output_queue:
	destroy_workqueue(haptic->wq);
	haptic->wq = NULL;
duration_map:
	kfree(haptic->duration_map);
	haptic->duration_map = NULL;
usage_map:
	kfree(haptic->hid_usage_map);
	haptic->hid_usage_map = NULL;
exit:
	return ret;
}
EXPORT_SYMBOL_GPL(hid_haptic_init);

void hid_haptic_pressure_reset(struct hid_haptic_device *haptic)
{
	haptic->pressure_sum = 0;
}
EXPORT_SYMBOL_GPL(hid_haptic_pressure_reset);

void hid_haptic_pressure_increase(struct hid_haptic_device *haptic,
				 __s32 pressure)
{
	haptic->pressure_sum += pressure;
}
EXPORT_SYMBOL_GPL(hid_haptic_pressure_increase);
