/*
 *  Copyright (C) 2008 Nokia Corporation
 *
 *  Based on lirc_serial.c
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <linux/platform_device.h>
#include <linux/sched.h>
#include <linux/wait.h>

#include <plat/dmtimer.h>
#include <plat/clock.h>

#include <media/lirc.h>
#include <media/lirc_dev.h>
#include <media/ir-rx51.h>

#define LIRC_RX51_DRIVER_FEATURES (LIRC_CAN_SET_SEND_DUTY_CYCLE |	\
				   LIRC_CAN_SET_SEND_CARRIER |		\
				   LIRC_CAN_SEND_PULSE)

#define DRIVER_NAME "lirc_rx51"

#define WBUF_LEN 256

#define TIMER_MAX_VALUE 0xffffffff

struct lirc_rx51 {
	struct omap_dm_timer *pwm_timer;
	struct omap_dm_timer *pulse_timer;
	struct device	     *dev;
	struct lirc_rx51_platform_data *pdata;
	wait_queue_head_t     wqueue;

	unsigned long	fclk_khz;
	unsigned int	freq;		/* carrier frequency */
	unsigned int	duty_cycle;	/* carrier duty cycle */
	unsigned int	irq_num;
	unsigned int	match;
	int		wbuf[WBUF_LEN];
	int		wbuf_index;
	unsigned long	device_is_open;
	int		pwm_timer_num;
};

static void lirc_rx51_on(struct lirc_rx51 *lirc_rx51)
{
	omap_dm_timer_set_pwm(lirc_rx51->pwm_timer, 0, 1,
			      OMAP_TIMER_TRIGGER_OVERFLOW_AND_COMPARE);
}

static void lirc_rx51_off(struct lirc_rx51 *lirc_rx51)
{
	omap_dm_timer_set_pwm(lirc_rx51->pwm_timer, 0, 1,
			      OMAP_TIMER_TRIGGER_NONE);
}

static int init_timing_params(struct lirc_rx51 *lirc_rx51)
{
	u32 load, match;

	load = -(lirc_rx51->fclk_khz * 1000 / lirc_rx51->freq);
	match = -(lirc_rx51->duty_cycle * -load / 100);
	omap_dm_timer_set_load(lirc_rx51->pwm_timer, 1, load);
	omap_dm_timer_set_match(lirc_rx51->pwm_timer, 1, match);
	omap_dm_timer_write_counter(lirc_rx51->pwm_timer, TIMER_MAX_VALUE - 2);
	omap_dm_timer_start(lirc_rx51->pwm_timer);
	omap_dm_timer_set_int_enable(lirc_rx51->pulse_timer, 0);
	omap_dm_timer_start(lirc_rx51->pulse_timer);

	lirc_rx51->match = 0;

	return 0;
}

#define tics_after(a, b) ((long)(b) - (long)(a) < 0)

static int pulse_timer_set_timeout(struct lirc_rx51 *lirc_rx51, int usec)
{
	int counter;

	BUG_ON(usec < 0);

	if (lirc_rx51->match == 0)
		counter = omap_dm_timer_read_counter(lirc_rx51->pulse_timer);
	else
		counter = lirc_rx51->match;

	counter += (u32)(lirc_rx51->fclk_khz * usec / (1000));
	omap_dm_timer_set_match(lirc_rx51->pulse_timer, 1, counter);
	omap_dm_timer_set_int_enable(lirc_rx51->pulse_timer,
				     OMAP_TIMER_INT_MATCH);
	if (tics_after(omap_dm_timer_read_counter(lirc_rx51->pulse_timer),
		       counter)) {
		return 1;
	}
	return 0;
}

