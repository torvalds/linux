// SPDX-License-Identifier: GPL-2.0

#include <linux/completion.h>
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
	struct completion sysfs;
	const char *ttyname;
	struct tty_struct *tty;
	int rx, tx;
	bool mode_rx;
	bool mode_tx;
};

/* Indicates which state the LED should now display */
enum led_trigger_tty_state {
	TTY_LED_BLINK,
	TTY_LED_DISABLE,
};

enum led_trigger_tty_modes {
	TRIGGER_TTY_RX = 0,
	TRIGGER_TTY_TX,
};

static int ledtrig_tty_wait_for_completion(struct device *dev)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	int ret;

	ret = wait_for_completion_timeout(&trigger_data->sysfs,
					  msecs_to_jiffies(LEDTRIG_TTY_INTERVAL * 20));
	if (ret == 0)
		return -ETIMEDOUT;

	return ret;
}

static ssize_t ttyname_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	ssize_t len = 0;
	int completion;

	reinit_completion(&trigger_data->sysfs);
	completion = ledtrig_tty_wait_for_completion(dev);
	if (completion < 0)
		return completion;

	if (trigger_data->ttyname)
		len = sprintf(buf, "%s\n", trigger_data->ttyname);

	return len;
}

static ssize_t ttyname_store(struct device *dev,
			     struct device_attribute *attr, const char *buf,
			     size_t size)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	char *ttyname;
	ssize_t ret = size;
	int completion;

	if (size > 0 && buf[size - 1] == '\n')
		size -= 1;

	if (size) {
		ttyname = kmemdup_nul(buf, size, GFP_KERNEL);
		if (!ttyname)
			return -ENOMEM;
	} else {
		ttyname = NULL;
	}

	reinit_completion(&trigger_data->sysfs);
	completion = ledtrig_tty_wait_for_completion(dev);
	if (completion < 0)
		return completion;

	kfree(trigger_data->ttyname);
	tty_kref_put(trigger_data->tty);
	trigger_data->tty = NULL;

	trigger_data->ttyname = ttyname;

	return ret;
}
static DEVICE_ATTR_RW(ttyname);

static ssize_t ledtrig_tty_attr_show(struct device *dev, char *buf,
				     enum led_trigger_tty_modes attr)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	bool state;

	switch (attr) {
	case TRIGGER_TTY_RX:
		state = trigger_data->mode_rx;
		break;
	case TRIGGER_TTY_TX:
		state = trigger_data->mode_tx;
		break;
	}

	return sysfs_emit(buf, "%u\n", state);
}

static ssize_t ledtrig_tty_attr_store(struct device *dev, const char *buf,
				      size_t size, enum led_trigger_tty_modes attr)
{
	struct ledtrig_tty_data *trigger_data = led_trigger_get_drvdata(dev);
	bool state;
	int ret;

	ret = kstrtobool(buf, &state);
	if (ret)
		return ret;

	switch (attr) {
	case TRIGGER_TTY_RX:
		trigger_data->mode_rx = state;
		break;
	case TRIGGER_TTY_TX:
		trigger_data->mode_tx = state;
		break;
	}

	return size;
}

#define DEFINE_TTY_TRIGGER(trigger_name, trigger) \
	static ssize_t trigger_name##_show(struct device *dev, \
		struct device_attribute *attr, char *buf) \
	{ \
		return ledtrig_tty_attr_show(dev, buf, trigger); \
	} \
	static ssize_t trigger_name##_store(struct device *dev, \
		struct device_attribute *attr, const char *buf, size_t size) \
	{ \
		return ledtrig_tty_attr_store(dev, buf, size, trigger); \
	} \
	static DEVICE_ATTR_RW(trigger_name)

DEFINE_TTY_TRIGGER(rx, TRIGGER_TTY_RX);
DEFINE_TTY_TRIGGER(tx, TRIGGER_TTY_TX);

static void ledtrig_tty_work(struct work_struct *work)
{
	struct ledtrig_tty_data *trigger_data =
		container_of(work, struct ledtrig_tty_data, dwork.work);
	enum led_trigger_tty_state state = TTY_LED_DISABLE;
	unsigned long interval = LEDTRIG_TTY_INTERVAL;
	int ret;

	if (!trigger_data->ttyname)
		goto out;

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

	if (trigger_data->mode_rx || trigger_data->mode_tx) {
		struct serial_icounter_struct icount;

		ret = tty_get_icount(trigger_data->tty, &icount);
		if (ret)
			goto out;

		if (trigger_data->mode_tx && (icount.tx != trigger_data->tx)) {
			trigger_data->tx = icount.tx;
			state = TTY_LED_BLINK;
		}

		if (trigger_data->mode_rx && (icount.rx != trigger_data->rx)) {
			trigger_data->rx = icount.rx;
			state = TTY_LED_BLINK;
		}
	}

out:
	switch (state) {
	case TTY_LED_BLINK:
		led_blink_set_oneshot(trigger_data->led_cdev, &interval,
				&interval, 0);
		break;
	case TTY_LED_DISABLE:
		fallthrough;
	default:
		led_set_brightness(trigger_data->led_cdev, LED_OFF);
		break;
	}

	complete_all(&trigger_data->sysfs);
	schedule_delayed_work(&trigger_data->dwork,
			      msecs_to_jiffies(LEDTRIG_TTY_INTERVAL * 2));
}

static struct attribute *ledtrig_tty_attrs[] = {
	&dev_attr_ttyname.attr,
	&dev_attr_rx.attr,
	&dev_attr_tx.attr,
	NULL
};
ATTRIBUTE_GROUPS(ledtrig_tty);

static int ledtrig_tty_activate(struct led_classdev *led_cdev)
{
	struct ledtrig_tty_data *trigger_data;

	trigger_data = kzalloc(sizeof(*trigger_data), GFP_KERNEL);
	if (!trigger_data)
		return -ENOMEM;

	/* Enable default rx/tx mode */
	trigger_data->mode_rx = true;
	trigger_data->mode_tx = true;

	led_set_trigger_data(led_cdev, trigger_data);

	INIT_DELAYED_WORK(&trigger_data->dwork, ledtrig_tty_work);
	trigger_data->led_cdev = led_cdev;
	init_completion(&trigger_data->sysfs);

	schedule_delayed_work(&trigger_data->dwork, 0);

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
