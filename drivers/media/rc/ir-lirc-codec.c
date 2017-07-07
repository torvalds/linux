/* ir-lirc-codec.c - rc-core to classic lirc interface bridge
 *
 * Copyright (C) 2010 by Jarod Wilson <jarod@redhat.com>
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

#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/module.h>
#include <media/lirc.h>
#include <media/lirc_dev.h>
#include <media/rc-core.h>
#include "rc-core-priv.h"

#define LIRCBUF_SIZE 256

/**
 * ir_lirc_decode() - Send raw IR data to lirc_dev to be relayed to the
 *		      lircd userspace daemon for decoding.
 * @input_dev:	the struct rc_dev descriptor of the device
 * @duration:	the struct ir_raw_event descriptor of the pulse/space
 *
 * This function returns -EINVAL if the lirc interfaces aren't wired up.
 */
static int ir_lirc_decode(struct rc_dev *dev, struct ir_raw_event ev)
{
	struct lirc_codec *lirc = &dev->raw->lirc;
	int sample;

	if (!dev->raw->lirc.drv || !dev->raw->lirc.drv->rbuf)
		return -EINVAL;

	/* Packet start */
	if (ev.reset) {
		/* Userspace expects a long space event before the start of
		 * the signal to use as a sync.  This may be done with repeat
		 * packets and normal samples.  But if a reset has been sent
		 * then we assume that a long time has passed, so we send a
		 * space with the maximum time value. */
		sample = LIRC_SPACE(LIRC_VALUE_MASK);
		IR_dprintk(2, "delivering reset sync space to lirc_dev\n");

	/* Carrier reports */
	} else if (ev.carrier_report) {
		sample = LIRC_FREQUENCY(ev.carrier);
		IR_dprintk(2, "carrier report (freq: %d)\n", sample);

	/* Packet end */
	} else if (ev.timeout) {

		if (lirc->gap)
			return 0;

		lirc->gap_start = ktime_get();
		lirc->gap = true;
		lirc->gap_duration = ev.duration;

		if (!lirc->send_timeout_reports)
			return 0;

		sample = LIRC_TIMEOUT(ev.duration / 1000);
		IR_dprintk(2, "timeout report (duration: %d)\n", sample);

	/* Normal sample */
	} else {

		if (lirc->gap) {
			int gap_sample;

			lirc->gap_duration += ktime_to_ns(ktime_sub(ktime_get(),
				lirc->gap_start));

			/* Convert to ms and cap by LIRC_VALUE_MASK */
			do_div(lirc->gap_duration, 1000);
			lirc->gap_duration = min(lirc->gap_duration,
							(u64)LIRC_VALUE_MASK);

			gap_sample = LIRC_SPACE(lirc->gap_duration);
			lirc_buffer_write(dev->raw->lirc.drv->rbuf,
						(unsigned char *) &gap_sample);
			lirc->gap = false;
		}

		sample = ev.pulse ? LIRC_PULSE(ev.duration / 1000) :
					LIRC_SPACE(ev.duration / 1000);
		IR_dprintk(2, "delivering %uus %s to lirc_dev\n",
			   TO_US(ev.duration), TO_STR(ev.pulse));
	}

	lirc_buffer_write(dev->raw->lirc.drv->rbuf,
			  (unsigned char *) &sample);
	wake_up(&dev->raw->lirc.drv->rbuf->wait_poll);

	return 0;
}

static ssize_t ir_lirc_transmit_ir(struct file *file, const char __user *buf,
				   size_t n, loff_t *ppos)
{
	struct lirc_codec *lirc;
	struct rc_dev *dev;
	unsigned int *txbuf; /* buffer with values to transmit */
	ssize_t ret = -EINVAL;
	size_t count;
	ktime_t start;
	s64 towait;
	unsigned int duration = 0; /* signal duration in us */
	int i;

	start = ktime_get();

	lirc = lirc_get_pdata(file);
	if (!lirc)
		return -EFAULT;

	if (n < sizeof(unsigned) || n % sizeof(unsigned))
		return -EINVAL;

	count = n / sizeof(unsigned);
	if (count > LIRCBUF_SIZE || count % 2 == 0)
		return -EINVAL;

	txbuf = memdup_user(buf, n);
	if (IS_ERR(txbuf))
		return PTR_ERR(txbuf);

	dev = lirc->dev;
	if (!dev) {
		ret = -EFAULT;
		goto out;
	}

	if (!dev->tx_ir) {
		ret = -EINVAL;
		goto out;
	}

	for (i = 0; i < count; i++) {
		if (txbuf[i] > IR_MAX_DURATION / 1000 - duration || !txbuf[i]) {
			ret = -EINVAL;
			goto out;
		}

		duration += txbuf[i];
	}

	ret = dev->tx_ir(dev, txbuf, count);
	if (ret < 0)
		goto out;

	for (duration = i = 0; i < ret; i++)
		duration += txbuf[i];

	ret *= sizeof(unsigned int);

	/*
	 * The lircd gap calculation expects the write function to
	 * wait for the actual IR signal to be transmitted before
	 * returning.
	 */
	towait = ktime_us_delta(ktime_add_us(start, duration), ktime_get());
	if (towait > 0) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(usecs_to_jiffies(towait));
	}

