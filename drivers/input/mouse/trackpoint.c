/*
 * Stephen Evanchik <evanchsa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/serio.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/libps2.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "psmouse.h"
#include "trackpoint.h"

/*
 * Power-on Reset: Resets all trackpoint parameters, including RAM values,
 * to defaults.
 * Returns zero on success, non-zero on failure.
 */
static int trackpoint_power_on_reset(struct ps2dev *ps2dev)
{
	unsigned char results[2];
	int tries = 0;

	/* Issue POR command, and repeat up to once if 0xFC00 received */
	do {
		if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
		    ps2_command(ps2dev, results, MAKE_PS2_CMD(0, 2, TP_POR)))
			return -1;
	} while (results[0] == 0xFC && results[1] == 0x00 && ++tries < 2);

	/* Check for success response -- 0xAA00 */
	if (results[0] != 0xAA || results[1] != 0x00)
		return -1;

	return 0;
}

/*
 * Device IO: read, write and toggle bit
 */
static int trackpoint_read(struct ps2dev *ps2dev,
			   unsigned char loc, unsigned char *results)
{
	if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
	    ps2_command(ps2dev, results, MAKE_PS2_CMD(0, 1, loc))) {
		return -1;
	}

	return 0;
}

static int trackpoint_write(struct ps2dev *ps2dev,
			    unsigned char loc, unsigned char val)
{
	if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_WRITE_MEM)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, loc)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, val))) {
		return -1;
	}

	return 0;
}

static int trackpoint_toggle_bit(struct ps2dev *ps2dev,
				 unsigned char loc, unsigned char mask)
{
	/* Bad things will happen if the loc param isn't in this range */
	if (loc < 0x20 || loc >= 0x2F)
		return -1;

	if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_TOGGLE)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, loc)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, mask))) {
		return -1;
	}

	return 0;
}

static int trackpoint_update_bit(struct ps2dev *ps2dev, unsigned char loc,
				 unsigned char mask, unsigned char value)
{
	int retval = 0;
	unsigned char data;

	trackpoint_read(ps2dev, loc, &data);
	if (((data & mask) == mask) != !!value)
		retval = trackpoint_toggle_bit(ps2dev, loc, mask);

	return retval;
}

/*
 * Trackpoint-specific attributes
 */
struct trackpoint_attr_data {
	size_t field_offset;
	unsigned char command;
	unsigned char mask;
	unsigned char inverted;
	unsigned char power_on_default;
};

static ssize_t trackpoint_show_int_attr(struct psmouse *psmouse, void *data, char *buf)
{
	struct trackpoint_data *tp = psmouse->private;
	struct trackpoint_attr_data *attr = data;
	unsigned char value = *(unsigned char *)((char *)tp + attr->field_offset);

	if (attr->inverted)
		value = !value;

	return sprintf(buf, "%u\n", value);
}

static ssize_t trackpoint_set_int_attr(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	struct trackpoint_data *tp = psmouse->private;
	struct trackpoint_attr_data *attr = data;
	unsigned char *field = (unsigned char *)((char *)tp + attr->field_offset);
	unsigned char value;
	int err;

	err = kstrtou8(buf, 10, &value);
	if (err)
		return err;

	*field = value;
	trackpoint_write(&psmouse->ps2dev, attr->command, value);

	return count;
}

#define TRACKPOINT_INT_ATTR(_name, _command, _default)				\
	static struct trackpoint_attr_data trackpoint_attr_##_name = {		\
		.field_offset = offsetof(struct trackpoint_data, _name),	\
		.command = _command,						\
		.power_on_default = _default,					\
	};									\
	PSMOUSE_DEFINE_ATTR(_name, S_IWUSR | S_IRUGO,				\
			    &trackpoint_attr_##_name,				\
			    trackpoint_show_int_attr, trackpoint_set_int_attr)

static ssize_t trackpoint_set_bit_attr(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	struct trackpoint_data *tp = psmouse->private;
	struct trackpoint_attr_data *attr = data;
	unsigned char *field = (unsigned char *)((char *)tp + attr->field_offset);
	unsigned int value;
	int err;

	err = kstrtouint(buf, 10, &value);
	if (err)
		return err;

	if (value > 1)
		return -EINVAL;

	if (attr->inverted)
		value = !value;

	if (*field != value) {
		*field = value;
		trackpoint_toggle_bit(&psmouse->ps2dev, attr->command, attr->mask);
	}

	return count;
}


