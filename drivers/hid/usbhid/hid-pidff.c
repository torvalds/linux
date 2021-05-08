// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Force feedback driver for USB HID PID compliant devices
 *
 *  Copyright (c) 2005, 2006 Anssi Hannula <anssi.hannula@gmail.com>
 */

/*
 */

/* #define DEBUG */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/usb.h>

#include <linux/hid.h>

#include "usbhid.h"

#define	PID_EFFECTS_MAX		64

/* Report usage table used to put reports into an array */

#define PID_SET_EFFECT		0
#define PID_EFFECT_OPERATION	1
#define PID_DEVICE_GAIN		2
#define PID_POOL		3
#define PID_BLOCK_LOAD		4
#define PID_BLOCK_FREE		5
#define PID_DEVICE_CONTROL	6
#define PID_CREATE_NEW_EFFECT	7

#define PID_REQUIRED_REPORTS	7

#define PID_SET_ENVELOPE	8
#define PID_SET_CONDITION	9
#define PID_SET_PERIODIC	10
#define PID_SET_CONSTANT	11
#define PID_SET_RAMP		12
static const u8 pidff_reports[] = {
	0x21, 0x77, 0x7d, 0x7f, 0x89, 0x90, 0x96, 0xab,
	0x5a, 0x5f, 0x6e, 0x73, 0x74
};

/* device_control is really 0x95, but 0x96 specified as it is the usage of
the only field in that report */

/* Value usage tables used to put fields and values into arrays */

#define PID_EFFECT_BLOCK_INDEX	0

#define PID_DURATION		1
#define PID_GAIN		2
#define PID_TRIGGER_BUTTON	3
#define PID_TRIGGER_REPEAT_INT	4
#define PID_DIRECTION_ENABLE	5
#define PID_START_DELAY		6
static const u8 pidff_set_effect[] = {
	0x22, 0x50, 0x52, 0x53, 0x54, 0x56, 0xa7
};

#define PID_ATTACK_LEVEL	1
#define PID_ATTACK_TIME		2
#define PID_FADE_LEVEL		3
#define PID_FADE_TIME		4
static const u8 pidff_set_envelope[] = { 0x22, 0x5b, 0x5c, 0x5d, 0x5e };

#define PID_PARAM_BLOCK_OFFSET	1
#define PID_CP_OFFSET		2
#define PID_POS_COEFFICIENT	3
#define PID_NEG_COEFFICIENT	4
#define PID_POS_SATURATION	5
#define PID_NEG_SATURATION	6
#define PID_DEAD_BAND		7
static const u8 pidff_set_condition[] = {
	0x22, 0x23, 0x60, 0x61, 0x62, 0x63, 0x64, 0x65
};

#define PID_MAGNITUDE		1
#define PID_OFFSET		2
#define PID_PHASE		3
#define PID_PERIOD		4
static const u8 pidff_set_periodic[] = { 0x22, 0x70, 0x6f, 0x71, 0x72 };
static const u8 pidff_set_constant[] = { 0x22, 0x70 };

#define PID_RAMP_START		1
#define PID_RAMP_END		2
static const u8 pidff_set_ramp[] = { 0x22, 0x75, 0x76 };

#define PID_RAM_POOL_AVAILABLE	1
static const u8 pidff_block_load[] = { 0x22, 0xac };

#define PID_LOOP_COUNT		1
static const u8 pidff_effect_operation[] = { 0x22, 0x7c };

static const u8 pidff_block_free[] = { 0x22 };

#define PID_DEVICE_GAIN_FIELD	0
static const u8 pidff_device_gain[] = { 0x7e };

#define PID_RAM_POOL_SIZE	0
#define PID_SIMULTANEOUS_MAX	1
#define PID_DEVICE_MANAGED_POOL	2
static const u8 pidff_pool[] = { 0x80, 0x83, 0xa9 };

/* Special field key tables used to put special field keys into arrays */

#define PID_ENABLE_ACTUATORS	0
#define PID_RESET		1
static const u8 pidff_device_control[] = { 0x97, 0x9a };

#define PID_CONSTANT	0
#define PID_RAMP	1
#define PID_SQUARE	2
#define PID_SINE	3
#define PID_TRIANGLE	4
#define PID_SAW_UP	5
#define PID_SAW_DOWN	6
#define PID_SPRING	7
#define PID_DAMPER	8
#define PID_INERTIA	9
#define PID_FRICTION	10
static const u8 pidff_effect_types[] = {
	0x26, 0x27, 0x30, 0x31, 0x32, 0x33, 0x34,
	0x40, 0x41, 0x42, 0x43
};

#define PID_BLOCK_LOAD_SUCCESS	0
#define PID_BLOCK_LOAD_FULL	1
static const u8 pidff_block_load_status[] = { 0x8c, 0x8d };

#define PID_EFFECT_START	0
#define PID_EFFECT_STOP		1
static const u8 pidff_effect_operation_status[] = { 0x79, 0x7b };

struct pidff_usage {
	struct hid_field *field;
	s32 *value;
};

struct pidff_device {
	struct hid_device *hid;

	struct hid_report *reports[sizeof(pidff_reports)];

	struct pidff_usage set_effect[sizeof(pidff_set_effect)];
	struct pidff_usage set_envelope[sizeof(pidff_set_envelope)];
	struct pidff_usage set_condition[sizeof(pidff_set_condition)];
	struct pidff_usage set_periodic[sizeof(pidff_set_periodic)];
	struct pidff_usage set_constant[sizeof(pidff_set_constant)];
	struct pidff_usage set_ramp[sizeof(pidff_set_ramp)];

