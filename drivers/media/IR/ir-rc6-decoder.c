/* ir-rc6-decoder.c - A decoder for the RC6 IR protocol
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

#include "ir-core-priv.h"

/*
 * This decoder currently supports:
 * RC6-0-16	(standard toggle bit in header)
 * RC6-6A-24	(no toggle bit)
 * RC6-6A-32	(MCE version with toggle bit in body)
 */

#define RC6_UNIT		444444	/* us */
#define RC6_HEADER_NBITS	4	/* not including toggle bit */
#define RC6_0_NBITS		16
#define RC6_6A_SMALL_NBITS	24
#define RC6_6A_LARGE_NBITS	32
#define RC6_PREFIX_PULSE	PULSE(6)
#define RC6_PREFIX_SPACE	SPACE(2)
#define RC6_MODE_MASK		0x07	/* for the header bits */
#define RC6_STARTBIT_MASK	0x08	/* for the header bits */
#define RC6_6A_MCE_TOGGLE_MASK	0x8000	/* for the body bits */

/* Used to register rc6_decoder clients */
static LIST_HEAD(decoder_list);
static DEFINE_SPINLOCK(decoder_lock);

enum rc6_mode {
	RC6_MODE_0,
	RC6_MODE_6A,
	RC6_MODE_UNKNOWN,
};

enum rc6_state {
	STATE_INACTIVE,
	STATE_PREFIX_SPACE,
	STATE_HEADER_BIT_START,
	STATE_HEADER_BIT_END,
	STATE_TOGGLE_START,
	STATE_TOGGLE_END,
	STATE_BODY_BIT_START,
	STATE_BODY_BIT_END,
	STATE_FINISHED,
};

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;

	/* State machine control */
	enum rc6_state		state;
	u8			header;
	u32			body;
	int			last_unit;
	bool			toggle;
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
	.name	= "rc6_decoder",
	.attrs	= decoder_attributes,
};

static enum rc6_mode rc6_mode(struct decoder_data *data) {
	switch (data->header & RC6_MODE_MASK) {
	case 0:
		return RC6_MODE_0;
	case 6:
		if (!data->toggle)
			return RC6_MODE_6A;
		/* fall through */
	default:
		return RC6_MODE_UNKNOWN;
	}
}

/**
 * ir_rc6_decode() - Decode one RC6 pulse or space
 * @input_dev:	the struct input_dev descriptor of the device
 * @duration:	duration of pulse/space in ns
 *
 * This function returns -EINVAL if the pulse violates the state machine
 */
