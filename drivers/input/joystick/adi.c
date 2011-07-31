/*
 *  Copyright (c) 1998-2005 Vojtech Pavlik
 */

/*
 * Logitech ADI joystick family driver for Linux
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Should you need to contact me, the author, you can do so either by
 * e-mail - mail your message to <vojtech@ucw.cz>, or by paper mail:
 * Vojtech Pavlik, Simunkova 1594, Prague 8, 182 00 Czech Republic
 */

#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/gameport.h>
#include <linux/init.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Logitech ADI joystick family driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Times, array sizes, flags, ids.
 */

#define ADI_MAX_START		200	/* Trigger to packet timeout [200us] */
#define ADI_MAX_STROBE		40	/* Single bit timeout [40us] */
#define ADI_INIT_DELAY		10	/* Delay after init packet [10ms] */
#define ADI_DATA_DELAY		4	/* Delay after data packet [4ms] */

#define ADI_MAX_LENGTH		256
#define ADI_MIN_LENGTH		8
#define ADI_MIN_LEN_LENGTH	10
#define ADI_MIN_ID_LENGTH	66
#define ADI_MAX_NAME_LENGTH	64
#define ADI_MAX_CNAME_LENGTH	16
#define ADI_MAX_PHYS_LENGTH	64

#define ADI_FLAG_HAT		0x04
#define ADI_FLAG_10BIT		0x08

#define ADI_ID_TPD		0x01
#define ADI_ID_WGP		0x06
#define ADI_ID_WGPE		0x08
#define ADI_ID_MAX		0x0a

/*
 * Names, buttons, axes ...
 */

static char *adi_names[] = {	"WingMan Extreme Digital", "ThunderPad Digital", "SideCar", "CyberMan 2",
				"WingMan Interceptor", "WingMan Formula", "WingMan GamePad",
				"WingMan Extreme Digital 3D", "WingMan GamePad Extreme",
				"WingMan GamePad USB", "Unknown Device %#x" };

static char adi_wmgpe_abs[] =	{ ABS_X, ABS_Y, ABS_HAT0X, ABS_HAT0Y };
static char adi_wmi_abs[] =	{ ABS_X, ABS_Y, ABS_THROTTLE, ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y };
static char adi_wmed3d_abs[] =	{ ABS_X, ABS_Y, ABS_THROTTLE, ABS_RZ, ABS_HAT0X, ABS_HAT0Y };
static char adi_cm2_abs[] =	{ ABS_X, ABS_Y, ABS_Z, ABS_RX, ABS_RY, ABS_RZ };
static char adi_wmf_abs[] =	{ ABS_WHEEL, ABS_GAS, ABS_BRAKE, ABS_HAT0X, ABS_HAT0Y, ABS_HAT1X, ABS_HAT1Y, ABS_HAT2X, ABS_HAT2Y };

static short adi_wmgpe_key[] =	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z,  BTN_TL, BTN_TR, BTN_START, BTN_MODE, BTN_SELECT };
static short adi_wmi_key[] =	{ BTN_TRIGGER,  BTN_TOP, BTN_THUMB, BTN_TOP2, BTN_BASE, BTN_BASE2, BTN_BASE3, BTN_BASE4, BTN_EXTRA };
static short adi_wmed3d_key[] =	{ BTN_TRIGGER, BTN_THUMB, BTN_THUMB2, BTN_TOP, BTN_TOP2, BTN_BASE, BTN_BASE2 };
static short adi_cm2_key[] =	{ BTN_1, BTN_2, BTN_3, BTN_4, BTN_5, BTN_6, BTN_7, BTN_8 };

static char* adi_abs[] = { adi_wmi_abs, adi_wmgpe_abs, adi_wmf_abs, adi_cm2_abs, adi_wmi_abs, adi_wmf_abs,
			   adi_wmgpe_abs, adi_wmed3d_abs, adi_wmgpe_abs, adi_wmgpe_abs, adi_wmi_abs };

static short* adi_key[] = { adi_wmi_key, adi_wmgpe_key, adi_cm2_key, adi_cm2_key, adi_wmi_key, adi_cm2_key,
			    adi_wmgpe_key, adi_wmed3d_key, adi_wmgpe_key, adi_wmgpe_key, adi_wmi_key };

/*
 * Hat to axis conversion arrays.
 */