	struct pidff_usage device_gain[sizeof(pidff_device_gain)];
	struct pidff_usage block_load[sizeof(pidff_block_load)];
	struct pidff_usage pool[sizeof(pidff_pool)];
	struct pidff_usage effect_operation[sizeof(pidff_effect_operation)];
	struct pidff_usage block_free[sizeof(pidff_block_free)];

	/* Special field is a field that is not composed of
	   usage<->value pairs that pidff_usage values are */

	/* Special field in create_new_effect */
	struct hid_field *create_new_effect_type;

	/* Special fields in set_effect */
	struct hid_field *set_effect_type;
	struct hid_field *effect_direction;

	/* Special field in device_control */
	struct hid_field *device_control;

	/* Special field in block_load */
	struct hid_field *block_load_status;

	/* Special field in effect_operation */
	struct hid_field *effect_operation_status;

	int control_id[sizeof(pidff_device_control)];
	int type_id[sizeof(pidff_effect_types)];
	int status_id[sizeof(pidff_block_load_status)];
	int operation_id[sizeof(pidff_effect_operation_status)];

	int pid_id[PID_EFFECTS_MAX];
};

/*
 * Scale an unsigned value with range 0..max for the given field
 */
static int pidff_rescale(int i, int max, struct hid_field *field)
{
	return i * (field->logical_maximum - field->logical_minimum) / max +
	    field->logical_minimum;
}

/*
 * Scale a signed value in range -0x8000..0x7fff for the given field
 */
static int pidff_rescale_signed(int i, struct hid_field *field)
{
	return i == 0 ? 0 : i >
	    0 ? i * field->logical_maximum / 0x7fff : i *
	    field->logical_minimum / -0x8000;
}

static void pidff_set(struct pidff_usage *usage, u16 value)
{
	usage->value[0] = pidff_rescale(value, 0xffff, usage->field);
	pr_debug("calculated from %d to %d\n", value, usage->value[0]);
}

static void pidff_set_signed(struct pidff_usage *usage, s16 value)
{
	if (usage->field->logical_minimum < 0)
		usage->value[0] = pidff_rescale_signed(value, usage->field);
	else {
		if (value < 0)
			usage->value[0] =
			    pidff_rescale(-value, 0x8000, usage->field);
		else
			usage->value[0] =
			    pidff_rescale(value, 0x7fff, usage->field);
	}
	pr_debug("calculated from %d to %d\n", value, usage->value[0]);
}

/*
 * Send envelope report to the device
 */
static void pidff_set_envelope_report(struct pidff_device *pidff,
				      struct ff_envelope *envelope)
{
	pidff->set_envelope[PID_EFFECT_BLOCK_INDEX].value[0] =
	    pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];

	pidff->set_envelope[PID_ATTACK_LEVEL].value[0] =
	    pidff_rescale(envelope->attack_level >
			  0x7fff ? 0x7fff : envelope->attack_level, 0x7fff,
			  pidff->set_envelope[PID_ATTACK_LEVEL].field);
	pidff->set_envelope[PID_FADE_LEVEL].value[0] =
	    pidff_rescale(envelope->fade_level >
			  0x7fff ? 0x7fff : envelope->fade_level, 0x7fff,
			  pidff->set_envelope[PID_FADE_LEVEL].field);

	pidff->set_envelope[PID_ATTACK_TIME].value[0] = envelope->attack_length;
	pidff->set_envelope[PID_FADE_TIME].value[0] = envelope->fade_length;

	hid_dbg(pidff->hid, "attack %u => %d\n",
		envelope->attack_level,
		pidff->set_envelope[PID_ATTACK_LEVEL].value[0]);

	hid_hw_request(pidff->hid, pidff->reports[PID_SET_ENVELOPE],
			HID_REQ_SET_REPORT);
}

/*
 * Test if the new envelope differs from old one
 */
static int pidff_needs_set_envelope(struct ff_envelope *envelope,
				    struct ff_envelope *old)
{
	return envelope->attack_level != old->attack_level ||
	       envelope->fade_level != old->fade_level ||
	       envelope->attack_length != old->attack_length ||
	       envelope->fade_length != old->fade_length;
}

/*
 * Send constant force report to the device
 */
static void pidff_set_constant_force_report(struct pidff_device *pidff,
					    struct ff_effect *effect)
{
	pidff->set_constant[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];
	pidff_set_signed(&pidff->set_constant[PID_MAGNITUDE],
			 effect->u.constant.level);

	hid_hw_request(pidff->hid, pidff->reports[PID_SET_CONSTANT],
			HID_REQ_SET_REPORT);
}

/*
 * Test if the constant parameters have changed between effects
 */
static int pidff_needs_set_constant(struct ff_effect *effect,
				    struct ff_effect *old)
{
	return effect->u.constant.level != old->u.constant.level;
}

/*
 * Send set effect report to the device
 */
static void pidff_set_effect_report(struct pidff_device *pidff,
				    struct ff_effect *effect)
{
	pidff->set_effect[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];
	pidff->set_effect_type->value[0] =
		pidff->create_new_effect_type->value[0];
	pidff->set_effect[PID_DURATION].value[0] = effect->replay.length;
	pidff->set_effect[PID_TRIGGER_BUTTON].value[0] = effect->trigger.button;
	pidff->set_effect[PID_TRIGGER_REPEAT_INT].value[0] =
		effect->trigger.interval;
	pidff->set_effect[PID_GAIN].value[0] =
		pidff->set_effect[PID_GAIN].field->logical_maximum;
	pidff->set_effect[PID_DIRECTION_ENABLE].value[0] = 1;
	pidff->effect_direction->value[0] =
		pidff_rescale(effect->direction, 0xffff,
				pidff->effect_direction);
	pidff->set_effect[PID_START_DELAY].value[0] = effect->replay.delay;

