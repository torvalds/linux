/*
 * Stephen Evanchik <evanchsa@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

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
 * Device IO: read, write and toggle bit
 */
static int trackpoint_read(struct ps2dev *ps2dev, unsigned char loc, unsigned char *results)
{
	if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
	    ps2_command(ps2dev, results, MAKE_PS2_CMD(0, 1, loc))) {
		return -1;
	}

	return 0;
}

static int trackpoint_write(struct ps2dev *ps2dev, unsigned char loc, unsigned char val)
{
	if (ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_COMMAND)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, TP_WRITE_MEM)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, loc)) ||
	    ps2_command(ps2dev, NULL, MAKE_PS2_CMD(0, 0, val))) {
		return -1;
	}

	return 0;
}

static int trackpoint_toggle_bit(struct ps2dev *ps2dev, unsigned char loc, unsigned char mask)
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


/*
 * Trackpoint-specific attributes
 */
struct trackpoint_attr_data {
	size_t field_offset;
	unsigned char command;
	unsigned char mask;
	unsigned char inverted;
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
	unsigned long value;

	if (strict_strtoul(buf, 10, &value) || value > 255)
		return -EINVAL;

	*field = value;
	trackpoint_write(&psmouse->ps2dev, attr->command, value);

	return count;
}

#define TRACKPOINT_INT_ATTR(_name, _command)					\
	static struct trackpoint_attr_data trackpoint_attr_##_name = {		\
		.field_offset = offsetof(struct trackpoint_data, _name),	\
		.command = _command,						\
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
	unsigned long value;

	if (strict_strtoul(buf, 10, &value) || value > 1)
		return -EINVAL;

	if (attr->inverted)
		value = !value;

	if (*field != value) {
		*field = value;
		trackpoint_toggle_bit(&psmouse->ps2dev, attr->command, attr->mask);
	}

	return count;
}


#define TRACKPOINT_BIT_ATTR(_name, _command, _mask, _inv)				\
	static struct trackpoint_attr_data trackpoint_attr_##_name = {		\
		.field_offset	= offsetof(struct trackpoint_data, _name),	\
		.command	= _command,					\
		.mask		= _mask,					\
		.inverted	= _inv,						\
	};									\
	PSMOUSE_DEFINE_ATTR(_name, S_IWUSR | S_IRUGO,				\
			    &trackpoint_attr_##_name,				\
			    trackpoint_show_int_attr, trackpoint_set_bit_attr)

TRACKPOINT_INT_ATTR(sensitivity, TP_SENS);
TRACKPOINT_INT_ATTR(speed, TP_SPEED);
TRACKPOINT_INT_ATTR(inertia, TP_INERTIA);
TRACKPOINT_INT_ATTR(reach, TP_REACH);
TRACKPOINT_INT_ATTR(draghys, TP_DRAGHYS);
TRACKPOINT_INT_ATTR(mindrag, TP_MINDRAG);
TRACKPOINT_INT_ATTR(thresh, TP_THRESH);
TRACKPOINT_INT_ATTR(upthresh, TP_UP_THRESH);
TRACKPOINT_INT_ATTR(ztime, TP_Z_TIME);
TRACKPOINT_INT_ATTR(jenks, TP_JENKS_CURV);

TRACKPOINT_BIT_ATTR(press_to_select, TP_TOGGLE_PTSON, TP_MASK_PTSON, 0);
TRACKPOINT_BIT_ATTR(skipback, TP_TOGGLE_SKIPBACK, TP_MASK_SKIPBACK, 0);
TRACKPOINT_BIT_ATTR(ext_dev, TP_TOGGLE_EXT_DEV, TP_MASK_EXT_DEV, 1);

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

