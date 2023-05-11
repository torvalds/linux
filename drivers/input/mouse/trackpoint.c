// SPDX-License-Identifier: GPL-2.0-only
/*
 * Stephen Evanchik <evanchsa@gmail.com>
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
#include <linux/uaccess.h>
#include "psmouse.h"
#include "trackpoint.h"

static const char * const trackpoint_variants[] = {
	[TP_VARIANT_IBM]		= "IBM",
	[TP_VARIANT_ALPS]		= "ALPS",
	[TP_VARIANT_ELAN]		= "Elan",
	[TP_VARIANT_NXP]		= "NXP",
	[TP_VARIANT_JYT_SYNAPTICS]	= "JYT_Synaptics",
	[TP_VARIANT_SYNAPTICS]		= "Synaptics",
};

/*
 * Power-on Reset: Resets all trackpoint parameters, including RAM values,
 * to defaults.
 * Returns zero on success, non-zero on failure.
 */
static int trackpoint_power_on_reset(struct ps2dev *ps2dev)
{
	u8 param[2] = { TP_POR };
	int err;

	err = ps2_command(ps2dev, param, MAKE_PS2_CMD(1, 2, TP_COMMAND));
	if (err)
		return err;

	/* Check for success response -- 0xAA00 */
	if (param[0] != 0xAA || param[1] != 0x00)
		return -ENODEV;

	return 0;
}

/*
 * Device IO: read, write and toggle bit
 */
static int trackpoint_read(struct ps2dev *ps2dev, u8 loc, u8 *results)
{
	results[0] = loc;

	return ps2_command(ps2dev, results, MAKE_PS2_CMD(1, 1, TP_COMMAND));
}

static int trackpoint_write(struct ps2dev *ps2dev, u8 loc, u8 val)
{
	u8 param[3] = { TP_WRITE_MEM, loc, val };

	return ps2_command(ps2dev, param, MAKE_PS2_CMD(3, 0, TP_COMMAND));
}

static int trackpoint_toggle_bit(struct ps2dev *ps2dev, u8 loc, u8 mask)
{
	u8 param[3] = { TP_TOGGLE, loc, mask };

	/* Bad things will happen if the loc param isn't in this range */
	if (loc < 0x20 || loc >= 0x2F)
		return -EINVAL;

	return ps2_command(ps2dev, param, MAKE_PS2_CMD(3, 0, TP_COMMAND));
}

static int trackpoint_update_bit(struct ps2dev *ps2dev,
				 u8 loc, u8 mask, u8 value)
{
	int retval;
	u8 data;

	retval = trackpoint_read(ps2dev, loc, &data);
	if (retval)
		return retval;

	if (((data & mask) == mask) != !!value)
		retval = trackpoint_toggle_bit(ps2dev, loc, mask);

	return retval;
}

/*
 * Trackpoint-specific attributes
 */
struct trackpoint_attr_data {
	size_t field_offset;
	u8 command;
	u8 mask;
	bool inverted;
	u8 power_on_default;
};

static ssize_t trackpoint_show_int_attr(struct psmouse *psmouse,
					void *data, char *buf)
{
	struct trackpoint_data *tp = psmouse->private;
	struct trackpoint_attr_data *attr = data;
	u8 value = *(u8 *)((void *)tp + attr->field_offset);

	if (attr->inverted)
		value = !value;

	return sprintf(buf, "%u\n", value);
}

static ssize_t trackpoint_set_int_attr(struct psmouse *psmouse, void *data,
					const char *buf, size_t count)
{
	struct trackpoint_data *tp = psmouse->private;
	struct trackpoint_attr_data *attr = data;
	u8 *field = (void *)tp + attr->field_offset;
	u8 value;
	int err;

	err = kstrtou8(buf, 10, &value);
	if (err)
		return err;

	*field = value;
	err = trackpoint_write(&psmouse->ps2dev, attr->command, value);

	return err ?: count;
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
	bool *field = (void *)tp + attr->field_offset;
	bool value;
	int err;

	err = kstrtobool(buf, &value);
	if (err)
		return err;

	if (attr->inverted)
		value = !value;

	if (*field != value) {
		*field = value;
		err = trackpoint_toggle_bit(&psmouse->ps2dev,
					    attr->command, attr->mask);
	}

	return err ?: count;
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

TRACKPOINT_BIT_ATTR(press_to_select, TP_TOGGLE_PTSON, TP_MASK_PTSON, false,
		    TP_DEF_PTSON);
TRACKPOINT_BIT_ATTR(skipback, TP_TOGGLE_SKIPBACK, TP_MASK_SKIPBACK, false,
		    TP_DEF_SKIPBACK);
TRACKPOINT_BIT_ATTR(ext_dev, TP_TOGGLE_EXT_DEV, TP_MASK_EXT_DEV, true,
		    TP_DEF_EXT_DEV);

static bool trackpoint_is_attr_available(struct psmouse *psmouse,
					 struct attribute *attr)
{
	struct trackpoint_data *tp = psmouse->private;

	return tp->variant_id == TP_VARIANT_IBM ||
		attr == &psmouse_attr_sensitivity.dattr.attr ||
		attr == &psmouse_attr_press_to_select.dattr.attr;
}

static umode_t trackpoint_is_attr_visible(struct kobject *kobj,
					  struct attribute *attr, int n)
{
	struct device *dev = kobj_to_dev(kobj);
	struct serio *serio = to_serio_port(dev);
	struct psmouse *psmouse = psmouse_from_serio(serio);

	return trackpoint_is_attr_available(psmouse, attr) ? attr->mode : 0;
}

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
	.is_visible	= trackpoint_is_attr_visible,
	.attrs		= trackpoint_attrs,
};