static int ir_rc6_decode(struct input_dev *input_dev, s64 duration)
{
	struct decoder_data *data;
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	u32 scancode;
	u8 toggle;
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

	u =  TO_UNITS(duration, RC6_UNIT);
	if (DURATION(u) == 0)
		goto out;

again:
	IR_dprintk(2, "RC6 decode started at state %i (%i units, %ius)\n",
		   data->state, u, TO_US(duration));

	if (DURATION(u) == 0 && data->state != STATE_FINISHED)
		return 0;

	switch (data->state) {

	case STATE_INACTIVE:
		if (u >= RC6_PREFIX_PULSE - 1 && u <= RC6_PREFIX_PULSE + 1) {
			data->state = STATE_PREFIX_SPACE;
			data->count = 0;
			return 0;
		}
		break;

	case STATE_PREFIX_SPACE:
		if (u == RC6_PREFIX_SPACE) {
			data->state = STATE_HEADER_BIT_START;
			return 0;
		}
		break;

	case STATE_HEADER_BIT_START:
		if (DURATION(u) == 1) {
			data->header <<= 1;
			if (IS_PULSE(u))
				data->header |= 1;
			data->count++;
			data->last_unit = u;
			data->state = STATE_HEADER_BIT_END;
			return 0;
		}
		break;

	case STATE_HEADER_BIT_END:
		if (IS_TRANSITION(u, data->last_unit)) {
			if (data->count == RC6_HEADER_NBITS)
				data->state = STATE_TOGGLE_START;
			else
				data->state = STATE_HEADER_BIT_START;

			DECREASE_DURATION(u, 1);
			goto again;
		}
		break;

	case STATE_TOGGLE_START:
		if (DURATION(u) == 2) {
			data->toggle = IS_PULSE(u);
			data->last_unit = u;
			data->state = STATE_TOGGLE_END;
			return 0;
		}
		break;

	case STATE_TOGGLE_END:
		if (IS_TRANSITION(u, data->last_unit) && DURATION(u) >= 2) {
			data->state = STATE_BODY_BIT_START;
			data->last_unit = u;
			DECREASE_DURATION(u, 2);
			data->count = 0;

			if (!(data->header & RC6_STARTBIT_MASK)) {
				IR_dprintk(1, "RC6 invalid start bit\n");
				break;
			}

			switch (rc6_mode(data)) {
			case RC6_MODE_0:
				data->wanted_bits = RC6_0_NBITS;
				break;
			case RC6_MODE_6A:
				/* This might look weird, but we basically
				   check the value of the first body bit to
				   determine the number of bits in mode 6A */
				if ((DURATION(u) == 0 && IS_SPACE(data->last_unit)) || DURATION(u) > 0)
					data->wanted_bits = RC6_6A_LARGE_NBITS;
				else
					data->wanted_bits = RC6_6A_SMALL_NBITS;
				break;
			default:
				IR_dprintk(1, "RC6 unknown mode\n");
				goto out;
			}
			goto again;
		}
		break;

	case STATE_BODY_BIT_START:
		if (DURATION(u) == 1) {
			data->body <<= 1;
			if (IS_PULSE(u))
				data->body |= 1;
			data->count++;
			data->last_unit = u;

			/*
			 * If the last bit is one, a space will merge
			 * with the silence after the command.
			 */
			if (IS_PULSE(u) && data->count == data->wanted_bits) {
				data->state = STATE_FINISHED;
				goto again;
			}

			data->state = STATE_BODY_BIT_END;
			return 0;
		}
		break;

	case STATE_BODY_BIT_END:
		if (IS_TRANSITION(u, data->last_unit)) {
			if (data->count == data->wanted_bits)
				data->state = STATE_FINISHED;
			else
				data->state = STATE_BODY_BIT_START;

			DECREASE_DURATION(u, 1);
			goto again;
		}
		break;

	case STATE_FINISHED:
		switch (rc6_mode(data)) {
		case RC6_MODE_0:
			scancode = data->body & 0xffff;
			toggle = data->toggle;
			IR_dprintk(1, "RC6(0) scancode 0x%04x (toggle: %u)\n",
				   scancode, toggle);
			break;
		case RC6_MODE_6A:
			if (data->wanted_bits == RC6_6A_LARGE_NBITS) {
				toggle = data->body & RC6_6A_MCE_TOGGLE_MASK ? 1 : 0;
				scancode = data->body & ~RC6_6A_MCE_TOGGLE_MASK;
			} else {
				toggle = 0;
				scancode = data->body & 0xffffff;
			}

			IR_dprintk(1, "RC6(6A) scancode 0x%08x (toggle: %u)\n",
				   scancode, toggle);
			break;
		default:
			IR_dprintk(1, "RC6 unknown mode\n");
			goto out;
		}

		ir_keydown(input_dev, scancode, toggle);
		data->state = STATE_INACTIVE;
		return 0;
	}

out:
	IR_dprintk(1, "RC6 decode failed at state %i (%i units, %ius)\n",
		   data->state, u, TO_US(duration));
	data->state = STATE_INACTIVE;
	return -EINVAL;
}

static int ir_rc6_register(struct input_dev *input_dev)
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

static int ir_rc6_unregister(struct input_dev *input_dev)
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

static struct ir_raw_handler rc6_handler = {
	.decode		= ir_rc6_decode,
	.raw_register	= ir_rc6_register,
	.raw_unregister	= ir_rc6_unregister,
};

static int __init ir_rc6_decode_init(void)
{
	ir_raw_handler_register(&rc6_handler);

	printk(KERN_INFO "IR RC6 protocol handler initialized\n");
	return 0;
}

static void __exit ir_rc6_decode_exit(void)
{
	ir_raw_handler_unregister(&rc6_handler);
}

module_init(ir_rc6_decode_init);
module_exit(ir_rc6_decode_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Härdeman <david@hardeman.nu>");
MODULE_DESCRIPTION("RC6 IR protocol decoder");
