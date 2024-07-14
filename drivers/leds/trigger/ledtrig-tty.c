// SPDX-License-Identifier: GPL-2.0

#include <linux/delay.h>
#include <linux/leds.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/tty.h>
#include <uapi/linux/serial.h>

#define LEDTRIG_TTY_INTERVAL	50

struct ledtrig_tty_data {
	struct led_classdev *led_cdev;
	struct delayed_work dwork;
	struct mutex mutex;
	const char *ttyname;
	struct tty_struct *tty;
	int rx, tx;
};

static void ledtrig_tty_restart(struct ledtrig_tty_data *trigger_data)
{
	schedule_delayed_work(&trigger_data->dwork, 0);
}

static ssize_t ttyname_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	ssize_t len = 0;

	mutex_lock(&trigger_data->mutex);

	if (trigger_data->ttyname)
		len = sprintf(buf, "%s\n", trigger_data->ttyname);

	mutex_unlock(&trigger_data->mutex);

	return len;
}

static ssize_t ttyname_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	char *ttyname;
	ssize_t ret = size;
	bool running;

	if (size > 0 && buf[size - 1] == '\n')
		size -= 1;

	if (size) {
		ttyname = kmemdup_nul(buf, size, GFP_KERNEL);
		if (!ttyname)
			return -ENOMEM;
	} else {
		ttyname = NULL;
	}

	mutex_lock(&trigger_data->mutex);

	running = trigger_data->ttyname != NULL;

	kfree(trigger_data->ttyname);
	tty_kref_put(trigger_data->tty);
	trigger_data->tty = NULL;

	trigger_data->ttyname = ttyname;

	mutex_unlock(&trigger_data->mutex);

	if (ttyname && !running)
		ledtrig_tty_restart(trigger_data);

	return ret;
}
static DEVICE_ATTR_RW(ttyname);

static void ledtrig_tty_work(struct work_struct *work)
{
	struct ledtrig_tty_data *trigger_data =
		container_of(work, struct ledtrig_tty_data, dwork.work);
	struct serial_icounter_struct icount;
	int ret;

	mutex_lock(&trigger_data->mutex);

	if (!trigger_data->ttyname) {
		/* exit without rescheduling */
		mutex_unlock(&trigger_data->mutex);
		return;
	}

	/* try to get the tty corresponding to $ttyname */
	if (!trigger_data->tty) {
		dev_t devno;
		struct tty_struct *tty;
		int ret;

		ret = tty_dev_name_to_number(trigger_data->ttyname, &devno);
		if (ret < 0)
			/*
			 * A device with this name might appear later, so keep
			 * retrying.
			 */
			goto out;

		tty = tty_kopen_shared(devno);
		if (IS_ERR(tty) || !tty)
			/* What to do? retry or abort */
			goto out;

		trigger_data->tty = tty;
	}

	ret = tty_get_icount(trigger_data->tty, &icount);
	if (ret) {
		dev_info(trigger_data->tty->dev, "Failed to get icount, stopped polling\n");
		mutex_unlock(&trigger_data->mutex);
		return;
	}

	if (icount.rx != trigger_data->rx ||
	    icount.tx != trigger_data->tx) {
		unsigned long interval = LEDTRIG_TTY_INTERVAL;

		led_blink_set_oneshot(trigger_data->led_cdev, &interval,
				      &interval, 0);

		trigger_data->rx = icount.rx;
		trigger_data->tx = icount.tx;
	}

out:
	mutex_unlock(&trigger_data->mutex);
	schedule_delayed_work(&trigger_data->dwork,
			      msecs_to_jiffies(LEDTRIG_TTY_INTERVAL * 2));
}

static struct attribute *ledtrig_tty_attrs[] = {
	&dev_attr_ttyname.attr,
	NULL
};
ATTRIBUTE_GROUPS(ledtrig_tty);

static int ledtrig_tty_activate(struct led_classdev *led_cdev)
{
	struct ledtrig_tty_data *trigger_data;

	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		return -ENOMEM;

	led_set_trigger_data(led_cdev, trigger_data);

	INIT_DELAYED_WORK(&trigger_data->dwork, ledtrig_tty_work);
	trigger_data->led_cdev = led_cdev;
	mutex_init(&trigger_data->mutex);

	return 0;
}

static void ledtrig_tty_deactivate(struct led_classdev *led_cdev)
{
	struct ledtrig_tty_data *trigger_data = led_get_trigger_data(led_cdev);

	cancel_delayed_work_sync(&trigger_data->dwork);

	kfree(trigger_data->ttyname);
	tty_kref_put(trigger_data->tty);
	trigger_data->tty = NULL;

	kfree(trigger_data);
}

static struct led_trigger ledtrig_tty = {
	.name = "tty",
	.activate = ledtrig_tty_activate,
	.deactivate = ledtrig_tty_deactivate,
	.groups = ledtrig_tty_groups,
};
module_led_trigger(ledtrig_tty);

MODULE_AUTHOR("Uwe Kleine-König <u.kleine-koenig@pengutronix.de>");
MODULE_DESCRIPTION("UART LED trigger");
MODULE_LICENSE("GPL v2");
