// SPDX-License-Identifier: GPL-2.0-only
/*
 * Elo serial touchscreen driver
 *
 * Copyright (c) 2004 Vojtech Pavlik
 */


/*
 * This driver can handle serial Elo touchscreens using either the Elo standard
 * 'E271-2210' 10-byte protocol, Elo legacy 'E281A-4002' 6-byte protocol, Elo
 * legacy 'E271-140' 4-byte protocol and Elo legacy 'E261-280' 3-byte protocol.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/serio.h>
#include <linux/ctype.h>

#define DRIVER_DESC	"Elo serial touchscreen driver"

MODULE_AUTHOR("Vojtech Pavlik <vojtech@ucw.cz>");
MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_LICENSE("GPL");

/*
 * Definitions & global arrays.
 */

#define ELO_MAX_LENGTH		10

#define ELO10_PACKET_LEN	8
#define ELO10_TOUCH		0x03
#define ELO10_PRESSURE		0x80

#define ELO10_LEAD_BYTE		'U'

#define ELO10_ID_CMD		'i'

#define ELO10_TOUCH_PACKET	'T'
#define ELO10_ACK_PACKET	'A'
#define ELI10_ID_PACKET		'I'

/*
 * Per-touchscreen data.
 */

struct elo {
	struct input_dev *dev;
	struct serio *serio;
	struct mutex cmd_mutex;
	struct completion cmd_done;
	int id;
	int idx;
	unsigned char expected_packet;
	unsigned char csum;
	unsigned char data[ELO_MAX_LENGTH];
	unsigned char response[ELO10_PACKET_LEN];
	char phys[32];
};

static void elo_process_data_10(struct elo *elo, unsigned char data)
{
	struct input_dev *dev = elo->dev;

	elo->data[elo->idx] = data;

	switch (elo->idx++) {
	case 0:
		elo->csum = 0xaa;
		if (data != ELO10_LEAD_BYTE) {
			dev_dbg(&elo->serio->dev,
				"unsynchronized data: 0x%02x\n", data);
			elo->idx = 0;
		}
		break;

	case 9:
		elo->idx = 0;
		if (data != elo->csum) {
			dev_dbg(&elo->serio->dev,
				"bad checksum: 0x%02x, expected 0x%02x\n",
				 data, elo->csum);
			break;
		}
		if (elo->data[1] != elo->expected_packet) {
			if (elo->data[1] != ELO10_TOUCH_PACKET)
				dev_dbg(&elo->serio->dev,
					"unexpected packet: 0x%02x\n",
					 elo->data[1]);
			break;
		}
		if (likely(elo->data[1] == ELO10_TOUCH_PACKET)) {
			input_report_abs(dev, ABS_X, (elo->data[4] << 8) | elo->data[3]);
			input_report_abs(dev, ABS_Y, (elo->data[6] << 8) | elo->data[5]);
			if (elo->data[2] & ELO10_PRESSURE)
				input_report_abs(dev, ABS_PRESSURE,
						(elo->data[8] << 8) | elo->data[7]);
			input_report_key(dev, BTN_TOUCH, elo->data[2] & ELO10_TOUCH);
			input_sync(dev);
		} else if (elo->data[1] == ELO10_ACK_PACKET) {
			if (elo->data[2] == '0')
				elo->expected_packet = ELO10_TOUCH_PACKET;
			complete(&elo->cmd_done);
		} else {
			memcpy(elo->response, &elo->data[1], ELO10_PACKET_LEN);
			elo->expected_packet = ELO10_ACK_PACKET;
		}
		break;
	}
	elo->csum += data;
}

static void elo_process_data_6(struct elo *elo, unsigned char data)
{
	struct input_dev *dev = elo->dev;

	elo->data[elo->idx] = data;

	switch (elo->idx++) {

	case 0:
		if ((data & 0xc0) != 0xc0)
			elo->idx = 0;
		break;

	case 1:
		if ((data & 0xc0) != 0x80)
			elo->idx = 0;
		break;

	case 2:
		if ((data & 0xc0) != 0x40)
			elo->idx = 0;
		break;

	case 3:
		if (data & 0xc0) {
			elo->idx = 0;
			break;
		}

		input_report_abs(dev, ABS_X, ((elo->data[0] & 0x3f) << 6) | (elo->data[1] & 0x3f));
		input_report_abs(dev, ABS_Y, ((elo->data[2] & 0x3f) << 6) | (elo->data[3] & 0x3f));

		if (elo->id == 2) {
			input_report_key(dev, BTN_TOUCH, 1);
			input_sync(dev);
			elo->idx = 0;
		}

		break;

	case 4:
		if (data) {
			input_sync(dev);
			elo->idx = 0;
		}
		break;

	case 5:
		if ((data & 0xf0) == 0) {
			input_report_abs(dev, ABS_PRESSURE, elo->data[5]);
			input_report_key(dev, BTN_TOUCH, !!elo->data[5]);
		}
		input_sync(dev);
		elo->idx = 0;
		break;
	}
}

static void elo_process_data_3(struct elo *elo, unsigned char data)
{
	struct input_dev *dev = elo->dev;

	elo->data[elo->idx] = data;

	switch (elo->idx++) {

	case 0:
		if ((data & 0x7f) != 0x01)
			elo->idx = 0;
		break;
	case 2:
		input_report_key(dev, BTN_TOUCH, !(elo->data[1] & 0x80));
		input_report_abs(dev, ABS_X, elo->data[1]);
		input_report_abs(dev, ABS_Y, elo->data[2]);
		input_sync(dev);
		elo->idx = 0;
		break;
	}
}

