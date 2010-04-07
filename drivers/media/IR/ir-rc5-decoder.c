/* ir-rc5-decoder.c - handle RC-5 IR Pulse/Space protocol
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

/*
 * This code only handles 14 bits RC-5 protocols. There are other variants
 * that use a different number of bits. This is currently unsupported
 * It considers a carrier of 36 kHz, with a total of 14 bits, where
 * the first two bits are start bits, and a third one is a filing bit
 */

#include <media/ir-core.h>

static unsigned int ir_rc5_remote_gap = 888888;

#define RC5_NBITS		14
#define RC5_BIT			(ir_rc5_remote_gap * 2)
#define RC5_DURATION		(ir_rc5_remote_gap * RC5_NBITS)

/* Used to register rc5_decoder clients */
static LIST_HEAD(decoder_list);
static DEFINE_SPINLOCK(decoder_lock);

enum rc5_state {
	STATE_INACTIVE,
	STATE_MARKSPACE,
	STATE_TRAILER,
};

struct rc5_code {
	u8	address;
	u8	command;
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum rc5_state		state;
	struct rc5_code		rc5_code;
	unsigned		code, elapsed, last_bit, last_code;
};


/**
 * get_decoder_data()	- gets decoder data
 * @input_dev:	input device
 *
 * Returns the struct decoder_data that corresponds to a device
 */

static struct decoder_data *get_decoder_data(struct  ir_input_dev *ir_dev)
{
	struct decoder_data *data = NULL;

	spin_lock(&decoder_lock);
	list_for_each_entry(data, &decoder_list, list) {
		if (data->ir_dev == ir_dev)
			break;
	}
	spin_unlock(&decoder_lock);
	return data;
}

static ssize_t store_enabled(struct device *d,
			     struct device_attribute *mattr,
			     const char *buf,
			     size_t len)
{
	unsigned long value;
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	struct decoder_data *data = get_decoder_data(ir_dev);

	if (!data)
		return -EINVAL;

	if (strict_strtoul(buf, 10, &value) || value > 1)
		return -EINVAL;

	data->enabled = value;

	return len;
}

static ssize_t show_enabled(struct device *d,
			     struct device_attribute *mattr, char *buf)
{
	struct ir_input_dev *ir_dev = dev_get_drvdata(d);
	struct decoder_data *data = get_decoder_data(ir_dev);

	if (!data)
		return -EINVAL;

	if (data->enabled)
		return sprintf(buf, "1\n");
	else
	return sprintf(buf, "0\n");
}

static DEVICE_ATTR(enabled, S_IRUGO | S_IWUSR, show_enabled, store_enabled);

static struct attribute *decoder_attributes[] = {
	&dev_attr_enabled.attr,
	NULL
};

static struct attribute_group decoder_attribute_group = {
	.name	= "rc5_decoder",
	.attrs	= decoder_attributes,
};

/**
 * handle_event() - Decode one RC-5 pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @ev:		event array with type/duration of pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc5_decode(struct input_dev *input_dev,
			struct ir_raw_event *ev)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int is_pulse, scancode, delta, toggle;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	delta = DIV_ROUND_CLOSEST(ev->delta.tv_nsec, ir_rc5_remote_gap);

	/* The duration time refers to the last bit time */
	is_pulse = (ev->type & IR_PULSE) ? 1 : 0;

	/* Very long delays are considered as start events */
	if (delta > RC5_DURATION || (ev->type & IR_START_EVENT))
		data->state = STATE_INACTIVE;

	switch (data->state) {
	case STATE_INACTIVE:
	IR_dprintk(2, "currently inative. Start bit (%s) @%uus\n",
		   is_pulse ? "pulse" : "space",
		   (unsigned)(ev->delta.tv_nsec + 500) / 1000);

		/* Discards the initial start space */
		if (!is_pulse)
			goto err;
		data->code = 1;
		data->last_bit = 1;
		data->elapsed = 0;
		memset(&data->rc5_code, 0, sizeof(data->rc5_code));
		data->state = STATE_MARKSPACE;
		return 0;
	case STATE_MARKSPACE:
		if (delta != 1)
			data->last_bit = data->last_bit ? 0 : 1;

		data->elapsed += delta;

		if ((data->elapsed % 2) == 1)
			return 0;

		data->code <<= 1;
		data->code |= data->last_bit;

		/* Fill the 2 unused bits at the command with 0 */
		if (data->elapsed / 2 == 6)
			data->code <<= 2;

		if (data->elapsed >= (RC5_NBITS - 1) * 2) {
			scancode = data->code;

			/* Check for the start bits */
			if ((scancode & 0xc000) != 0xc000) {
				IR_dprintk(1, "Code 0x%04x doesn't have two start bits. It is not RC-5\n", scancode);
				goto err;
			}

			toggle = (scancode & 0x2000) ? 1 : 0;

			if (scancode == data->last_code) {
				IR_dprintk(1, "RC-5 repeat\n");
				ir_repeat(input_dev);
			} else {
				data->last_code = scancode;
				scancode &= 0x1fff;
				IR_dprintk(1, "RC-5 scancode 0x%04x\n", scancode);

				ir_keydown(input_dev, scancode, 0);
			}
			data->state = STATE_TRAILER;
		}
		return 0;
	case STATE_TRAILER:
		data->state = STATE_INACTIVE;
		return 0;
	}

err:
	IR_dprintk(1, "RC-5 decoded failed at %s @ %luus\n",
		   is_pulse ? "pulse" : "space",
		   (ev->delta.tv_nsec + 500) / 1000);
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static int ir_rc5_register(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	struct decoder_data *data;
	int rc;

	rc = sysfs_create_group(&ir_dev->dev.kobj, &decoder_attribute_group);
	if (rc < 0)
		return rc;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data) {
		sysfs_remove_group(&ir_dev->dev.kobj, &decoder_attribute_group);
		return -ENOMEM;
	}

	data->ir_dev = ir_dev;
	data->enabled = 1;

	spin_lock(&decoder_lock);
	list_add_tail(&data->list, &decoder_list);
	spin_unlock(&decoder_lock);

	return 0;
}

static int ir_rc5_unregister(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	static struct decoder_data *data;

	data = get_decoder_data(ir_dev);
	if (!data)
		return 0;

	sysfs_remove_group(&ir_dev->dev.kobj, &decoder_attribute_group);

	spin_lock(&decoder_lock);
	list_del(&data->list);
	spin_unlock(&decoder_lock);

	return 0;
}

static struct ir_raw_handler rc5_handler = {
	.decode		= ir_rc5_decode,
	.raw_register	= ir_rc5_register,
	.raw_unregister	= ir_rc5_unregister,
};

static int __init ir_rc5_decode_init(void)
{
	ir_raw_handler_register(&rc5_handler);

	printk(KERN_INFO "IR RC-5 protocol handler initialized\n");
	return 0;
}

static void __exit ir_rc5_decode_exit(void)
{
	ir_raw_handler_unregister(&rc5_handler);
}

module_init(ir_rc5_decode_init);
module_exit(ir_rc5_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("RC-5 IR protocol decoder");
