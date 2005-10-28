/*
 * ALPS touchpad PS/2 mouse driver
 *
 * Copyright (c) 2003 Neil Brown <neilb@cse.unsw.edu.au>
 * Copyright (c) 2003-2005 Peter Osterlund <petero2@telia.com>
 * Copyright (c) 2004 Dmitry Torokhov <dtor@mail.ru>
 * Copyright (c) 2005 Vojtech Pavlik <vojtech@suse.cz>
 *
 * ALPS detection, tap switching and status querying info is taken from
 * tpconfig utility (by C. Scott Ananian and Bruce Kall).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */

#include <linux/input.h>
#include <linux/serio.h>
#include <linux/libps2.h>

#include "psmouse.h"
#include "alps.h"

#undef DEBUG
#ifdef DEBUG
#define dbg(format, arg...) printk(KERN_INFO "alps.c: " format "\n", ## arg)
#else
#define dbg(format, arg...) do {} while (0)
#endif

#define ALPS_DUALPOINT	0x01
#define ALPS_WHEEL	0x02
#define ALPS_FW_BK_1	0x04
#define ALPS_4BTN	0x08
#define ALPS_OLDPROTO	0x10
#define ALPS_PASS	0x20
#define ALPS_FW_BK_2	0x40

static struct alps_model_info alps_model_data[] = {
	{ { 0x33, 0x02, 0x0a },	0x88, 0xf8, ALPS_OLDPROTO },		/* UMAX-530T */
	{ { 0x53, 0x02, 0x0a },	0xf8, 0xf8, 0 },
	{ { 0x53, 0x02, 0x14 },	0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x0a },	0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x14 },	0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x28 },	0xf8, 0xf8, 0 },
	{ { 0x63, 0x02, 0x3c },	0x8f, 0x8f, ALPS_WHEEL },		/* Toshiba Satellite S2400-103 */
	{ { 0x63, 0x02, 0x50 },	0xef, 0xef, ALPS_FW_BK_1 },		/* NEC Versa L320 */
	{ { 0x63, 0x02, 0x64 },	0xf8, 0xf8, 0 },
	{ { 0x63, 0x03, 0xc8 }, 0xf8, 0xf8, ALPS_PASS },		/* Dell Latitude D800 */
	{ { 0x73, 0x02, 0x0a },	0xf8, 0xf8, 0 },
	{ { 0x73, 0x02, 0x14 },	0xf8, 0xf8, ALPS_FW_BK_2 },		/* Ahtec Laptop */
	{ { 0x20, 0x02, 0x0e },	0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT }, /* XXX */
	{ { 0x22, 0x02, 0x0a },	0xf8, 0xf8, ALPS_PASS | ALPS_DUALPOINT },
	{ { 0x22, 0x02, 0x14 }, 0xff, 0xff, ALPS_PASS | ALPS_DUALPOINT }, /* Dell Latitude D600 */
};

/*
 * XXX - this entry is suspicious. First byte has zero lower nibble,
 * which is what a normal mouse would report. Also, the value 0x0e
 * isn't valid per PS/2 spec.
 */

/*
 * ALPS abolute Mode - new format
 *
 * byte 0:  1    ?    ?    ?    1    ?    ?    ?
 * byte 1:  0   x6   x5   x4   x3   x2   x1   x0
 * byte 2:  0   x10  x9   x8   x7    ?  fin  ges
 * byte 3:  0   y9   y8   y7    1    M    R    L
 * byte 4:  0   y6   y5   y4   y3   y2   y1   y0
 * byte 5:  0   z6   z5   z4   z3   z2   z1   z0
 *
 * ?'s can have different meanings on different models,
 * such as wheel rotation, extra buttons, stick buttons
 * on a dualpoint, etc.
 */

