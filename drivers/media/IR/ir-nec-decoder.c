/* ir-nec-decoder.c - handle NEC IR Pulse/Space protocol
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

#include <media/ir-core.h>
#include <linux/bitrev.h>

#define NEC_NBITS		32
#define NEC_UNIT		562500  /* ns */
#define NEC_HEADER_PULSE	PULSE(16)
#define NEC_HEADER_SPACE	SPACE(8)
#define NEC_REPEAT_SPACE	SPACE(4)
#define NEC_BIT_PULSE		PULSE(1)
#define NEC_BIT_0_SPACE		SPACE(1)
#define NEC_BIT_1_SPACE		SPACE(3)

/* Used to register nec_decoder clients */
static LIST_HEAD(decoder_list);
static DEFINE_SPINLOCK(decoder_lock);

enum nec_state {
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
	enum nec_state		state;
	u32			nec_bits;
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
	.name	= "nec_decoder",
	.attrs	= decoder_attributes,
};

/**
 * ir_nec_decode() - Decode one NEC pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @duration:	duration in ns of pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_nec_decode(struct input_dev *input_dev, s64 duration)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int u;
	u32 scancode;
	u8 address, not_address, command, not_command;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	if (IS_RESET(duration)) {
		data->state = STATE_INACTIVE;
		return 0;
	}

	u = TO_UNITS(duration, NEC_UNIT);
	if (DURATION(u) == 0)
		goto out;

	IR_dprintk(2, "NEC decode started at state %d (%i units, %ius)\n",
		   data->state, u, TO_US(duration));

	switch (data->state) {

	case STATE_INACTIVE:
		if (u == NEC_HEADER_PULSE) {
			data->count = 0;
			data->state = STATE_HEADER_SPACE;
		}
		return 0;

	case STATE_HEADER_SPACE:
		if (u == NEC_HEADER_SPACE) {
			data->state = STATE_BIT_PULSE;
			return 0;
		} else if (u == NEC_REPEAT_SPACE) {
			ir_repeat(input_dev);
			IR_dprintk(1, "Repeat last key\n");
			data->state = STATE_TRAILER_PULSE;
			return 0;
		}
		break;

	case STATE_BIT_PULSE:
		if (u == NEC_BIT_PULSE) {
			data->state = STATE_BIT_SPACE;
			return 0;
		}
		break;

	case STATE_BIT_SPACE:
		if (u != NEC_BIT_0_SPACE && u != NEC_BIT_1_SPACE)
			break;

		data->nec_bits <<= 1;
		if (u == NEC_BIT_1_SPACE)
			data->nec_bits |= 1;
		data->count++;

		if (data->count != NEC_NBITS) {
			data->state = STATE_BIT_PULSE;
			return 0;
		}

		address     = bitrev8((data->nec_bits >> 24) & 0xff);
		not_address = bitrev8((data->nec_bits >> 16) & 0xff);
		command	    = bitrev8((data->nec_bits >>  8) & 0xff);
		not_command = bitrev8((data->nec_bits >>  0) & 0xff);

		if ((command ^ not_command) != 0xff) {
			IR_dprintk(1, "NEC checksum error: received 0x%08x\n",
				   data->nec_bits);
			break;
		}

		if ((address ^ not_address) != 0xff) {
			/* Extended NEC */
			scancode = address     << 16 |
				   not_address <<  8 |
				   command;
			IR_dprintk(1, "NEC (Ext) scancode 0x%06x\n", scancode);
		} else {
			/* normal NEC */
			scancode = address << 8 | command;
			IR_dprintk(1, "NEC scancode 0x%04x\n", scancode);
		}

		ir_keydown(input_dev, scancode, 0);
		data->state = STATE_TRAILER_PULSE;
		return 0;

	case STATE_TRAILER_PULSE:
		if (u > 0) {
			data->state = STATE_TRAILER_SPACE;
			return 0;
		}
		break;

	case STATE_TRAILER_SPACE:
		if (u < 0) {
			data->state = STATE_INACTIVE;
			return 0;
		}

		break;
	}

out:
	IR_dprintk(1, "NEC decode failed at state %d (%i units, %ius)\n",
		   data->state, u, TO_US(duration));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static int ir_nec_register(struct input_dev *input_dev)
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

static int ir_nec_unregister(struct input_dev *input_dev)
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

static struct ir_raw_handler nec_handler = {
	.decode		= ir_nec_decode,
	.raw_register	= ir_nec_register,
	.raw_unregister	= ir_nec_unregister,
};

static int __init ir_nec_decode_init(void)
{
	ir_raw_handler_register(&nec_handler);

	printk(KERN_INFO "IR NEC protocol handler initialized\n");
	return 0;
}

static void __exit ir_nec_decode_exit(void)
{
	ir_raw_handler_unregister(&nec_handler);
}

module_init(ir_nec_decode_init);
module_exit(ir_nec_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("NEC IR protocol decoder");
