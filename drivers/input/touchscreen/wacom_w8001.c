/*
 * Wacom W8001 penabled serial touchscreen driver
 *
 * Copyright (c) 2008 Jaya Kumar
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file COPYING in the main directory of this archive for
 * more details.
 *
 * Layout based on Elo serial touchscreen driver by Vojtech Pavlik
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/init.h>
#include <linux/ctype.h>

#define DRIVER_DESC	"Wacom W8001 serial touchscreen driver"

MODULE_AUTHOR("Jaya Kumar <jayakumar.lkml@gmail.com>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define W8001_MAX_LENGTH	11
#define W8001_PACKET_LEN	11
#define W8001_LEAD_MASK 0x80
#define W8001_LEAD_BYTE 0x80
#define W8001_TAB_MASK 0x40
#define W8001_TAB_BYTE 0x40

#define W8001_QUERY_PACKET 0x20

struct w8001_coord {
	u8 rdy;
	u8 tsw;
	u8 f1;
	u8 f2;
	u16 x;
	u16 y;
	u16 pen_pressure;
	u8 tilt_x;
	u8 tilt_y;
};

/*
 * Per-touchscreen data.
 */

struct w8001 {
	struct input_dev *dev;
	struct serio *serio;
	struct mutex cmd_mutex;
	struct completion cmd_done;
	int id;
	int idx;
	unsigned char expected_packet;
	unsigned char data[W8001_MAX_LENGTH];
	unsigned char response[W8001_PACKET_LEN];
	char phys[32];
};

static int parse_data(u8 *data, struct w8001_coord *coord)
{
	coord->rdy = data[0] & 0x20;
	coord->tsw = data[0] & 0x01;
	coord->f1 = data[0] & 0x02;
	coord->f2 = data[0] & 0x04;

	coord->x = (data[1] & 0x7F) << 9;
	coord->x |= (data[2] & 0x7F) << 2;
	coord->x |= (data[6] & 0x60) >> 5;

	coord->y = (data[3] & 0x7F) << 9;
	coord->y |= (data[4] & 0x7F) << 2;
	coord->y |= (data[6] & 0x18) >> 3;

	coord->pen_pressure = data[5] & 0x7F;
	coord->pen_pressure |= (data[6] & 0x07) << 7 ;

	coord->tilt_x = data[7] & 0x7F;
	coord->tilt_y = data[8] & 0x7F;

	return 0;
}

static void w8001_process_data(struct w8001 *w8001, unsigned char data)
{
	struct input_dev *dev = w8001->dev;
	u8 tmp;
	struct w8001_coord coord;

	w8001->data[w8001->idx] = data;
	switch (w8001->idx++) {
	case 0:
		if ((data & W8001_LEAD_MASK) != W8001_LEAD_BYTE) {
			pr_debug("w8001: unsynchronized data: 0x%02x\n", data);
			w8001->idx = 0;
		}
		break;
	case 8:
		tmp = w8001->data[0] & W8001_TAB_MASK;
		if (unlikely(tmp == W8001_TAB_BYTE))
			break;
		w8001->idx = 0;
		memset(&coord, 0, sizeof(coord));
		parse_data(w8001->data, &coord);
		input_report_abs(dev, ABS_X, coord.x);
		input_report_abs(dev, ABS_Y, coord.y);
		input_report_abs(dev, ABS_PRESSURE, coord.pen_pressure);
		input_report_key(dev, BTN_TOUCH, coord.tsw);
		input_sync(dev);
		break;
	case 10:
		w8001->idx = 0;
		memcpy(w8001->response, &w8001->data, W8001_PACKET_LEN);
		w8001->expected_packet = W8001_QUERY_PACKET;
		complete(&w8001->cmd_done);
		break;
	}
}


static irqreturn_t w8001_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct w8001 *w8001 = serio_get_drvdata(serio);

	w8001_process_data(w8001, data);

	return IRQ_HANDLED;
}

static int w8001_async_command(struct w8001 *w8001, unsigned char *packet,
					int len)
{
	int rc = -1;
	int i;

	mutex_lock(&w8001->cmd_mutex);

	for (i = 0; i < len; i++) {
		if (serio_write(w8001->serio, packet[i]))
			goto out;
	}
	rc = 0;

out:
	mutex_unlock(&w8001->cmd_mutex);
	return rc;
}