static irqreturn_t elo_interrupt(struct serio *serio,
		unsigned char data, unsigned int flags)
{
	struct elo *elo = serio_get_drvdata(serio);

	switch (elo->id) {
	case 0:
		elo_process_data_10(elo, data);
		break;

	case 1:
	case 2:
		elo_process_data_6(elo, data);
		break;

	case 3:
		elo_process_data_3(elo, data);
		break;
	}

	return IRQ_HANDLED;
}

static int elo_command_10(struct elo *elo, unsigned char *packet)
{
	int rc = -1;
	int i;
	unsigned char csum = 0xaa + ELO10_LEAD_BYTE;

	mutex_lock(&elo->cmd_mutex);

	serio_pause_rx(elo->serio);
	elo->expected_packet = toupper(packet[0]);
	init_completion(&elo->cmd_done);
	serio_continue_rx(elo->serio);

	if (serio_write(elo->serio, ELO10_LEAD_BYTE))
		goto out;

	for (i = 0; i < ELO10_PACKET_LEN; i++) {
		csum += packet[i];
		if (serio_write(elo->serio, packet[i]))
			goto out;
	}

	if (serio_write(elo->serio, csum))
		goto out;

	wait_for_completion_timeout(&elo->cmd_done, HZ);

	if (elo->expected_packet == ELO10_TOUCH_PACKET) {
		/* We are back in reporting mode, the command was ACKed */
		memcpy(packet, elo->response, ELO10_PACKET_LEN);
		rc = 0;
	}

 out:
	mutex_unlock(&elo->cmd_mutex);
	return rc;
}

static int elo_setup_10(struct elo *elo)
{
	static const char *elo_types[] = { "Accu", "Dura", "Intelli", "Carroll" };
	struct input_dev *dev = elo->dev;
	unsigned char packet[ELO10_PACKET_LEN] = { ELO10_ID_CMD };

	if (elo_command_10(elo, packet))
		return -1;

	dev->id.version = (packet[5] << 8) | packet[4];

	input_set_abs_params(dev, ABS_X, 96, 4000, 0, 0);
	input_set_abs_params(dev, ABS_Y, 96, 4000, 0, 0);
	if (packet[3] & ELO10_PRESSURE)
		input_set_abs_params(dev, ABS_PRESSURE, 0, 255, 0, 0);

	dev_info(&elo->serio->dev,
		 "%sTouch touchscreen, fw: %02x.%02x, features: 0x%02x, controller: 0x%02x\n",
		 elo_types[(packet[1] -'0') & 0x03],
		 packet[5], packet[4], packet[3], packet[7]);

	return 0;
}

/*
 * elo_disconnect() is the opposite of elo_connect()
 */

static void elo_disconnect(struct serio *serio)
{
	struct elo *elo = serio_get_drvdata(serio);

	input_get_device(elo->dev);
	input_unregister_device(elo->dev);
	serio_close(serio);
	serio_set_drvdata(serio, NULL);
	input_put_device(elo->dev);
	kfree(elo);
}

/*
 * elo_connect() is the routine that is called when someone adds a
 * new serio device that supports Gunze protocol and registers it as
 * an input device.
 */

static int elo_connect(struct serio *serio, struct serio_driver *drv)
{
	struct elo *elo;
	struct input_dev *input_dev;
	int err;

	elo = kzalloc(sizeof(struct elo), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!elo || !input_dev) {
		err = -ENOMEM;
		goto fail1;
	}

	elo->serio = serio;
	elo->id = serio->id.id;
	elo->dev = input_dev;
	elo->expected_packet = ELO10_TOUCH_PACKET;
	mutex_init(&elo->cmd_mutex);
	init_completion(&elo->cmd_done);
	snprintf(elo->phys, sizeof(elo->phys), "%s/input0", serio->phys);

	input_dev->name = "Elo Serial TouchScreen";
	input_dev->phys = elo->phys;
	input_dev->id.bustype = BUS_RS232;
	input_dev->id.vendor = SERIO_ELO;
	input_dev->id.product = elo->id;
	input_dev->id.version = 0x0100;
	input_dev->dev.parent = &serio->dev;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	serio_set_drvdata(serio, elo);
	err = serio_open(serio, drv);
	if (err)
		goto fail2;

	switch (elo->id) {

	case 0: /* 10-byte protocol */
		if (elo_setup_10(elo))
			goto fail3;

		break;

	case 1: /* 6-byte protocol */
		input_set_abs_params(input_dev, ABS_PRESSURE, 0, 15, 0, 0);
		fallthrough;

	case 2: /* 4-byte protocol */
		input_set_abs_params(input_dev, ABS_X, 96, 4000, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 96, 4000, 0, 0);
		break;

	case 3: /* 3-byte protocol */
		input_set_abs_params(input_dev, ABS_X, 0, 255, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, 255, 0, 0);
		break;
	}

	err = input_register_device(elo->dev);
	if (err)
		goto fail3;

	return 0;

 fail3: serio_close(serio);
 fail2:	serio_set_drvdata(serio, NULL);
 fail1:	input_free_device(input_dev);
	kfree(elo);
	return err;
}

/*
 * The serio driver structure.
 */

static const struct serio_device_id elo_serio_ids[] = {
	{
		.type	= SERIO_RS232,
		.proto	= SERIO_ELO,
		.id	= SERIO_ANY,
		.extra	= SERIO_ANY,
	},
	{ 0 }
};

MODULE_DEVICE_TABLE(serio, elo_serio_ids);

static struct serio_driver elo_drv = {
	.driver		= {
		.name	= "elo",
	},
	.description	= DRIVER_DESC,
	.id_table	= elo_serio_ids,
	.interrupt	= elo_interrupt,
	.connect	= elo_connect,
	.disconnect	= elo_disconnect,
};

module_serio_driver(elo_drv);
