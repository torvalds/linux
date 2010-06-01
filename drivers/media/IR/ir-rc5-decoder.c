/* ir-rc5-decoder.c - handle RC5(x) IR Pulse/Space protocol
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
 * This code handles 14 bits RC5 protocols and 20 bits RC5x protocols.
 * There are other variants that use a different number of bits.
 * This is currently unsupported.
 * It considers a carrier of 36 kHz, with a total of 14/20 bits, where
 * the first two bits are start bits, and a third one is a filing bit
 */

#include "ir-core-priv.h"

#define RC5_NBITS		14
#define RC5X_NBITS		20
#define CHECK_RC5X_NBITS	8
#define RC5_UNIT		888888 /* ns */
#define RC5_BIT_START		(1 * RC5_UNIT)
#define RC5_BIT_END		(1 * RC5_UNIT)
#define RC5X_SPACE		(4 * RC5_UNIT)

/* Used to register rc5_decoder clients */
static LIST_HEAD(decoder_list);
static DEFINE_SPINLOCK(decoder_lock);

enum rc5_state {
	STATE_INACTIVE,
	STATE_BIT_START,
	STATE_BIT_END,
	STATE_CHECK_RC5X,
	STATE_FINISHED,
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum rc5_state		state;
	u32			rc5_bits;
	struct ir_raw_event	prev_ev;
	unsigned		count;
	unsigned		wanted_bits;
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
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc5_decode(struct input_dev *input_dev, struct ir_raw_event ev)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	u8 toggle;
	u32 scancode;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	if (IS_RESET(ev)) {
		data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, RC5_UNIT, RC5_UNIT / 2))
		goto out;

again:
	IR_dprintk(2, "RC5(x) decode started at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	if (!geq_margin(ev.duration, RC5_UNIT, RC5_UNIT / 2))
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		data->state = STATE_BIT_START;
		data->count = 1;
		/* We just need enough bits to get to STATE_CHECK_RC5X */
		data->wanted_bits = RC5X_NBITS;
		decrease_duration(&ev, RC5_BIT_START);
		goto again;

	case STATE_BIT_START:
		if (!eq_margin(ev.duration, RC5_BIT_START, RC5_UNIT / 2))
			break;

		data->rc5_bits <<= 1;
		if (!ev.pulse)
			data->rc5_bits |= 1;
		data->count++;
		data->prev_ev = ev;
		data->state = STATE_BIT_END;
		return 0;

	case STATE_BIT_END:
		if (!is_transition(&ev, &data->prev_ev))
			break;

		if (data->count == data->wanted_bits)
			data->state = STATE_FINISHED;
		else if (data->count == CHECK_RC5X_NBITS)
			data->state = STATE_CHECK_RC5X;
		else
			data->state = STATE_BIT_START;

		decrease_duration(&ev, RC5_BIT_END);
		goto again;

	case STATE_CHECK_RC5X:
		if (!ev.pulse && geq_margin(ev.duration, RC5X_SPACE, RC5_UNIT / 2)) {
			/* RC5X */
			data->wanted_bits = RC5X_NBITS;
			decrease_duration(&ev, RC5X_SPACE);
		} else {
			/* RC5 */
			data->wanted_bits = RC5_NBITS;
		}
		data->state = STATE_BIT_START;
		goto again;

	case STATE_FINISHED:
		if (ev.pulse)
			break;

		if (data->wanted_bits == RC5X_NBITS) {
			/* RC5X */
			u8 xdata, command, system;
			xdata    = (data->rc5_bits & 0x0003F) >> 0;
			command  = (data->rc5_bits & 0x00FC0) >> 6;
			system   = (data->rc5_bits & 0x1F000) >> 12;
			toggle   = (data->rc5_bits & 0x20000) ? 1 : 0;
			command += (data->rc5_bits & 0x01000) ? 0 : 0x40;
			scancode = system << 16 | command << 8 | xdata;

			IR_dprintk(1, "RC5X scancode 0x%06x (toggle: %u)\n",
				   scancode, toggle);

		} else {
			/* RC5 */
			u8 command, system;
			command  = (data->rc5_bits & 0x0003F) >> 0;
			system   = (data->rc5_bits & 0x007C0) >> 6;
			toggle   = (data->rc5_bits & 0x00800) ? 1 : 0;
			command += (data->rc5_bits & 0x01000) ? 0 : 0x40;
			scancode = system << 8 | command;

			IR_dprintk(1, "RC5 scancode 0x%04x (toggle: %u)\n",
				   scancode, toggle);
		}

		ir_keydown(input_dev, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "RC5(x) decode failed at state %i (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
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

	printk(KERN_INFO "IR RC5(x) protocol handler initialized\n");
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
MODULE_DESCRIPTION("RC5(x) IR protocol decoder");
