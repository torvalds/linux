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
#include <media/lirc.h>
#include <media/lirc_dev.h>
#include <media/rc-core.h>
#include "rc-core-priv.h"

#define LIRCBUF_SIZE 256

/**
 * ir_lirc_raw_event() - Send raw IR data to lirc to be relayed to userspace
 *
 * @dev:	the struct rc_dev descriptor of the device
 * @ev:		the struct ir_raw_event descriptor of the pulse/space
 */
void ir_lirc_raw_event(struct rc_dev *dev, struct ir_raw_event ev)
{
	int sample;

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

		if (dev->gap)
			return;

		dev->gap_start = ktime_get();
		dev->gap = true;
		dev->gap_duration = ev.duration;

		if (!dev->send_timeout_reports)
			return;

		sample = LIRC_TIMEOUT(ev.duration / 1000);
		IR_dprintk(2, "timeout report (duration: %d)\n", sample);

	/* Normal sample */
	} else {

		if (dev->gap) {
			int gap_sample;

			dev->gap_duration += ktime_to_ns(ktime_sub(ktime_get(),
							 dev->gap_start));

			/* Convert to ms and cap by LIRC_VALUE_MASK */
			do_div(dev->gap_duration, 1000);
			dev->gap_duration = min_t(u64, dev->gap_duration,
						  LIRC_VALUE_MASK);

			gap_sample = LIRC_SPACE(dev->gap_duration);
			lirc_buffer_write(dev->lirc_dev->buf,
					  (unsigned char *)&gap_sample);
			dev->gap = false;
		}

		sample = ev.pulse ? LIRC_PULSE(ev.duration / 1000) :
					LIRC_SPACE(ev.duration / 1000);
		IR_dprintk(2, "delivering %uus %s to lirc_dev\n",
			   TO_US(ev.duration), TO_STR(ev.pulse));
	}

	lirc_buffer_write(dev->lirc_dev->buf,
			  (unsigned char *) &sample);

	wake_up(&dev->lirc_dev->buf->wait_poll);
}

static ssize_t ir_lirc_transmit_ir(struct file *file, const char __user *buf,
				   size_t n, loff_t *ppos)
{
	struct rc_dev *dev = file->private_data;
	unsigned int *txbuf = NULL;
	struct ir_raw_event *raw = NULL;
	ssize_t ret = -EINVAL;
	size_t count;
	ktime_t start;
	s64 towait;
	unsigned int duration = 0; /* signal duration in us */
	int i;

	start = ktime_get();

	if (!dev->tx_ir) {
		ret = -EINVAL;
		goto out;
	}

	if (dev->send_mode == LIRC_MODE_SCANCODE) {
		struct lirc_scancode scan;

		if (n != sizeof(scan))
			return -EINVAL;

		if (copy_from_user(&scan, buf, sizeof(scan)))
			return -EFAULT;

		if (scan.flags || scan.keycode || scan.timestamp)
			return -EINVAL;

		/*
		 * The scancode field in lirc_scancode is 64-bit simply
		 * to future-proof it, since there are IR protocols encode
		 * use more than 32 bits. For now only 32-bit protocols
		 * are supported.
		 */
		if (scan.scancode > U32_MAX ||
		    !rc_validate_scancode(scan.rc_proto, scan.scancode))
			return -EINVAL;

		raw = kmalloc_array(LIRCBUF_SIZE, sizeof(*raw), GFP_KERNEL);
		if (!raw)
			return -ENOMEM;

		ret = ir_raw_encode_scancode(scan.rc_proto, scan.scancode,
					     raw, LIRCBUF_SIZE);
		if (ret < 0)
			goto out;

		count = ret;

		txbuf = kmalloc_array(count, sizeof(unsigned int), GFP_KERNEL);
		if (!txbuf) {
			ret = -ENOMEM;
			goto out;
		}

		for (i = 0; i < count; i++)
			/* Convert from NS to US */
			txbuf[i] = DIV_ROUND_UP(raw[i].duration, 1000);

		if (dev->s_tx_carrier) {
			int carrier = ir_raw_encode_carrier(scan.rc_proto);

			if (carrier > 0)
				dev->s_tx_carrier(dev, carrier);
		}
	} else {
		if (n < sizeof(unsigned int) || n % sizeof(unsigned int))
			return -EINVAL;

		count = n / sizeof(unsigned int);
		if (count > LIRCBUF_SIZE || count % 2 == 0)
			return -EINVAL;

		txbuf = memdup_user(buf, n);
		if (IS_ERR(txbuf))
			return PTR_ERR(txbuf);
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

	if (dev->send_mode == LIRC_MODE_SCANCODE) {
		ret = n;
	} else {
		for (duration = i = 0; i < ret; i++)
			duration += txbuf[i];

		ret *= sizeof(unsigned int);

		/*
		 * The lircd gap calculation expects the write function to
		 * wait for the actual IR signal to be transmitted before
		 * returning.
		 */
		towait = ktime_us_delta(ktime_add_us(start, duration),
					ktime_get());
		if (towait > 0) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(usecs_to_jiffies(towait));
		}
	}

out:
	kfree(txbuf);
	kfree(raw);
	return ret;
}

