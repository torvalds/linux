/*
 * Synaptics TouchPad PS/2 mouse driver
 *
 *   2003 Dmitry Torokhov <dtor@mail.ru>
 *     Added support for pass-through port. Special thanks to Peter Berg Larsen
 *     for explaining various Synaptics quirks.
 *
 *   2003 Peter Osterlund <petero2@telia.com>
 *     Ported to 2.5 input device infrastructure.
 *
 *   Copyright (C) 2001 Stefan Gmeiner <riddlebox@freesurf.ch>
 *     start merging tpconfig and gpm code to a xfree-input module
 *     adding some changes and extensions (ex. 3rd and 4th button)
 *
 *   Copyright (c) 1997 C. Scott Ananian <cananian@alumni.priceton.edu>
 *   Copyright (c) 1998-2000 Bruce Kalk <kall@compass.com>
 *     code for the special synaptics commands (from the tpconfig-source)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * Trademarks are the property of their respective owners.
 */

#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>
#include <linux/slab.h>
#include "psmouse.h"
#include "synaptics.h"

/*
 * The x/y limits are taken from the Synaptics TouchPad interfacing Guide,
 * section 2.3.2, which says that they should be valid regardless of the
 * actual size of the sensor.
 * Note that newer firmware allows querying device for maximum useable
 * coordinates.
 */
#define XMIN_NOMINAL 1472
#define XMAX_NOMINAL 5472
#define YMIN_NOMINAL 1408
#define YMAX_NOMINAL 4448


/*****************************************************************************
 *	Stuff we need even when we do not want native Synaptics support
 ****************************************************************************/

/*
 * Set the synaptics touchpad mode byte by special commands
 */
static int synaptics_mode_cmd(struct psmouse *psmouse, unsigned char mode)
{
	unsigned char param[1];

	if (psmouse_sliced_command(psmouse, mode))
		return -1;
	param[0] = SYN_PS_SET_MODE2;
	if (ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_SETRATE))
		return -1;
	return 0;
}

int synaptics_detect(struct psmouse *psmouse, bool set_properties)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[4];

	param[0] = 0;

	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO);

	if (param[1] != 0x47)
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "Synaptics";
		psmouse->name = "TouchPad";
	}

	return 0;
}

void synaptics_reset(struct psmouse *psmouse)
{
	/* reset touchpad back to relative mode, gestures enabled */
	synaptics_mode_cmd(psmouse, 0);
}

#ifdef CONFIG_MOUSE_PS2_SYNAPTICS

/*****************************************************************************
 *	Synaptics communications functions
 ****************************************************************************/

/*
 * Send a command to the synpatics touchpad by special commands
 */
static int synaptics_send_cmd(struct psmouse *psmouse, unsigned char c, unsigned char *param)
{
	if (psmouse_sliced_command(psmouse, c))
		return -1;
	if (ps2_command(&psmouse->ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;
	return 0;
}

/*
 * Read the model-id bytes from the touchpad
 * see also SYN_MODEL_* macros
 */
static int synaptics_model_id(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char mi[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_MODEL, mi))
		return -1;
	priv->model_id = (mi[0]<<16) | (mi[1]<<8) | mi[2];
	return 0;
}

/*
 * Read the capability-bits from the touchpad
 * see also the SYN_CAP_* macros
 */
static int synaptics_capability(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char cap[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_CAPABILITIES, cap))
		return -1;
	priv->capabilities = (cap[0] << 16) | (cap[1] << 8) | cap[2];
	priv->ext_cap = priv->ext_cap_0c = 0;

	/*
	 * Older firmwares had submodel ID fixed to 0x47
	 */
	if (SYN_ID_FULL(priv->identity) < 0x705 &&
	    SYN_CAP_SUBMODEL_ID(priv->capabilities) != 0x47) {
		return -1;
	}

	/*
	 * Unless capExtended is set the rest of the flags should be ignored
	 */
	if (!SYN_CAP_EXTENDED(priv->capabilities))
		priv->capabilities = 0;

	if (SYN_EXT_CAP_REQUESTS(priv->capabilities) >= 1) {
		if (synaptics_send_cmd(psmouse, SYN_QUE_EXT_CAPAB, cap)) {
			printk(KERN_ERR "Synaptics claims to have extended capabilities,"
			       " but I'm not able to read them.\n");
		} else {
			priv->ext_cap = (cap[0] << 16) | (cap[1] << 8) | cap[2];

			/*
			 * if nExtBtn is greater than 8 it should be considered
			 * invalid and treated as 0
			 */
			if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) > 8)
				priv->ext_cap &= 0xff0fff;
		}
	}

	if (SYN_EXT_CAP_REQUESTS(priv->capabilities) >= 4) {
		if (synaptics_send_cmd(psmouse, SYN_QUE_EXT_CAPAB_0C, cap)) {
			printk(KERN_ERR "Synaptics claims to have extended capability 0x0c,"
			       " but I'm not able to read it.\n");
		} else {
			priv->ext_cap_0c = (cap[0] << 16) | (cap[1] << 8) | cap[2];
		}
	}

	return 0;
}

