/*
 *  Copyright (c) 2001 Vojtech Pavlik
 */

/*
 * Guillemot Digital Interface Protocol driver for Linux
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

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/gameport.h>
#include <linux/input.h>
#include <linux/jiffies.h>

#define DRIVER_DESC	"Guillemot Digital joystick driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

#define GUILLEMOT_MAX_START	600	/* 600 us */
#define GUILLEMOT_MAX_STROBE	60	/* 60 us */
#define GUILLEMOT_MAX_LENGTH	17	/* 17 bytes */

static short guillemot_abs_pad[] =
	{ ABS_X, ABS_Y, ABS_THROTTLE, ABS_RUDDER, -1 };

static short guillemot_btn_pad[] =
	{ BTN_A, BTN_B, BTN_C, BTN_X, BTN_Y, BTN_Z, BTN_TL, BTN_TR, BTN_MODE, BTN_SELECT, -1 };

static struct {
        int x;
        int y;
} guillemot_hat_to_axis[16] = {{ 0,-1}, { 1,-1}, { 1, 0}, { 1, 1}, { 0, 1}, {-1, 1}, {-1, 0}, {-1,-1}};

struct guillemot_type {
	unsigned char id;
	short *abs;
	short *btn;
	int hat;
	char *name;
};

struct guillemot {
	struct gameport *gameport;
	struct input_dev *dev;
	int bads;
	int reads;
	struct guillemot_type *type;
	unsigned char length;
	char phys[32];
};

static struct guillemot_type guillemot_type[] = {
	{ 0x00, guillemot_abs_pad, guillemot_btn_pad, 1, "Guillemot Pad" },
	{ 0 }};

/*
 * guillemot_read_packet() reads Guillemot joystick data.
 */

static int guillemot_read_packet(struct gameport *gameport, u8 *data)
{
	unsigned long flags;
	unsigned char u, v;
	unsigned int t, s;
	int i;

	for (i = 0; i < GUILLEMOT_MAX_LENGTH; i++)
		data[i] = 0;

	i = 0;
	t = gameport_time(gameport, GUILLEMOT_MAX_START);
	s = gameport_time(gameport, GUILLEMOT_MAX_STROBE);

	local_irq_save(flags);
	gameport_trigger(gameport);
	v = gameport_read(gameport);

	while (t > 0 && i < GUILLEMOT_MAX_LENGTH * 8) {
		t--;
		u = v; v = gameport_read(gameport);
		if (v & ~u & 0x10) {
			data[i >> 3] |= ((v >> 5) & 1) << (i & 7);
			i++;
			t = s;
		}
	}

	local_irq_restore(flags);

	return i;
}

/*
 * guillemot_poll() reads and analyzes Guillemot joystick data.
 */

static void guillemot_poll(struct gameport *gameport)
{
	struct guillemot *guillemot = gameport_get_drvdata(gameport);
	struct input_dev *dev = guillemot->dev;
	u8 data[GUILLEMOT_MAX_LENGTH];
	int i;

	guillemot->reads++;

	if (guillemot_read_packet(guillemot->gameport, data) != GUILLEMOT_MAX_LENGTH * 8 ||
		data[0] != 0x55 || data[16] != 0xaa) {
		guillemot->bads++;
	} else {

		for (i = 0; i < 6 && guillemot->type->abs[i] >= 0; i++)
			input_report_abs(dev, guillemot->type->abs[i], data[i + 5]);

		if (guillemot->type->hat) {
			input_report_abs(dev, ABS_HAT0X, guillemot_hat_to_axis[data[4] >> 4].x);
			input_report_abs(dev, ABS_HAT0Y, guillemot_hat_to_axis[data[4] >> 4].y);
		}

		for (i = 0; i < 16 && guillemot->type->btn[i] >= 0; i++)
			input_report_key(dev, guillemot->type->btn[i], (data[2 + (i >> 3)] >> (i & 7)) & 1);
	}

	input_sync(dev);
}

/*
 * guillemot_open() is a callback from the input open routine.
 */