	hid_hw_request(pidff->hid, pidff->reports[PID_SET_EFFECT],
			HID_REQ_SET_REPORT);
}

/*
 * Test if the values used in set_effect have changed
 */
static int pidff_needs_set_effect(struct ff_effect *effect,
				  struct ff_effect *old)
{
	return effect->replay.length != old->replay.length ||
	       effect->trigger.interval != old->trigger.interval ||
	       effect->trigger.button != old->trigger.button ||
	       effect->direction != old->direction ||
	       effect->replay.delay != old->replay.delay;
}

/*
 * Send periodic effect report to the device
 */
static void pidff_set_periodic_report(struct pidff_device *pidff,
				      struct ff_effect *effect)
{
	pidff->set_periodic[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];
	pidff_set_signed(&pidff->set_periodic[PID_MAGNITUDE],
			 effect->u.periodic.magnitude);
	pidff_set_signed(&pidff->set_periodic[PID_OFFSET],
			 effect->u.periodic.offset);
	pidff_set(&pidff->set_periodic[PID_PHASE], effect->u.periodic.phase);
	pidff->set_periodic[PID_PERIOD].value[0] = effect->u.periodic.period;

	hid_hw_request(pidff->hid, pidff->reports[PID_SET_PERIODIC],
			HID_REQ_SET_REPORT);

}

/*
 * Test if periodic effect parameters have changed
 */
static int pidff_needs_set_periodic(struct ff_effect *effect,
				    struct ff_effect *old)
{
	return effect->u.periodic.magnitude != old->u.periodic.magnitude ||
	       effect->u.periodic.offset != old->u.periodic.offset ||
	       effect->u.periodic.phase != old->u.periodic.phase ||
	       effect->u.periodic.period != old->u.periodic.period;
}

/*
 * Send condition effect reports to the device
 */
static void pidff_set_condition_report(struct pidff_device *pidff,
				       struct ff_effect *effect)
{
	int i;

	pidff->set_condition[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];

	for (i = 0; i < 2; i++) {
		pidff->set_condition[PID_PARAM_BLOCK_OFFSET].value[0] = i;
		pidff_set_signed(&pidff->set_condition[PID_CP_OFFSET],
				 effect->u.condition[i].center);
		pidff_set_signed(&pidff->set_condition[PID_POS_COEFFICIENT],
				 effect->u.condition[i].right_coeff);
		pidff_set_signed(&pidff->set_condition[PID_NEG_COEFFICIENT],
				 effect->u.condition[i].left_coeff);
		pidff_set(&pidff->set_condition[PID_POS_SATURATION],
			  effect->u.condition[i].right_saturation);
		pidff_set(&pidff->set_condition[PID_NEG_SATURATION],
			  effect->u.condition[i].left_saturation);
		pidff_set(&pidff->set_condition[PID_DEAD_BAND],
			  effect->u.condition[i].deadband);
		hid_hw_request(pidff->hid, pidff->reports[PID_SET_CONDITION],
				HID_REQ_SET_REPORT);
	}
}

/*
 * Test if condition effect parameters have changed
 */
static int pidff_needs_set_condition(struct ff_effect *effect,
				     struct ff_effect *old)
{
	int i;
	int ret = 0;

	for (i = 0; i < 2; i++) {
		struct ff_condition_effect *cond = &effect->u.condition[i];
		struct ff_condition_effect *old_cond = &old->u.condition[i];

		ret |= cond->center != old_cond->center ||
		       cond->right_coeff != old_cond->right_coeff ||
		       cond->left_coeff != old_cond->left_coeff ||
		       cond->right_saturation != old_cond->right_saturation ||
		       cond->left_saturation != old_cond->left_saturation ||
		       cond->deadband != old_cond->deadband;
	}

	return ret;
}

/*
 * Send ramp force report to the device
 */
static void pidff_set_ramp_force_report(struct pidff_device *pidff,
					struct ff_effect *effect)
{
	pidff->set_ramp[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];
	pidff_set_signed(&pidff->set_ramp[PID_RAMP_START],
			 effect->u.ramp.start_level);
	pidff_set_signed(&pidff->set_ramp[PID_RAMP_END],
			 effect->u.ramp.end_level);
	hid_hw_request(pidff->hid, pidff->reports[PID_SET_RAMP],
			HID_REQ_SET_REPORT);
}

/*
 * Test if ramp force parameters have changed
 */
static int pidff_needs_set_ramp(struct ff_effect *effect, struct ff_effect *old)
{
	return effect->u.ramp.start_level != old->u.ramp.start_level ||
	       effect->u.ramp.end_level != old->u.ramp.end_level;
}

/*
 * Send a request for effect upload to the device
 *
 * Returns 0 if device reported success, -ENOSPC if the device reported memory
 * is full. Upon unknown response the function will retry for 60 times, if
 * still unsuccessful -EIO is returned.
 */
