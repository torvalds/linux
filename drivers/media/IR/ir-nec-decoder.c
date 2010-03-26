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

/* Start time: 4.5 ms + 560 us of the next pulse */
#define MIN_START_TIME	(3900000 + 560000)
#define MAX_START_TIME	(5100000 + 560000)

/* Bit 1 time: 2.25ms us */
#define MIN_BIT1_TIME	2050000
#define MAX_BIT1_TIME	2450000

/* Bit 0 time: 1.12ms us */
#define MIN_BIT0_TIME	920000
#define MAX_BIT0_TIME	1320000

/* Total IR code is 110 ms, including the 9 ms for the start pulse */
#define MAX_NEC_TIME	4000000

/* Total IR code is 110 ms, including the 9 ms for the start pulse */
#define MIN_REPEAT_TIME	99000000
#define MAX_REPEAT_TIME	112000000

/* Repeat time: 2.25ms us */
#define MIN_REPEAT_START_TIME	2050000
#define MAX_REPEAT_START_TIME	3000000

#define REPEAT_TIME	240 /* ms */

/* Used to register nec_decoder clients */
static LIST_HEAD(decoder_list);
static spinlock_t decoder_lock;

struct decoder_data {
	struct list_head	list;
	struct ir_input_dev	*ir_dev;
	int			enabled:1;
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


/** is_repeat - Check if it is a NEC repeat event
 * @input_dev:	the struct input_dev descriptor of the device
 * @pos:	the position of the first event
 * @len:	the length of the buffer
 */
static int is_repeat(struct ir_raw_event *evs, int len, int pos)
{
	if ((evs[pos].delta.tv_nsec < MIN_REPEAT_START_TIME) ||
	    (evs[pos].delta.tv_nsec > MAX_REPEAT_START_TIME))
		return 0;

	if (++pos >= len)
		return 0;

	if ((evs[pos].delta.tv_nsec < MIN_REPEAT_TIME) ||
	    (evs[pos].delta.tv_nsec > MAX_REPEAT_TIME))
		return 0;

	return 1;
}

/**
 * __ir_nec_decode() - Decode one NEC pulsecode
 * @input_dev:	the struct input_dev descriptor of the device
 * @evs:	event array with type/duration of pulse/space
 * @len:	length of the array
 * @pos:	position to start seeking for a code
 * This function returns -EINVAL if no pulse got decoded,
 * 0 if buffer is empty and 1 if one keycode were handled.
 */
static int __ir_nec_decode(struct input_dev *input_dev,
			   struct ir_raw_event *evs,
			   int len, int *pos)
{
	struct ir_input_dev *ir = input_get_drvdata(input_dev);
	int count = -1;
	int ircode = 0, not_code = 0;

	/* Be sure that the first event is an start one and is a pulse */
	for (; *pos < len; (*pos)++) {
		/* Very long delays are considered as start events */
		if (evs[*pos].delta.tv_nsec > MAX_NEC_TIME)
			break;
		if (evs[*pos].type & IR_START_EVENT)
			break;
		IR_dprintk(1, "%luus: Spurious NEC %s\n",
			   (evs[*pos].delta.tv_nsec + 500) / 1000,
			   (evs[*pos].type & IR_SPACE) ? "space" : "pulse");

	}
	if (*pos >= len)
		return 0;

	(*pos)++;	/* First event doesn't contain data */

	if (evs[*pos].type != IR_PULSE)
		goto err;

	/* Check if it is a NEC repeat event */
	if (is_repeat(evs, len, *pos)) {
		*pos += 2;
		if (ir->keypressed) {
			mod_timer(&ir->raw->timer_keyup,
				jiffies + msecs_to_jiffies(REPEAT_TIME));
			IR_dprintk(1, "NEC repeat event\n");
			return 1;
		} else {
			IR_dprintk(1, "missing NEC repeat event\n");
			return 0;
		}
	}

	/* First space should have 4.5 ms otherwise is not NEC protocol */
	if ((evs[*pos].delta.tv_nsec < MIN_START_TIME) ||
	    (evs[*pos].delta.tv_nsec > MAX_START_TIME))
		goto err;

	count = 0;
	for ((*pos)++; *pos < len; (*pos)++) {
		int bit;
		if ((evs[*pos].delta.tv_nsec > MIN_BIT1_TIME) &&
		    (evs[*pos].delta.tv_nsec < MAX_BIT1_TIME))
			bit = 1;
		else if ((evs[*pos].delta.tv_nsec > MIN_BIT0_TIME) &&
			 (evs[*pos].delta.tv_nsec < MAX_BIT0_TIME))
			bit = 0;
		else
			goto err;

		if (bit) {
			int shift = count;
			/* Address first, then command */
			if (shift < 8) {
				shift += 8;
				ircode |= 1 << shift;
			} else if (shift < 16) {
				not_code |= 1 << shift;
			} else if (shift < 24) {
				shift -= 16;
				ircode |= 1 << shift;
			} else {
				shift -= 24;
				not_code |= 1 << shift;
			}
		}
		if (++count == 32)
			break;
	}
	(*pos)++;

	/*
	 * Fixme: may need to accept Extended NEC protocol?
	 */
	if ((ircode & ~not_code) != ircode) {
		IR_dprintk(1, "NEC checksum error: code 0x%04x, not-code 0x%04x\n",
			   ircode, not_code);
		return -EINVAL;
	}

	IR_dprintk(1, "NEC scancode 0x%04x\n", ircode);
	ir_keydown(input_dev, ircode);
	mod_timer(&ir->raw->timer_keyup,
		  jiffies + msecs_to_jiffies(REPEAT_TIME));

	return 1;
err:
	IR_dprintk(1, "NEC decoded failed at bit %d (%s) while decoding %luus time\n",
		   count,
		   (evs[*pos].type & IR_SPACE) ? "space" : "pulse",
		   (evs[*pos].delta.tv_nsec + 500) / 1000);

	return -EINVAL;
}

/**
 * __ir_nec_decode() - Decodes all NEC pulsecodes on a given array
 * @input_dev:	the struct input_dev descriptor of the device
 * @evs:	event array with type/duration of pulse/space
 * @len:	length of the array
 * This function returns the number of decoded pulses
 */
static int ir_nec_decode(struct input_dev *input_dev,
			 struct ir_raw_event *evs,
			 int len)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);
	struct decoder_data *data;
	int pos = 0;
	int rc = 0;

	data = get_decoder_data(ir_dev);
	if (!data || !data->enabled)
		return 0;

	while (pos < len) {
		if (__ir_nec_decode(input_dev, evs, len, &pos) > 0)
			rc++;
	}
	return rc;
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