static long ir_lirc_ioctl(struct file *filep, unsigned int cmd,
			unsigned long arg)
{
	struct rc_dev *dev = filep->private_data;
	u32 __user *argp = (u32 __user *)(arg);
	int ret = 0;
	__u32 val = 0, tmp;

	if (_IOC_DIR(cmd) & _IOC_WRITE) {
		ret = get_user(val, argp);
		if (ret)
			return ret;
	}

	switch (cmd) {
	case LIRC_GET_FEATURES:
		if (dev->driver_type == RC_DRIVER_IR_RAW) {
			val |= LIRC_CAN_REC_MODE2;
			if (dev->rx_resolution)
				val |= LIRC_CAN_GET_REC_RESOLUTION;
		}

		if (dev->tx_ir) {
			val |= LIRC_CAN_SEND_PULSE | LIRC_CAN_SEND_SCANCODE;
			if (dev->s_tx_mask)
				val |= LIRC_CAN_SET_TRANSMITTER_MASK;
			if (dev->s_tx_carrier)
				val |= LIRC_CAN_SET_SEND_CARRIER;
			if (dev->s_tx_duty_cycle)
				val |= LIRC_CAN_SET_SEND_DUTY_CYCLE;
		}

		if (dev->s_rx_carrier_range)
			val |= LIRC_CAN_SET_REC_CARRIER |
				LIRC_CAN_SET_REC_CARRIER_RANGE;

		if (dev->s_learning_mode)
			val |= LIRC_CAN_USE_WIDEBAND_RECEIVER;

		if (dev->s_carrier_report)
			val |= LIRC_CAN_MEASURE_CARRIER;

		if (dev->max_timeout)
			val |= LIRC_CAN_SET_REC_TIMEOUT;

		break;

	/* mode support */
	case LIRC_GET_REC_MODE:
		if (dev->driver_type == RC_DRIVER_IR_RAW_TX)
			return -ENOTTY;

		val = LIRC_MODE_MODE2;
		break;

	case LIRC_SET_REC_MODE:
		if (dev->driver_type == RC_DRIVER_IR_RAW_TX)
			return -ENOTTY;

		if (val != LIRC_MODE_MODE2)
			return -EINVAL;
		return 0;

	case LIRC_GET_SEND_MODE:
		if (!dev->tx_ir)
			return -ENOTTY;

		val = dev->send_mode;
		break;

	case LIRC_SET_SEND_MODE:
		if (!dev->tx_ir)
			return -ENOTTY;

		if (!(val == LIRC_MODE_PULSE || val == LIRC_MODE_SCANCODE))
			return -EINVAL;

		dev->send_mode = val;
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
					       dev->carrier_low,
					       val);

	case LIRC_SET_REC_CARRIER_RANGE:
		if (!dev->s_rx_carrier_range)
			return -ENOTTY;

		if (val <= 0)
			return -EINVAL;

		dev->carrier_low = val;
		return 0;

	case LIRC_GET_REC_RESOLUTION:
		if (!dev->rx_resolution)
			return -ENOTTY;

		val = dev->rx_resolution / 1000;
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

		/* Check for multiply overflow */
		if (val > U32_MAX / 1000)
			return -EINVAL;

		tmp = val * 1000;

		if (tmp < dev->min_timeout || tmp > dev->max_timeout)
			return -EINVAL;

		if (dev->s_timeout)
			ret = dev->s_timeout(dev, tmp);
		if (!ret)
			dev->timeout = tmp;
		break;

	case LIRC_SET_REC_TIMEOUT_REPORTS:
		if (!dev->timeout)
			return -ENOTTY;

		dev->send_timeout_reports = !!val;
		break;

	default:
		return -ENOTTY;
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

int ir_lirc_register(struct rc_dev *dev)
{
	struct lirc_dev *ldev;
	int rc = -ENOMEM;

	ldev = lirc_allocate_device();
	if (!ldev)
		return rc;

	snprintf(ldev->name, sizeof(ldev->name), "ir-lirc-codec (%s)",
		 dev->driver_name);
	ldev->buf = NULL;
	ldev->chunk_size = sizeof(int);
	ldev->buffer_size = LIRCBUF_SIZE;
	ldev->fops = &lirc_fops;
	ldev->dev.parent = &dev->dev;
	ldev->rdev = dev;
	ldev->owner = THIS_MODULE;

	rc = lirc_register_device(ldev);
	if (rc < 0)
		goto out;

	dev->send_mode = LIRC_MODE_PULSE;
	dev->lirc_dev = ldev;
	return 0;

out:
	lirc_free_device(ldev);
	return rc;
}

void ir_lirc_unregister(struct rc_dev *dev)
{
	lirc_unregister_device(dev->lirc_dev);
	dev->lirc_dev = NULL;
}