static int pidff_request_effect_upload(struct pidff_device *pidff, int efnum)
{
	int j;

	pidff->create_new_effect_type->value[0] = efnum;
	hid_hw_request(pidff->hid, pidff->reports[PID_CREATE_NEW_EFFECT],
			HID_REQ_SET_REPORT);
	hid_dbg(pidff->hid, "create_new_effect sent, type: %d\n", efnum);

	pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0] = 0;
	pidff->block_load_status->value[0] = 0;
	hid_hw_wait(pidff->hid);

	for (j = 0; j < 60; j++) {
		hid_dbg(pidff->hid, "pid_block_load requested\n");
		hid_hw_request(pidff->hid, pidff->reports[PID_BLOCK_LOAD],
				HID_REQ_GET_REPORT);
		hid_hw_wait(pidff->hid);
		if (pidff->block_load_status->value[0] ==
		    pidff->status_id[PID_BLOCK_LOAD_SUCCESS]) {
			hid_dbg(pidff->hid, "device reported free memory: %d bytes\n",
				 pidff->block_load[PID_RAM_POOL_AVAILABLE].value ?
				 pidff->block_load[PID_RAM_POOL_AVAILABLE].value[0] : -1);
			return 0;
		}
		if (pidff->block_load_status->value[0] ==
		    pidff->status_id[PID_BLOCK_LOAD_FULL]) {
			hid_dbg(pidff->hid, "not enough memory free: %d bytes\n",
				pidff->block_load[PID_RAM_POOL_AVAILABLE].value ?
				pidff->block_load[PID_RAM_POOL_AVAILABLE].value[0] : -1);
			return -ENOSPC;
		}
	}
	hid_err(pidff->hid, "pid_block_load failed 60 times\n");
	return -EIO;
}

/*
 * Play the effect with PID id n times
 */
static void pidff_playback_pid(struct pidff_device *pidff, int pid_id, int n)
{
	pidff->effect_operation[PID_EFFECT_BLOCK_INDEX].value[0] = pid_id;

	if (n == 0) {
		pidff->effect_operation_status->value[0] =
			pidff->operation_id[PID_EFFECT_STOP];
	} else {
		pidff->effect_operation_status->value[0] =
			pidff->operation_id[PID_EFFECT_START];
		pidff->effect_operation[PID_LOOP_COUNT].value[0] = n;
	}

	hid_hw_request(pidff->hid, pidff->reports[PID_EFFECT_OPERATION],
			HID_REQ_SET_REPORT);
}

/*
 * Play the effect with effect id @effect_id for @value times
 */
static int pidff_playback(struct input_dev *dev, int effect_id, int value)
{
	struct pidff_device *pidff = dev->ff->private;

	pidff_playback_pid(pidff, pidff->pid_id[effect_id], value);

	return 0;
}

/*
 * Erase effect with PID id
 */
static void pidff_erase_pid(struct pidff_device *pidff, int pid_id)
{
	pidff->block_free[PID_EFFECT_BLOCK_INDEX].value[0] = pid_id;
	hid_hw_request(pidff->hid, pidff->reports[PID_BLOCK_FREE],
			HID_REQ_SET_REPORT);
}

/*
 * Stop and erase effect with effect_id
 */
static int pidff_erase_effect(struct input_dev *dev, int effect_id)
{
	struct pidff_device *pidff = dev->ff->private;
	int pid_id = pidff->pid_id[effect_id];

	hid_dbg(pidff->hid, "starting to erase %d/%d\n",
		effect_id, pidff->pid_id[effect_id]);
	/* Wait for the queue to clear. We do not want a full fifo to
	   prevent the effect removal. */
	hid_hw_wait(pidff->hid);
	pidff_playback_pid(pidff, pid_id, 0);
	pidff_erase_pid(pidff, pid_id);

	return 0;
}

/*
 * Effect upload handler
 */
static int pidff_upload_effect(struct input_dev *dev, struct ff_effect *effect,
			       struct ff_effect *old)
{
	struct pidff_device *pidff = dev->ff->private;
	int type_id;
	int error;

	pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0] = 0;
	if (old) {
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0] =
			pidff->pid_id[effect->id];
	}

	switch (effect->type) {
	case FF_CONSTANT:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_CONSTANT]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_constant(effect, old))
			pidff_set_constant_force_report(pidff, effect);
		if (!old ||
		    pidff_needs_set_envelope(&effect->u.constant.envelope,
					&old->u.constant.envelope))
			pidff_set_envelope_report(pidff,
					&effect->u.constant.envelope);
		break;

	case FF_PERIODIC:
		if (!old) {
			switch (effect->u.periodic.waveform) {
			case FF_SQUARE:
				type_id = PID_SQUARE;
				break;
			case FF_TRIANGLE:
				type_id = PID_TRIANGLE;
				break;
			case FF_SINE:
				type_id = PID_SINE;
				break;
			case FF_SAW_UP:
				type_id = PID_SAW_UP;
				break;
			case FF_SAW_DOWN:
				type_id = PID_SAW_DOWN;
				break;
			default:
				hid_err(pidff->hid, "invalid waveform\n");
				return -EINVAL;
			}

			error = pidff_request_effect_upload(pidff,
					pidff->type_id[type_id]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_periodic(effect, old))
			pidff_set_periodic_report(pidff, effect);
		if (!old ||
		    pidff_needs_set_envelope(&effect->u.periodic.envelope,
					&old->u.periodic.envelope))
			pidff_set_envelope_report(pidff,
					&effect->u.periodic.envelope);
		break;

	case FF_RAMP:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_RAMP]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_ramp(effect, old))
			pidff_set_ramp_force_report(pidff, effect);
		if (!old ||
		    pidff_needs_set_envelope(&effect->u.ramp.envelope,
					&old->u.ramp.envelope))
			pidff_set_envelope_report(pidff,
					&effect->u.ramp.envelope);
		break;

	case FF_SPRING:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_SPRING]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_condition(effect, old))
			pidff_set_condition_report(pidff, effect);
		break;

	case FF_FRICTION:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_FRICTION]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_condition(effect, old))
			pidff_set_condition_report(pidff, effect);
		break;

	case FF_DAMPER:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_DAMPER]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_condition(effect, old))
			pidff_set_condition_report(pidff, effect);
		break;

	case FF_INERTIA:
		if (!old) {
			error = pidff_request_effect_upload(pidff,
					pidff->type_id[PID_INERTIA]);
			if (error)
				return error;
		}
		if (!old || pidff_needs_set_effect(effect, old))
			pidff_set_effect_report(pidff, effect);
		if (!old || pidff_needs_set_condition(effect, old))
			pidff_set_condition_report(pidff, effect);
		break;

	default:
		hid_err(pidff->hid, "invalid type\n");
		return -EINVAL;
	}

	if (!old)
		pidff->pid_id[effect->id] =
		    pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0];

	hid_dbg(pidff->hid, "uploaded\n");

	return 0;
}

