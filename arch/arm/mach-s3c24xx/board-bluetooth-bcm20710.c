/*
 * Bluetooth Broadcom GPIO and Low Power Mode control
 *
 *  Copyright (C) 2015 FriendlyARM (www.arm9.net)
 *  Copyright (C) 2011 Samsung Electronics Co., Ltd.
 *  Copyright (C) 2011 Google, Inc.
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

#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/rfkill.h>

#include <mach/gpio-samsung.h>
#include <plat/gpio-cfg.h>
#include <mach/board-bluetooth-bcm.h>

#define BT_UART_CFG
#define BT_LPM_ENABLE

static struct rfkill *bt_rfkill;

struct bcm_bt_lpm {
	int host_wake;

	struct hrtimer enter_lpm_timer;
	ktime_t enter_lpm_delay;

	struct uart_port *uport;

#if defined(CONFIG_WAKELOCK)
	struct wake_lock host_wake_lock;
	struct wake_lock bt_wake_lock;
	char wake_lock_name[100];
#endif
} bt_lpm;

int bt_is_running;
EXPORT_SYMBOL(bt_is_running);

static inline int bt_gpio_en(void) {
	return GPIO_BT_EN;
}

#ifdef BT_UART_CFG
static unsigned int bt_uart_on_table[][4] = {
	{GPIO_BT_RXD, 2, 2, S3C_GPIO_PULL_NONE},
	{GPIO_BT_TXD, 2, 2, S3C_GPIO_PULL_NONE},
	{GPIO_BT_CTS, 2, 2, S3C_GPIO_PULL_NONE},
	{GPIO_BT_RTS, 2, 2, S3C_GPIO_PULL_NONE},
};

void bt_config_gpio_table(int array_size, unsigned int (*gpio_table)[4])
{
	u32 i, gpio;

	for (i = 0; i < array_size; i++) {
		gpio = gpio_table[i][0];
		s3c_gpio_cfgpin(gpio, S3C_GPIO_SFN(gpio_table[i][1]));
		s3c_gpio_setpull(gpio, gpio_table[i][3]);
		if (gpio_table[i][2] != 2)
			gpio_set_value(gpio, gpio_table[i][2]);
	}
}

void bt_uart_rts_ctrl(int flag)
{
	if (!gpio_get_value(bt_gpio_en()))
		return;
	if (flag) {
		/* BT RTS Set to HIGH */
		s3c_gpio_cfgpin(GPIO_BT_RTS, S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(GPIO_BT_RTS, S3C_GPIO_PULL_NONE);
		gpio_set_value(GPIO_BT_RTS, 1);
	} else {
		/* BT RTS Set to LOW */
		s3c_gpio_cfgpin(GPIO_BT_RTS, S3C_GPIO_OUTPUT);
		gpio_set_value(GPIO_BT_RTS, 0);
		s3c_gpio_cfgpin(GPIO_BT_RTS, S3C_GPIO_SFN(2));
		s3c_gpio_setpull(GPIO_BT_RTS, S3C_GPIO_PULL_NONE);
	}
}
EXPORT_SYMBOL(bt_uart_rts_ctrl);
#endif /* !BT_UART_CFG */

static int bcm20710_bt_rfkill_set_power(void *data, bool blocked)
{
	/* rfkill_ops callback. Turn transmitter on when blocked is false */
	msleep(20);
	if (!blocked) {
		pr_info("[BT] Bluetooth Power On.\n");
#ifdef BT_UART_CFG
		bt_config_gpio_table(ARRAY_SIZE(bt_uart_on_table),
				bt_uart_on_table);
#endif

		gpio_set_value(bt_gpio_en(), 1);
		bt_is_running = 1;
	} else {
		pr_info("[BT] Bluetooth Power Off.\n");
		bt_is_running = 0;
		gpio_set_value(bt_gpio_en(), 0);
	}
	msleep(100);
	return 0;
}

static const struct rfkill_ops bcm20710_bt_rfkill_ops = {
	.set_block = bcm20710_bt_rfkill_set_power,
};