/*
 * Identify Touchpad
 * See also the SYN_ID_* macros
 */
static int synaptics_identify(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char id[3];

	if (synaptics_send_cmd(psmouse, SYN_QUE_IDENTIFY, id))
		return -1;
	priv->identity = (id[0]<<16) | (id[1]<<8) | id[2];
	if (SYN_ID_IS_SYNAPTICS(priv->identity))
		return 0;
	return -1;
}

/*
 * Read touchpad resolution and maximum reported coordinates
 * Resolution is left zero if touchpad does not support the query
 */
static int synaptics_resolution(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	unsigned char res[3];
	unsigned char max[3];

	if (SYN_ID_MAJOR(priv->identity) < 4)
		return 0;

	if (synaptics_send_cmd(psmouse, SYN_QUE_RESOLUTION, res) == 0) {
		if (res[0] != 0 && (res[1] & 0x80) && res[2] != 0) {
			priv->x_res = res[0]; /* x resolution in units/mm */
			priv->y_res = res[2]; /* y resolution in units/mm */
		}
	}

	if (SYN_EXT_CAP_REQUESTS(priv->capabilities) >= 5 &&
	    SYN_CAP_MAX_DIMENSIONS(priv->ext_cap_0c)) {
		if (synaptics_send_cmd(psmouse, SYN_QUE_EXT_DIMENSIONS, max)) {
			printk(KERN_ERR "Synaptics claims to have dimensions query,"
			       " but I'm not able to read it.\n");
		} else {
			priv->x_max = (max[0] << 5) | ((max[1] & 0x0f) << 1);
			priv->y_max = (max[2] << 5) | ((max[1] & 0xf0) >> 3);
		}
	}

	return 0;
}

static int synaptics_query_hardware(struct psmouse *psmouse)
{
	if (synaptics_identify(psmouse))
		return -1;
	if (synaptics_model_id(psmouse))
		return -1;
	if (synaptics_capability(psmouse))
		return -1;
	if (synaptics_resolution(psmouse))
		return -1;

	return 0;
}

static int synaptics_set_absolute_mode(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;

	priv->mode = SYN_BIT_ABSOLUTE_MODE;
	if (SYN_ID_MAJOR(priv->identity) >= 4)
		priv->mode |= SYN_BIT_DISABLE_GESTURE;
	if (SYN_CAP_EXTENDED(priv->capabilities))
		priv->mode |= SYN_BIT_W_MODE;

	if (synaptics_mode_cmd(psmouse, priv->mode))
		return -1;

	return 0;
}

static void synaptics_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	struct synaptics_data *priv = psmouse->private;

	if (rate >= 80) {
		priv->mode |= SYN_BIT_HIGH_RATE;
		psmouse->rate = 80;
	} else {
		priv->mode &= ~SYN_BIT_HIGH_RATE;
		psmouse->rate = 40;
	}

	synaptics_mode_cmd(psmouse, priv->mode);
}

