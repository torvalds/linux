/*
 * Focaltech TouchPad PS/2 mouse driver
 *
 * Copyright (c) 2014 Red Hat Inc.
 * Copyright (c) 2014 Mathias Gottschlag <mgottschlag@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Red Hat authors:
 *
 * Hans de Goede <hdegoede@redhat.com>
 */


#include <linux/device.h>
#include <linux/libps2.h>
#include <linux/input/mt.h>
#include <linux/serio.h>
#include <linux/slab.h>
#include "psmouse.h"
#include "focaltech.h"

static const char * const focaltech_pnp_ids[] = {
	"FLT0101",
	"FLT0102",
	"FLT0103",
	NULL
};

/*
 * Even if the kernel is built without support for Focaltech PS/2 touchpads (or
 * when the real driver fails to recognize the device), we still have to detect
 * them in order to avoid further detection attempts confusing the touchpad.
 * This way it at least works in PS/2 mouse compatibility mode.
 */
int focaltech_detect(struct psmouse *psmouse, bool set_properties)
{
	if (!psmouse_matches_pnp_id(psmouse, focaltech_pnp_ids))
		return -ENODEV;

	if (set_properties) {
		psmouse->vendor = "FocalTech";
		psmouse->name = "FocalTech Touchpad";
	}

	return 0;
}

static void focaltech_reset(struct psmouse *psmouse)
{
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);
	psmouse_reset(psmouse);
}

#ifdef CONFIG_MOUSE_PS2_FOCALTECH

/*
 * Packet types - the numbers are not consecutive, so we might be missing
 * something here.
 */
#define FOC_TOUCH 0x3 /* bitmap of active fingers */
#define FOC_ABS 0x6 /* absolute position of one finger */
#define FOC_REL 0x9 /* relative position of 1-2 fingers */

#define FOC_MAX_FINGERS 5

/*
 * Current state of a single finger on the touchpad.
 */
struct focaltech_finger_state {
	/* The touchpad has generated a touch event for the finger */
	bool active;

	/*
	 * The touchpad has sent position data for the finger. The
	 * flag is 0 when the finger is not active, and there is a
	 * time between the first touch event for the finger and the
	 * following absolute position packet for the finger where the
	 * touchpad has declared the finger to be valid, but we do not
	 * have any valid position yet.
	 */
	bool valid;

	/*
	 * Absolute position (from the bottom left corner) of the
	 * finger.
	 */
	unsigned int x;
	unsigned int y;
};

/*
 * Description of the current state of the touchpad hardware.
 */
struct focaltech_hw_state {
	/*
	 * The touchpad tracks the positions of the fingers for us,
	 * the array indices correspond to the finger indices returned
	 * in the report packages.
	 */
	struct focaltech_finger_state fingers[FOC_MAX_FINGERS];

	/* True if the clickpad has been pressed. */
	bool pressed;
};

struct focaltech_data {
	unsigned int x_max, y_max;
	struct focaltech_hw_state state;
};

static void focaltech_report_state(struct psmouse *psmouse)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	struct input_dev *dev = psmouse->dev;
	int i;

	for (i = 0; i < FOC_MAX_FINGERS; i++) {
		struct focaltech_finger_state *finger = &state->fingers[i];
		bool active = finger->active && finger->valid;

		input_mt_slot(dev, i);
		input_mt_report_slot_state(dev, MT_TOOL_FINGER, active);
		if (active) {
			unsigned int clamped_x, clamped_y;
			/*
			 * The touchpad might report invalid data, so we clamp
			 * the resulting values so that we do not confuse
			 * userspace.
			 */
			clamped_x = clamp(finger->x, 0U, priv->x_max);
			clamped_y = clamp(finger->y, 0U, priv->y_max);
			input_report_abs(dev, ABS_MT_POSITION_X, clamped_x);
			input_report_abs(dev, ABS_MT_POSITION_Y,
					 priv->y_max - clamped_y);
		}
	}
	input_mt_report_pointer_emulation(dev, true);

	input_report_key(psmouse->dev, BTN_LEFT, state->pressed);
	input_sync(psmouse->dev);
}