static void alps_process_packet(struct psmouse *psmouse, struct pt_regs *regs)
{
	struct alps_data *priv = psmouse->private;
	unsigned char *packet = psmouse->packet;
	struct input_dev *dev = psmouse->dev;
	struct input_dev *dev2 = priv->dev2;
	int x, y, z, ges, fin, left, right, middle;
	int back = 0, forward = 0;

	input_regs(dev, regs);

	if ((packet[0] & 0xc8) == 0x08) {   /* 3-byte PS/2 packet */
		input_report_key(dev2, BTN_LEFT,   packet[0] & 1);
		input_report_key(dev2, BTN_RIGHT,  packet[0] & 2);
		input_report_key(dev2, BTN_MIDDLE, packet[0] & 4);
		input_report_rel(dev2, REL_X,
			packet[1] ? packet[1] - ((packet[0] << 4) & 0x100) : 0);
		input_report_rel(dev2, REL_Y,
			packet[2] ? ((packet[0] << 3) & 0x100) - packet[2] : 0);
		input_sync(dev2);
		return;
	}

	if (priv->i->flags & ALPS_OLDPROTO) {
		left = packet[2] & 0x08;
		right = packet[2] & 0x10;
		middle = 0;
		x = packet[1] | ((packet[0] & 0x07) << 7);
		y = packet[4] | ((packet[3] & 0x07) << 7);
		z = packet[5];
	} else {
		left = packet[3] & 1;
		right = packet[3] & 2;
		middle = packet[3] & 4;
		x = packet[1] | ((packet[2] & 0x78) << (7 - 3));
		y = packet[4] | ((packet[3] & 0x70) << (7 - 4));
		z = packet[5];
	}

	if (priv->i->flags & ALPS_FW_BK_1) {
		back = packet[2] & 4;
		forward = packet[0] & 0x10;
	}

	if (priv->i->flags & ALPS_FW_BK_2) {
		back = packet[3] & 4;
		forward = packet[2] & 4;
		if ((middle = forward && back))
			forward = back = 0;
	}

	ges = packet[2] & 1;
	fin = packet[2] & 2;

	input_report_key(dev, BTN_LEFT, left);
	input_report_key(dev, BTN_RIGHT, right);
	input_report_key(dev, BTN_MIDDLE, middle);

	if ((priv->i->flags & ALPS_DUALPOINT) && z == 127) {
		input_report_rel(dev2, REL_X,  (x > 383 ? (x - 768) : x));
		input_report_rel(dev2, REL_Y, -(y > 255 ? (y - 512) : y));
		input_sync(dev);
		input_sync(dev2);
		return;
	}

	/* Convert hardware tap to a reasonable Z value */
	if (ges && !fin) z = 40;

	/*
	 * A "tap and drag" operation is reported by the hardware as a transition
	 * from (!fin && ges) to (fin && ges). This should be translated to the
	 * sequence Z>0, Z==0, Z>0, so the Z==0 event has to be generated manually.
	 */
	if (ges && fin && !priv->prev_fin) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
		input_report_abs(dev, ABS_PRESSURE, 0);
		input_report_key(dev, BTN_TOOL_FINGER, 0);
		input_sync(dev);
	}
	priv->prev_fin = fin;

	if (z > 30) input_report_key(dev, BTN_TOUCH, 1);
	if (z < 25) input_report_key(dev, BTN_TOUCH, 0);

	if (z > 0) {
		input_report_abs(dev, ABS_X, x);
		input_report_abs(dev, ABS_Y, y);
	}

	input_report_abs(dev, ABS_PRESSURE, z);
	input_report_key(dev, BTN_TOOL_FINGER, z > 0);

	if (priv->i->flags & ALPS_WHEEL)
		input_report_rel(dev, REL_WHEEL, ((packet[2] << 1) & 0x08) - ((packet[0] >> 4) & 0x07));

	if (priv->i->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		input_report_key(dev, BTN_FORWARD, forward);
		input_report_key(dev, BTN_BACK, back);
	}

	input_sync(dev);
}

static psmouse_ret_t alps_process_byte(struct psmouse *psmouse, struct pt_regs *regs)
{
	struct alps_data *priv = psmouse->private;

	if ((psmouse->packet[0] & 0xc8) == 0x08) { /* PS/2 packet */
		if (psmouse->pktcnt == 3) {
			alps_process_packet(psmouse, regs);
			return PSMOUSE_FULL_PACKET;
		}
		return PSMOUSE_GOOD_DATA;
	}

	if ((psmouse->packet[0] & priv->i->mask0) != priv->i->byte0)
		return PSMOUSE_BAD_DATA;

	/* Bytes 2 - 6 should have 0 in the highest bit */
	if (psmouse->pktcnt >= 2 && psmouse->pktcnt <= 6 &&
	    (psmouse->packet[psmouse->pktcnt - 1] & 0x80))
		return PSMOUSE_BAD_DATA;

	if (psmouse->pktcnt == 6) {
		alps_process_packet(psmouse, regs);
		return PSMOUSE_FULL_PACKET;
	}

	return PSMOUSE_GOOD_DATA;
}

