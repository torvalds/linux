/*
 * Elantech Touchpad driver (v6)
 *
 * Copyright (C) 2007-2009 Arjan Opmeer <arjan@opmeer.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include "psmouse.h"
#include "elantech.h"

#define elantech_debug(format, arg...)				\
	do {							\
		if (etd->debug)					\
			printk(KERN_DEBUG format, ##arg);	\
	} while (0)

static bool force_elantech;
module_param_named(force_elantech, force_elantech, bool, 0644);
MODULE_PARM_DESC(force_elantech, "Force the Elantech PS/2 protocol extension to be used, 1 = enabled, 0 = disabled (default).");

/*
 * Send a Synaptics style sliced query command
 */
static int synaptics_send_cmd(struct psmouse *psmouse, unsigned char c,
				unsigned char *param)
{
	if (psmouse_sliced_command(psmouse, c) ||
	    ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_GETINFO)) {
		pr_err("elantech.c: synaptics_send_cmd query 0x%02x failed.\n", c);
		return -1;
	}

	return 0;
}

/*
 * A retrying version of ps2_command
 */
static int elantech_ps2_command(struct psmouse *psmouse,
				unsigned char *param, int command)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct elantech_data *etd = psmouse->private;
	int rc;
	int tries = ETP_PS2_COMMAND_TRIES;

	do {
		rc = ps2_command(ps2dev, param, command);
		if (rc == 0)
			break;
		tries--;
		elantech_debug("elantech.c: retrying ps2 command 0x%02x (%d).\n",
			command, tries);
		msleep(ETP_PS2_COMMAND_DELAY);
	} while (tries > 0);

	if (rc)
		pr_err("elantech.c: ps2 command 0x%02x failed.\n", command);

	return rc;
}

/*
 * Send an Elantech style special command to read a value from a register
 */