static irqreturn_t lirc_rx51_interrupt_handler(int irq, void *ptr)
{
	unsigned int retval;
	struct lirc_rx51 *lirc_rx51 = ptr;

	retval = omap_dm_timer_read_status(lirc_rx51->pulse_timer);
	if (!retval)
		return IRQ_NONE;

	if (retval & ~OMAP_TIMER_INT_MATCH)
		dev_err_ratelimited(lirc_rx51->dev,
				": Unexpected interrupt source: %x\n", retval);

	omap_dm_timer_write_status(lirc_rx51->pulse_timer,
				OMAP_TIMER_INT_MATCH	|
				OMAP_TIMER_INT_OVERFLOW	|
				OMAP_TIMER_INT_CAPTURE);
	if (lirc_rx51->wbuf_index < 0) {
		dev_err_ratelimited(lirc_rx51->dev,
				": BUG wbuf_index has value of %i\n",
				lirc_rx51->wbuf_index);
		goto end;
	}

	/*
	 * If we happen to hit an odd latency spike, loop through the
	 * pulses until we catch up.
	 */
	do {
		if (lirc_rx51->wbuf_index >= WBUF_LEN)
			goto end;
		if (lirc_rx51->wbuf[lirc_rx51->wbuf_index] == -1)
			goto end;

		if (lirc_rx51->wbuf_index % 2)
			lirc_rx51_off(lirc_rx51);
		else
			lirc_rx51_on(lirc_rx51);

		retval = pulse_timer_set_timeout(lirc_rx51,
					lirc_rx51->wbuf[lirc_rx51->wbuf_index]);
		lirc_rx51->wbuf_index++;

	} while (retval);

	return IRQ_HANDLED;
end:
	/* Stop TX here */
	lirc_rx51_off(lirc_rx51);
	lirc_rx51->wbuf_index = -1;
	omap_dm_timer_stop(lirc_rx51->pwm_timer);
	omap_dm_timer_stop(lirc_rx51->pulse_timer);
	omap_dm_timer_set_int_enable(lirc_rx51->pulse_timer, 0);
	wake_up_interruptible(&lirc_rx51->wqueue);

	return IRQ_HANDLED;
}

static int lirc_rx51_init_port(struct lirc_rx51 *lirc_rx51)
{
	struct clk *clk_fclk;
	int retval, pwm_timer = lirc_rx51->pwm_timer_num;

	lirc_rx51->pwm_timer = omap_dm_timer_request_specific(pwm_timer);
	if (lirc_rx51->pwm_timer == NULL) {
		dev_err(lirc_rx51->dev, ": Error requesting GPT%d timer\n",
			pwm_timer);
		return -EBUSY;
	}

	lirc_rx51->pulse_timer = omap_dm_timer_request();
	if (lirc_rx51->pulse_timer == NULL) {
		dev_err(lirc_rx51->dev, ": Error requesting pulse timer\n");
		retval = -EBUSY;
		goto err1;
	}

	omap_dm_timer_set_source(lirc_rx51->pwm_timer, OMAP_TIMER_SRC_SYS_CLK);
	omap_dm_timer_set_source(lirc_rx51->pulse_timer,
				OMAP_TIMER_SRC_SYS_CLK);

	omap_dm_timer_enable(lirc_rx51->pwm_timer);
	omap_dm_timer_enable(lirc_rx51->pulse_timer);

	lirc_rx51->irq_num = omap_dm_timer_get_irq(lirc_rx51->pulse_timer);
	retval = request_irq(lirc_rx51->irq_num, lirc_rx51_interrupt_handler,
			     IRQF_SHARED, "lirc_pulse_timer", lirc_rx51);
	if (retval) {
		dev_err(lirc_rx51->dev, ": Failed to request interrupt line\n");
		goto err2;
	}

	clk_fclk = omap_dm_timer_get_fclk(lirc_rx51->pwm_timer);
	lirc_rx51->fclk_khz = clk_fclk->rate / 1000;

	return 0;

err2:
	omap_dm_timer_free(lirc_rx51->pulse_timer);
err1:
	omap_dm_timer_free(lirc_rx51->pwm_timer);

	return retval;
}