#define TRACKPOINT_UPDATE(_power_on, _psmouse, _tp, _name)		\
do {									\
	struct trackpoint_attr_data *_attr = &trackpoint_attr_##_name;	\
									\
	if ((!_power_on || _tp->_name != _attr->power_on_default) &&	\
	    trackpoint_is_attr_available(_psmouse,			\
				&psmouse_attr_##_name.dattr.attr)) {	\
		if (!_attr->mask)					\
			trackpoint_write(&_psmouse->ps2dev,		\
					 _attr->command, _tp->_name);	\
		else							\
			trackpoint_update_bit(&_psmouse->ps2dev,	\
					_attr->command, _attr->mask,	\
					_tp->_name);			\
	}								\
} while (0)

#define TRACKPOINT_SET_POWER_ON_DEFAULT(_tp, _name)			\
do {									\
	_tp->_name = trackpoint_attr_##_name.power_on_default;		\
} while (0)

static int trackpoint_start_protocol(struct psmouse *psmouse,
				     u8 *variant_id, u8 *firmware_id)
{
	u8 param[2] = { 0 };
	int error;

	error = ps2_command(&psmouse->ps2dev,
			    param, MAKE_PS2_CMD(0, 2, TP_READ_ID));
	if (error)
		return error;

	switch (param[0]) {
	case TP_VARIANT_IBM:
	case TP_VARIANT_ALPS:
	case TP_VARIANT_ELAN:
	case TP_VARIANT_NXP:
	case TP_VARIANT_JYT_SYNAPTICS:
	case TP_VARIANT_SYNAPTICS:
		if (variant_id)
			*variant_id = param[0];
		if (firmware_id)
			*firmware_id = param[1];
		return 0;
	}

	return -ENODEV;
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

	if (!in_power_on_state && tp->variant_id == TP_VARIANT_IBM) {
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
	device_remove_group(&psmouse->ps2dev.serio->dev,
			    &trackpoint_attr_group);

	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int trackpoint_reconnect(struct psmouse *psmouse)
{
	struct trackpoint_data *tp = psmouse->private;
	int error;
	bool was_reset;

	error = trackpoint_start_protocol(psmouse, NULL, NULL);
	if (error)
		return error;

	was_reset = tp->variant_id == TP_VARIANT_IBM &&
		    trackpoint_power_on_reset(&psmouse->ps2dev) == 0;

	error = trackpoint_sync(psmouse, was_reset);
	if (error)
		return error;

	return 0;
}

int trackpoint_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct trackpoint_data *tp;
	u8 variant_id;
	u8 firmware_id;
	u8 button_info;
	int error;

	error = trackpoint_start_protocol(psmouse, &variant_id, &firmware_id);
	if (error)
		return error;

	if (!set_properties)
		return 0;

	tp = kzalloc(sizeof(*tp), GFP_KERNEL);
	if (!tp)
		return -ENOMEM;

	trackpoint_defaults(tp);
	tp->variant_id = variant_id;
	tp->firmware_id = firmware_id;

	psmouse->private = tp;

	psmouse->vendor = trackpoint_variants[variant_id];
	psmouse->name = "TrackPoint";

	psmouse->reconnect = trackpoint_reconnect;
	psmouse->disconnect = trackpoint_disconnect;

	if (variant_id != TP_VARIANT_IBM) {
		/* Newer variants do not support extended button query. */
		button_info = 0x33;
	} else {
		error = trackpoint_read(ps2dev, TP_EXT_BTN, &button_info);
		if (error) {
			psmouse_warn(psmouse,
				     "failed to get extended button data, assuming 3 buttons\n");
			button_info = 0x33;
		} else if (!button_info) {
			psmouse_warn(psmouse,
				     "got 0 in extended button data, assuming 3 buttons\n");
			button_info = 0x33;
		}
	}

	if ((button_info & 0x0f) >= 3)
		input_set_capability(psmouse->dev, EV_KEY, BTN_MIDDLE);

	__set_bit(INPUT_PROP_POINTER, psmouse->dev->propbit);
	__set_bit(INPUT_PROP_POINTING_STICK, psmouse->dev->propbit);

	if (variant_id != TP_VARIANT_IBM ||
	    trackpoint_power_on_reset(ps2dev) != 0) {
		/*
		 * Write defaults to TP if we did not reset the trackpoint.
		 */
		trackpoint_sync(psmouse, false);
	}

	error = device_add_group(&ps2dev->serio->dev, &trackpoint_attr_group);
	if (error) {
		psmouse_err(psmouse,
			    "failed to create sysfs attributes, error: %d\n",
			    error);
		kfree(psmouse->private);
		psmouse->private = NULL;
		return -1;
	}

	psmouse_info(psmouse,
		     "%s TrackPoint firmware: 0x%02x, buttons: %d/%d\n",
		     psmouse->vendor, firmware_id,
		     (button_info & 0xf0) >> 4, button_info & 0x0f);

	return 0;
}

