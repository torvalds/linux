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

#include "ir-core-priv.h"

#define RC5_NBITS		14
#define RC5_UNIT		888888 /* ns */

/* Used to register rc5_decoder clients */
static LIST_HEAD(decoder_list);
static DEFINE_SPINLOCK(decoder_lock);

enum rc5_state {
	STATE_INACTIVE,
	STATE_BIT_START,
	STATE_BIT_END,
	STATE_FINISHED,
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum rc5_state		state;
	u32			rc5_bits;
	int			last_unit;
	unsigned		count;
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
 * ir_rc5_decode() - Decode one RC-5 pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @duration:	duration of pulse/space in ns
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc5_decode(struct input_dev *input_dev, s64 duration)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	u8 command, system, toggle;
	u32 scancode;
	int u;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	if (IS_RESET(duration)) {
		data->state = STATE_INACTIVE;
		return 0;
	}

	u = TO_UNITS(duration, RC5_UNIT);
	if (DURATION(u) == 0)
		goto out;

again:
	IR_dprintk(2, "RC5 decode started at state %i (%i units, %ius)\n",
		   data->state, u, TO_US(duration));

	if (DURATION(u) == 0 && data->state != STATE_FINISHED)
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (IS_PULSE(u)) {
			data->state = STATE_BIT_START;
			data->count = 1;
			DECREASE_DURATION(u, 1);
			goto again;
		}
		break;

	case STATE_BIT_START:
		if (DURATION(u) == 1) {
			data->rc5_bits <<= 1;
			if (IS_SPACE(u))
				data->rc5_bits |= 1;
			data->count++;
			data->last_unit = u;

			/*
			 * If the last bit is zero, a space will merge
			 * with the silence after the command.
			 */
			if (IS_PULSE(u) && data->count == RC5_NBITS) {
				data->state = STATE_FINISHED;
				goto again;
			}

			data->state = STATE_BIT_END;
			return 0;
		}
		break;

	case STATE_BIT_END:
		if (IS_TRANSITION(u, data->last_unit)) {
			if (data->count == RC5_NBITS)
				data->state = STATE_FINISHED;
			else
				data->state = STATE_BIT_START;

			DECREASE_DURATION(u, 1);
			goto again;
		}
		break;

	case STATE_FINISHED:
		command  = (data->rc5_bits & 0x0003F) >> 0;
		system   = (data->rc5_bits & 0x007C0) >> 6;
		toggle   = (data->rc5_bits & 0x00800) ? 1 : 0;
		command += (data->rc5_bits & 0x01000) ? 0 : 0x40;
		scancode = system << 8 | command;

		IR_dprintk(1, "RC5 scancode 0x%04x (toggle: %u)\n",
			   scancode, toggle);
		ir_keydown(input_dev, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "RC5 decode failed at state %i (%i units, %ius)\n",
		   data->state, u, TO_US(duration));
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