/*****************************************************************************
 *	Synaptics pass-through PS/2 port support
 ****************************************************************************/
static int synaptics_pt_write(struct serio *serio, unsigned char c)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	char rate_param = SYN_PS_CLIENT_CMD; /* indicates that we want pass-through port */

	if (psmouse_sliced_command(parent, c))
		return -1;
	if (ps2_command(&parent->ps2dev, &rate_param, PSMOUSE_CMD_SETRATE))
		return -1;
	return 0;
}

static int synaptics_pt_start(struct serio *serio)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	struct synaptics_data *priv = parent->private;

	serio_pause_rx(parent->ps2dev.serio);
	priv->pt_port = serio;
	serio_continue_rx(parent->ps2dev.serio);

	return 0;
}

static void synaptics_pt_stop(struct serio *serio)
{
	struct psmouse *parent = serio_get_drvdata(serio->parent);
	struct synaptics_data *priv = parent->private;

	serio_pause_rx(parent->ps2dev.serio);
	priv->pt_port = NULL;
	serio_continue_rx(parent->ps2dev.serio);
}

static int synaptics_is_pt_packet(unsigned char *buf)
{
	return (buf[0] & 0xFC) == 0x84 && (buf[3] & 0xCC) == 0xC4;
}

static void synaptics_pass_pt_packet(struct serio *ptport, unsigned char *packet)
{
	struct psmouse *child = serio_get_drvdata(ptport);

	if (child && child->state == PSMOUSE_ACTIVATED) {
		serio_interrupt(ptport, packet[1], 0);
		serio_interrupt(ptport, packet[4], 0);
		serio_interrupt(ptport, packet[5], 0);
		if (child->pktsize == 4)
			serio_interrupt(ptport, packet[2], 0);
	} else
		serio_interrupt(ptport, packet[1], 0);
}

static void synaptics_pt_activate(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	struct psmouse *child = serio_get_drvdata(priv->pt_port);

	/* adjust the touchpad to child's choice of protocol */
	if (child) {
		if (child->pktsize == 4)
			priv->mode |= SYN_BIT_FOUR_BYTE_CLIENT;
		else
			priv->mode &= ~SYN_BIT_FOUR_BYTE_CLIENT;

		if (synaptics_mode_cmd(psmouse, priv->mode))
			printk(KERN_INFO "synaptics: failed to switch guest protocol\n");
	}
}

static void synaptics_pt_create(struct psmouse *psmouse)
{
	struct serio *serio;

	serio = kzalloc(sizeof(struct serio), GFP_KERNEL);
	if (!serio) {
		printk(KERN_ERR "synaptics: not enough memory to allocate pass-through port\n");
		return;
	}

	serio->id.type = SERIO_PS_PSTHRU;
	strlcpy(serio->name, "Synaptics pass-through", sizeof(serio->name));
	strlcpy(serio->phys, "synaptics-pt/serio0", sizeof(serio->name));
	serio->write = synaptics_pt_write;
	serio->start = synaptics_pt_start;
	serio->stop = synaptics_pt_stop;
	serio->parent = psmouse->ps2dev.serio;

	psmouse->pt_activate = synaptics_pt_activate;

	printk(KERN_INFO "serio: %s port at %s\n", serio->name, psmouse->phys);
	serio_register_port(serio);
}

/*****************************************************************************
 *	Functions to interpret the absolute mode packets
 ****************************************************************************/

