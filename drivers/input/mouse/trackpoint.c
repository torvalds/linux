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
#include <linux/moduleparam.h>
#include <linux/input.h>
#include <linux/libps2.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include "psmouse.h"
#include "trackpoint.h"

PSMOUSE_DEFINE_ATTR(sensitivity);
PSMOUSE_DEFINE_ATTR(speed);
PSMOUSE_DEFINE_ATTR(inertia);
PSMOUSE_DEFINE_ATTR(reach);
PSMOUSE_DEFINE_ATTR(draghys);
PSMOUSE_DEFINE_ATTR(mindrag);
PSMOUSE_DEFINE_ATTR(thresh);
PSMOUSE_DEFINE_ATTR(upthresh);
PSMOUSE_DEFINE_ATTR(ztime);
PSMOUSE_DEFINE_ATTR(jenks);
PSMOUSE_DEFINE_ATTR(press_to_select);
PSMOUSE_DEFINE_ATTR(skipback);
PSMOUSE_DEFINE_ATTR(ext_dev);

#define MAKE_ATTR_READ(_item) \
	static ssize_t psmouse_attr_show_##_item(struct psmouse *psmouse, char *buf) \
	{ \
		struct trackpoint_data *tp = psmouse->private; \
		return sprintf(buf, "%lu\n", (unsigned long)tp->_item); \
	}

#define MAKE_ATTR_WRITE(_item, command) \
	static ssize_t psmouse_attr_set_##_item(struct psmouse *psmouse, const char *buf, size_t count) \
	{ \
		char *rest; \
		unsigned long value; \
		struct trackpoint_data *tp = psmouse->private; \
		value = simple_strtoul(buf, &rest, 10); \
		if (*rest) \
			return -EINVAL; \
		tp->_item = value; \
		trackpoint_write(&psmouse->ps2dev, command, tp->_item); \
		return count; \
	}

#define MAKE_ATTR_TOGGLE(_item, command, mask) \
	static ssize_t psmouse_attr_set_##_item(struct psmouse *psmouse, const char *buf, size_t count) \
	{ \
		unsigned char toggle; \
		struct trackpoint_data *tp = psmouse->private; \
		toggle = (buf[0] == '1') ? 1 : 0; \
		if (toggle != tp->_item) { \
			tp->_item = toggle; \
			trackpoint_toggle_bit(&psmouse->ps2dev, command, mask); \
		} \
		return count; \
	}

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

MAKE_ATTR_WRITE(sensitivity, TP_SENS);
MAKE_ATTR_READ(sensitivity);

MAKE_ATTR_WRITE(speed, TP_SPEED);
MAKE_ATTR_READ(speed);

MAKE_ATTR_WRITE(inertia, TP_INERTIA);
MAKE_ATTR_READ(inertia);

MAKE_ATTR_WRITE(reach, TP_REACH);
MAKE_ATTR_READ(reach);

MAKE_ATTR_WRITE(draghys, TP_DRAGHYS);
MAKE_ATTR_READ(draghys);

MAKE_ATTR_WRITE(mindrag, TP_MINDRAG);
MAKE_ATTR_READ(mindrag);

MAKE_ATTR_WRITE(thresh, TP_THRESH);
MAKE_ATTR_READ(thresh);

MAKE_ATTR_WRITE(upthresh, TP_UP_THRESH);
MAKE_ATTR_READ(upthresh);

MAKE_ATTR_WRITE(ztime, TP_Z_TIME);
MAKE_ATTR_READ(ztime);

MAKE_ATTR_WRITE(jenks, TP_JENKS_CURV);
MAKE_ATTR_READ(jenks);

MAKE_ATTR_TOGGLE(press_to_select, TP_TOGGLE_PTSON, TP_MASK_PTSON);
MAKE_ATTR_READ(press_to_select);

MAKE_ATTR_TOGGLE(skipback, TP_TOGGLE_SKIPBACK, TP_MASK_SKIPBACK);
MAKE_ATTR_READ(skipback);

MAKE_ATTR_TOGGLE(ext_dev, TP_TOGGLE_EXT_DEV, TP_MASK_EXT_DEV);
MAKE_ATTR_READ(ext_dev);

static struct attribute *trackpoint_attrs[] = {
	&psmouse_attr_sensitivity.attr,
	&psmouse_attr_speed.attr,
	&psmouse_attr_inertia.attr,
	&psmouse_attr_reach.attr,
	&psmouse_attr_draghys.attr,
	&psmouse_attr_mindrag.attr,
	&psmouse_attr_thresh.attr,
	&psmouse_attr_upthresh.attr,
	&psmouse_attr_ztime.attr,
	&psmouse_attr_jenks.attr,
	&psmouse_attr_press_to_select.attr,
	&psmouse_attr_skipback.attr,
	&psmouse_attr_ext_dev.attr,
	NULL
};

static struct attribute_group trackpoint_attr_group = {
	.attrs = trackpoint_attrs,
};

static void trackpoint_disconnect(struct psmouse *psmouse)
{
	sysfs_remove_group(&psmouse->ps2dev.serio->dev.kobj, &trackpoint_attr_group);

	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int trackpoint_sync(struct psmouse *psmouse)
{
	unsigned char toggle;
	struct trackpoint_data *tp = psmouse->private;

	if (!tp)
		return -1;

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

int trackpoint_detect(struct psmouse *psmouse, int set_properties)
{
	struct trackpoint_data *priv;
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char firmware_id;
	unsigned char button_info;
	unsigned char param[2];

	param[0] = param[1] = 0;

	if (ps2_command(ps2dev, param, MAKE_PS2_CMD(0, 2, TP_READ_ID)))
		return -1;

	if (param[0] != TP_MAGIC_IDENT)
		return -1;

	if (!set_properties)
		return 0;

	firmware_id = param[1];

	if (trackpoint_read(&psmouse->ps2dev, TP_EXT_BTN, &button_info)) {
		printk(KERN_WARNING "trackpoint.c: failed to get extended button data\n");
		button_info = 0;
	}

	psmouse->private = priv = kcalloc(1, sizeof(struct trackpoint_data), GFP_KERNEL);
	if (!priv)
		return -1;

	psmouse->vendor = "IBM";
	psmouse->name = "TrackPoint";

	psmouse->reconnect = trackpoint_sync;
	psmouse->disconnect = trackpoint_disconnect;

	trackpoint_defaults(priv);
	trackpoint_sync(psmouse);

	sysfs_create_group(&ps2dev->serio->dev.kobj, &trackpoint_attr_group);

	printk(KERN_INFO "IBM TrackPoint firmware: 0x%02x, buttons: %d/%d\n",
		firmware_id, (button_info & 0xf0) >> 4, button_info & 0x0f);

	return 0;
}