static int elantech_read_reg(struct psmouse *psmouse, unsigned char reg,
				unsigned char *val)
{
	struct elantech_data *etd = psmouse->private;
	unsigned char param[3];
	int rc = 0;

	if (reg < 0x10 || reg > 0x26)
		return -1;

	if (reg > 0x11 && reg < 0x20)
		return -1;

	switch (etd->hw_version) {
	case 1:
		if (psmouse_sliced_command(psmouse, ETP_REGISTER_READ) ||
		    psmouse_sliced_command(psmouse, reg) ||
		    ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_GETINFO)) {
			rc = -1;
		}
		break;

	case 2:
		if (elantech_ps2_command(psmouse,  NULL, ETP_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_command(psmouse,  NULL, ETP_REGISTER_READ) ||
		    elantech_ps2_command(psmouse,  NULL, ETP_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_command(psmouse,  NULL, reg) ||
		    elantech_ps2_command(psmouse, param, PSMOUSE_CMD_GETINFO)) {
			rc = -1;
		}
		break;
	}

	if (rc)
		pr_err("elantech.c: failed to read register 0x%02x.\n", reg);
	else
		*val = param[0];

	return rc;
}

/*
 * Send an Elantech style special command to write a register with a value
 */
static int elantech_write_reg(struct psmouse *psmouse, unsigned char reg,
				unsigned char val)
{
	struct elantech_data *etd = psmouse->private;
	int rc = 0;

	if (reg < 0x10 || reg > 0x26)
		return -1;

	if (reg > 0x11 && reg < 0x20)
		return -1;

	switch (etd->hw_version) {
	case 1:
		if (psmouse_sliced_command(psmouse, ETP_REGISTER_WRITE) ||
		    psmouse_sliced_command(psmouse, reg) ||
		    psmouse_sliced_command(psmouse, val) ||
		    ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETSCALE11)) {
			rc = -1;
		}
		break;

	case 2:
		if (elantech_ps2_command(psmouse, NULL, ETP_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_command(psmouse, NULL, ETP_REGISTER_WRITE) ||
		    elantech_ps2_command(psmouse, NULL, ETP_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_command(psmouse, NULL, reg) ||
		    elantech_ps2_command(psmouse, NULL, ETP_PS2_CUSTOM_COMMAND) ||
		    elantech_ps2_command(psmouse, NULL, val) ||
		    elantech_ps2_command(psmouse, NULL, PSMOUSE_CMD_SETSCALE11)) {
			rc = -1;
		}
		break;
	}

	if (rc)
		pr_err("elantech.c: failed to write register 0x%02x with value 0x%02x.\n",
			reg, val);

	return rc;
}

/*
 * Dump a complete mouse movement packet to the syslog
 */
static void elantech_packet_dump(unsigned char *packet, int size)
{
	int	i;

	printk(KERN_DEBUG "elantech.c: PS/2 packet [");
	for (i = 0; i < size; i++)
		printk("%s0x%02x ", (i) ? ", " : " ", packet[i]);
	printk("]\n");
}

/*
 * Interpret complete data packets and report absolute mode input events for
 * hardware version 1. (4 byte packets)
 */
static void elantech_report_absolute_v1(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct elantech_data *etd = psmouse->private;
	unsigned char *packet = psmouse->packet;
	int fingers;

	if (etd->fw_version < 0x020000) {
		/*
		 * byte 0:  D   U  p1  p2   1  p3   R   L
		 * byte 1:  f   0  th  tw  x9  x8  y9  y8
		 */
		fingers = ((packet[1] & 0x80) >> 7) +
				((packet[1] & 0x30) >> 4);
	} else {
		/*
		 * byte 0: n1  n0  p2  p1   1  p3   R   L
		 * byte 1:  0   0   0   0  x9  x8  y9  y8
		 */
		fingers = (packet[0] & 0xc0) >> 6;
	}

	if (etd->jumpy_cursor) {
		if (fingers != 1) {
			etd->single_finger_reports = 0;
		} else if (etd->single_finger_reports < 2) {
			/* Discard first 2 reports of one finger, bogus */
			etd->single_finger_reports++;
			elantech_debug("elantech.c: discarding packet\n");
			return;
		}
	}

	input_report_key(dev, BTN_TOUCH, fingers != 0);

	/*
	 * byte 2: x7  x6  x5  x4  x3  x2  x1  x0
	 * byte 3: y7  y6  y5  y4  y3  y2  y1  y0
	 */
	if (fingers) {
		input_report_abs(dev, ABS_X,
			((packet[1] & 0x0c) << 6) | packet[2]);
		input_report_abs(dev, ABS_Y,
			ETP_YMAX_V1 - (((packet[1] & 0x03) << 8) | packet[3]));
	}

	input_report_key(dev, BTN_TOOL_FINGER, fingers == 1);
	input_report_key(dev, BTN_TOOL_DOUBLETAP, fingers == 2);
	input_report_key(dev, BTN_TOOL_TRIPLETAP, fingers == 3);
	input_report_key(dev, BTN_LEFT, packet[0] & 0x01);
	input_report_key(dev, BTN_RIGHT, packet[0] & 0x02);

	if (etd->fw_version < 0x020000 &&
	    (etd->capabilities & ETP_CAP_HAS_ROCKER)) {
		/* rocker up */
		input_report_key(dev, BTN_FORWARD, packet[0] & 0x40);
		/* rocker down */
		input_report_key(dev, BTN_BACK, packet[0] & 0x80);
	}

	input_sync(dev);
}

/*
 * Interpret complete data packets and report absolute mode input events for
 * hardware version 2. (6 byte packets)
 */
static void elantech_report_absolute_v2(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	unsigned char *packet = psmouse->packet;
	int fingers, x1, y1, x2, y2;

	/* byte 0: n1  n0   .   .   .   .   R   L */
	fingers = (packet[0] & 0xc0) >> 6;
	input_report_key(dev, BTN_TOUCH, fingers != 0);

	switch (fingers) {
	case 1:
		/*
		 * byte 1:  .   .   .   .   .  x10 x9  x8
		 * byte 2: x7  x6  x5  x4  x4  x2  x1  x0
		 */
		input_report_abs(dev, ABS_X,
			((packet[1] & 0x07) << 8) | packet[2]);
		/*
		 * byte 4:  .   .   .   .   .   .  y9  y8
		 * byte 5: y7  y6  y5  y4  y3  y2  y1  y0
		 */
		input_report_abs(dev, ABS_Y,
			ETP_YMAX_V2 - (((packet[4] & 0x03) << 8) | packet[5]));
		break;

	case 2:
		/*
		 * The coordinate of each finger is reported separately
		 * with a lower resolution for two finger touches:
		 * byte 0:  .   .  ay8 ax8  .   .   .   .
		 * byte 1: ax7 ax6 ax5 ax4 ax3 ax2 ax1 ax0
		 */
		x1 = ((packet[0] & 0x10) << 4) | packet[1];
		/* byte 2: ay7 ay6 ay5 ay4 ay3 ay2 ay1 ay0 */
		y1 = ETP_2FT_YMAX - (((packet[0] & 0x20) << 3) | packet[2]);
		/*
		 * byte 3:  .   .  by8 bx8  .   .   .   .
		 * byte 4: bx7 bx6 bx5 bx4 bx3 bx2 bx1 bx0
		 */
		x2 = ((packet[3] & 0x10) << 4) | packet[4];
		/* byte 5: by7 by8 by5 by4 by3 by2 by1 by0 */
		y2 = ETP_2FT_YMAX - (((packet[3] & 0x20) << 3) | packet[5]);
		/*
		 * For compatibility with the X Synaptics driver scale up
		 * one coordinate and report as ordinary mouse movent
		 */
		input_report_abs(dev, ABS_X, x1 << 2);
		input_report_abs(dev, ABS_Y, y1 << 2);
		/*
		 * For compatibility with the proprietary X Elantech driver
		 * report both coordinates as hat coordinates
		 */
		input_report_abs(dev, ABS_HAT0X, x1);
		input_report_abs(dev, ABS_HAT0Y, y1);
		input_report_abs(dev, ABS_HAT1X, x2);
		input_report_abs(dev, ABS_HAT1Y, y2);
		break;
	}

	input_report_key(dev, BTN_TOOL_FINGER, fingers == 1);
	input_report_key(dev, BTN_TOOL_DOUBLETAP, fingers == 2);
	input_report_key(dev, BTN_TOOL_TRIPLETAP, fingers == 3);
	input_report_key(dev, BTN_LEFT, packet[0] & 0x01);
	input_report_key(dev, BTN_RIGHT, packet[0] & 0x02);

	input_sync(dev);
}

static int elantech_check_parity_v1(struct psmouse *psmouse)
{
	struct elantech_data *etd = psmouse->private;
	unsigned char *packet = psmouse->packet;
	unsigned char p1, p2, p3;

	/* Parity bits are placed differently */
	if (etd->fw_version < 0x020000) {
		/* byte 0:  D   U  p1  p2   1  p3   R   L */
		p1 = (packet[0] & 0x20) >> 5;
		p2 = (packet[0] & 0x10) >> 4;
	} else {
		/* byte 0: n1  n0  p2  p1   1  p3   R   L */
		p1 = (packet[0] & 0x10) >> 4;
		p2 = (packet[0] & 0x20) >> 5;
	}

	p3 = (packet[0] & 0x04) >> 2;

	return etd->parity[packet[1]] == p1 &&
	       etd->parity[packet[2]] == p2 &&
	       etd->parity[packet[3]] == p3;
}

/*
 * Process byte stream from mouse and handle complete packets
 */
static psmouse_ret_t elantech_process_byte(struct psmouse *psmouse)
{
	struct elantech_data *etd = psmouse->private;

	if (psmouse->pktcnt < psmouse->pktsize)
		return PSMOUSE_GOOD_DATA;

	if (etd->debug > 1)
		elantech_packet_dump(psmouse->packet, psmouse->pktsize);

	switch (etd->hw_version) {
	case 1:
		if (etd->paritycheck && !elantech_check_parity_v1(psmouse))
			return PSMOUSE_BAD_DATA;

		elantech_report_absolute_v1(psmouse);
		break;

	case 2:
		/* We don't know how to check parity in protocol v2 */
		elantech_report_absolute_v2(psmouse);
		break;
	}

	return PSMOUSE_FULL_PACKET;
}

/*
 * Put the touchpad into absolute mode
 */
static int elantech_set_absolute_mode(struct psmouse *psmouse)
{
	struct elantech_data *etd = psmouse->private;
	unsigned char val;
	int tries = ETP_READ_BACK_TRIES;
	int rc = 0;

	switch (etd->hw_version) {
	case 1:
		etd->reg_10 = 0x16;
		etd->reg_11 = 0x8f;
		if (elantech_write_reg(psmouse, 0x10, etd->reg_10) ||
		    elantech_write_reg(psmouse, 0x11, etd->reg_11)) {
			rc = -1;
		}
		break;

	case 2:
					/* Windows driver values */
		etd->reg_10 = 0x54;
		etd->reg_11 = 0x88;	/* 0x8a */
		etd->reg_21 = 0x60;	/* 0x00 */
		if (elantech_write_reg(psmouse, 0x10, etd->reg_10) ||
		    elantech_write_reg(psmouse, 0x11, etd->reg_11) ||
		    elantech_write_reg(psmouse, 0x21, etd->reg_21)) {
			rc = -1;
			break;
		}
	}

	if (rc == 0) {
		/*
		 * Read back reg 0x10. For hardware version 1 we must make
		 * sure the absolute mode bit is set. For hardware version 2
		 * the touchpad is probably initalising and not ready until
		 * we read back the value we just wrote.
		 */
		do {
			rc = elantech_read_reg(psmouse, 0x10, &val);
			if (rc == 0)
				break;
			tries--;
			elantech_debug("elantech.c: retrying read (%d).\n",
					tries);
			msleep(ETP_READ_BACK_DELAY);
		} while (tries > 0);

		if (rc) {
			pr_err("elantech.c: failed to read back register 0x10.\n");
		} else if (etd->hw_version == 1 &&
			   !(val & ETP_R10_ABSOLUTE_MODE)) {
			pr_err("elantech.c: touchpad refuses "
				"to switch to absolute mode.\n");
			rc = -1;
		}
	}

	if (rc)
		pr_err("elantech.c: failed to initialise registers.\n");

	return rc;
}

/*
 * Set the appropriate event bits for the input subsystem
 */
static void elantech_set_input_params(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct elantech_data *etd = psmouse->private;

	__set_bit(EV_KEY, dev->evbit);
	__set_bit(EV_ABS, dev->evbit);
	__clear_bit(EV_REL, dev->evbit);

	__set_bit(BTN_LEFT, dev->keybit);
	__set_bit(BTN_RIGHT, dev->keybit);

	__set_bit(BTN_TOUCH, dev->keybit);
	__set_bit(BTN_TOOL_FINGER, dev->keybit);
	__set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
	__set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);

	switch (etd->hw_version) {
	case 1:
		/* Rocker button */
		if (etd->fw_version < 0x020000 &&
		    (etd->capabilities & ETP_CAP_HAS_ROCKER)) {
			__set_bit(BTN_FORWARD, dev->keybit);
			__set_bit(BTN_BACK, dev->keybit);
		}
		input_set_abs_params(dev, ABS_X, ETP_XMIN_V1, ETP_XMAX_V1, 0, 0);
		input_set_abs_params(dev, ABS_Y, ETP_YMIN_V1, ETP_YMAX_V1, 0, 0);
		break;

	case 2:
		input_set_abs_params(dev, ABS_X, ETP_XMIN_V2, ETP_XMAX_V2, 0, 0);
		input_set_abs_params(dev, ABS_Y, ETP_YMIN_V2, ETP_YMAX_V2, 0, 0);
		input_set_abs_params(dev, ABS_HAT0X, ETP_2FT_XMIN, ETP_2FT_XMAX, 0, 0);
		input_set_abs_params(dev, ABS_HAT0Y, ETP_2FT_YMIN, ETP_2FT_YMAX, 0, 0);
		input_set_abs_params(dev, ABS_HAT1X, ETP_2FT_XMIN, ETP_2FT_XMAX, 0, 0);
		input_set_abs_params(dev, ABS_HAT1Y, ETP_2FT_YMIN, ETP_2FT_YMAX, 0, 0);
		break;
	}
}

struct elantech_attr_data {
	size_t		field_offset;
	unsigned char	reg;
};

/*
 * Display a register value by reading a sysfs entry
 */
static ssize_t elantech_show_int_attr(struct psmouse *psmouse, void *data,
					char *buf)
{
	struct elantech_data *etd = psmouse->private;
	struct elantech_attr_data *attr = data;
	unsigned char *reg = (unsigned char *) etd + attr->field_offset;
	int rc = 0;

	if (attr->reg)
		rc = elantech_read_reg(psmouse, attr->reg, reg);

	return sprintf(buf, "0x%02x\n", (attr->reg && rc) ? -1 : *reg);
}

/*
 * Write a register value by writing a sysfs entry
 */
static ssize_t elantech_set_int_attr(struct psmouse *psmouse,
				     void *data, const char *buf, size_t count)
{
	struct elantech_data *etd = psmouse->private;
	struct elantech_attr_data *attr = data;
	unsigned char *reg = (unsigned char *) etd + attr->field_offset;
	unsigned long value;
	int err;

	err = strict_strtoul(buf, 16, &value);
	if (err)
		return err;

	if (value > 0xff)
		return -EINVAL;

	/* Do we need to preserve some bits for version 2 hardware too? */
	if (etd->hw_version == 1) {
		if (attr->reg == 0x10)
			/* Force absolute mode always on */
			value |= ETP_R10_ABSOLUTE_MODE;
		else if (attr->reg == 0x11)
			/* Force 4 byte mode always on */
			value |= ETP_R11_4_BYTE_MODE;
	}

	if (!attr->reg || elantech_write_reg(psmouse, attr->reg, value) == 0)
		*reg = value;

	return count;
}

#define ELANTECH_INT_ATTR(_name, _register)				\
	static struct elantech_attr_data elantech_attr_##_name = {	\
		.field_offset = offsetof(struct elantech_data, _name),	\
		.reg = _register,					\
	};								\
	PSMOUSE_DEFINE_ATTR(_name, S_IWUSR | S_IRUGO,			\
			    &elantech_attr_##_name,			\
			    elantech_show_int_attr,			\
			    elantech_set_int_attr)