static int lirc_rx51_free_port(struct lirc_rx51 *lirc_rx51)
{
	omap_dm_timer_set_int_enable(lirc_rx51->pulse_timer, 0);
	free_irq(lirc_rx51->irq_num, lirc_rx51);
	lirc_rx51_off(lirc_rx51);
	omap_dm_timer_disable(lirc_rx51->pwm_timer);
	omap_dm_timer_disable(lirc_rx51->pulse_timer);
	omap_dm_timer_free(lirc_rx51->pwm_timer);
	omap_dm_timer_free(lirc_rx51->pulse_timer);
	lirc_rx51->wbuf_index = -1;

	return 0;
}

static ssize_t lirc_rx51_write(struct file *file, const char *buf,
			  size_t n, loff_t *ppos)
{
	int count, i;
	struct lirc_rx51 *lirc_rx51 = file->private_data;

	if (n % sizeof(int))
		return -EINVAL;

	count = n / sizeof(int);
	if ((count > WBUF_LEN) || (count % 2 == 0))
		return -EINVAL;

	/* Wait any pending transfers to finish */
	wait_event_interruptible(lirc_rx51->wqueue, lirc_rx51->wbuf_index < 0);

	if (copy_from_user(lirc_rx51->wbuf, buf, n))
		return -EFAULT;

	/* Sanity check the input pulses */
	for (i = 0; i < count; i++)
		if (lirc_rx51->wbuf[i] < 0)
			return -EINVAL;

	init_timing_params(lirc_rx51);
	if (count < WBUF_LEN)
		lirc_rx51->wbuf[count] = -1; /* Insert termination mark */

	/*
	 * Adjust latency requirements so the device doesn't go in too
	 * deep sleep states
	 */
	lirc_rx51->pdata->set_max_mpu_wakeup_lat(lirc_rx51->dev, 50);

	lirc_rx51_on(lirc_rx51);
	lirc_rx51->wbuf_index = 1;
	pulse_timer_set_timeout(lirc_rx51, lirc_rx51->wbuf[0]);

	/*
	 * Don't return back to the userspace until the transfer has
	 * finished
	 */
	wait_event_interruptible(lirc_rx51->wqueue, lirc_rx51->wbuf_index < 0);

	/* We can sleep again */
	lirc_rx51->pdata->set_max_mpu_wakeup_lat(lirc_rx51->dev, -1);

	return n;
}

static long lirc_rx51_ioctl(struct file *filep,
			unsigned int cmd, unsigned long arg)
{
	int result;
	unsigned long value;
	unsigned int ivalue;
	struct lirc_rx51 *lirc_rx51 = filep->private_data;

	switch (cmd) {
	case LIRC_GET_SEND_MODE:
		result = put_user(LIRC_MODE_PULSE, (unsigned long *)arg);
		if (result)
			return result;
		break;

	case LIRC_SET_SEND_MODE:
		result = get_user(value, (unsigned long *)arg);
		if (result)
			return result;

		/* only LIRC_MODE_PULSE supported */
		if (value != LIRC_MODE_PULSE)
			return -ENOSYS;
		break;

	case LIRC_GET_REC_MODE:
		result = put_user(0, (unsigned long *) arg);
		if (result)
			return result;
		break;

	case LIRC_GET_LENGTH:
		return -ENOSYS;
		break;

	case LIRC_SET_SEND_DUTY_CYCLE:
		result = get_user(ivalue, (unsigned int *) arg);
		if (result)
			return result;

		if (ivalue <= 0 || ivalue > 100) {
			dev_err(lirc_rx51->dev, ": invalid duty cycle %d\n",
				ivalue);
			return -EINVAL;
		}

		lirc_rx51->duty_cycle = ivalue;
		break;

	case LIRC_SET_SEND_CARRIER:
		result = get_user(ivalue, (unsigned int *) arg);
		if (result)
			return result;

		if (ivalue > 500000 || ivalue < 20000) {
			dev_err(lirc_rx51->dev, ": invalid carrier freq %d\n",
				ivalue);
			return -EINVAL;
		}

		lirc_rx51->freq = ivalue;
		break;

	case LIRC_GET_FEATURES:
		result = put_user(LIRC_RX51_DRIVER_FEATURES,
				  (unsigned long *) arg);
		if (result)
			return result;
		break;

	default:
		return -ENOIOCTLCMD;
	}

	return 0;
}

