/* ir-jvc-decoder.c - handle JVC IR Pulse/Space protocol
 *
 * Copyright (C) 2010 by David Härdeman <david@hardeman.nu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/bitrev.h>
#include "ir-core-priv.h"

#define JVC_NBITS		16		/* dev(8) + func(8) */
#define JVC_UNIT		525000		/* ns */
#define JVC_HEADER_PULSE	(16 * JVC_UNIT) /* lack of header -> repeat */
#define JVC_HEADER_SPACE	(8  * JVC_UNIT)
#define JVC_BIT_PULSE		(1  * JVC_UNIT)
#define JVC_BIT_0_SPACE		(1  * JVC_UNIT)
#define JVC_BIT_1_SPACE		(3  * JVC_UNIT)
#define JVC_TRAILER_PULSE	(1  * JVC_UNIT)
#define	JVC_TRAILER_SPACE	(35 * JVC_UNIT)

/* Used to register jvc_decoder clients */
static LIST_HEAD(decoder_list);
DEFINE_SPINLOCK(decoder_lock);

enum jvc_state {
	STATE_INACTIVE,
	STATE_HEADER_SPACE,
	STATE_BIT_PULSE,
	STATE_BIT_SPACE,
	STATE_TRAILER_PULSE,
	STATE_TRAILER_SPACE,
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum jvc_state		state;
	u16			jvc_bits;
	u16			jvc_old_bits;
	unsigned		count;
	bool			first;
	bool			toggle;
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
	.name	= "jvc_decoder",
	.attrs	= decoder_attributes,
};

/**
 * ir_jvc_decode() - Decode one JVC pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @duration:   the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_jvc_decode(struct input_dev *input_dev, struct ir_raw_event ev)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	if (IS_RESET(ev)) {
		data->state = STATE_INACTIVE;
		return 0;
	}

	if (!geq_margin(ev.duration, JVC_UNIT, JVC_UNIT / 2))
		goto out;

	IR_dprintk(2, "JVC decode started at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));

	switch (data->state) {

	case STATE_INACTIVE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_HEADER_PULSE, JVC_UNIT / 2))
			break;

		data->count = 0;
		data->first = true;
		data->toggle = !data->toggle;
		data->state = STATE_HEADER_SPACE;
		return 0;

	case STATE_HEADER_SPACE:
		if (ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_HEADER_SPACE, JVC_UNIT / 2))
			break;

		data->state = STATE_BIT_PULSE;
		return 0;

	case STATE_BIT_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_BIT_PULSE, JVC_UNIT / 2))
			break;

		data->state = STATE_BIT_SPACE;
		return 0;

	case STATE_BIT_SPACE:
		if (ev.pulse)
			break;

		data->jvc_bits <<= 1;
		if (eq_margin(ev.duration, JVC_BIT_1_SPACE, JVC_UNIT / 2)) {
			data->jvc_bits |= 1;
			decrease_duration(&ev, JVC_BIT_1_SPACE);
		} else if (eq_margin(ev.duration, JVC_BIT_0_SPACE, JVC_UNIT / 2))
			decrease_duration(&ev, JVC_BIT_0_SPACE);
		else
			break;
		data->count++;

		if (data->count == JVC_NBITS)
			data->state = STATE_TRAILER_PULSE;
		else
			data->state = STATE_BIT_PULSE;
		return 0;

	case STATE_TRAILER_PULSE:
		if (!ev.pulse)
			break;

		if (!eq_margin(ev.duration, JVC_TRAILER_PULSE, JVC_UNIT / 2))
			break;

		data->state = STATE_TRAILER_SPACE;
		return 0;

	case STATE_TRAILER_SPACE:
		if (ev.pulse)
			break;

		if (!geq_margin(ev.duration, JVC_TRAILER_SPACE, JVC_UNIT / 2))
			break;

		if (data->first) {
			u32 scancode;
			scancode = (bitrev8((data->jvc_bits >> 8) & 0xff) << 8) |
				   (bitrev8((data->jvc_bits >> 0) & 0xff) << 0);
			IR_dprintk(1, "JVC scancode 0x%04x\n", scancode);
			ir_keydown(input_dev, scancode, data->toggle);
			data->first = false;
			data->jvc_old_bits = data->jvc_bits;
		} else if (data->jvc_bits == data->jvc_old_bits) {
			IR_dprintk(1, "JVC repeat\n");
			ir_repeat(input_dev);
		} else {
			IR_dprintk(1, "JVC invalid repeat msg\n");
			break;
		}

		data->count = 0;
		data->state = STATE_BIT_PULSE;
		return 0;
	}

out:
	IR_dprintk(1, "JVC decode failed at state %d (%uus %s)\n",
		   data->state, TO_US(ev.duration), TO_STR(ev.pulse));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static int ir_jvc_register(struct input_dev *input_dev)
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

static int ir_jvc_unregister(struct input_dev *input_dev)
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

static struct ir_raw_handler jvc_handler = {
	.decode		= ir_jvc_decode,
	.raw_register	= ir_jvc_register,
	.raw_unregister	= ir_jvc_unregister,
};

static int __init ir_jvc_decode_init(void)
{
	ir_raw_handler_register(&jvc_handler);

	printk(KERN_INFO "IR JVC protocol handler initialized\n");
	return 0;
}

static void __exit ir_jvc_decode_exit(void)
{
	ir_raw_handler_unregister(&jvc_handler);
}

module_init(ir_jvc_decode_init);
module_exit(ir_jvc_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("JVC IR protocol decoder");