ELANTECH_INT_ATTR(reg_10, 0x10);
ELANTECH_INT_ATTR(reg_11, 0x11);
ELANTECH_INT_ATTR(reg_20, 0x20);
ELANTECH_INT_ATTR(reg_21, 0x21);
ELANTECH_INT_ATTR(reg_22, 0x22);
ELANTECH_INT_ATTR(reg_23, 0x23);
ELANTECH_INT_ATTR(reg_24, 0x24);
ELANTECH_INT_ATTR(reg_25, 0x25);
ELANTECH_INT_ATTR(reg_26, 0x26);
ELANTECH_INT_ATTR(debug, 0);
ELANTECH_INT_ATTR(paritycheck, 0);

static struct attribute *elantech_attrs[] = {
	&psmouse_attr_reg_10.dattr.attr,
	&psmouse_attr_reg_11.dattr.attr,
	&psmouse_attr_reg_20.dattr.attr,
	&psmouse_attr_reg_21.dattr.attr,
	&psmouse_attr_reg_22.dattr.attr,
	&psmouse_attr_reg_23.dattr.attr,
	&psmouse_attr_reg_24.dattr.attr,
	&psmouse_attr_reg_25.dattr.attr,
	&psmouse_attr_reg_26.dattr.attr,
	&psmouse_attr_debug.dattr.attr,
	&psmouse_attr_paritycheck.dattr.attr,
	NULL
};

