// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Pixart Touchpad Controller 1336U PS2 driver
 *
 * Author: Jon Xie <jon_xie@pixart.com>
 *         Jay Lee <jay_lee@pixart.com>
 * Further cleanup and restructuring by:
 *         Binbin Zhou <zhoubinbin@loongson.cn>
 *
 * Copyright (C) 2021-2024 Pixart Imaging.
 * Copyright (C) 2024 Loongson Technology Corporation Limited.
 *
 */

#include <linux/bitfield.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/libps2.h>
#include <linux/serio.h>
#include <linux/slab.h>

#include "pixart_ps2.h"

static int pixart_read_tp_mode(struct ps2dev *ps2dev, u8 *mode)
{
	int error;
	u8 param[1] = { 0 };

	error = ps2_command(ps2dev, param, PIXART_CMD_REPORT_FORMAT);
	if (error)
		return error;

	*mode = param[0] == 1 ? PIXART_MODE_ABS : PIXART_MODE_REL;

	return 0;
}

static int pixart_read_tp_type(struct ps2dev *ps2dev, u8 *type)
{
	int error;
	u8 param[3] = { 0 };

	param[0] = 0x0a;
	error = ps2_command(ps2dev, param, PSMOUSE_CMD_SETRATE);
	if (error)
		return error;

	param[0] = 0x0;
	error = ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	if (error)
		return error;

	error = ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	if (error)
		return error;

	error = ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	if (error)
		return error;

	param[0] = 0x03;
	error = ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES);
	if (error)
		return error;

	error = ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO);
	if (error)
		return error;

	*type = param[0] == 0x0e ? PIXART_TYPE_TOUCHPAD : PIXART_TYPE_CLICKPAD;

	return 0;
}

static void pixart_reset(struct psmouse *psmouse)
{
	ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_RESET_DIS);

	/* according to PixArt, 100ms is required for the upcoming reset */
	msleep(100);
	psmouse_reset(psmouse);
}

static void pixart_process_packet(struct psmouse *psmouse)
{
	struct pixart_data *priv = psmouse->private;
	struct input_dev *dev = psmouse->dev;
	const u8 *pkt = psmouse->packet;
	unsigned int contact_cnt = FIELD_GET(CONTACT_CNT_MASK, pkt[0]);
	unsigned int i, id, abs_x, abs_y;
	bool tip;

	for (i = 0; i < contact_cnt; i++) {
		const u8 *p = &pkt[i * 3];

		id = FIELD_GET(SLOT_ID_MASK, p[3]);
		abs_y = FIELD_GET(ABS_Y_MASK, p[3]) << 8 | p[1];
		abs_x = FIELD_GET(ABS_X_MASK, p[3]) << 8 | p[2];

		if (i == PIXART_MAX_FINGERS - 1)
			tip = pkt[14] & BIT(1);
		else
			tip = pkt[3 * contact_cnt + 1] & BIT(2 * i + 1);

		input_mt_slot(dev, id);
		if (input_mt_report_slot_state(dev, MT_TOOL_FINGER, tip)) {
			input_report_abs(dev, ABS_MT_POSITION_Y, abs_y);
			input_report_abs(dev, ABS_MT_POSITION_X, abs_x);
		}
	}

	input_mt_sync_frame(dev);

	if (priv->type == PIXART_TYPE_CLICKPAD) {
		input_report_key(dev, BTN_LEFT, pkt[0] & 0x03);
	} else {
		input_report_key(dev, BTN_LEFT, pkt[0] & BIT(0));
		input_report_key(dev, BTN_RIGHT, pkt[0] & BIT(1));
	}

	input_sync(dev);
}

static psmouse_ret_t pixart_protocol_handler(struct psmouse *psmouse)
{
	u8 *pkt = psmouse->packet;
	u8 contact_cnt;

	if ((pkt[0] & 0x8c) != 0x80)
		return PSMOUSE_BAD_DATA;

	contact_cnt = FIELD_GET(CONTACT_CNT_MASK, pkt[0]);
	if (contact_cnt > PIXART_MAX_FINGERS)
		return PSMOUSE_BAD_DATA;

	if (contact_cnt == PIXART_MAX_FINGERS &&
	    psmouse->pktcnt < psmouse->pktsize) {
		return PSMOUSE_GOOD_DATA;
	}

	if (contact_cnt == 0 && psmouse->pktcnt < 5)
		return PSMOUSE_GOOD_DATA;

	if (psmouse->pktcnt < 3 * contact_cnt + 2)
		return PSMOUSE_GOOD_DATA;

	pixart_process_packet(psmouse);

	return PSMOUSE_FULL_PACKET;
}