static int w8001_command(struct w8001 *w8001, unsigned char *packet, int len)
{
	int rc = -1;
	int i;

	mutex_lock(&w8001->cmd_mutex);

	serio_pause_rx(w8001->serio);
	init_completion(&w8001->cmd_done);
	serio_continue_rx(w8001->serio);

	for (i = 0; i < len; i++) {
		if (serio_write(w8001->serio, packet[i]))
			goto out;
	}

	wait_for_completion_timeout(&w8001->cmd_done, HZ);

	if (w8001->expected_packet == W8001_QUERY_PACKET) {
		/* We are back in reporting mode, the query was ACKed */
		memcpy(packet, w8001->response, W8001_PACKET_LEN);
		rc = 0;
	}

out:
	mutex_unlock(&w8001->cmd_mutex);
	return rc;
}

static int w8001_setup(struct w8001 *w8001)
{
	struct w8001_coord coord;
	struct input_dev *dev = w8001->dev;
	unsigned char start[1] = { '1' };
	unsigned char query[11] = { '*' };

	if (w8001_command(w8001, query, 1))
		return -1;

	memset(&coord, 0, sizeof(coord));
	parse_data(query, &coord);

	input_set_abs_params(dev, ABS_X, 0, coord.x, 0, 0);
	input_set_abs_params(dev, ABS_Y, 0, coord.y, 0, 0);
	input_set_abs_params(dev, ABS_PRESSURE, 0, coord.pen_pressure, 0, 0);
	input_set_abs_params(dev, ABS_TILT_X, 0, coord.tilt_x, 0, 0);
	input_set_abs_params(dev, ABS_TILT_Y, 0, coord.tilt_y, 0, 0);

	if (w8001_async_command(w8001, start, 1))
		return -1;

	return 0;
}

/*
 * w8001_disconnect() is the opposite of w8001_connect()
 */

static void w8001_disconnect(struct serio *serio)
{
	struct w8001 *w8001 = serio_get_drvdata(serio);

	input_get_device(w8001->dev);
	input_unregister_device(w8001->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(w8001->dev);
	kfree(w8001);
}

/*
 * w8001_connect() is the routine that is called when someone adds a
 * new serio device that supports the w8001 protocol and registers it as
 * an input device.
 */

static int w8001_connect(struct serio *serio, struct serio_driver *drv)
{
	struct w8001 *w8001;
	struct input_dev *input_dev;
	int err;

	w8001 = kzalloc(sizeof(struct w8001), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!w8001 || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	w8001->serio = serio;
	w8001->id = serio->id.id;
	w8001->dev = input_dev;
	mutex_init(&w8001->cmd_mutex);
	init_completion(&w8001->cmd_done);
	snprintf(w8001->phys, sizeof(w8001->phys), "%s/input0", serio->phys);

	input_dev->name = "Wacom W8001 Penabled Serial TouchScreen";
	input_dev->phys = w8001->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_W8001;
	input_dev->id.product = w8001->id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	serio_set_drvdata(serio, w8001);
	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	if (w8001_setup(w8001))
		goto fail3;

	err = input_register_device(w8001->dev);
	if (err)
		goto fail3;

	return 0;

fail3:
	serio_close(serio);
fail2:
	serio_set_drvdata(serio, NULL);
fail1:
	input_free_device(input_dev);
	kfree(w8001);
	return err;
}

static struct serio_device_id w8001_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_W8001,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, w8001_serio_ids);

static struct serio_driver w8001_drv = {
	.driver		= {
		.name	= "w8001",
	},
	.description	= DRIVER_DESC,
	.id_table	= w8001_serio_ids,
	.interrupt	= w8001_interrupt,
	.connect	= w8001_connect,
	.disconnect	= w8001_disconnect,
};

static int __init w8001_init(void)
{
	return serio_register_driver(&w8001_drv);
}

static void __exit w8001_exit(void)
{
	serio_unregister_driver(&w8001_drv);
}

module_init(w8001_init);
module_exit(w8001_exit);