static struct alps_model_info *alps_get_model(struct psmouse *psmouse, int *version)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	unsigned char rates[] = { 0, 10, 20, 40, 60, 80, 100, 200 };
	unsigned char param[4];
	int i;

	/*
	 * First try "E6 report".
	 * ALPS should return 0,0,10 or 0,0,100
	 */
	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE11))
		return NULL;

	param[0] = param[1] = param[2] = 0xff;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return NULL;

	dbg("E6 report: %2.2x %2.2x %2.2x", param[0], param[1], param[2]);

	if (param[0] != 0 || param[1] != 0 || (param[2] != 10 && param[2] != 100))
		return NULL;

	/*
	 * Now try "E7 report". Allowed responses are in
	 * alps_model_data[].signature
	 */
	param[0] = 0;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_SETRES) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21) ||
	    ps2_command(ps2dev,  NULL, PSMOUSE_CMD_SETSCALE21))
		return NULL;

	param[0] = param[1] = param[2] = 0xff;
	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return NULL;

	dbg("E7 report: %2.2x %2.2x %2.2x", param[0], param[1], param[2]);

	for (i = 0; i < ARRAY_SIZE(rates) && param[2] != rates[i]; i++);
	*version = (param[0] << 8) | (param[1] << 4) | i;

	for (i = 0; i < ARRAY_SIZE(alps_model_data); i++)
		if (!memcmp(param, alps_model_data[i].signature, sizeof(alps_model_data[i].signature)))
			return alps_model_data + i;

	return NULL;
}

/*
 * For DualPoint devices select the device that should respond to
 * subsequent commands. It looks like glidepad is behind stickpointer,
 * I'd thought it would be other way around...
 */
static int alps_passthrough_mode(struct psmouse *psmouse, int enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETSCALE21 : PSMOUSE_CMD_SETSCALE11;

	if (ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, cmd) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE))
		return -1;

	/* we may get 3 more bytes, just ignore them */
	ps2_drain(ps2dev, 3, 100);

	return 0;
}

static int alps_absolute_mode(struct psmouse *psmouse)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* Try ALPS magic knock - 4 disable before enable */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_ENABLE))
		return -1;

	/*
	 * Switch mouse to poll (remote) mode so motion data will not
	 * get in our way
	 */
	return ps2_command(&psmouse->ps2dev, NULL, PSMOUSE_CMD_SETPOLL);
}

static int alps_get_status(struct psmouse *psmouse, char *param)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;

	/* Get status: 0xF5 0xF5 0xF5 0xE9 */
	if (ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO))
		return -1;

	dbg("Status: %2.2x %2.2x %2.2x", param[0], param[1], param[2]);

	return 0;
}

/*
 * Turn touchpad tapping on or off. The sequences are:
 * 0xE9 0xF5 0xF5 0xF3 0x0A to enable,
 * 0xE9 0xF5 0xF5 0xE8 0x00 to disable.
 * My guess that 0xE9 (GetInfo) is here as a sync point.
 * For models that also have stickpointer (DualPoints) its tapping
 * is controlled separately (0xE6 0xE6 0xE6 0xF3 0x14|0x0A) but
 * we don't fiddle with it.
 */
static int alps_tap_mode(struct psmouse *psmouse, int enable)
{
	struct ps2dev *ps2dev = &psmouse->ps2dev;
	int cmd = enable ? PSMOUSE_CMD_SETRATE : PSMOUSE_CMD_SETRES;
	unsigned char tap_arg = enable ? 0x0A : 0x00;
	unsigned char param[4];

	if (ps2_command(ps2dev, param, PSMOUSE_CMD_GETINFO) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, NULL, PSMOUSE_CMD_DISABLE) ||
	    ps2_command(ps2dev, &tap_arg, cmd))
		return -1;

	if (alps_get_status(psmouse, param))
		return -1;

	return 0;
}

static int alps_reconnect(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;
	int version;

	psmouse_reset(psmouse);

	if (!(priv->i = alps_get_model(psmouse, &version)))
		return -1;

	if ((priv->i->flags & ALPS_PASS) && alps_passthrough_mode(psmouse, 1))
		return -1;

	if (alps_tap_mode(psmouse, 1)) {
		printk(KERN_WARNING "alps.c: Failed to reenable hardware tapping\n");
		return -1;
	}

	if (alps_absolute_mode(psmouse)) {
		printk(KERN_ERR "alps.c: Failed to reenable absolute mode\n");
		return -1;
	}

	if ((priv->i->flags & ALPS_PASS) && alps_passthrough_mode(psmouse, 0))
		return -1;

	return 0;
}