static void pixart_disconnect(struct psmouse *psmouse)
{
	pixart_reset(psmouse);
	kfree(psmouse->private);
	psmouse->private = NULL;
}

static int pixart_reconnect(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	u8 mode;
	int error;

	pixart_reset(psmouse);

	error = pixart_read_tp_mode(ps2dev, &mode);
	if (error)
		return error;

	if (mode != PIXART_MODE_ABS)
		return -EIO;

	error = ps2_command(ps2dev, NULL, PIXART_CMD_SWITCH_PROTO);
	if (error)
		return error;

	return 0;
}

static int pixart_set_input_params(struct input_dev *dev,
				   struct pixart_data *priv)
{
	/* No relative support */
	__clear_bit(EV_REL, dev->evbit);
	__clear_bit(REL_X, dev->relbit);
	__clear_bit(REL_Y, dev->relbit);
	__clear_bit(BTN_MIDDLE, dev->keybit);

	/* Buttons */
	__set_bit(EV_KEY, dev->evbit);
	__set_bit(BTN_LEFT, dev->keybit);
	if (priv->type == PIXART_TYPE_CLICKPAD)
		__set_bit(INPUT_PROP_BUTTONPAD, dev->propbit);
	else
		__set_bit(BTN_RIGHT, dev->keybit);

	/* Absolute position */
	input_set_abs_params(dev, ABS_X, 0, PIXART_PAD_WIDTH, 0, 0);
	input_set_abs_params(dev, ABS_Y, 0, PIXART_PAD_HEIGHT, 0, 0);

	input_set_abs_params(dev, ABS_MT_POSITION_X,
			     0, PIXART_PAD_WIDTH, 0, 0);
	input_set_abs_params(dev, ABS_MT_POSITION_Y,
			     0, PIXART_PAD_HEIGHT, 0, 0);

	return input_mt_init_slots(dev, PIXART_MAX_FINGERS, INPUT_MT_POINTER);
}

static int pixart_query_hardware(struct ps2dev *ps2dev, u8 *mode, u8 *type)
{
	int error;

	error = pixart_read_tp_type(ps2dev, type);
	if (error)
		return error;

	error = pixart_read_tp_mode(ps2dev, mode);
	if (error)
		return error;

	return 0;
}

int pixart_detect(struct psmouse *psmouse, bool set_properties)
{
	u8 type;
	int error;

	pixart_reset(psmouse);

	error = pixart_read_tp_type(&psmouse->ps2dev, &type);
	if (error)
		return error;

	if (set_properties) {
		psmouse->vendor = "PixArt";
		psmouse->name = (type == PIXART_TYPE_TOUCHPAD) ?
				"touchpad" : "clickpad";
	}

	return 0;
}

int pixart_init(struct psmouse *psmouse)
{
	int error;
	struct pixart_data *priv;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	psmouse->private = priv;
	pixart_reset(psmouse);

	error = pixart_query_hardware(&psmouse->ps2dev,
				      &priv->mode, &priv->type);
	if (error) {
		psmouse_err(psmouse, "init: Unable to query PixArt touchpad hardware.\n");
		goto err_exit;
	}

	/* Relative mode follows standard PS/2 mouse protocol */
	if (priv->mode != PIXART_MODE_ABS) {
		error = -EIO;
		goto err_exit;
	}

	/* Set absolute mode */
	error = ps2_command(&psmouse->ps2dev, NULL, PIXART_CMD_SWITCH_PROTO);
	if (error) {
		psmouse_err(psmouse, "init: Unable to initialize PixArt absolute mode.\n");
		goto err_exit;
	}

	error = pixart_set_input_params(psmouse->dev, priv);
	if (error) {
		psmouse_err(psmouse, "init: Unable to set input params.\n");
		goto err_exit;
	}

	psmouse->pktsize = 15;
	psmouse->protocol_handler = pixart_protocol_handler;
	psmouse->disconnect = pixart_disconnect;
	psmouse->reconnect = pixart_reconnect;
	psmouse->cleanup = pixart_reset;
	/* resync is not supported yet */
	psmouse->resync_time = 0;

	return 0;

err_exit:
	pixart_reset(psmouse);
	kfree(priv);
	psmouse->private = NULL;
	return error;
}