out:
	kfree(txbuf);
	return ret;
}

static long ir_lirc_ioctl(struct file *filep, unsigned int cmd,
			unsigned long arg)
{
	struct lirc_codec *lirc;
	struct rc_dev *dev;
	u32 __user *argp = (u32 __user *)(arg);
	int ret = 0;
	__u32 val = 0, tmp;

	lirc = lirc_get_pdata(filep);
	if (!lirc)
		return -EFAULT;

	dev = lirc->dev;
	if (!dev)
		return -EFAULT;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = get_user(val, argp);
		if (ret)
			return ret;
	}

	switch (cmd) {

	/* legacy support */
	case LIRC_GET_SEND_MODE:
		if (!dev->tx_ir)
			return -ENOTTY;

		val = LIRC_MODE_PULSE;
		break;

	case LIRC_SET_SEND_MODE:
		if (!dev->tx_ir)
			return -ENOTTY;

		if (val != LIRC_MODE_PULSE)
			return -EINVAL;
		return 0;

	/* TX settings */
	case LIRC_SET_TRANSMITTER_MASK:
		if (!dev->s_tx_mask)
			return -ENOTTY;

		return dev->s_tx_mask(dev, val);

	case LIRC_SET_SEND_CARRIER:
		if (!dev->s_tx_carrier)
			return -ENOTTY;

		return dev->s_tx_carrier(dev, val);

	case LIRC_SET_SEND_DUTY_CYCLE:
		if (!dev->s_tx_duty_cycle)
			return -ENOTTY;

		if (val <= 0 || val >= 100)
			return -EINVAL;

		return dev->s_tx_duty_cycle(dev, val);

	/* RX settings */
	case LIRC_SET_REC_CARRIER:
		if (!dev->s_rx_carrier_range)
			return -ENOTTY;

		if (val <= 0)
			return -EINVAL;

		return dev->s_rx_carrier_range(dev,
					       dev->raw->lirc.carrier_low,
					       val);

	case LIRC_SET_REC_CARRIER_RANGE:
		if (!dev->s_rx_carrier_range)
			return -ENOTTY;

		if (val <= 0)
			return -EINVAL;

		dev->raw->lirc.carrier_low = val;
		return 0;

	case LIRC_GET_REC_RESOLUTION:
		if (!dev->rx_resolution)
			return -ENOTTY;

		val = dev->rx_resolution;
		break;

	case LIRC_SET_WIDEBAND_RECEIVER:
		if (!dev->s_learning_mode)
			return -ENOTTY;

		return dev->s_learning_mode(dev, !!val);

	case LIRC_SET_MEASURE_CARRIER_MODE:
		if (!dev->s_carrier_report)
			return -ENOTTY;

		return dev->s_carrier_report(dev, !!val);

	/* Generic timeout support */
	case LIRC_GET_MIN_TIMEOUT:
		if (!dev->max_timeout)
			return -ENOTTY;
		val = DIV_ROUND_UP(dev->min_timeout, 1000);
		break;

	case LIRC_GET_MAX_TIMEOUT:
		if (!dev->max_timeout)
			return -ENOTTY;
		val = dev->max_timeout / 1000;
		break;

	case LIRC_SET_REC_TIMEOUT:
		if (!dev->max_timeout)
			return -ENOTTY;

		tmp = val * 1000;

		if (tmp < dev->min_timeout ||
		    tmp > dev->max_timeout)
				return -EINVAL;

		if (dev->s_timeout)
			ret = dev->s_timeout(dev, tmp);
		if (!ret)
			dev->timeout = tmp;
		break;

	case LIRC_SET_REC_TIMEOUT_REPORTS:
		if (!dev->timeout)
			return -ENOTTY;

		lirc->send_timeout_reports = !!val;
		break;

	default:
		return lirc_dev_fop_ioctl(filep, cmd, arg);
	}

	if (_IOC_DIR(cmd) & _IOC_READ)
		ret = put_user(val, argp);

	return ret;
}

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.write		= ir_lirc_transmit_ir,
	.unlocked_ioctl	= ir_lirc_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= ir_lirc_ioctl,