static struct attribute_group elantech_attr_group = {
	.attrs = elantech_attrs,
};

static bool elantech_is_signature_valid(const unsigned char *param)
{
	static const unsigned char rates[] = { 200, 100, 80, 60, 40, 20, 10 };
	int i;

	if (param[0] == 0)
		return false;

	if (param[1] == 0)
		return true;

	for (i = 0; i < ARRAY_SIZE(rates); i++)
		if (param[2] == rates[i])
			return false;

	return true;
}

/*
 * Use magic knock to detect Elantech touchpad
 */
int elantech_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];

	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);

	if (ps2_command(ps2dev,  NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO)) {
		pr_debug("elantech.c: sending Elantech magic knock failed.\n");
		return -1;
	}

	/*
	 * Report this in case there are Elantech models that use a different
	 * set of magic numbers
	 */
	if (param[0] != 0x3c || param[1] != 0x03 || param[2] != 0xc8) {
		pr_debug("elantech.c: "
			 "unexpected magic knock result 0x%02x, 0x%02x, 0x%02x.\n",
			 param[0], param[1], param[2]);
		return -1;
	}

	/*
	 * Query touchpad's firmware version and see if it reports known
	 * value to avoid mis-detection. Logitech mice are known to respond
	 * to Elantech magic knock and there might be more.
	 */
	if (synaptics_send_cmd(psmouse, ETP_FW_VERSION_QUERY, param)) {
		pr_debug("elantech.c: failed to query firmware version.\n");
		return -1;
	}

	pr_debug("elantech.c: Elantech version query result 0x%02x, 0x%02x, 0x%02x.\n",
		 param[0], param[1], param[2]);

	if (!elantech_is_signature_valid(param)) {
		if (!force_elantech) {
			pr_debug("elantech.c: Probably not a real Elantech touchpad. Aborting.\n");
			return -1;
		}

		pr_debug("elantech.c: Probably not a real Elantech touchpad. Enabling anyway due to force_elantech.\n");
	}

	if (set_properties) {
		psmouse->vendor = "Elantech";
		psmouse->name = "Touchpad";
	}

	return 0;
}

