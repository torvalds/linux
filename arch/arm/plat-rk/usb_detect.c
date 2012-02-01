#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/board.h>

static irqreturn_t usb_detect_irq_handler(int irq, void *dev_id);
static int detect_gpio = INVALID_GPIO;

static void usb_detect_do_wakeup(struct work_struct *work)
{
	int ret;
	int irq = gpio_to_irq(detect_gpio);
	unsigned long flags;

	rk28_send_wakeup_key();
	free_irq(irq, NULL);
	flags = gpio_get_value(detect_gpio) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	ret = request_irq(irq, usb_detect_irq_handler, flags, "usb_detect", NULL);
	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed\n", __func__, irq);
	}
}

static DECLARE_DELAYED_WORK(wakeup_work, usb_detect_do_wakeup);
static bool wakelock_inited;
static struct wake_lock usb_wakelock;

static irqreturn_t usb_detect_irq_handler(int irq, void *dev_id)
{
	wake_lock_timeout(&usb_wakelock, 10 * HZ);
	schedule_delayed_work(&wakeup_work, HZ / 10);
	return IRQ_HANDLED;
}

int board_usb_detect_init(unsigned gpio)
{
	int ret;
	int irq = gpio_to_irq(gpio);
	unsigned long flags;

	if (detect_gpio != INVALID_GPIO) {
		pr_err("only support call %s once\n", __func__);
		return -EINVAL;
	}

	ret = gpio_request(gpio, "usb_detect");
	if (ret < 0) {
		pr_err("%s: gpio_request(%d) failed\n", __func__, gpio);
		return ret;
	}

	if (!wakelock_inited) {
		wake_lock_init(&usb_wakelock, WAKE_LOCK_SUSPEND, "usb_detect");
		wakelock_inited = true;
	}

	gpio_direction_input(gpio);

	detect_gpio = gpio;

	flags = gpio_get_value(gpio) ? IRQF_TRIGGER_FALLING : IRQF_TRIGGER_RISING;
	ret = request_irq(irq, usb_detect_irq_handler, flags, "usb_detect", NULL);
	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(gpio);
		detect_gpio = INVALID_GPIO;
		return ret;
	}
	enable_irq_wake(irq);

	return 0;
}