static int lirc_rx51_open(struct inode *inode, struct file *file)
{
	struct lirc_rx51 *lirc_rx51 = lirc_get_pdata(file);
	BUG_ON(!lirc_rx51);

	file->private_data = lirc_rx51;

	if (test_and_set_bit(1, &lirc_rx51->device_is_open))
		return -EBUSY;

	return lirc_rx51_init_port(lirc_rx51);
}

static int lirc_rx51_release(struct inode *inode, struct file *file)
{
	struct lirc_rx51 *lirc_rx51 = file->private_data;

	lirc_rx51_free_port(lirc_rx51);

	clear_bit(1, &lirc_rx51->device_is_open);

	return 0;
}

static struct lirc_rx51 lirc_rx51 = {
	.freq		= 38000,
	.duty_cycle	= 50,
	.wbuf_index	= -1,
};

static const struct file_operations lirc_fops = {
	.owner		= THIS_MODULE,
	.write		= lirc_rx51_write,
	.unlocked_ioctl	= lirc_rx51_ioctl,
	.read		= lirc_dev_fop_read,
	.poll		= lirc_dev_fop_poll,
	.open		= lirc_rx51_open,
	.release	= lirc_rx51_release,
};

static struct lirc_driver lirc_rx51_driver = {
	.name		= DRIVER_NAME,
	.minor		= -1,
	.code_length	= 1,
	.data		= &lirc_rx51,
	.fops		= &lirc_fops,
	.owner		= THIS_MODULE,
};

#ifdef CONFIG_PM

static int lirc_rx51_suspend(struct platform_device *dev, pm_message_t state)
{
	/*
	 * In case the device is still open, do not suspend. Normally
	 * this should not be a problem as lircd only keeps the device
	 * open only for short periods of time. We also don't want to
	 * get involved with race conditions that might happen if we
	 * were in a middle of a transmit. Thus, we defer any suspend
	 * actions until transmit has completed.
	 */
	if (test_and_set_bit(1, &lirc_rx51.device_is_open))
		return -EAGAIN;

	clear_bit(1, &lirc_rx51.device_is_open);

	return 0;
}

static int lirc_rx51_resume(struct platform_device *dev)
{
	return 0;
}

#else

#define lirc_rx51_suspend	NULL
#define lirc_rx51_resume	NULL

#endif /* CONFIG_PM */

static int lirc_rx51_probe(struct platform_device *dev)
{
	lirc_rx51_driver.features = LIRC_RX51_DRIVER_FEATURES;
	lirc_rx51.pdata = dev->dev.platform_data;
	lirc_rx51.pwm_timer_num = lirc_rx51.pdata->pwm_timer;
	lirc_rx51.dev = &dev->dev;
	lirc_rx51_driver.dev = &dev->dev;
	lirc_rx51_driver.minor = lirc_register_driver(&lirc_rx51_driver);
	init_waitqueue_head(&lirc_rx51.wqueue);

	if (lirc_rx51_driver.minor < 0) {
		dev_err(lirc_rx51.dev, ": lirc_register_driver failed: %d\n",
		       lirc_rx51_driver.minor);
		return lirc_rx51_driver.minor;
	}
	dev_info(lirc_rx51.dev, "registration ok, minor: %d, pwm: %d\n",
		 lirc_rx51_driver.minor, lirc_rx51.pwm_timer_num);

	return 0;
}

static int lirc_rx51_remove(struct platform_device *dev)
{
	return lirc_unregister_driver(lirc_rx51_driver.minor);
}

struct platform_driver lirc_rx51_platform_driver = {
	.probe		= lirc_rx51_probe,
	.remove		= lirc_rx51_remove,
	.suspend	= lirc_rx51_suspend,
	.resume		= lirc_rx51_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};
module_platform_driver(lirc_rx51_platform_driver);

MODULE_DESCRIPTION("LIRC TX driver for Nokia RX51");
MODULE_AUTHOR("Nokia Corporation");
MODULE_LICENSE("GPL");
