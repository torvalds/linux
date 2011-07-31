/*
 * Bluetooth Broadcomm  and low power control via GPIO
 *
 *  Copyright (C) 2010 Google, Inc.
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
#include <linux/init.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/irq.h>
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/wakelock.h>
#include <asm/mach-types.h>

#include "gpio-names.h"

#define BT_SHUTDOWN_GPIO TEGRA_GPIO_PI7
#define BT_RESET_GPIO TEGRA_GPIO_PU0

#define BT_WAKE_GPIO TEGRA_GPIO_PU1
#define BT_HOST_WAKE_GPIO TEGRA_GPIO_PU6

extern void change_power_brcm_4329(bool);
static struct rfkill *bt_rfkill;

struct bcm_bt_lpm {
	int wake;
	int host_wake;
	bool rx_wake_lock_taken;

	struct hrtimer enter_lpm_timer;
	ktime_t enter_lpm_delay;

	struct uart_port *uport;

	struct wake_lock wake_lock;
	char wake_lock_name[100];
} bt_lpm;

static int bcm4329_bt_rfkill_set_power(void *data, bool blocked)
{
	// rfkill_ops callback. Turn transmitter on when blocked is false
	if (!blocked) {
		change_power_brcm_4329(true);
		gpio_direction_output(BT_RESET_GPIO, 1);
		gpio_direction_output(BT_SHUTDOWN_GPIO, 0);
	} else {
		change_power_brcm_4329(false);
		gpio_direction_output(BT_SHUTDOWN_GPIO, 0);
		gpio_direction_output(BT_RESET_GPIO, 0);
	}

	return 0;
}

static const struct rfkill_ops bcm4329_bt_rfkill_ops = {
	.set_block = bcm4329_bt_rfkill_set_power,
};

static void set_wake_locked(int wake)
{
	bt_lpm.wake = wake;

	if (!wake)
		wake_unlock(&bt_lpm.wake_lock);

	gpio_set_value(BT_WAKE_GPIO, wake);
}

static enum hrtimer_restart enter_lpm(struct hrtimer *timer) {
	unsigned long flags;
	spin_lock_irqsave(&bt_lpm.uport->lock, flags);
	set_wake_locked(0);
	spin_unlock_irqrestore(&bt_lpm.uport->lock, flags);

	return HRTIMER_NORESTART;
}

void bcm_bt_lpm_exit_lpm_locked(struct uart_port *uport) {
	bt_lpm.uport = uport;

	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);

	set_wake_locked(1);

	hrtimer_start(&bt_lpm.enter_lpm_timer, bt_lpm.enter_lpm_delay,
		HRTIMER_MODE_REL);
}
EXPORT_SYMBOL(bcm_bt_lpm_exit_lpm_locked);

void bcm_bt_rx_done_locked(struct uart_port *uport) {
	if (bt_lpm.host_wake) {
		// Release wake in 500 ms so that higher layers can take it.
		wake_lock_timeout(&bt_lpm.wake_lock, HZ/2);
		bt_lpm.rx_wake_lock_taken = true;
	}
}
EXPORT_SYMBOL(bcm_bt_rx_done_locked);

static void update_host_wake_locked(int host_wake)
{
	if (host_wake == bt_lpm.host_wake)
		return;

	bt_lpm.host_wake = host_wake;

	if (host_wake) {
		bt_lpm.rx_wake_lock_taken = false;
		wake_lock(&bt_lpm.wake_lock);
	} else if (!bt_lpm.rx_wake_lock_taken) {
		// Failsafe timeout of wakelock.
		// If the host wake pin is asserted and no data is sent,
		// when its deasserted we will enter this path
		wake_lock_timeout(&bt_lpm.wake_lock, HZ/2);
	}

}

static irqreturn_t host_wake_isr(int irq, void *dev)
{
	int host_wake;
	unsigned long flags;

	host_wake = gpio_get_value(BT_HOST_WAKE_GPIO);
	set_irq_type(irq, host_wake ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH);

	if (!bt_lpm.uport) {
		bt_lpm.host_wake = host_wake;
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&bt_lpm.uport->lock, flags);
	update_host_wake_locked(host_wake);
	spin_unlock_irqrestore(&bt_lpm.uport->lock, flags);

	return IRQ_HANDLED;
}

static int bcm_bt_lpm_init(struct platform_device *pdev)
{
	int irq;
	int ret;
	int rc;

	tegra_gpio_enable(BT_WAKE_GPIO);
	rc = gpio_request(BT_WAKE_GPIO, "bcm4329_wake_gpio");
	if (unlikely(rc)) {
		tegra_gpio_disable(BT_WAKE_GPIO);
		return rc;
	}

	tegra_gpio_enable(BT_HOST_WAKE_GPIO);
	rc = gpio_request(BT_HOST_WAKE_GPIO, "bcm4329_host_wake_gpio");
	if (unlikely(rc)) {
		tegra_gpio_disable(BT_WAKE_GPIO);
		tegra_gpio_disable(BT_HOST_WAKE_GPIO);
		gpio_free(BT_WAKE_GPIO);
		return rc;
	}

	hrtimer_init(&bt_lpm.enter_lpm_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	bt_lpm.enter_lpm_delay = ktime_set(1, 0);  /* 1 sec */
	bt_lpm.enter_lpm_timer.function = enter_lpm;

	bt_lpm.host_wake = 0;

	irq = gpio_to_irq(BT_HOST_WAKE_GPIO);
	ret = request_irq(irq, host_wake_isr, IRQF_TRIGGER_HIGH,
		"bt host_wake", NULL);
	if (ret) {
		tegra_gpio_disable(BT_WAKE_GPIO);
		tegra_gpio_disable(BT_HOST_WAKE_GPIO);

		gpio_free(BT_WAKE_GPIO);
		gpio_free(BT_HOST_WAKE_GPIO);
		return ret;
	}

	ret = set_irq_wake(irq, 1);
	if (ret) {
		tegra_gpio_disable(BT_WAKE_GPIO);
		tegra_gpio_disable(BT_HOST_WAKE_GPIO);

		gpio_free(BT_WAKE_GPIO);
		gpio_free(BT_HOST_WAKE_GPIO);
		return ret;
	}

	gpio_direction_output(BT_WAKE_GPIO, 0);
	gpio_direction_input(BT_HOST_WAKE_GPIO);

	snprintf(bt_lpm.wake_lock_name, sizeof(bt_lpm.wake_lock_name),
			"BTLowPower");
	wake_lock_init(&bt_lpm.wake_lock, WAKE_LOCK_SUSPEND,
			 bt_lpm.wake_lock_name);
	return 0;
}