/*
 * set_gain() handler
 */
static void pidff_set_gain(struct input_dev *dev, u16 gain)
{
	struct pidff_device *pidff = dev->ff->private;

	pidff_set(&pidff->device_gain[PID_DEVICE_GAIN_FIELD], gain);
	hid_hw_request(pidff->hid, pidff->reports[PID_DEVICE_GAIN],
			HID_REQ_SET_REPORT);
}

static void pidff_autocenter(struct pidff_device *pidff, u16 magnitude)
{
	struct hid_field *field =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].field;

	if (!magnitude) {
		pidff_playback_pid(pidff, field->logical_minimum, 0);
		return;
	}

	pidff_playback_pid(pidff, field->logical_minimum, 1);

	pidff->set_effect[PID_EFFECT_BLOCK_INDEX].value[0] =
		pidff->block_load[PID_EFFECT_BLOCK_INDEX].field->logical_minimum;
	pidff->set_effect_type->value[0] = pidff->type_id[PID_SPRING];
	pidff->set_effect[PID_DURATION].value[0] = 0;
	pidff->set_effect[PID_TRIGGER_BUTTON].value[0] = 0;
	pidff->set_effect[PID_TRIGGER_REPEAT_INT].value[0] = 0;
	pidff_set(&pidff->set_effect[PID_GAIN], magnitude);
	pidff->set_effect[PID_DIRECTION_ENABLE].value[0] = 1;
	pidff->set_effect[PID_START_DELAY].value[0] = 0;

	hid_hw_request(pidff->hid, pidff->reports[PID_SET_EFFECT],
			HID_REQ_SET_REPORT);
}

/*
 * pidff_set_autocenter() handler
 */
static void pidff_set_autocenter(struct input_dev *dev, u16 magnitude)
{
	struct pidff_device *pidff = dev->ff->private;

	pidff_autocenter(pidff, magnitude);
}

/*
 * Find fields from a report and fill a pidff_usage
 */
static int pidff_find_fields(struct pidff_usage *usage, const u8 *table,
			     struct hid_report *report, int count, int strict)
{
	int i, j, k, found;

	for (k = 0; k < count; k++) {
		found = 0;
		for (i = 0; i < report->maxfield; i++) {
			if (report->field[i]->maxusage !=
			    report->field[i]->report_count) {
				pr_debug("maxusage and report_count do not match, skipping\n");
				continue;
			}
			for (j = 0; j < report->field[i]->maxusage; j++) {
				if (report->field[i]->usage[j].hid ==
				    (HID_UP_PID | table[k])) {
					pr_debug("found %d at %d->%d\n",
						 k, i, j);
					usage[k].field = report->field[i];
					usage[k].value =
						&report->field[i]->value[j];
					found = 1;
					break;
				}
			}
			if (found)
				break;
		}
		if (!found && strict) {
			pr_debug("failed to locate %d\n", k);
			return -1;
		}
	}
	return 0;
}

/*
 * Return index into pidff_reports for the given usage
 */
static int pidff_check_usage(int usage)
{
	int i;

	for (i = 0; i < sizeof(pidff_reports); i++)
		if (usage == (HID_UP_PID | pidff_reports[i]))
			return i;

	return -1;
}

/*
 * Find the reports and fill pidff->reports[]
 * report_type specifies either OUTPUT or FEATURE reports
 */
static void pidff_find_reports(struct hid_device *hid, int report_type,
			       struct pidff_device *pidff)
{
	struct hid_report *report;
	int i, ret;

	list_for_each_entry(report,
			    &hid->report_enum[report_type].report_list, list) {
		if (report->maxfield < 1)
			continue;
		ret = pidff_check_usage(report->field[0]->logical);
		if (ret != -1) {
			hid_dbg(hid, "found usage 0x%02x from field->logical\n",
				pidff_reports[ret]);
			pidff->reports[ret] = report;
			continue;
		}

		/*
		 * Sometimes logical collections are stacked to indicate
		 * different usages for the report and the field, in which
		 * case we want the usage of the parent. However, Linux HID
		 * implementation hides this fact, so we have to dig it up
		 * ourselves
		 */
		i = report->field[0]->usage[0].collection_index;
		if (i <= 0 ||
		    hid->collection[i - 1].type != HID_COLLECTION_LOGICAL)
			continue;
		ret = pidff_check_usage(hid->collection[i - 1].usage);
		if (ret != -1 && !pidff->reports[ret]) {
			hid_dbg(hid,
				"found usage 0x%02x from collection array\n",
				pidff_reports[ret]);
			pidff->reports[ret] = report;
		}
	}
}

/*
 * Test if the required reports have been found
 */