#ifdef BT_LPM_ENABLE
static void set_wake_locked(int wake)
{
	if (wake) {
#if defined(CONFIG_WAKELOCK)
		wake_lock(&bt_lpm.bt_wake_lock);
#endif
	}

#ifdef GPIO_BT_WAKE
	gpio_set_value(GPIO_BT_WAKE, wake);
	pr_info("[BT] set_wake_locked %d\n", wake);
#endif
}

static enum hrtimer_restart enter_lpm(struct hrtimer *timer)
{
	if (bt_lpm.uport != NULL) {
		set_wake_locked(0);
	}

	bt_is_running = 0;
#if defined(CONFIG_WAKELOCK)
	wake_lock_timeout(&bt_lpm.bt_wake_lock, HZ/2);
#endif

	return HRTIMER_NORESTART;
}

void bcm_bt_lpm_exit_lpm_locked(struct uart_port *uport)
{
	bt_lpm.uport = uport;

	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);
	bt_is_running = 1;
	set_wake_locked(1);

	pr_debug("[BT] bt_lpm_exit_lpm_locked.\n");
	hrtimer_start(&bt_lpm.enter_lpm_timer, bt_lpm.enter_lpm_delay,
			HRTIMER_MODE_REL);
}

#ifdef GPIO_BT_HOST_WAKE
static void update_host_wake_locked(int host_wake)
{
	if (host_wake == bt_lpm.host_wake)
		return;

	bt_lpm.host_wake = host_wake;

	bt_is_running = 1;

#if defined(CONFIG_WAKELOCK)
	if (host_wake) {
		wake_lock(&bt_lpm.host_wake_lock);
	} else {
		/* Take a timed wakelock, so that upper layers can take it.
		 * The chipset deasserts the hostwake lock, when there is no
		 * more data to send.
		 */
		wake_lock_timeout(&bt_lpm.host_wake_lock, HZ/2);
	}
#endif
}

static irqreturn_t host_wake_isr(int irq, void *dev)
{
	int host_wake;

	host_wake = gpio_get_value(GPIO_BT_HOST_WAKE);
	irq_set_irq_type(irq, host_wake ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH);

	if (!bt_lpm.uport) {
		bt_lpm.host_wake = host_wake;
		return IRQ_HANDLED;
	}

	update_host_wake_locked(host_wake);

	return IRQ_HANDLED;
}
#endif

static int bcm_bt_lpm_init(struct platform_device *pdev)
{
#ifdef GPIO_BT_HOST_WAKE
	int irq;
	int ret;
#endif

	hrtimer_init(&bt_lpm.enter_lpm_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	bt_lpm.enter_lpm_delay = ktime_set(4, 0);
	bt_lpm.enter_lpm_timer.function = enter_lpm;

	bt_lpm.host_wake = 0;
	bt_is_running = 0;

#if defined(CONFIG_WAKELOCK)
	snprintf(bt_lpm.wake_lock_name, sizeof(bt_lpm.wake_lock_name),
			"BT_host_wake");
	wake_lock_init(&bt_lpm.host_wake_lock, WAKE_LOCK_SUSPEND,
			 bt_lpm.wake_lock_name);

	snprintf(bt_lpm.wake_lock_name, sizeof(bt_lpm.wake_lock_name),
			"BT_bt_wake");
	wake_lock_init(&bt_lpm.bt_wake_lock, WAKE_LOCK_SUSPEND,
			 bt_lpm.wake_lock_name);
#endif

#ifdef GPIO_BT_HOST_WAKE
	irq = gpio_to_irq(GPIO_BT_HOST_WAKE);
	ret = request_irq(irq, host_wake_isr, IRQF_TRIGGER_HIGH,
			"bt host_wake", NULL);
	if (ret) {
		pr_err("[BT] Request_host wake irq failed.\n");
		return ret;
	}

	ret = irq_set_irq_wake(irq, 1);
	if (ret) {
		pr_err("[BT] Set_irq_wake failed.\n");
		return ret;
	}
#endif

	return 0;
}
#endif /* !BT_LPM_ENABLE */

static int bcm20710_bluetooth_probe(struct platform_device *pdev)
{
	int rc = 0;

	rc = gpio_request(bt_gpio_en(), "bcm20710_bten_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BT_EN request failed.\n");
		goto err_gpio_bt_en;
	}

#ifdef GPIO_BT_WAKE
	rc = gpio_request(GPIO_BT_WAKE, "bcm20710_btwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BT_WAKE request failed.\n");
		goto err_gpio_bt_wake;
	}