static void focaltech_process_touch_packet(struct psmouse *psmouse,
					   unsigned char *packet)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	unsigned char fingers = packet[1];
	int i;

	state->pressed = (packet[0] >> 4) & 1;

	/* the second byte contains a bitmap of all fingers touching the pad */
	for (i = 0; i < FOC_MAX_FINGERS; i++) {
		state->fingers[i].active = fingers & 0x1;
		if (!state->fingers[i].active) {
			/*
			 * Even when the finger becomes active again, we still
			 * will have to wait for the first valid position.
			 */
			state->fingers[i].valid = false;
		}
		fingers >>= 1;
	}
}

static void focaltech_process_abs_packet(struct psmouse *psmouse,
					 unsigned char *packet)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	unsigned int finger;

	finger = (packet[1] >> 4) - 1;
	if (finger >= FOC_MAX_FINGERS) {
		psmouse_err(psmouse, "Invalid finger in abs packet: %d\n",
			    finger);
		return;
	}

	state->pressed = (packet[0] >> 4) & 1;

	state->fingers[finger].x = ((packet[1] & 0xf) << 8) | packet[2];
	state->fingers[finger].y = (packet[3] << 8) | packet[4];
	state->fingers[finger].valid = true;
}

static void focaltech_process_rel_packet(struct psmouse *psmouse,
					 unsigned char *packet)
{
	struct focaltech_data *priv = psmouse->private;
	struct focaltech_hw_state *state = &priv->state;
	int finger1, finger2;

	state->pressed = packet[0] >> 7;
	finger1 = ((packet[0] >> 4) & 0x7) - 1;
	if (finger1 < FOC_MAX_FINGERS) {
		state->fingers[finger1].x += (char)packet[1];
		state->fingers[finger1].y += (char)packet[2];
	} else {
		psmouse_err(psmouse, "First finger in rel packet invalid: %d\n",
			    finger1);
	}

	/*
	 * If there is an odd number of fingers, the last relative
	 * packet only contains one finger. In this case, the second
	 * finger index in the packet is 0 (we subtract 1 in the lines
	 * above to create array indices, so the finger will overflow
	 * and be above FOC_MAX_FINGERS).
	 */
	finger2 = ((packet[3] >> 4) & 0x7) - 1;
	if (finger2 < FOC_MAX_FINGERS) {
		state->fingers[finger2].x += (char)packet[4];
		state->fingers[finger2].y += (char)packet[5];
	}
}

static void focaltech_process_packet(struct psmouse *psmouse)
{
	unsigned char *packet = psmouse->packet;

	switch (packet[0] & 0xf) {
	case FOC_TOUCH:
		focaltech_process_touch_packet(psmouse, packet);
		break;

	case FOC_ABS:
		focaltech_process_abs_packet(psmouse, packet);
		break;

	case FOC_REL:
		focaltech_process_rel_packet(psmouse, packet);
		break;

	default:
		psmouse_err(psmouse, "Unknown packet type: %02x\n", packet[0]);
		break;
	}

	focaltech_report_state(psmouse);
}

static psmouse_ret_t focaltech_process_byte(struct psmouse *psmouse)
{
	if (psmouse->pktcnt >= 6) { /* Full packet received */
		focaltech_process_packet(psmouse);
		return PSMOUSE_FULL_PACKET;
	}

	/*
	 * We might want to do some validation of the data here, but
	 * we do not know the protocol well enough
	 */
	return PSMOUSE_GOOD_DATA;
}

static int focaltech_switch_protocol(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char param[3];

	param[0] = 0;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;

	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;

	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;

	param[0] = 1;
	if (ps2_command(ps2dev, param, 0x10f8))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETSCALE11))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_ENABLE))
		return -EIO;

	return 0;
}