static void synaptics_parse_hw_state(unsigned char buf[], struct synaptics_data *priv, struct synaptics_hw_state *hw)
{
	memset(hw, 0, sizeof(struct synaptics_hw_state));

	if (SYN_MODEL_NEWABS(priv->model_id)) {
		hw->x = (((buf[3] & 0x10) << 8) |
			 ((buf[1] & 0x0f) << 8) |
			 buf[4]);
		hw->y = (((buf[3] & 0x20) << 7) |
			 ((buf[1] & 0xf0) << 4) |
			 buf[5]);

		hw->z = buf[2];
		hw->w = (((buf[0] & 0x30) >> 2) |
			 ((buf[0] & 0x04) >> 1) |
			 ((buf[3] & 0x04) >> 2));

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;

		if (SYN_CAP_CLICKPAD(priv->ext_cap_0c)) {
			/*
			 * Clickpad's button is transmitted as middle button,
			 * however, since it is primary button, we will report
			 * it as BTN_LEFT.
			 */
			hw->left = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;

		} else if (SYN_CAP_MIDDLE_BUTTON(priv->capabilities)) {
			hw->middle = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;
			if (hw->w == 2)
				hw->scroll = (signed char)(buf[1]);
		}

		if (SYN_CAP_FOUR_BUTTON(priv->capabilities)) {
			hw->up   = ((buf[0] ^ buf[3]) & 0x01) ? 1 : 0;
			hw->down = ((buf[0] ^ buf[3]) & 0x02) ? 1 : 0;
		}

		if (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) &&
		    ((buf[0] ^ buf[3]) & 0x02)) {
			switch (SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap) & ~0x01) {
			default:
				/*
				 * if nExtBtn is greater than 8 it should be
				 * considered invalid and treated as 0
				 */
				break;
			case 8:
				hw->ext_buttons |= ((buf[5] & 0x08)) ? 0x80 : 0;
				hw->ext_buttons |= ((buf[4] & 0x08)) ? 0x40 : 0;
			case 6:
				hw->ext_buttons |= ((buf[5] & 0x04)) ? 0x20 : 0;
				hw->ext_buttons |= ((buf[4] & 0x04)) ? 0x10 : 0;
			case 4:
				hw->ext_buttons |= ((buf[5] & 0x02)) ? 0x08 : 0;
				hw->ext_buttons |= ((buf[4] & 0x02)) ? 0x04 : 0;
			case 2:
				hw->ext_buttons |= ((buf[5] & 0x01)) ? 0x02 : 0;
				hw->ext_buttons |= ((buf[4] & 0x01)) ? 0x01 : 0;
			}
		}
	} else {
		hw->x = (((buf[1] & 0x1f) << 8) | buf[2]);
		hw->y = (((buf[4] & 0x1f) << 8) | buf[5]);

		hw->z = (((buf[0] & 0x30) << 2) | (buf[3] & 0x3F));
		hw->w = (((buf[1] & 0x80) >> 4) | ((buf[0] & 0x04) >> 1));

		hw->left  = (buf[0] & 0x01) ? 1 : 0;
		hw->right = (buf[0] & 0x02) ? 1 : 0;
	}
}

/*
 *  called for each full received packet from the touchpad
 */