#define TRACKPOINT_BIT_ATTR(_name, _command, _mask, _inv, _default)	\
static struct trackpoint_attr_data trackpoint_attr_##_name = {		\
	.field_offset		= offsetof(struct trackpoint_data,	\
					   _name),			\
	.command		= _command,				\
	.mask			= _mask,				\
	.inverted		= _inv,					\
	.power_on_default	= _default,				\
	};								\
PSMOUSE_DEFINE_ATTR(_name, S_IWUSR | S_IRUGO,				\
		    &trackpoint_attr_##_name,				\
		    trackpoint_show_int_attr, trackpoint_set_bit_attr)

#define TRACKPOINT_UPDATE_BIT(_psmouse, _tp, _name)			\
do {									\
	struct trackpoint_attr_data *_attr = &trackpoint_attr_##_name;	\
									\
	trackpoint_update_bit(&_psmouse->ps2dev,			\
			_attr->command, _attr->mask, _tp->_name);	\
} while (0)

#define TRACKPOINT_UPDATE(_power_on, _psmouse, _tp, _name)		\
do {									\
	if (!_power_on ||						\
	    _tp->_name != trackpoint_attr_##_name.power_on_default) {	\
		if (!trackpoint_attr_##_name.mask)			\
			trackpoint_write(&_psmouse->ps2dev,		\
				 trackpoint_attr_##_name.command,	\
				 _tp->_name);				\
		else							\
			TRACKPOINT_UPDATE_BIT(_psmouse, _tp, _name);	\
	}								\
} while (0)

#define TRACKPOINT_SET_POWER_ON_DEFAULT(_tp, _name)				\
	(_tp->_name = trackpoint_attr_##_name.power_on_default)

TRACKPOINT_INT_ATTR(sensitivity, TP_SENS, TP_DEF_SENS);
TRACKPOINT_INT_ATTR(speed, TP_SPEED, TP_DEF_SPEED);
TRACKPOINT_INT_ATTR(inertia, TP_INERTIA, TP_DEF_INERTIA);
TRACKPOINT_INT_ATTR(reach, TP_REACH, TP_DEF_REACH);
TRACKPOINT_INT_ATTR(draghys, TP_DRAGHYS, TP_DEF_DRAGHYS);
TRACKPOINT_INT_ATTR(mindrag, TP_MINDRAG, TP_DEF_MINDRAG);
TRACKPOINT_INT_ATTR(thresh, TP_THRESH, TP_DEF_THRESH);
TRACKPOINT_INT_ATTR(upthresh, TP_UP_THRESH, TP_DEF_UP_THRESH);
TRACKPOINT_INT_ATTR(ztime, TP_Z_TIME, TP_DEF_Z_TIME);
TRACKPOINT_INT_ATTR(jenks, TP_JENKS_CURV, TP_DEF_JENKS_CURV);
TRACKPOINT_INT_ATTR(drift_time, TP_DRIFT_TIME, TP_DEF_DRIFT_TIME);

TRACKPOINT_BIT_ATTR(press_to_select, TP_TOGGLE_PTSON, TP_MASK_PTSON, 0,
		    TP_DEF_PTSON);
TRACKPOINT_BIT_ATTR(skipback, TP_TOGGLE_SKIPBACK, TP_MASK_SKIPBACK, 0,
		    TP_DEF_SKIPBACK);
TRACKPOINT_BIT_ATTR(ext_dev, TP_TOGGLE_EXT_DEV, TP_MASK_EXT_DEV, 1,
		    TP_DEF_EXT_DEV);

static struct attribute *trackpoint_attrs[] = {
	&psmouse_attr_sensitivity.dattr.attr,
	&psmouse_attr_speed.dattr.attr,
	&psmouse_attr_inertia.dattr.attr,
	&psmouse_attr_reach.dattr.attr,
	&psmouse_attr_draghys.dattr.attr,
	&psmouse_attr_mindrag.dattr.attr,
	&psmouse_attr_thresh.dattr.attr,
	&psmouse_attr_upthresh.dattr.attr,
	&psmouse_attr_ztime.dattr.attr,
	&psmouse_attr_jenks.dattr.attr,
	&psmouse_attr_drift_time.dattr.attr,
	&psmouse_attr_press_to_select.dattr.attr,
	&psmouse_attr_skipback.dattr.attr,
	&psmouse_attr_ext_dev.dattr.attr,
	NULL
};

static struct attribute_group trackpoint_attr_group = {
	.attrs = trackpoint_attrs,
};

static int trackpoint_start_protocol(struct psmouse *psmouse, unsigned char *firmware_id)
{
	unsigned char param[2] = { 0 };

	if (ps2_command(&psmouse->ps2dev, param, MAKE_PS2_CMD(0, 2, TP_READ_ID)))
		return -1;

	if (param[0] != TP_MAGIC_IDENT)
		return -1;

	if (firmware_id)
		*firmware_id = param[1];

	return 0;
}

/*
 * Write parameters to trackpad.
 * in_power_on_state: Set to true if TP is in default / power-on state (ex. if
 *		      power-on reset was run). If so, values will only be
 *		      written to TP if they differ from power-on default.
 */
static int trackpoint_sync(struct psmouse *psmouse, bool in_power_on_state)
{
	struct trackpoint_data *tp = psmouse->private;

	if (!in_power_on_state) {
		/*
		 * Disable features that may make device unusable
		 * with this driver.
		 */
		trackpoint_update_bit(&psmouse->ps2dev, TP_TOGGLE_TWOHAND,
				      TP_MASK_TWOHAND, TP_DEF_TWOHAND);

		trackpoint_update_bit(&psmouse->ps2dev, TP_TOGGLE_SOURCE_TAG,
				      TP_MASK_SOURCE_TAG, TP_DEF_SOURCE_TAG);

		trackpoint_update_bit(&psmouse->ps2dev, TP_TOGGLE_MB,
				      TP_MASK_MB, TP_DEF_MB);
	}

	/*
	 * These properties can be changed in this driver. Only
	 * configure them if the values are non-default or if the TP is in
	 * an unknown state.
	 */
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, sensitivity);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, inertia);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, speed);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, reach);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, draghys);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, mindrag);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, thresh);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, upthresh);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, ztime);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, jenks);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, drift_time);

	/* toggles */
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, press_to_select);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, skipback);
	TRACKPOINT_UPDATE(in_power_on_state, psmouse, tp, ext_dev);

	return 0;
}

