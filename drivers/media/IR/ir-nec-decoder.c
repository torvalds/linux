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

#define NEC_NBITS		32
#define NEC_UNIT		559979 /* ns */
#define NEC_HEADER_MARK		(16 * NEC_UNIT)
#define NEC_HEADER_SPACE	(8 * NEC_UNIT)
#define NEC_REPEAT_SPACE	(4 * NEC_UNIT)
#define NEC_MARK		(NEC_UNIT)
#define NEC_0_SPACE		(NEC_UNIT)
#define NEC_1_SPACE		(3 * NEC_UNIT)

/* Used to register nec_decoder clients */
static LIST_HEAD(decoder_list);
static spinlock_t decoder_lock;

enum nec_state {
	STATE_INACTIVE,
	STATE_HEADER_MARK,
	STATE_HEADER_SPACE,
	STATE_MARK,
	STATE_SPACE,
	STATE_TRAILER_MARK,
	STATE_TRAILER_SPACE,
};

struct nec_code {
	u8	address;
	u8	not_address;
	u8	command;
	u8	not_command;
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum nec_state		state;
	struct nec_code		nec_code;
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
 * @ev:		event array with type/duration of pulse/space
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_nec_decode(struct input_dev *input_dev,
			 struct ir_raw_event *ev)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	int bit, last_bit;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	/* Except for the initial event, what matters is the previous bit */
	bit = (ev->type & IR_PULSE) ? 1 : 0;

	last_bit = !bit;

	/* Discards spurious space last_bits when inactive */

	/* Very long delays are considered as start events */
	if (ev->delta.tv_nsec > NEC_HEADER_MARK + NEC_HEADER_SPACE - NEC_UNIT / 2)
		data->state = STATE_INACTIVE;

	if (ev->type & IR_START_EVENT)
		data->state = STATE_INACTIVE;

	switch (data->state) {
	case STATE_INACTIVE:
		if (!bit)		/* PULSE marks the start event */
			return 0;

		data->count = 0;
		data->state = STATE_HEADER_MARK;
		memset (&data->nec_code, 0, sizeof(data->nec_code));
		return 0;
	case STATE_HEADER_MARK:
		if (!last_bit)
			goto err;
		if (ev->delta.tv_nsec < NEC_HEADER_MARK - 6 * NEC_UNIT)
			goto err;
		data->state = STATE_HEADER_SPACE;
		return 0;
	case STATE_HEADER_SPACE:
		if (last_bit)
			goto err;
		if (ev->delta.tv_nsec >= NEC_HEADER_SPACE - NEC_UNIT / 2) {
			data->state = STATE_MARK;
			return 0;
		}

		if (ev->delta.tv_nsec >= NEC_REPEAT_SPACE - NEC_UNIT / 2) {
			ir_repeat(input_dev);
			IR_dprintk(1, "Repeat last key\n");
			data->state = STATE_TRAILER_MARK;
			return 0;
		}
		goto err;
	case STATE_MARK:
		if (!last_bit)
			goto err;
		if ((ev->delta.tv_nsec > NEC_MARK + NEC_UNIT / 2) ||
		    (ev->delta.tv_nsec < NEC_MARK - NEC_UNIT / 2))
			goto err;
		data->state = STATE_SPACE;
		return 0;
	case STATE_SPACE:
		if (last_bit)
			goto err;

		if ((ev->delta.tv_nsec >= NEC_0_SPACE - NEC_UNIT / 2) &&
		    (ev->delta.tv_nsec < NEC_0_SPACE + NEC_UNIT / 2))
			bit = 0;
		else if ((ev->delta.tv_nsec >= NEC_1_SPACE - NEC_UNIT / 2) &&
		         (ev->delta.tv_nsec < NEC_1_SPACE + NEC_UNIT / 2))
			bit = 1;
		else {
			IR_dprintk(1, "Decode failed at %d-th bit (%s) @%luus\n",
				data->count,
				last_bit ? "pulse" : "space",
				(ev->delta.tv_nsec + 500) / 1000);

			goto err2;
		}

		/* Ok, we've got a valid bit. proccess it */
		if (bit) {
			int shift = data->count;

			/*
			 * NEC transmit bytes on this temporal order:
			 * address | not address | command | not command
			 */
			if (shift < 8) {
				data->nec_code.address |= 1 << shift;
			} else if (shift < 16) {
				data->nec_code.not_address |= 1 << (shift - 8);
			} else if (shift < 24) {
				data->nec_code.command |= 1 << (shift - 16);
			} else {
				data->nec_code.not_command |= 1 << (shift - 24);
			}
		}
		if (++data->count == NEC_NBITS) {
			u32 scancode;
			/*
			 * Fixme: may need to accept Extended NEC protocol?
			 */
			if ((data->nec_code.command ^ data->nec_code.not_command) != 0xff)
				goto checksum_err;

			if ((data->nec_code.address ^ data->nec_code.not_address) != 0xff) {
				/* Extended NEC */
				scancode = data->nec_code.address << 16 |
					   data->nec_code.not_address << 8 |
					   data->nec_code.command;
				IR_dprintk(1, "NEC scancode 0x%06x\n", scancode);
			} else {
				/* normal NEC */
				scancode = data->nec_code.address << 8 |
					   data->nec_code.command;
				IR_dprintk(1, "NEC scancode 0x%04x\n", scancode);
			}
			ir_keydown(input_dev, scancode, 0);

			data->state = STATE_TRAILER_MARK;
		} else
			data->state = STATE_MARK;
		return 0;
	case STATE_TRAILER_MARK:
		if (!last_bit)
			goto err;
		data->state = STATE_TRAILER_SPACE;
		return 0;
	case STATE_TRAILER_SPACE:
		if (last_bit)
			goto err;
		data->state = STATE_INACTIVE;
		return 0;
	}

err:
	IR_dprintk(1, "NEC decoded failed at state %d (%s) @ %luus\n",
		   data->state,
		   bit ? "pulse" : "space",
		   (ev->delta.tv_nsec + 500) / 1000);
err2:
	data->state = STATE_INACTIVE;
	return -EINVAL;

checksum_err:
	data->state = STATE_INACTIVE;
	IR_dprintk(1, "NEC checksum error: received 0x%02x%02x%02x%02x\n",
		   data->nec_code.address,
		   data->nec_code.not_address,
		   data->nec_code.command,
		   data->nec_code.not_command);
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