#endif
	.read		= lirc_dev_fop_read,
	.poll		= lirc_dev_fop_poll,
	.open		= lirc_dev_fop_open,
	.release	= lirc_dev_fop_close,
	.llseek		= no_llseek,
};

static int ir_lirc_register(struct rc_dev *dev)
{
	struct lirc_driver *drv;
	int rc = -ENOMEM;
	unsigned long features = 0;

	drv = kzalloc(sizeof(struct lirc_driver), GFP_KERNEL);
	if (!drv)
		return rc;

	if (dev->driver_type != RC_DRIVER_IR_RAW_TX) {
		features |= LIRC_CAN_REC_MODE2;
		if (dev->rx_resolution)
			features |= LIRC_CAN_GET_REC_RESOLUTION;
	}

	if (dev->tx_ir) {
		features |= LIRC_CAN_SEND_PULSE;
		if (dev->s_tx_mask)
			features |= LIRC_CAN_SET_TRANSMITTER_MASK;
		if (dev->s_tx_carrier)
			features |= LIRC_CAN_SET_SEND_CARRIER;
		if (dev->s_tx_duty_cycle)
			features |= LIRC_CAN_SET_SEND_DUTY_CYCLE;
	}

	if (dev->s_rx_carrier_range)
		features |= LIRC_CAN_SET_REC_CARRIER |
			LIRC_CAN_SET_REC_CARRIER_RANGE;

	if (dev->s_learning_mode)
		features |= LIRC_CAN_USE_WIDEBAND_RECEIVER;

	if (dev->s_carrier_report)
		features |= LIRC_CAN_MEASURE_CARRIER;

	if (dev->max_timeout)
		features |= LIRC_CAN_SET_REC_TIMEOUT;

	snprintf(drv->name, sizeof(drv->name), "ir-lirc-codec (%s)",
		 dev->driver_name);
	drv->minor = -1;
	drv->features = features;
	drv->data = &dev->raw->lirc;
	drv->rbuf = NULL;
	drv->code_length = sizeof(struct ir_raw_event) * 8;
	drv->chunk_size = sizeof(int);
	drv->buffer_size = LIRCBUF_SIZE;
	drv->fops = &lirc_fops;
	drv->dev = &dev->dev;
	drv->rdev = dev;
	drv->owner = THIS_MODULE;

	drv->minor = lirc_register_driver(drv);
	if (drv->minor < 0) {
		rc = -ENODEV;
		goto out;
	}

	dev->raw->lirc.drv = drv;
	dev->raw->lirc.dev = dev;
	return 0;

out:
	kfree(drv);
	return rc;
}

static int ir_lirc_unregister(struct rc_dev *dev)
{
	struct lirc_codec *lirc = &dev->raw->lirc;

	lirc_unregister_driver(lirc->drv->minor);
	kfree(lirc->drv);
	lirc->drv = NULL;

	return 0;
}

static struct ir_raw_handler lirc_handler = {
	.protocols	= 0,
	.decode		= ir_lirc_decode,
	.raw_register	= ir_lirc_register,
	.raw_unregister	= ir_lirc_unregister,
};

static int __init ir_lirc_codec_init(void)
{
	ir_raw_handler_register(&lirc_handler);

	printk(KERN_INFO "IR LIRC bridge handler initialized\n");
	return 0;
}

static void __exit ir_lirc_codec_exit(void)
{
	ir_raw_handler_unregister(&lirc_handler);
}

module_init(ir_lirc_codec_init);
module_exit(ir_lirc_codec_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Jarod Wilson <jarod@redhat.com>");
MODULE_AUTHOR("Red Hat Inc. (http://www.redhat.com)");
MODULE_DESCRIPTION("LIRC IR handler bridge");