static void trackpoint_defaults(struct trackpoint_data *tp)
{
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, sensitivity);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, speed);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, reach);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, draghys);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, mindrag);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, thresh);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, upthresh);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, ztime);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, jenks);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, drift_time);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, inertia);

	/* toggles */
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, press_to_select);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, skipback);
	TRACKPOINT_SET_POWER_ON_DEFAULT(tp, ext_dev);
}

static void trackpoint_disconnect(struct psmouse *psmouse)
{
	sysfs_remove_group(&psmouse->ps2dev.serio->dev.kobj, &trackpoint_attr_group);

	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int trackpoint_reconnect(struct psmouse *psmouse)
{
	int reset_fail;

	if (trackpoint_start_protocol(psmouse, NULL))
		return -1;

	reset_fail = trackpoint_power_on_reset(&psmouse->ps2dev);
	if (trackpoint_sync(psmouse, !reset_fail))
		return -1;

	return 0;
}

int trackpoint_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char firmware_id;
	unsigned char button_info;
	int error;

	if (trackpoint_start_protocol(psmouse, &firmware_id))
		return -1;

	if (!set_properties)
		return 0;

	if (trackpoint_read(&psmouse->ps2dev, TP_EXT_BTN, &button_info)) {
		psmouse_warn(psmouse, "failed to get extended button data\n");
		button_info = 0;
	}

	psmouse->private = kzalloc(sizeof(struct trackpoint_data), GFP_KERNEL);
	if (!psmouse->private)
		return -ENOMEM;

	psmouse->vendor = "IBM";
	psmouse->name = "TrackPoint";

	psmouse->reconnect = trackpoint_reconnect;
	psmouse->disconnect = trackpoint_disconnect;

	if ((button_info & 0x0f) >= 3)
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);

	__set_bit(INPUT_PROP_POINTER, psmouse->dev->propbit);
	__set_bit(INPUT_PROP_POINTING_STICK, psmouse->dev->propbit);

	trackpoint_defaults(psmouse->private);

	error = trackpoint_power_on_reset(&psmouse->ps2dev);

	/* Write defaults to TP only if reset fails. */
	if (error)
		trackpoint_sync(psmouse, false);

	error = sysfs_create_group(&ps2dev->serio->dev.kobj, &trackpoint_attr_group);
	if (error) {
		psmouse_err(psmouse,
			    "failed to create sysfs attributes, error: %d\n",
			    error);
		kfree(psmouse->private);
		psmouse->private = NULL;
		return -1;
	}

	psmouse_info(psmouse,
		     "IBM TrackPoint firmware: 0x%02x, buttons: %d/%d\n",
		     firmware_id,
		     (button_info & 0xf0) >> 4, button_info & 0x0f);

	return 0;
}