static struct {
	int x;
	int y;
} adi_hat_to_axis[] = {{ 0, 0}, { 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

/*
 * Per-port information.
 */

struct adi {
	struct input_dev *dev;
	int length;
	int ret;
	int idx;
	unsigned char id;
	char buttons;
	char axes10;
	char axes8;
	signed char pad;
	char hats;
	char *abs;
	short *key;
	char name[ADI_MAX_NAME_LENGTH];
	char cname[ADI_MAX_CNAME_LENGTH];
	char phys[ADI_MAX_PHYS_LENGTH];
	unsigned char data[ADI_MAX_LENGTH];
};

struct adi_port {
	struct gameport *gameport;
	struct adi adi[2];
	int bad;
	int reads;
};

/*
 * adi_read_packet() reads a Logitech ADI packet.
 */

static void adi_read_packet(struct adi_port *port)
{
	struct adi *adi = port->adi;
	struct gameport *gameport = port->gameport;
	unsigned char u, v, w, x, z;
	int t[2], s[2], i;
	unsigned long flags;

	for (i = 0; i < 2; i++) {
		adi[i].ret = -1;
		t[i] = gameport_time(gameport, ADI_MAX_START);
		s[i] = 0;
	}

	local_irq_save(flags);

	gameport_trigger(gameport);
	v = z = gameport_read(gameport);

	do {
		u = v;
		w = u ^ (v = x = gameport_read(gameport));
		for (i = 0; i < 2; i++, w >>= 2, x >>= 2) {
			t[i]--;
			if ((w & 0x30) && s[i]) {
				if ((w & 0x30) < 0x30 && adi[i].ret < ADI_MAX_LENGTH && t[i] > 0) {
					adi[i].data[++adi[i].ret] = w;
					t[i] = gameport_time(gameport, ADI_MAX_STROBE);
				} else t[i] = 0;
			} else if (!(x & 0x30)) s[i] = 1;
		}
	} while (t[0] > 0 || t[1] > 0);

	local_irq_restore(flags);

	return;
}

/*
 * adi_move_bits() detects a possible 2-stream mode, and moves
 * the bits accordingly.
 */

static void adi_move_bits(struct adi_port *port, int length)
{
	int i;
	struct adi *adi = port->adi;

	adi[0].idx = adi[1].idx = 0;

	if (adi[0].ret <= 0 || adi[1].ret <= 0) return;
	if (adi[0].data[0] & 0x20 || ~adi[1].data[0] & 0x20) return;

	for (i = 1; i <= adi[1].ret; i++)
		adi[0].data[((length - 1) >> 1) + i + 1] = adi[1].data[i];

	adi[0].ret += adi[1].ret;
	adi[1].ret = -1;
}

/*
 * adi_get_bits() gathers bits from the data packet.
 */

static inline int adi_get_bits(struct adi *adi, int count)
{
	int bits = 0;
	int i;
	if ((adi->idx += count) > adi->ret) return 0;
	for (i = 0; i < count; i++)
		bits |= ((adi->data[adi->idx - i] >> 5) & 1) << i;
	return bits;
}

/*
 * adi_decode() decodes Logitech joystick data into input events.
 */

static int adi_decode(struct adi *adi)
{
	struct input_dev *dev = adi->dev;
	char *abs = adi->abs;
	short *key = adi->key;
	int i, t;

	if (adi->ret < adi->length || adi->id != (adi_get_bits(adi, 4) | (adi_get_bits(adi, 4) << 4)))
		return -1;

	for (i = 0; i < adi->axes10; i++)
		input_report_abs(dev, *abs++, adi_get_bits(adi, 10));

	for (i = 0; i < adi->axes8; i++)
		input_report_abs(dev, *abs++, adi_get_bits(adi, 8));

	for (i = 0; i < adi->buttons && i < 63; i++) {
		if (i == adi->pad) {
			t = adi_get_bits(adi, 4);
			input_report_abs(dev, *abs++, ((t >> 2) & 1) - ( t       & 1));
			input_report_abs(dev, *abs++, ((t >> 1) & 1) - ((t >> 3) & 1));
		}
		input_report_key(dev, *key++, adi_get_bits(adi, 1));
	}

	for (i = 0; i < adi->hats; i++) {
		if ((t = adi_get_bits(adi, 4)) > 8) t = 0;
		input_report_abs(dev, *abs++, adi_hat_to_axis[t].x);
		input_report_abs(dev, *abs++, adi_hat_to_axis[t].y);
	}

	for (i = 63; i < adi->buttons; i++)
		input_report_key(dev, *key++, adi_get_bits(adi, 1));

	input_sync(dev);

	return 0;
}

/*
 * adi_read() reads the data packet and decodes it.
 */

static int adi_read(struct adi_port *port)
{
	int i;
	int result = 0;

	adi_read_packet(port);
	adi_move_bits(port, port->adi[0].length);

	for (i = 0; i < 2; i++)
		if (port->adi[i].length)
			 result |= adi_decode(port->adi + i);

	return result;
}

/*
 * adi_poll() repeatedly polls the Logitech joysticks.
 */

static void adi_poll(struct gameport *gameport)
{
	struct adi_port *port = gameport_get_drvdata(gameport);

	port->bad -= adi_read(port);
	port->reads++;
}

/*
 * adi_open() is a callback from the input open routine.
 */

static int adi_open(struct input_dev *dev)
{
	struct adi_port *port = input_get_drvdata(dev);

	gameport_start_polling(port->gameport);
	return 0;
}

/*
 * adi_close() is a callback from the input close routine.
 */

static void adi_close(struct input_dev *dev)
{
	struct adi_port *port = input_get_drvdata(dev);

	gameport_stop_polling(port->gameport);
}

/*
 * adi_init_digital() sends a trigger & delay sequence
 * to reset and initialize a Logitech joystick into digital mode.
 */

static void adi_init_digital(struct gameport *gameport)
{
	int seq[] = { 4, -2, -3, 10, -6, -11, -7, -9, 11, 0 };
	int i;

	for (i = 0; seq[i]; i++) {
		gameport_trigger(gameport);
		if (seq[i] > 0)
			msleep(seq[i]);
		if (seq[i] < 0) {
			mdelay(-seq[i]);
			udelay(-seq[i]*14);	/* It looks like mdelay() is off by approx 1.4% */
		}
	}
}

static void adi_id_decode(struct adi *adi, struct adi_port *port)
{
	int i, t;

	if (adi->ret < ADI_MIN_ID_LENGTH) /* Minimum ID packet length */
		return;

	if (adi->ret < (t = adi_get_bits(adi, 10))) {
		printk(KERN_WARNING "adi: Short ID packet: reported: %d != read: %d\n", t, adi->ret);
		return;
	}

	adi->id = adi_get_bits(adi, 4) | (adi_get_bits(adi, 4) << 4);

	if ((t = adi_get_bits(adi, 4)) & ADI_FLAG_HAT) adi->hats++;

	adi->length = adi_get_bits(adi, 10);

	if (adi->length >= ADI_MAX_LENGTH || adi->length < ADI_MIN_LENGTH) {
		printk(KERN_WARNING "adi: Bad data packet length (%d).\n", adi->length);
		adi->length = 0;
		return;
	}

	adi->axes8 = adi_get_bits(adi, 4);
	adi->buttons = adi_get_bits(adi, 6);

	if (adi_get_bits(adi, 6) != 8 && adi->hats) {
		printk(KERN_WARNING "adi: Other than 8-dir POVs not supported yet.\n");
		adi->length = 0;
		return;
	}

	adi->buttons += adi_get_bits(adi, 6);
	adi->hats += adi_get_bits(adi, 4);

	i = adi_get_bits(adi, 4);

	if (t & ADI_FLAG_10BIT) {
		adi->axes10 = adi->axes8 - i;
		adi->axes8 = i;
	}

	t = adi_get_bits(adi, 4);

	for (i = 0; i < t; i++)
		adi->cname[i] = adi_get_bits(adi, 8);
	adi->cname[i] = 0;

	t = 8 + adi->buttons + adi->axes10 * 10 + adi->axes8 * 8 + adi->hats * 4;
	if (adi->length != t && adi->length != t + (t & 1)) {
		printk(KERN_WARNING "adi: Expected length %d != data length %d\n", t, adi->length);
		adi->length = 0;
		return;
	}

	switch (adi->id) {
		case ADI_ID_TPD:
			adi->pad = 4;
			adi->buttons -= 4;
			break;
		case ADI_ID_WGP:
			adi->pad = 0;
			adi->buttons -= 4;
			break;
		default:
			adi->pad = -1;
			break;
	}
}

static int adi_init_input(struct adi *adi, struct adi_port *port, int half)
{
	struct input_dev *input_dev;
	char buf[ADI_MAX_NAME_LENGTH];
	int i, t;

	adi->dev = input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	t = adi->id < ADI_ID_MAX ? adi->id : ADI_ID_MAX;

	snprintf(buf, ADI_MAX_PHYS_LENGTH, adi_names[t], adi->id);
	snprintf(adi->name, ADI_MAX_NAME_LENGTH, "Logitech %s [%s]", buf, adi->cname);
	snprintf(adi->phys, ADI_MAX_PHYS_LENGTH, "%s/input%d", port->gameport->phys, half);

	adi->abs = adi_abs[t];
	adi->key = adi_key[t];

	input_dev->name = adi->name;
	input_dev->phys = adi->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_LOGITECH;
	input_dev->id.product = adi->id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &port->gameport->dev;

	input_set_drvdata(input_dev, port);

	input_dev->open = adi_open;
	input_dev->close = adi_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = 0; i < adi->axes10 + adi->axes8 + (adi->hats + (adi->pad != -1)) * 2; i++)
		set_bit(adi->abs[i], input_dev->absbit);

	for (i = 0; i < adi->buttons; i++)
		set_bit(adi->key[i], input_dev->keybit);

	return 0;
}

static void adi_init_center(struct adi *adi)
{
	int i, t, x;

	if (!adi->length)
		return;

	for (i = 0; i < adi->axes10 + adi->axes8 + (adi->hats + (adi->pad != -1)) * 2; i++) {

		t = adi->abs[i];
		x = input_abs_get_val(adi->dev, t);

		if (t == ABS_THROTTLE || t == ABS_RUDDER || adi->id == ADI_ID_WGPE)
			x = i < adi->axes10 ? 512 : 128;

		if (i < adi->axes10)
			input_set_abs_params(adi->dev, t, 64, x * 2 - 64, 2, 16);
		else if (i < adi->axes10 + adi->axes8)
			input_set_abs_params(adi->dev, t, 48, x * 2 - 48, 1, 16);
		else
			input_set_abs_params(adi->dev, t, -1, 1, 0, 0);
	}
}

/*
 * adi_connect() probes for Logitech ADI joysticks.
 */

static int adi_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct adi_port *port;
	int i;
	int err;

	port = kzalloc(sizeof(struct adi_port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	port->gameport = gameport;

	gameport_set_drvdata(gameport, port);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	adi_init_digital(gameport);
	adi_read_packet(port);

	if (port->adi[0].ret >= ADI_MIN_LEN_LENGTH)
		adi_move_bits(port, adi_get_bits(port->adi, 10));

	for (i = 0; i < 2; i++) {
		adi_id_decode(port->adi + i, port);

		if (!port->adi[i].length)
			continue;

		err = adi_init_input(port->adi + i, port, i);
		if (err)
			goto fail2;
	}

	if (!port->adi[0].length && !port->adi[1].length) {
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, adi_poll);
	gameport_set_poll_interval(gameport, 20);

	msleep(ADI_INIT_DELAY);
	if (adi_read(port)) {
		msleep(ADI_DATA_DELAY);
		adi_read(port);
	}

	for (i = 0; i < 2; i++)
		if (port->adi[i].length > 0) {
			adi_init_center(port->adi + i);
			err = input_register_device(port->adi[i].dev);
			if (err)
				goto fail3;
		}

	return 0;

 fail3: while (--i >= 0) {
		if (port->adi[i].length > 0) {
			input_unregister_device(port->adi[i].dev);
			port->adi[i].dev = NULL;
		}
	}
 fail2:	for (i = 0; i < 2; i++)
		if (port->adi[i].dev)
			input_free_device(port->adi[i].dev);
	gameport_close(gameport);
 fail1:	gameport_set_drvdata(gameport, NULL);
	kfree(port);
	return err;
}

static void adi_disconnect(struct gameport *gameport)
{
	int i;
	struct adi_port *port = gameport_get_drvdata(gameport);

	for (i = 0; i < 2; i++)
		if (port->adi[i].length > 0)
			input_unregister_device(port->adi[i].dev);
	gameport_close(gameport);
	gameport_set_drvdata(gameport, NULL);
	kfree(port);
}

/*
 * The gameport device structure.
 */

static struct gameport_driver adi_drv = {
	.driver		= {
		.name	= "adi",
	},
	.description	= DRIVER_DESC,
	.connect	= adi_connect,
	.disconnect	= adi_disconnect,
};

static int __init adi_init(void)
{
	return gameport_register_driver(&adi_drv);
}

static void __exit adi_exit(void)
{
	gameport_unregister_driver(&adi_drv);
}

module_init(adi_init);
module_exit(adi_exit);