static void synaptics_process_packet(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct synaptics_data *priv = psmouse->private;
	struct synaptics_hw_state hw;
	int num_fingers;
	int finger_width;
	int i;

	synaptics_parse_hw_state(psmouse->packet, priv, &hw);

	if (hw.scroll) {
		priv->scroll += hw.scroll;

		while (priv->scroll >= 4) {
			input_report_key(dev, BTN_BACK, !hw.down);
			input_sync(dev);
			input_report_key(dev, BTN_BACK, hw.down);
			input_sync(dev);
			priv->scroll -= 4;
		}
		while (priv->scroll <= -4) {
			input_report_key(dev, BTN_FORWARD, !hw.up);
			input_sync(dev);
			input_report_key(dev, BTN_FORWARD, hw.up);
			input_sync(dev);
			priv->scroll += 4;
		}
		return;
	}

	if (hw.z > 0) {
		num_fingers = 1;
		finger_width = 5;
		if (SYN_CAP_EXTENDED(priv->capabilities)) {
			switch (hw.w) {
			case 0 ... 1:
				if (SYN_CAP_MULTIFINGER(priv->capabilities))
					num_fingers = hw.w + 2;
				break;
			case 2:
				if (SYN_MODEL_PEN(priv->model_id))
					;   /* Nothing, treat a pen as a single finger */
				break;
			case 4 ... 15:
				if (SYN_CAP_PALMDETECT(priv->capabilities))
					finger_width = hw.w;
				break;
			}
		}
	} else {
		num_fingers = 0;
		finger_width = 0;
	}

	/* Post events
	 * BTN_TOUCH has to be first as mousedev relies on it when doing
	 * absolute -> relative conversion
	 */
	if (hw.z > 30) input_report_key(dev, BTN_TOUCH, 1);
	if (hw.z < 25) input_report_key(dev, BTN_TOUCH, 0);

	if (hw.z > 0) {
		input_report_abs(dev, ABS_X, hw.x);
		input_report_abs(dev, ABS_Y, YMAX_NOMINAL + YMIN_NOMINAL - hw.y);
	}
	input_report_abs(dev, ABS_PRESSURE, hw.z);

	if (SYN_CAP_PALMDETECT(priv->capabilities))
		input_report_abs(dev, ABS_TOOL_WIDTH, finger_width);

	input_report_key(dev, BTN_TOOL_FINGER, num_fingers == 1);
	input_report_key(dev, BTN_LEFT, hw.left);
	input_report_key(dev, BTN_RIGHT, hw.right);

	if (SYN_CAP_MULTIFINGER(priv->capabilities)) {
		input_report_key(dev, BTN_TOOL_DOUBLETAP, num_fingers == 2);
		input_report_key(dev, BTN_TOOL_TRIPLETAP, num_fingers == 3);
	}

	if (SYN_CAP_MIDDLE_BUTTON(priv->capabilities))
		input_report_key(dev, BTN_MIDDLE, hw.middle);

	if (SYN_CAP_FOUR_BUTTON(priv->capabilities)) {
		input_report_key(dev, BTN_FORWARD, hw.up);
		input_report_key(dev, BTN_BACK, hw.down);
	}

	for (i = 0; i < SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap); i++)
		input_report_key(dev, BTN_0 + i, hw.ext_buttons & (1 << i));

	input_sync(dev);
}

static int synaptics_validate_byte(unsigned char packet[], int idx, unsigned char pkt_type)
{
	static const unsigned char newabs_mask[]	= { 0xC8, 0x00, 0x00, 0xC8, 0x00 };
	static const unsigned char newabs_rel_mask[]	= { 0xC0, 0x00, 0x00, 0xC0, 0x00 };
	static const unsigned char newabs_rslt[]	= { 0x80, 0x00, 0x00, 0xC0, 0x00 };
	static const unsigned char oldabs_mask[]	= { 0xC0, 0x60, 0x00, 0xC0, 0x60 };
	static const unsigned char oldabs_rslt[]	= { 0xC0, 0x00, 0x00, 0x80, 0x00 };

	if (idx < 0 || idx > 4)
		return 0;

	switch (pkt_type) {

	case SYN_NEWABS:
	case SYN_NEWABS_RELAXED:
		return (packet[idx] & newabs_rel_mask[idx]) == newabs_rslt[idx];

	case SYN_NEWABS_STRICT:
		return (packet[idx] & newabs_mask[idx]) == newabs_rslt[idx];

	case SYN_OLDABS:
		return (packet[idx] & oldabs_mask[idx]) == oldabs_rslt[idx];

	default:
		printk(KERN_ERR "synaptics: unknown packet type %d\n", pkt_type);
		return 0;
	}
}

static unsigned char synaptics_detect_pkt_type(struct psmouse *psmouse)
{
	int i;

	for (i = 0; i < 5; i++)
		if (!synaptics_validate_byte(psmouse->packet, i, SYN_NEWABS_STRICT)) {
			printk(KERN_INFO "synaptics: using relaxed packet validation\n");
			return SYN_NEWABS_RELAXED;
		}

	return SYN_NEWABS_STRICT;
}