static int trackpoint_sync(struct psmouse *psmouse)
{
	struct trackpoint_data *tp = psmouse->private;
	unsigned char toggle;

	/* Disable features that may make device unusable with this driver */
	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_TWOHAND, &toggle);
	if (toggle & TP_MASK_TWOHAND)
		trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_TWOHAND, TP_MASK_TWOHAND);

	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_SOURCE_TAG, &toggle);
	if (toggle & TP_MASK_SOURCE_TAG)
		trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_SOURCE_TAG, TP_MASK_SOURCE_TAG);

	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_MB, &toggle);
	if (toggle & TP_MASK_MB)
		trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_MB, TP_MASK_MB);

	/* Push the config to the device */
	trackpoint_write(&psmouse->ps2dev, TP_SENS, tp->sensitivity);
	trackpoint_write(&psmouse->ps2dev, TP_INERTIA, tp->inertia);
	trackpoint_write(&psmouse->ps2dev, TP_SPEED, tp->speed);

	trackpoint_write(&psmouse->ps2dev, TP_REACH, tp->reach);
	trackpoint_write(&psmouse->ps2dev, TP_DRAGHYS, tp->draghys);
	trackpoint_write(&psmouse->ps2dev, TP_MINDRAG, tp->mindrag);

	trackpoint_write(&psmouse->ps2dev, TP_THRESH, tp->thresh);
	trackpoint_write(&psmouse->ps2dev, TP_UP_THRESH, tp->upthresh);

	trackpoint_write(&psmouse->ps2dev, TP_Z_TIME, tp->ztime);
	trackpoint_write(&psmouse->ps2dev, TP_JENKS_CURV, tp->jenks);

	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_PTSON, &toggle);
	if (((toggle & TP_MASK_PTSON) == TP_MASK_PTSON) != tp->press_to_select)
		 trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_PTSON, TP_MASK_PTSON);

	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_SKIPBACK, &toggle);
	if (((toggle & TP_MASK_SKIPBACK) == TP_MASK_SKIPBACK) != tp->skipback)
		trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_SKIPBACK, TP_MASK_SKIPBACK);

	trackpoint_read(&psmouse->ps2dev, TP_TOGGLE_EXT_DEV, &toggle);
	if (((toggle & TP_MASK_EXT_DEV) == TP_MASK_EXT_DEV) != tp->ext_dev)
		trackpoint_toggle_bit(&psmouse->ps2dev, TP_TOGGLE_EXT_DEV, TP_MASK_EXT_DEV);

	return 0;
}

static void trackpoint_defaults(struct trackpoint_data *tp)
{
	tp->press_to_select = TP_DEF_PTSON;
	tp->sensitivity = TP_DEF_SENS;
	tp->speed = TP_DEF_SPEED;
	tp->reach = TP_DEF_REACH;

	tp->draghys = TP_DEF_DRAGHYS;
	tp->mindrag = TP_DEF_MINDRAG;

	tp->thresh = TP_DEF_THRESH;
	tp->upthresh = TP_DEF_UP_THRESH;

	tp->ztime = TP_DEF_Z_TIME;
	tp->jenks = TP_DEF_JENKS_CURV;

	tp->inertia = TP_DEF_INERTIA;
	tp->skipback = TP_DEF_SKIPBACK;
	tp->ext_dev = TP_DEF_EXT_DEV;
}

static void trackpoint_disconnect(struct psmouse *psmouse)
{
	sysfs_remove_group(&psmouse->ps2dev.serio->dev.kobj, &trackpoint_attr_group);

	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int trackpoint_reconnect(struct psmouse *psmouse)
{
	if (trackpoint_start_protocol(psmouse, NULL))
		return -1;

	if (trackpoint_sync(psmouse))
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
		printk(KERN_WARNING "trackpoint.c: failed to get extended button data\n");
		button_info = 0;
	}

	psmouse->private = kzalloc(sizeof(struct trackpoint_data), GFP_KERNEL);
	if (!psmouse->private)
		return -1;

	psmouse->vendor = "IBM";
	psmouse->name = "TrackPoint";

	psmouse->reconnect = trackpoint_reconnect;
	psmouse->disconnect = trackpoint_disconnect;

	if ((button_info & 0x0f) >= 3)
		__set_bit(BTN_MIDDLE, psmouse->dev->keybit);

	trackpoint_defaults(psmouse->private);
	trackpoint_sync(psmouse);

	error = sysfs_create_group(&ps2dev->serio->dev.kobj, &trackpoint_attr_group);
	if (error) {
		printk(KERN_ERR
			"trackpoint.c: failed to create sysfs attributes, error: %d\n",
			error);
		kfree(psmouse->private);
		psmouse->private = NULL;
		return -1;
	}

	printk(KERN_INFO "IBM TrackPoint firmware: 0x%02x, buttons: %d/%d\n",
		firmware_id, (button_info & 0xf0) >> 4, button_info & 0x0f);

	return 0;
}

