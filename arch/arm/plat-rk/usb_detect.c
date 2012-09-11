#include <linux/wakelock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <mach/board.h>

#define WAKE_LOCK_TIMEOUT	(10 * HZ)

static irqreturn_t usb_detect_irq_handler(int irq, void *dev_id);
static int detect_gpio = INVALID_GPIO;

static void usb_detect_do_wakeup(struct work_struct *work)
{
	int ret;
	int irq = gpio_to_irq(detect_gpio);
	unsigned int type;

	rk28_send_wakeup_key();
	type = gpio_get_value(detect_gpio) ? IRQ_TYPE_EDGE_FALLING : IRQ_TYPE_EDGE_RISING;
	ret = irq_set_irq_type(irq, type);
	if (ret < 0) {
		pr_err("%s: irq_set_irq_type(%d, %d) failed\n", __func__, irq, type);
	}
}

static DECLARE_DELAYED_WORK(wakeup_work, usb_detect_do_wakeup);
static bool wakelock_inited;
static struct wake_lock usb_wakelock;

static irqreturn_t usb_detect_irq_handler(int irq, void *dev_id)
{
	wake_lock_timeout(&usb_wakelock, WAKE_LOCK_TIMEOUT);
	schedule_delayed_work(&wakeup_work, HZ / 10);
	return IRQ_HANDLED;
}

int __init board_usb_detect_init(unsigned gpio)
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

#if defined(CONFIG_ARCH_RK2928)
#include <linux/io.h>
#include <mach/iomux.h>

static irqreturn_t bvalid_irq_handler(int irq, void *dev_id)
{
	/* clear irq */
#ifdef CONFIG_ARCH_RK2928
	writel_relaxed((1 << 31) | (1 << 15), RK2928_GRF_BASE + GRF_UOC0_CON5);
#endif
#ifdef CONFIG_RK_USB_UART 
    /* usb otg dp/dm switch to usb phy */
    writel_relaxed((3 << (12 + 16)),RK2928_GRF_BASE + GRF_UOC1_CON4);
#endif

	wake_lock_timeout(&usb_wakelock, WAKE_LOCK_TIMEOUT);
	rk28_send_wakeup_key();

	return IRQ_HANDLED;
}

static int __init bvalid_init(void)
{
	int ret;
	int irq = IRQ_OTG_BVALID;

	if (detect_gpio != INVALID_GPIO) {
		printk("usb detect inited by board_usb_detect_init, disable detect by bvalid irq\n");
		return 0;
	}

	if (!wakelock_inited) {
		wake_lock_init(&usb_wakelock, WAKE_LOCK_SUSPEND, "usb_detect");
		wakelock_inited = true;
	}

	ret = request_irq(irq, bvalid_irq_handler, 0, "bvalid", NULL);
	if (ret < 0) {
		pr_err("%s: request_irq(%d) failed\n", __func__, irq);
		return ret;
	}

	/* clear & enable bvalid irq */
#ifdef CONFIG_ARCH_RK2928
	writel_relaxed((3 << 30) | (3 << 14), RK2928_GRF_BASE + GRF_UOC0_CON5);
#endif

	enable_irq_wake(irq);

	return 0;
}
late_initcall(bvalid_init);
#endif