static int bcm4329_bluetooth_probe(struct platform_device *pdev)
{
	int rc = 0;
	int ret = 0;

	tegra_gpio_enable(BT_RESET_GPIO);
	rc = gpio_request(BT_RESET_GPIO, "bcm4329_nreset_gpip");
	if (unlikely(rc)) {
		tegra_gpio_disable(BT_RESET_GPIO);
		return rc;
	}

	tegra_gpio_enable(BT_SHUTDOWN_GPIO);
	rc = gpio_request(BT_SHUTDOWN_GPIO, "bcm4329_nshutdown_gpio");
	if (unlikely(rc)) {
		tegra_gpio_disable(BT_RESET_GPIO);
		tegra_gpio_disable(BT_SHUTDOWN_GPIO);
		gpio_free(BT_RESET_GPIO);
		return rc;
	}


	bcm4329_bt_rfkill_set_power(NULL, true);

	bt_rfkill = rfkill_alloc("bcm4329 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm4329_bt_rfkill_ops,
				NULL);

	if (unlikely(!bt_rfkill)) {
		tegra_gpio_disable(BT_RESET_GPIO);
		tegra_gpio_disable(BT_SHUTDOWN_GPIO);

		gpio_free(BT_RESET_GPIO);
		gpio_free(BT_SHUTDOWN_GPIO);
		return -ENOMEM;
	}

	rfkill_set_states(bt_rfkill, true, false);

	rc = rfkill_register(bt_rfkill);

	if (unlikely(rc)) {
		rfkill_destroy(bt_rfkill);
		tegra_gpio_disable(BT_RESET_GPIO);
		tegra_gpio_disable(BT_SHUTDOWN_GPIO);

		gpio_free(BT_RESET_GPIO);
		gpio_free(BT_SHUTDOWN_GPIO);
		return -1;
	}

	ret = bcm_bt_lpm_init(pdev);
	if (ret) {
		rfkill_unregister(bt_rfkill);
		rfkill_destroy(bt_rfkill);

		tegra_gpio_disable(BT_RESET_GPIO);
		tegra_gpio_disable(BT_SHUTDOWN_GPIO);

		gpio_free(BT_RESET_GPIO);
		gpio_free(BT_SHUTDOWN_GPIO);
	}

	return ret;
}

static int bcm4329_bluetooth_remove(struct platform_device *pdev)
{
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);

	tegra_gpio_disable(BT_SHUTDOWN_GPIO);
	tegra_gpio_disable(BT_RESET_GPIO);
	tegra_gpio_disable(BT_WAKE_GPIO);
	tegra_gpio_disable(BT_HOST_WAKE_GPIO);

	gpio_free(BT_SHUTDOWN_GPIO);
	gpio_free(BT_RESET_GPIO);
	gpio_free(BT_WAKE_GPIO);
	gpio_free(BT_HOST_WAKE_GPIO);

	wake_lock_destroy(&bt_lpm.wake_lock);
	return 0;
}

static struct platform_driver bcm4329_bluetooth_platform_driver = {
	.probe = bcm4329_bluetooth_probe,
	.remove = bcm4329_bluetooth_remove,
	.driver = {
		   .name = "bcm4329_bluetooth",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcm4329_bluetooth_init(void)
{
	return platform_driver_register(&bcm4329_bluetooth_platform_driver);
}

static void __exit bcm4329_bluetooth_exit(void)
{
	platform_driver_unregister(&bcm4329_bluetooth_platform_driver);
}


module_init(bcm4329_bluetooth_init);
module_exit(bcm4329_bluetooth_exit);

MODULE_ALIAS("platform:bcm4329");
MODULE_DESCRIPTION("bcm4329_bluetooth");
MODULE_AUTHOR("Jaikumar Ganesh <jaikumar@google.com>");
MODULE_LICENSE("GPL");