#endif

#ifdef GPIO_BT_HOST_WAKE
	rc = gpio_request(GPIO_BT_HOST_WAKE, "bcm20710_bthostwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BT_HOST_WAKE request failed.\n");
		goto err_gpio_bt_host_wake;
	}
	gpio_direction_input(GPIO_BT_HOST_WAKE);
#endif

#ifdef GPIO_BT_WAKE
	gpio_direction_output(GPIO_BT_WAKE, 0);
#endif
	gpio_direction_output(bt_gpio_en(), 0);

	bt_rfkill = rfkill_alloc("bcm20710 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm20710_bt_rfkill_ops,
				NULL);
	if (unlikely(!bt_rfkill)) {
		pr_err("[BT] bt_rfkill alloc failed.\n");
		rc = -ENOMEM;
		goto err_rfkill_alloc;
	}

	rfkill_init_sw_state(bt_rfkill, 0);

	rc = rfkill_register(bt_rfkill);
	if (unlikely(rc)) {
		pr_err("[BT] bt_rfkill register failed.\n");
		rc = -1;
		goto err_rfkill_register;
	}

	rfkill_set_sw_state(bt_rfkill, true);

#ifdef BT_LPM_ENABLE
	rc = bcm_bt_lpm_init(pdev);
	if (rc) {
		goto err_lpm_init;
	}
#endif
	return rc;

#ifdef BT_LPM_ENABLE
err_lpm_init:
	rfkill_unregister(bt_rfkill);
#endif
err_rfkill_register:
	rfkill_destroy(bt_rfkill);
err_rfkill_alloc:
#ifdef GPIO_BT_HOST_WAKE
	gpio_free(GPIO_BT_HOST_WAKE);
err_gpio_bt_host_wake:
#endif
#ifdef GPIO_BT_WAKE
	gpio_free(GPIO_BT_WAKE);
err_gpio_bt_wake:
#endif
	gpio_free(bt_gpio_en());
err_gpio_bt_en:
	return rc;
}

static int bcm20710_bluetooth_remove(struct platform_device *pdev)
{
#ifdef BT_LPM_ENABLE
	int irq;

#ifdef GPIO_BT_HOST_WAKE
	irq = gpio_to_irq(GPIO_BT_HOST_WAKE);
	irq_set_irq_wake(irq, 0);
	free_irq(irq, NULL);
#endif

#if defined(CONFIG_WAKELOCK)
	set_wake_locked(0);
#endif
	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);

#if defined(CONFIG_WAKELOCK)
	wake_lock_destroy(&bt_lpm.host_wake_lock);
	wake_lock_destroy(&bt_lpm.bt_wake_lock);
#endif
#endif

	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);

	gpio_free(bt_gpio_en());
#ifdef GPIO_BT_WAKE
	gpio_free(GPIO_BT_WAKE);
#endif
#ifdef GPIO_BT_HOST_WAKE
	gpio_free(GPIO_BT_HOST_WAKE);
#endif

	return 0;
}

static struct platform_driver bcm20710_bluetooth_platform_driver = {
	.probe = bcm20710_bluetooth_probe,
	.remove = bcm20710_bluetooth_remove,
	.driver = {
		.name = "bcm20710_bluetooth",
		.owner = THIS_MODULE,
	},
};

static int __init bcm20710_bluetooth_init(void)
{
	return platform_driver_register(&bcm20710_bluetooth_platform_driver);
}

static void __exit bcm20710_bluetooth_exit(void)
{
	platform_driver_unregister(&bcm20710_bluetooth_platform_driver);
}

module_init(bcm20710_bluetooth_init);
module_exit(bcm20710_bluetooth_exit);

MODULE_ALIAS("platform:bcm20710");
MODULE_DESCRIPTION("bcm20710_bluetooth");
MODULE_LICENSE("GPL");