static psmouse_ret_t synaptics_process_byte(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;

	if (psmouse->pktcnt >= 6) { /* Full packet received */
		if (unlikely(priv->pkt_type == SYN_NEWABS))
			priv->pkt_type = synaptics_detect_pkt_type(psmouse);

		if (SYN_CAP_PASS_THROUGH(priv->capabilities) &&
		    synaptics_is_pt_packet(psmouse->packet)) {
			if (priv->pt_port)
				synaptics_pass_pt_packet(priv->pt_port, psmouse->packet);
		} else
			synaptics_process_packet(psmouse);

		return PSMOUSE_FULL_PACKET;
	}

	return synaptics_validate_byte(psmouse->packet, psmouse->pktcnt - 1, priv->pkt_type) ?
		PSMOUSE_GOOD_DATA : PSMOUSE_BAD_DATA;
}

/*****************************************************************************
 *	Driver initialization/cleanup functions
 ****************************************************************************/
static void set_input_params(struct input_dev *dev, struct synaptics_data *priv)
{
	int i;

	__set_bit(EV_ABS, dev->evbit);
	input_set_abs_params(dev, ABS_X,
			     XMIN_NOMINAL, priv->x_max ?: XMAX_NOMINAL, 0, 0);
	input_set_abs_params(dev, ABS_Y,
			     YMIN_NOMINAL, priv->y_max ?: YMAX_NOMINAL, 0, 0);
	input_set_abs_params(dev, ABS_PRESSURE, 0, 255, 0, 0);

	if (SYN_CAP_PALMDETECT(priv->capabilities))
		input_set_abs_params(dev, ABS_TOOL_WIDTH, 0, 15, 0, 0);

	__set_bit(EV_KEY, dev->evbit);
	__set_bit(BTN_TOUCH, dev->keybit);
	__set_bit(BTN_TOOL_FINGER, dev->keybit);
	__set_bit(BTN_LEFT, dev->keybit);
	__set_bit(BTN_RIGHT, dev->keybit);

	if (SYN_CAP_MULTIFINGER(priv->capabilities)) {
		__set_bit(BTN_TOOL_DOUBLETAP, dev->keybit);
		__set_bit(BTN_TOOL_TRIPLETAP, dev->keybit);
	}

	if (SYN_CAP_MIDDLE_BUTTON(priv->capabilities))
		__set_bit(BTN_MIDDLE, dev->keybit);

	if (SYN_CAP_FOUR_BUTTON(priv->capabilities) ||
	    SYN_CAP_MIDDLE_BUTTON(priv->capabilities)) {
		__set_bit(BTN_FORWARD, dev->keybit);
		__set_bit(BTN_BACK, dev->keybit);
	}

	for (i = 0; i < SYN_CAP_MULTI_BUTTON_NO(priv->ext_cap); i++)
		__set_bit(BTN_0 + i, dev->keybit);

	__clear_bit(EV_REL, dev->evbit);
	__clear_bit(REL_X, dev->relbit);
	__clear_bit(REL_Y, dev->relbit);

	input_abs_set_res(dev, ABS_X, priv->x_res);
	input_abs_set_res(dev, ABS_Y, priv->y_res);

	if (SYN_CAP_CLICKPAD(priv->ext_cap_0c)) {
		/* Clickpads report only left button */
		__clear_bit(BTN_RIGHT, dev->keybit);
		__clear_bit(BTN_MIDDLE, dev->keybit);
	}
}