static int pidff_reports_ok(struct pidff_device *pidff)
{
	int i;

	for (i = 0; i <= PID_REQUIRED_REPORTS; i++) {
		if (!pidff->reports[i]) {
			hid_dbg(pidff->hid, "%d missing\n", i);
			return 0;
		}
	}

	return 1;
}

/*
 * Find a field with a specific usage within a report
 */
static struct hid_field *pidff_find_special_field(struct hid_report *report,
						  int usage, int enforce_min)
{
	int i;

	for (i = 0; i < report->maxfield; i++) {
		if (report->field[i]->logical == (HID_UP_PID | usage) &&
		    report->field[i]->report_count > 0) {
			if (!enforce_min ||
			    report->field[i]->logical_minimum == 1)
				return report->field[i];
			else {
				pr_err("logical_minimum is not 1 as it should be\n");
				return NULL;
			}
		}
	}
	return NULL;
}

/*
 * Fill a pidff->*_id struct table
 */
static int pidff_find_special_keys(int *keys, struct hid_field *fld,
				   const u8 *usagetable, int count)
{

	int i, j;
	int found = 0;

	for (i = 0; i < count; i++) {
		for (j = 0; j < fld->maxusage; j++) {
			if (fld->usage[j].hid == (HID_UP_PID | usagetable[i])) {
				keys[i] = j + 1;
				found++;
				break;
			}
		}
	}
	return found;
}