/*
 * Clean up sysfs entries when disconnecting
 */
static void elantech_disconnect(struct psmouse *psmouse)
{
	sysfs_remove_group(&psmouse->ps2dev.serio->dev.kobj,
			   &elantech_attr_group);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

/*
 * Put the touchpad back into absolute mode when reconnecting
 */
static int elantech_reconnect(struct psmouse *psmouse)
{
	if (elantech_detect(psmouse, 0))
		return -1;

	if (elantech_set_absolute_mode(psmouse)) {
		pr_err("elantech.c: failed to put touchpad back into absolute mode.\n");
		return -1;
	}

	return 0;
}

/*
 * Initialize the touchpad and create sysfs entries
 */
int elantech_init(struct psmouse *psmouse)
{
	struct elantech_data *etd;
	int i, error;
	unsigned char param[3];

	psmouse->private = etd = kzalloc(sizeof(struct elantech_data), GFP_KERNEL);
	if (!etd)
		return -1;

	etd->parity[0] = 1;
	for (i = 1; i < 256; i++)
		etd->parity[i] = etd->parity[i & (i - 1)] ^ 1;

	/*
	 * Do the version query again so we can store the result
	 */
	if (synaptics_send_cmd(psmouse, ETP_FW_VERSION_QUERY, param)) {
		pr_err("elantech.c: failed to query firmware version.\n");
		goto init_fail;
	}

	etd->fw_version = (param[0] << 16) | (param[1] << 8) | param[2];

	/*
	 * Assume every version greater than this is new EeePC style
	 * hardware with 6 byte packets
	 */
	if (etd->fw_version >= 0x020030) {
		etd->hw_version = 2;
		/* For now show extra debug information */
		etd->debug = 1;
		/* Don't know how to do parity checking for version 2 */
		etd->paritycheck = 0;
	} else {
		etd->hw_version = 1;
		etd->paritycheck = 1;
	}

	pr_info("elantech.c: assuming hardware version %d, firmware version %d.%d.%d\n",
		etd->hw_version, param[0], param[1], param[2]);

	if (synaptics_send_cmd(psmouse, ETP_CAPABILITIES_QUERY, param)) {
		pr_err("elantech.c: failed to query capabilities.\n");
		goto init_fail;
	}
	pr_info("elantech.c: Synaptics capabilities query result 0x%02x, 0x%02x, 0x%02x.\n",
		param[0], param[1], param[2]);
	etd->capabilities = param[0];

	/*
	 * This firmware suffers from misreporting coordinates when
	 * a touch action starts causing the mouse cursor or scrolled page
	 * to jump. Enable a workaround.
	 */
	if (etd->fw_version == 0x020022 || etd->fw_version == 0x020600) {
		pr_info("elantech.c: firmware version 2.0.34/2.6.0 detected, "
			"enabling jumpy cursor workaround\n");
		etd->jumpy_cursor = true;
	}

	if (elantech_set_absolute_mode(psmouse)) {
		pr_err("elantech.c: failed to put touchpad into absolute mode.\n");
		goto init_fail;
	}

	elantech_set_input_params(psmouse);

	error = sysfs_create_group(&psmouse->ps2dev.serio->dev.kobj,
				   &elantech_attr_group);
	if (error) {
		pr_err("elantech.c: failed to create sysfs attributes, error: %d.\n",
			error);
		goto init_fail;
	}

	psmouse->protocol_handler = elantech_process_byte;
	psmouse->disconnect = elantech_disconnect;
	psmouse->reconnect = elantech_reconnect;
	psmouse->pktsize = etd->hw_version == 2 ? 6 : 4;

	return 0;

 init_fail:
	kfree(etd);
	return -1;
}