static void synaptics_disconnect(struct psmouse *psmouse)
{
	synaptics_reset(psmouse);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int synaptics_reconnect(struct psmouse *psmouse)
{
	struct synaptics_data *priv = psmouse->private;
	struct synaptics_data old_priv = *priv;

	psmouse_reset(psmouse);

	if (synaptics_detect(psmouse, 0))
		return -1;

	if (synaptics_query_hardware(psmouse)) {
		printk(KERN_ERR "Unable to query Synaptics hardware.\n");
		return -1;
	}

	if (old_priv.identity != priv->identity ||
	    old_priv.model_id != priv->model_id ||
	    old_priv.capabilities != priv->capabilities ||
	    old_priv.ext_cap != priv->ext_cap)
		return -1;

	if (synaptics_set_absolute_mode(psmouse)) {
		printk(KERN_ERR "Unable to initialize Synaptics hardware.\n");
		return -1;
	}

	return 0;
}

static bool impaired_toshiba_kbc;

static const struct dmi_system_id __initconst toshiba_dmi_table[] = {
#if defined(CONFIG_DMI) && defined(CONFIG_X86)
	{
		/* Toshiba Satellite */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Satellite"),
		},
	},
	{
		/* Toshiba Dynabook */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "dynabook"),
		},
	},
	{
		/* Toshiba Portege M300 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "PORTEGE M300"),
		},

	},
	{
		/* Toshiba Portege M300 */
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "TOSHIBA"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Portable PC"),
			DMI_MATCH(DMI_PRODUCT_VERSION, "Version 1.0"),
		},

	},
	{ }
#endif
};

void __init synaptics_module_init(void)
{
	impaired_toshiba_kbc = dmi_check_system(toshiba_dmi_table);
}

int synaptics_init(struct psmouse *psmouse)
{
	struct synaptics_data *priv;

	psmouse->private = priv = kzalloc(sizeof(struct synaptics_data), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	psmouse_reset(psmouse);

	if (synaptics_query_hardware(psmouse)) {
		printk(KERN_ERR "Unable to query Synaptics hardware.\n");
		goto init_fail;
	}

	if (synaptics_set_absolute_mode(psmouse)) {
		printk(KERN_ERR "Unable to initialize Synaptics hardware.\n");
		goto init_fail;
	}

	priv->pkt_type = SYN_MODEL_NEWABS(priv->model_id) ? SYN_NEWABS : SYN_OLDABS;

	printk(KERN_INFO "Synaptics Touchpad, model: %ld, fw: %ld.%ld, id: %#lx, caps: %#lx/%#lx/%#lx\n",
		SYN_ID_MODEL(priv->identity),
		SYN_ID_MAJOR(priv->identity), SYN_ID_MINOR(priv->identity),
		priv->model_id, priv->capabilities, priv->ext_cap, priv->ext_cap_0c);

	set_input_params(psmouse->dev, priv);

	/*
	 * Encode touchpad model so that it can be used to set
	 * input device->id.version and be visible to userspace.
	 * Because version is __u16 we have to drop something.
	 * Hardware info bits seem to be good candidates as they
	 * are documented to be for Synaptics corp. internal use.
	 */
	psmouse->model = ((priv->model_id & 0x00ff0000) >> 8) |
			  (priv->model_id & 0x000000ff);

	psmouse->protocol_handler = synaptics_process_byte;
	psmouse->set_rate = synaptics_set_rate;
	psmouse->disconnect = synaptics_disconnect;
	psmouse->reconnect = synaptics_reconnect;
	psmouse->cleanup = synaptics_reset;
	psmouse->pktsize = 6;
	/* Synaptics can usually stay in sync without extra help */
	psmouse->resync_time = 0;

	if (SYN_CAP_PASS_THROUGH(priv->capabilities))
		synaptics_pt_create(psmouse);

	/*
	 * Toshiba's KBC seems to have trouble handling data from
	 * Synaptics as full rate, switch to lower rate which is roughly
	 * thye same as rate of standard PS/2 mouse.
	 */
	if (psmouse->rate >= 80 && impaired_toshiba_kbc) {
		printk(KERN_INFO "synaptics: Toshiba %s detected, limiting rate to 40pps.\n",
			dmi_get_system_info(DMI_PRODUCT_NAME));
		psmouse->rate = 40;
	}

	return 0;

 init_fail:
	kfree(priv);
	return -1;
}

bool synaptics_supported(void)
{
	return true;
}

#else /* CONFIG_MOUSE_PS2_SYNAPTICS */

void __init synaptics_module_init(void)
{
}

int synaptics_init(struct psmouse *psmouse)
{
	return -ENOSYS;
}

bool synaptics_supported(void)
{
	return false;
}

#endif /* CONFIG_MOUSE_PS2_SYNAPTICS */

