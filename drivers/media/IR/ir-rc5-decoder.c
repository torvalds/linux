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
 */

#include <media/ir-core.h>

#define RC5_NBITS		14
#define RC5_HALFBIT		888888 /* ns */
#define RC5_BIT			(RC5_HALFBIT * 2)
#define RC5_DURATION		(RC5_BIT * RC5_NBITS)

#define is_rc5_halfbit(nsec) ((ev->delta.tv_nsec >= RC5_HALFBIT / 2) &&	   \
		      (ev->delta.tv_nsec < RC5_HALFBIT + RC5_HALFBIT / 2))

#define n_half(nsec) ((ev->delta.tv_nsec + RC5_HALFBIT / 2) / RC5_HALFBIT)

/* Used to register rc5_decoder clients */
static LIST_HEAD(decoder_list);
static spinlock_t decoder_lock;

enum rc5_state {
	STATE_INACTIVE,
	STATE_START2_SPACE,
	STATE_START2_MARK,
	STATE_MARKSPACE,
	STATE_TRAILER_MARK,
};

static char *st_name[] = {
	"Inactive",
	"start2 sapce",
	"start2 mark",
	"mark",
	"space",
	"trailer"
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
	unsigned		n_half;
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
	int bit, last_bit, n_half;

	data = get_decoder_data(ir_dev);
	if (!data)
		return -EINVAL;

	if (!data->enabled)
		return 0;

	/* Except for the initial event, what matters is the previous bit */
	bit = (ev->type & IR_PULSE) ? 1 : 0;

	last_bit = !bit;

	/* Discards spurious space last_bits when inactive */

	/* Very long delays are considered as start events */
	if (ev->delta.tv_nsec > RC5_DURATION + RC5_HALFBIT / 2)
		data->state = STATE_INACTIVE;

	if (ev->type & IR_START_EVENT)
		data->state = STATE_INACTIVE;

	switch (data->state) {
	case STATE_INACTIVE:
IR_dprintk(1, "currently inative. Received bit (%s) @%luus\n",
	last_bit ? "pulse" : "space",
	(ev->delta.tv_nsec + 500) / 1000);

		/* Discards the initial start space */
		if (bit)
			return 0;
		data->count = 0;
		data->n_half = 0;
		memset (&data->rc5_code, 0, sizeof(data->rc5_code));

		data->state = STATE_START2_SPACE;
		return 0;
	case STATE_START2_SPACE:
		if (last_bit)
			goto err;
		if (!is_rc5_halfbit(ev->delta.tv_nsec))
			goto err;
		data->state = STATE_START2_MARK;
		return 0;
	case STATE_START2_MARK:
		if (!last_bit)
			goto err;

		if (!is_rc5_halfbit(ev->delta.tv_nsec))
			goto err;

		data->state = STATE_MARKSPACE;
		return 0;
	case STATE_MARKSPACE:
		n_half = n_half(ev->delta.tv_nsec);
		if (n_half < 1 || n_half > 3) {
			IR_dprintk(1, "Decode failed at %d-th bit (%s) @%luus\n",
				data->count,
				last_bit ? "pulse" : "space",
				(ev->delta.tv_nsec + 500) / 1000);
printk("%d halves\n", n_half);
			goto err2;
		}
		data->n_half += n_half;

		if (!last_bit)
			return 0;

		/* Got one complete mark/space cycle */

		bit = ((data->count + 1) * 2)/ data->n_half;

printk("%d halves, %d bits\n", n_half, bit);

#if 1 /* SANITY check - while testing the decoder */
		if (bit > 1) {
			IR_dprintk(1, "Decoder HAS failed at %d-th bit (%s) @%luus\n",
				data->count,
				last_bit ? "pulse" : "space",
				(ev->delta.tv_nsec + 500) / 1000);

			goto err2;
		}
#endif
		/* Ok, we've got a valid bit. proccess it */
		if (bit) {
			int shift = data->count;

			/*
			 * RC-5 transmit bytes on this temporal order:
			 * address | not address | command | not command
			 */
			if (shift < 8) {
				data->rc5_code.address |= 1 << shift;
			} else {
				data->rc5_code.command |= 1 << (shift - 8);
			}
		}
		IR_dprintk(1, "RC-5: bit #%d: %d (%d)\n",
				data->count, bit, data->n_half);
		if (++data->count >= RC5_NBITS) {
			u32 scancode;
			scancode = data->rc5_code.address << 8 |
					data->rc5_code.command;
			IR_dprintk(1, "RC-5 scancode 0x%04x\n", scancode);

			ir_keydown(input_dev, scancode, 0);

			data->state = STATE_TRAILER_MARK;
		}
		return 0;
	case STATE_TRAILER_MARK:
		if (!last_bit)
			goto err;
		data->state = STATE_INACTIVE;
		return 0;
	}

err:
	IR_dprintk(1, "RC-5 decoded failed at state %s (%s) @ %luus\n",
		   st_name[data->state],
		   bit ? "pulse" : "space",
		   (ev->delta.tv_nsec + 500) / 1000);
err2:
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