static void focaltech_disconnect(struct psmouse *psmouse)
{
	focaltech_reset(psmouse);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int focaltech_reconnect(struct psmouse *psmouse)
{
	int error;

	focaltech_reset(psmouse);

	error = focaltech_switch_protocol(psmouse);
	if (error) {
		psmouse_err(psmouse, "Unable to initialize the device\n");
		return error;
	}

	return 0;
}

static void focaltech_set_input_params(struct psmouse *psmouse)
{
	struct input_dev *dev = psmouse->dev;
	struct focaltech_data *priv = psmouse->private;

	/*
	 * Undo part of setup done for us by psmouse core since touchpad
	 * is not a relative device.
	 */
	__clear_bit(EV_REL, dev->evbit);
	__clear_bit(REL_X, dev->relbit);
	__clear_bit(REL_Y, dev->relbit);
	__clear_bit(BTN_RIGHT, dev->keybit);
	__clear_bit(BTN_MIDDLE, dev->keybit);

	/*
	 * Now set up our capabilities.
	 */
	__set_bit(EV_ABS, dev->evbit);
	input_set_abs_params(dev, ABS_MT_POSITION_X, 0, priv->x_max, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y, 0, priv->y_max, 0, 0);
	input_mt_init_slots(dev, 5, INPUT_MT_POINTER);
	__set_bit(INPUT_PROP_BUTTONPAD, dev->propbit);
}

static int focaltech_read_register(struct ps2dev *ps2dev, int reg,
				   unsigned char *param)
{
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETSCALE11))
		return -EIO;

	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;

	param[0] = reg;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES))
		return -EIO;

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -EIO;

	return 0;
}

static int focaltech_read_size(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	struct focaltech_data *priv = psmouse->private;
	char param[3];

	if (focaltech_read_register(ps2dev, 2, param))
		return -EIO;

	/* not sure whether this is 100% correct */
	priv->x_max = (unsigned char)param[1] * 128;
	priv->y_max = (unsigned char)param[2] * 128;

	return 0;
}

void focaltech_set_resolution(struct psmouse *psmouse, unsigned int resolution)
{
	/* not supported yet */
}

static void focaltech_set_rate(struct psmouse *psmouse, unsigned int rate)
{
	/* not supported yet */
}

static void focaltech_set_scale(struct psmouse *psmouse,
				enum psmouse_scale scale)
{
	/* not supported yet */
}

int focaltech_init(struct psmouse *psmouse)
{
	struct focaltech_data *priv;
	int error;

	psmouse->private = priv = kzalloc(sizeof(struct focaltech_data),
					  GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	focaltech_reset(psmouse);

	error = focaltech_read_size(psmouse);
	if (error) {
		psmouse_err(psmouse,
			    "Unable to read the size of the touchpad\n");
		goto fail;
	}

	error = focaltech_switch_protocol(psmouse);
	if (error) {
		psmouse_err(psmouse, "Unable to initialize the device\n");
		goto fail;
	}

	focaltech_set_input_params(psmouse);

	psmouse->protocol_handler = focaltech_process_byte;
	psmouse->pktsize = 6;
	psmouse->disconnect = focaltech_disconnect;
	psmouse->reconnect = focaltech_reconnect;
	psmouse->cleanup = focaltech_reset;
	/* resync is not supported yet */
	psmouse->resync_time = 0;
	/*
	 * rate/resolution/scale changes are not supported yet, and
	 * the generic implementations of these functions seem to
	 * confuse some touchpads
	 */
	psmouse->set_resolution = focaltech_set_resolution;
	psmouse->set_rate = focaltech_set_rate;
	psmouse->set_scale = focaltech_set_scale;

	return 0;

fail:
	focaltech_reset(psmouse);
	kfree(priv);
	return error;
}

#else /* CONFIG_MOUSE_PS2_FOCALTECH */

int focaltech_init(struct psmouse *psmouse)
{
	focaltech_reset(psmouse);

	return 0;
}

#endif /* CONFIG_MOUSE_PS2_FOCALTECH */
