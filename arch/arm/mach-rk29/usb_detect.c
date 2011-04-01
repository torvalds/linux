#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <mach/board.h>

static void do_wakeup(struct work_struct *work)
{
	rk28_send_wakeup_key();
}

static DECLARE_DELAYED_WORK(wakeup_work, do_wakeup);
static bool wakelock_inited;
static struct wake_lock usb_wakelock;

static irqreturn_t detect_irq_handler(int irq, void *dev_id)
{
        wake_lock_timeout(&usb_wakelock, 10 * HZ);
	schedule_delayed_work(&wakeup_work, HZ / 10);
        return IRQ_HANDLED;
}

int board_usb_detect_init(unsigned gpio, unsigned long flags)
{
	int ret;
	int irq = gpio_to_irq(gpio);

	ret = gpio_request(gpio, "usb_detect");
	if (ret < 0) {
		pr_err("%s: gpio_request(%d) failed\n", __func__, gpio);
		return ret;
	}

	if (!wakelock_inited)
		wake_lock_init(&usb_wakelock, WAKE_LOCK_SUSPEND, "usb_detect");

	gpio_direction_input(gpio);
	ret = request_irq(irq, detect_irq_handler, flags, "usb_detect", NULL);
	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed\n", __func__, irq);
		gpio_free(gpio);
		return ret;
	}
	enable_irq_wake(irq);

	return 0;
}