static void alps_disconnect(struct psmouse *psmouse)
{
	struct alps_data *priv = psmouse->private;

	psmouse_reset(psmouse);
	input_unregister_device(priv->dev2);
	kfree(priv);
}

int alps_init(struct psmouse *psmouse)
{
	struct alps_data *priv;
	struct input_dev *dev1 = psmouse->dev, *dev2;
	int version;

	psmouse->private = priv = kzalloc(sizeof(struct alps_data), GFP_KERNEL);
	dev2 = input_allocate_device();
	if (!priv || !dev2)
		goto init_fail;

	priv->dev2 = dev2;

	if (!(priv->i = alps_get_model(psmouse, &version)))
		goto init_fail;

	if ((priv->i->flags & ALPS_PASS) && alps_passthrough_mode(psmouse, 1))
		goto init_fail;

	if (alps_tap_mode(psmouse, 1))
		printk(KERN_WARNING "alps.c: Failed to enable hardware tapping\n");

	if (alps_absolute_mode(psmouse)) {
		printk(KERN_ERR "alps.c: Failed to enable absolute mode\n");
		goto init_fail;
	}

	if ((priv->i->flags & ALPS_PASS) && alps_passthrough_mode(psmouse, 0))
		goto init_fail;

	dev1->evbit[LONG(EV_KEY)] |= BIT(EV_KEY);
	dev1->keybit[LONG(BTN_TOUCH)] |= BIT(BTN_TOUCH);
	dev1->keybit[LONG(BTN_TOOL_FINGER)] |= BIT(BTN_TOOL_FINGER);
	dev1->keybit[LONG(BTN_LEFT)] |= BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);

	dev1->evbit[LONG(EV_ABS)] |= BIT(EV_ABS);
	input_set_abs_params(dev1, ABS_X, 0, 1023, 0, 0);
	input_set_abs_params(dev1, ABS_Y, 0, 767, 0, 0);
	input_set_abs_params(dev1, ABS_PRESSURE, 0, 127, 0, 0);

	if (priv->i->flags & ALPS_WHEEL) {
		dev1->evbit[LONG(EV_REL)] |= BIT(EV_REL);
		dev1->relbit[LONG(REL_WHEEL)] |= BIT(REL_WHEEL);
	}

	if (priv->i->flags & (ALPS_FW_BK_1 | ALPS_FW_BK_2)) {
		dev1->keybit[LONG(BTN_FORWARD)] |= BIT(BTN_FORWARD);
		dev1->keybit[LONG(BTN_BACK)] |= BIT(BTN_BACK);
	}

	sprintf(priv->phys, "%s/input1", psmouse->ps2dev.serio->phys);
	dev2->phys = priv->phys;
	dev2->name = (priv->i->flags & ALPS_DUALPOINT) ? "DualPoint Stick" : "PS/2 Mouse";
	dev2->id.bustype = BUS_I8042;
	dev2->id.vendor  = 0x0002;
	dev2->id.product = PSMOUSE_ALPS;
	dev2->id.version = 0x0000;

	dev2->evbit[0] = BIT(EV_KEY) | BIT(EV_REL);
	dev2->relbit[LONG(REL_X)] |= BIT(REL_X) | BIT(REL_Y);
	dev2->keybit[LONG(BTN_LEFT)] |= BIT(BTN_LEFT) | BIT(BTN_MIDDLE) | BIT(BTN_RIGHT);

	input_register_device(priv->dev2);

	psmouse->protocol_handler = alps_process_byte;
	psmouse->disconnect = alps_disconnect;
	psmouse->reconnect = alps_reconnect;
	psmouse->pktsize = 6;

	return 0;

init_fail:
	input_free_device(dev2);
	kfree(priv);
	return -1;
}

int alps_detect(struct psmouse *psmouse, int set_properties)
{
	int version;
	struct alps_model_info *model;

	if (!(model = alps_get_model(psmouse, &version)))
		return -1;

	if (set_properties) {
		psmouse->vendor = "ALPS";
		psmouse->name = model->flags & ALPS_DUALPOINT ?
				"DualPoint TouchPad" : "GlidePoint";
		psmouse->model = version;
	}
	return 0;
}