#define PIDFF_FIND_SPECIAL_KEYS(keys, field, name) \
	pidff_find_special_keys(pidff->keys, pidff->field, pidff_ ## name, \
		sizeof(pidff_ ## name))

/*
 * Find and check the special fields
 */
static int pidff_find_special_fields(struct pidff_device *pidff)
{
	hid_dbg(pidff->hid, "finding special fields\n");

	pidff->create_new_effect_type =
		pidff_find_special_field(pidff->reports[PID_CREATE_NEW_EFFECT],
					 0x25, 1);
	pidff->set_effect_type =
		pidff_find_special_field(pidff->reports[PID_SET_EFFECT],
					 0x25, 1);
	pidff->effect_direction =
		pidff_find_special_field(pidff->reports[PID_SET_EFFECT],
					 0x57, 0);
	pidff->device_control =
		pidff_find_special_field(pidff->reports[PID_DEVICE_CONTROL],
					 0x96, 1);
	pidff->block_load_status =
		pidff_find_special_field(pidff->reports[PID_BLOCK_LOAD],
					 0x8b, 1);
	pidff->effect_operation_status =
		pidff_find_special_field(pidff->reports[PID_EFFECT_OPERATION],
					 0x78, 1);

	hid_dbg(pidff->hid, "search done\n");

	if (!pidff->create_new_effect_type || !pidff->set_effect_type) {
		hid_err(pidff->hid, "effect lists not found\n");
		return -1;
	}

	if (!pidff->effect_direction) {
		hid_err(pidff->hid, "direction field not found\n");
		return -1;
	}

	if (!pidff->device_control) {
		hid_err(pidff->hid, "device control field not found\n");
		return -1;
	}

	if (!pidff->block_load_status) {
		hid_err(pidff->hid, "block load status field not found\n");
		return -1;
	}

	if (!pidff->effect_operation_status) {
		hid_err(pidff->hid, "effect operation field not found\n");
		return -1;
	}

	pidff_find_special_keys(pidff->control_id, pidff->device_control,
				pidff_device_control,
				sizeof(pidff_device_control));

	PIDFF_FIND_SPECIAL_KEYS(control_id, device_control, device_control);

	if (!PIDFF_FIND_SPECIAL_KEYS(type_id, create_new_effect_type,
				     effect_types)) {
		hid_err(pidff->hid, "no effect types found\n");
		return -1;
	}

	if (PIDFF_FIND_SPECIAL_KEYS(status_id, block_load_status,
				    block_load_status) !=
			sizeof(pidff_block_load_status)) {
		hid_err(pidff->hid,
			"block load status identifiers not found\n");
		return -1;
	}

	if (PIDFF_FIND_SPECIAL_KEYS(operation_id, effect_operation_status,
				    effect_operation_status) !=
			sizeof(pidff_effect_operation_status)) {
		hid_err(pidff->hid, "effect operation identifiers not found\n");
		return -1;
	}

	return 0;
}

/*
 * Find the implemented effect types
 */
static int pidff_find_effects(struct pidff_device *pidff,
			      struct input_dev *dev)
{
	int i;

	for (i = 0; i < sizeof(pidff_effect_types); i++) {
		int pidff_type = pidff->type_id[i];
		if (pidff->set_effect_type->usage[pidff_type].hid !=
		    pidff->create_new_effect_type->usage[pidff_type].hid) {
			hid_err(pidff->hid,
				"effect type number %d is invalid\n", i);
			return -1;
		}
	}

	if (pidff->type_id[PID_CONSTANT])
		set_bit(FF_CONSTANT, dev->ffbit);
	if (pidff->type_id[PID_RAMP])
		set_bit(FF_RAMP, dev->ffbit);
	if (pidff->type_id[PID_SQUARE]) {
		set_bit(FF_SQUARE, dev->ffbit);
		set_bit(FF_PERIODIC, dev->ffbit);
	}
	if (pidff->type_id[PID_SINE]) {
		set_bit(FF_SINE, dev->ffbit);
		set_bit(FF_PERIODIC, dev->ffbit);
	}
	if (pidff->type_id[PID_TRIANGLE]) {
		set_bit(FF_TRIANGLE, dev->ffbit);
		set_bit(FF_PERIODIC, dev->ffbit);
	}
	if (pidff->type_id[PID_SAW_UP]) {
		set_bit(FF_SAW_UP, dev->ffbit);
		set_bit(FF_PERIODIC, dev->ffbit);
	}
	if (pidff->type_id[PID_SAW_DOWN]) {
		set_bit(FF_SAW_DOWN, dev->ffbit);
		set_bit(FF_PERIODIC, dev->ffbit);
	}
	if (pidff->type_id[PID_SPRING])
		set_bit(FF_SPRING, dev->ffbit);
	if (pidff->type_id[PID_DAMPER])
		set_bit(FF_DAMPER, dev->ffbit);
	if (pidff->type_id[PID_INERTIA])
		set_bit(FF_INERTIA, dev->ffbit);
	if (pidff->type_id[PID_FRICTION])
		set_bit(FF_FRICTION, dev->ffbit);

	return 0;

}

#define PIDFF_FIND_FIELDS(name, report, strict) \
	pidff_find_fields(pidff->name, pidff_ ## name, \
		pidff->reports[report], \
		sizeof(pidff_ ## name), strict)

/*
 * Fill and check the pidff_usages
 */
static int pidff_init_fields(struct pidff_device *pidff, struct input_dev *dev)
{
	int envelope_ok = 0;

	if (PIDFF_FIND_FIELDS(set_effect, PID_SET_EFFECT, 1)) {
		hid_err(pidff->hid, "unknown set_effect report layout\n");
		return -ENODEV;
	}

	PIDFF_FIND_FIELDS(block_load, PID_BLOCK_LOAD, 0);
	if (!pidff->block_load[PID_EFFECT_BLOCK_INDEX].value) {
		hid_err(pidff->hid, "unknown pid_block_load report layout\n");
		return -ENODEV;
	}

	if (PIDFF_FIND_FIELDS(effect_operation, PID_EFFECT_OPERATION, 1)) {
		hid_err(pidff->hid, "unknown effect_operation report layout\n");
		return -ENODEV;
	}

	if (PIDFF_FIND_FIELDS(block_free, PID_BLOCK_FREE, 1)) {
		hid_err(pidff->hid, "unknown pid_block_free report layout\n");
		return -ENODEV;
	}

	if (!PIDFF_FIND_FIELDS(set_envelope, PID_SET_ENVELOPE, 1))
		envelope_ok = 1;

	if (pidff_find_special_fields(pidff) || pidff_find_effects(pidff, dev))
		return -ENODEV;

	if (!envelope_ok) {
		if (test_and_clear_bit(FF_CONSTANT, dev->ffbit))
			hid_warn(pidff->hid,
				 "has constant effect but no envelope\n");
		if (test_and_clear_bit(FF_RAMP, dev->ffbit))
			hid_warn(pidff->hid,
				 "has ramp effect but no envelope\n");

		if (test_and_clear_bit(FF_PERIODIC, dev->ffbit))
			hid_warn(pidff->hid,
				 "has periodic effect but no envelope\n");
	}

	if (test_bit(FF_CONSTANT, dev->ffbit) &&
	    PIDFF_FIND_FIELDS(set_constant, PID_SET_CONSTANT, 1)) {
		hid_warn(pidff->hid, "unknown constant effect layout\n");
		clear_bit(FF_CONSTANT, dev->ffbit);
	}

	if (test_bit(FF_RAMP, dev->ffbit) &&
	    PIDFF_FIND_FIELDS(set_ramp, PID_SET_RAMP, 1)) {
		hid_warn(pidff->hid, "unknown ramp effect layout\n");
		clear_bit(FF_RAMP, dev->ffbit);
	}

	if ((test_bit(FF_SPRING, dev->ffbit) ||
	     test_bit(FF_DAMPER, dev->ffbit) ||
	     test_bit(FF_FRICTION, dev->ffbit) ||
	     test_bit(FF_INERTIA, dev->ffbit)) &&
	    PIDFF_FIND_FIELDS(set_condition, PID_SET_CONDITION, 1)) {
		hid_warn(pidff->hid, "unknown condition effect layout\n");
		clear_bit(FF_SPRING, dev->ffbit);
		clear_bit(FF_DAMPER, dev->ffbit);
		clear_bit(FF_FRICTION, dev->ffbit);
		clear_bit(FF_INERTIA, dev->ffbit);
	}

	if (test_bit(FF_PERIODIC, dev->ffbit) &&
	    PIDFF_FIND_FIELDS(set_periodic, PID_SET_PERIODIC, 1)) {
		hid_warn(pidff->hid, "unknown periodic effect layout\n");
		clear_bit(FF_PERIODIC, dev->ffbit);
	}

	PIDFF_FIND_FIELDS(pool, PID_POOL, 0);

	if (!PIDFF_FIND_FIELDS(device_gain, PID_DEVICE_GAIN, 1))
		set_bit(FF_GAIN, dev->ffbit);

	return 0;
}

/*
 * Reset the device
 */
static void pidff_reset(struct pidff_device *pidff)
{
	struct hid_device *hid = pidff->hid;
	int i = 0;

	pidff->device_control->value[0] = pidff->control_id[PID_RESET];
	/* We reset twice as sometimes hid_wait_io isn't waiting long enough */
	hid_hw_request(hid, pidff->reports[PID_DEVICE_CONTROL], HID_REQ_SET_REPORT);
	hid_hw_wait(hid);
	hid_hw_request(hid, pidff->reports[PID_DEVICE_CONTROL], HID_REQ_SET_REPORT);
	hid_hw_wait(hid);

	pidff->device_control->value[0] =
		pidff->control_id[PID_ENABLE_ACTUATORS];
	hid_hw_request(hid, pidff->reports[PID_DEVICE_CONTROL], HID_REQ_SET_REPORT);
	hid_hw_wait(hid);

	/* pool report is sometimes messed up, refetch it */
	hid_hw_request(hid, pidff->reports[PID_POOL], HID_REQ_GET_REPORT);
	hid_hw_wait(hid);

	if (pidff->pool[PID_SIMULTANEOUS_MAX].value) {
		while (pidff->pool[PID_SIMULTANEOUS_MAX].value[0] < 2) {
			if (i++ > 20) {
				hid_warn(pidff->hid,
					 "device reports %d simultaneous effects\n",
					 pidff->pool[PID_SIMULTANEOUS_MAX].value[0]);
				break;
			}
			hid_dbg(pidff->hid, "pid_pool requested again\n");
			hid_hw_request(hid, pidff->reports[PID_POOL],
					  HID_REQ_GET_REPORT);
			hid_hw_wait(hid);
		}
	}
}

/*
 * Test if autocenter modification is using the supported method
 */
static int pidff_check_autocenter(struct pidff_device *pidff,
				  struct input_dev *dev)
{
	int error;

	/*
	 * Let's find out if autocenter modification is supported
	 * Specification doesn't specify anything, so we request an
	 * effect upload and cancel it immediately. If the approved
	 * effect id was one above the minimum, then we assume the first
	 * effect id is a built-in spring type effect used for autocenter
	 */

	error = pidff_request_effect_upload(pidff, 1);
	if (error) {
		hid_err(pidff->hid, "upload request failed\n");
		return error;
	}

	if (pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0] ==
	    pidff->block_load[PID_EFFECT_BLOCK_INDEX].field->logical_minimum + 1) {
		pidff_autocenter(pidff, 0xffff);
		set_bit(FF_AUTOCENTER, dev->ffbit);
	} else {
		hid_notice(pidff->hid,
			   "device has unknown autocenter control method\n");
	}

	pidff_erase_pid(pidff,
			pidff->block_load[PID_EFFECT_BLOCK_INDEX].value[0]);

	return 0;

}

/*
 * Check if the device is PID and initialize it
 */
int hid_pidff_init(struct hid_device *hid)
{
	struct pidff_device *pidff;
	struct hid_input *hidinput = list_entry(hid->inputs.next,
						struct hid_input, list);
	struct input_dev *dev = hidinput->input;
	struct ff_device *ff;
	int max_effects;
	int error;

	hid_dbg(hid, "starting pid init\n");

	if (list_empty(&hid->report_enum[HID_OUTPUT_REPORT].report_list)) {
		hid_dbg(hid, "not a PID device, no output report\n");
		return -ENODEV;
	}

	pidff = kzalloc(sizeof(*pidff), GFP_KERNEL);
	if (!pidff)
		return -ENOMEM;

	pidff->hid = hid;

	hid_device_io_start(hid);

	pidff_find_reports(hid, HID_OUTPUT_REPORT, pidff);
	pidff_find_reports(hid, HID_FEATURE_REPORT, pidff);

	if (!pidff_reports_ok(pidff)) {
		hid_dbg(hid, "reports not ok, aborting\n");
		error = -ENODEV;
		goto fail;
	}

	error = pidff_init_fields(pidff, dev);
	if (error)
		goto fail;

	pidff_reset(pidff);

	if (test_bit(FF_GAIN, dev->ffbit)) {
		pidff_set(&pidff->device_gain[PID_DEVICE_GAIN_FIELD], 0xffff);
		hid_hw_request(hid, pidff->reports[PID_DEVICE_GAIN],
				     HID_REQ_SET_REPORT);
	}

	error = pidff_check_autocenter(pidff, dev);
	if (error)
		goto fail;

	max_effects =
	    pidff->block_load[PID_EFFECT_BLOCK_INDEX].field->logical_maximum -
	    pidff->block_load[PID_EFFECT_BLOCK_INDEX].field->logical_minimum +
	    1;
	hid_dbg(hid, "max effects is %d\n", max_effects);

	if (max_effects > PID_EFFECTS_MAX)
		max_effects = PID_EFFECTS_MAX;

	if (pidff->pool[PID_SIMULTANEOUS_MAX].value)
		hid_dbg(hid, "max simultaneous effects is %d\n",
			pidff->pool[PID_SIMULTANEOUS_MAX].value[0]);

	if (pidff->pool[PID_RAM_POOL_SIZE].value)
		hid_dbg(hid, "device memory size is %d bytes\n",
			pidff->pool[PID_RAM_POOL_SIZE].value[0]);

	if (pidff->pool[PID_DEVICE_MANAGED_POOL].value &&
	    pidff->pool[PID_DEVICE_MANAGED_POOL].value[0] == 0) {
		error = -EPERM;
		hid_notice(hid,
			   "device does not support device managed pool\n");
		goto fail;
	}

	error = input_ff_create(dev, max_effects);
	if (error)
		goto fail;

	ff = dev->ff;
	ff->private = pidff;
	ff->upload = pidff_upload_effect;
	ff->erase = pidff_erase_effect;
	ff->set_gain = pidff_set_gain;
	ff->set_autocenter = pidff_set_autocenter;
	ff->playback = pidff_playback;

	hid_info(dev, "Force feedback for USB HID PID devices by Anssi Hannula <anssi.hannula@gmail.com>\n");

	hid_device_io_stop(hid);

	return 0;

 fail:
	hid_device_io_stop(hid);

	kfree(pidff);
	return error;
}