static int guillemot_open(struct input_dev *dev)
{
	struct guillemot *guillemot = input_get_drvdata(dev);

	gameport_start_polling(guillemot->gameport);
	return 0;
}

/*
 * guillemot_close() is a callback from the input close routine.
 */

static void guillemot_close(struct input_dev *dev)
{
	struct guillemot *guillemot = input_get_drvdata(dev);

	gameport_stop_polling(guillemot->gameport);
}

/*
 * guillemot_connect() probes for Guillemot joysticks.
 */

static int guillemot_connect(struct gameport *gameport, struct gameport_driver *drv)
{
	struct guillemot *guillemot;
	struct input_dev *input_dev;
	u8 data[GUILLEMOT_MAX_LENGTH];
	int i, t;
	int err;

	guillemot = kzalloc(sizeof(struct guillemot), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!guillemot || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	guillemot->gameport = gameport;
	guillemot->dev = input_dev;

	gameport_set_drvdata(gameport, guillemot);

	err = gameport_open(gameport, drv, GAMEPORT_MODE_RAW);
	if (err)
		goto fail1;

	i = guillemot_read_packet(gameport, data);

	if (i != GUILLEMOT_MAX_LENGTH * 8 || data[0] != 0x55 || data[16] != 0xaa) {
		err = -ENODEV;
		goto fail2;
	}

	for (i = 0; guillemot_type[i].name; i++)
		if (guillemot_type[i].id == data[11])
			break;

	if (!guillemot_type[i].name) {
		printk(KERN_WARNING "guillemot.c: Unknown joystick on %s. [ %02x%02x:%04x, ver %d.%02d ]\n",
			gameport->phys, data[12], data[13], data[11], data[14], data[15]);
		err = -ENODEV;
		goto fail2;
	}

	gameport_set_poll_handler(gameport, guillemot_poll);
	gameport_set_poll_interval(gameport, 20);

	snprintf(guillemot->phys, sizeof(guillemot->phys), "%s/input0", gameport->phys);
	guillemot->type = guillemot_type + i;

	input_dev->name = guillemot_type[i].name;
	input_dev->phys = guillemot->phys;
	input_dev->id.bustype = BUS_GAMEPORT;
	input_dev->id.vendor = GAMEPORT_ID_VENDOR_GUILLEMOT;
	input_dev->id.product = guillemot_type[i].id;
	input_dev->id.version = (int)data[14] << 8 | data[15];
	input_dev->dev.parent = &gameport->dev;

	input_set_drvdata(input_dev, guillemot);

	input_dev->open = guillemot_open;
	input_dev->close = guillemot_close;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);

	for (i = 0; (t = guillemot->type->abs[i]) >= 0; i++)
		input_set_abs_params(input_dev, t, 0, 255, 0, 0);

	if (guillemot->type->hat) {
		input_set_abs_params(input_dev, ABS_HAT0X, -1, 1, 0, 0);
		input_set_abs_params(input_dev, ABS_HAT0Y, -1, 1, 0, 0);
	}

	for (i = 0; (t = guillemot->type->btn[i]) >= 0; i++)
		set_bit(t, input_dev->keybit);

	err = input_register_device(guillemot->dev);
	if (err)
		goto fail2;

	return 0;

fail2:	gameport_close(gameport);
fail1:  gameport_set_drvdata(gameport, NULL);
	input_free_device(input_dev);
	kfree(guillemot);
	return err;
}

static void guillemot_disconnect(struct gameport *gameport)
{
	struct guillemot *guillemot = gameport_get_drvdata(gameport);

	printk(KERN_INFO "guillemot.c: Failed %d reads out of %d on %s\n", guillemot->reads, guillemot->bads, guillemot->phys);
	input_unregister_device(guillemot->dev);
	gameport_close(gameport);
	kfree(guillemot);
}

static struct gameport_driver guillemot_drv = {
	.driver		= {
		.name	= "guillemot",
	},
	.description	= DRIVER_DESC,
	.connect	= guillemot_connect,
	.disconnect	= guillemot_disconnect,
};

module_gameport_driver(guillemot_drv);
