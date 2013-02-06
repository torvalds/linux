/*
 * Bluetooth Broadcom GPIO and Low Power Mode control
 *
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
#include <linux/wakelock.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include <asm/mach-types.h>

#include <mach/gpio.h>
#include <plat/gpio-cfg.h>

#define BT_UART_CFG
#define BT_LPM_ENABLE

static struct rfkill *bt_rfkill;

struct bcm_bt_lpm {
	int wake;
	int host_wake;

	struct hrtimer enter_lpm_timer;
	ktime_t enter_lpm_delay;

	struct hci_dev *hdev;

	struct wake_lock wake_lock;
	char wake_lock_name[100];
} bt_lpm;

#ifdef BT_UART_CFG
int bt_is_running;
EXPORT_SYMBOL(bt_is_running);

extern int s3c_gpio_slp_cfgpin(unsigned int pin, unsigned int config);
extern int s3c_gpio_slp_setpull_updown(unsigned int pin, unsigned int config);

static unsigned int bt_uart_on_table[][4] = {
	{EXYNOS5_GPA0(0), 2, 2, S3C_GPIO_PULL_NONE},
	{EXYNOS5_GPA0(1), 2, 2, S3C_GPIO_PULL_NONE},
	{EXYNOS5_GPA0(2), 2, 2, S3C_GPIO_PULL_NONE},
	{EXYNOS5_GPA0(3), 2, 2, S3C_GPIO_PULL_NONE},
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
	if (!gpio_get_value(GPIO_BTREG_ON))
		return;
	if (flag) {
		/* BT RTS Set to HIGH */
		s3c_gpio_cfgpin(EXYNOS5_GPA0(3), S3C_GPIO_OUTPUT);
		s3c_gpio_setpull(EXYNOS5_GPA0(3), S3C_GPIO_PULL_NONE);
		gpio_set_value(EXYNOS5_GPA0(3), 1);
		s3c_gpio_slp_cfgpin(EXYNOS5_GPA0(3), S3C_GPIO_SLP_OUT0);
		s3c_gpio_slp_setpull_updown(EXYNOS5_GPA0(3), S3C_GPIO_PULL_NONE);
	} else {
		/* BT RTS Set to LOW */
		s3c_gpio_cfgpin(EXYNOS5_GPA0(3), S3C_GPIO_OUTPUT);
		gpio_set_value(EXYNOS5_GPA0(3), 0);
		s3c_gpio_cfgpin(EXYNOS5_GPA0(3), S3C_GPIO_SFN(2));
		s3c_gpio_setpull(EXYNOS5_GPA0(3), S3C_GPIO_PULL_NONE);
	}
}
EXPORT_SYMBOL(bt_uart_rts_ctrl);
#endif

static int bcm43241_bt_rfkill_set_power(void *data, bool blocked)
{
	/* rfkill_ops callback. Turn transmitter on when blocked is false */
	if (!blocked) {
		pr_info("[BT] Bluetooth Power On.\n");
#ifdef BT_UART_CFG
		bt_config_gpio_table(ARRAY_SIZE(bt_uart_on_table),
					bt_uart_on_table);
#endif
		gpio_set_value(GPIO_BTREG_ON, 1);
		msleep(50);
	} else {
		pr_info("[BT] Bluetooth Power Off.\n");
		bt_is_running = 0;
		gpio_set_value(GPIO_BTREG_ON, 0);
	}
	return 0;
}

static const struct rfkill_ops bcm43241_bt_rfkill_ops = {
	.set_block = bcm43241_bt_rfkill_set_power,
};

#ifdef BT_LPM_ENABLE
static void set_wake_locked(int wake)
{
	bt_lpm.wake = wake;

	if (!wake)
		wake_unlock(&bt_lpm.wake_lock);

	gpio_set_value(GPIO_BT_WAKE, wake);
}

static enum hrtimer_restart enter_lpm(struct hrtimer *timer)
{
	unsigned long flags;

	if (bt_lpm.hdev != NULL) {
		spin_lock_irqsave(&bt_lpm.hdev->lock, flags);
		set_wake_locked(0);
		spin_unlock_irqrestore(&bt_lpm.hdev->lock, flags);
	}

	bt_is_running = 0;

	return HRTIMER_NORESTART;
}

static void bcm_bt_lpm_exit_lpm_locked(struct hci_dev *hdev)
{
	bt_lpm.hdev = hdev;

	hrtimer_try_to_cancel(&bt_lpm.enter_lpm_timer);
	bt_is_running = 1;
	set_wake_locked(1);

	pr_debug("[BT] bcm_bt_lpm_exit_lpm_locked\n");
	hrtimer_start(&bt_lpm.enter_lpm_timer, bt_lpm.enter_lpm_delay,
		HRTIMER_MODE_REL);
}

static void update_host_wake_locked(int host_wake)
{
	if (host_wake == bt_lpm.host_wake)
		return;

	bt_lpm.host_wake = host_wake;

	bt_is_running = 1;

	if (host_wake) {
		wake_lock(&bt_lpm.wake_lock);
	} else  {
		/* Take a timed wakelock, so that upper layers can take it.
		 * The chipset deasserts the hostwake lock, when there is no
		 * more data to send.
		 */
		wake_lock_timeout(&bt_lpm.wake_lock, HZ/2);
	}
}

static irqreturn_t host_wake_isr(int irq, void *dev)
{
	int host_wake;
	unsigned long flags;

	host_wake = gpio_get_value(GPIO_BT_HOST_WAKE);
	irq_set_irq_type(irq, host_wake ? IRQF_TRIGGER_LOW : IRQF_TRIGGER_HIGH);

	if (!bt_lpm.hdev) {
		bt_lpm.host_wake = host_wake;
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&bt_lpm.hdev->lock, flags);
	update_host_wake_locked(host_wake);
	spin_unlock_irqrestore(&bt_lpm.hdev->lock, flags);

	return IRQ_HANDLED;
}

static int bcm_bt_lpm_init(struct platform_device *pdev)
{
	int irq;
	int ret;

	hrtimer_init(&bt_lpm.enter_lpm_timer, CLOCK_MONOTONIC,
			HRTIMER_MODE_REL);
	bt_lpm.enter_lpm_delay = ktime_set(1, 0);  /* 1 sec */
	bt_lpm.enter_lpm_timer.function = enter_lpm;

	bt_lpm.host_wake = 0;
	bt_is_running = 0;

	snprintf(bt_lpm.wake_lock_name, sizeof(bt_lpm.wake_lock_name),
			"BTLowPower");
	wake_lock_init(&bt_lpm.wake_lock, WAKE_LOCK_SUSPEND,
			 bt_lpm.wake_lock_name);

	irq = IRQ_BT_HOST_WAKE;
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

	return 0;
}

static int bcm_hci_wake_peer(struct notifier_block *this, unsigned long event, void *ptr)
{
	struct hci_dev *hdev = (struct hci_dev *) ptr;

	if (event == HCI_DEV_REG) {
		if (hdev != NULL) {
			hdev->wake_peer = bcm_bt_lpm_exit_lpm_locked;
			pr_info("[BT] wake_peer is registered.\n");
		}
	} else if (event == HCI_DEV_UNREG) {
		pr_info("[BT] %s: handle HCI_DEV_UNREG noti\n", __func__);
		if (hdev != NULL && bt_lpm.hdev == hdev) {
			bt_lpm.hdev = NULL;
			pr_info("[BT] bt_lpm.hdev set to NULL\n");
		}
	}

	return NOTIFY_DONE;
}

static struct notifier_block bcm_bt_nblock = {
	.notifier_call = bcm_hci_wake_peer
};
#endif

static int bcm43241_bluetooth_probe(struct platform_device *pdev)
{
	int rc = 0;
	int ret;

	rc = gpio_request(GPIO_BTREG_ON, "bcm43241_bten_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BTREG_ON request failed.\n");
		return rc;
	}
	rc = gpio_request(GPIO_BT_WAKE, "bcm43241_btwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BT_WAKE request failed.\n");
		gpio_free(GPIO_BTREG_ON);
		return rc;
	}
	rc = gpio_request(GPIO_BT_HOST_WAKE, "bcm43241_bthostwake_gpio");
	if (unlikely(rc)) {
		pr_err("[BT] GPIO_BT_HOST_WAKE request failed.\n");
		gpio_free(GPIO_BT_WAKE);
		gpio_free(GPIO_BTREG_ON);
		return rc;
	}
	gpio_direction_input(GPIO_BT_HOST_WAKE);
	gpio_direction_output(GPIO_BT_WAKE, 0);
	gpio_direction_output(GPIO_BTREG_ON, 0);

	bt_rfkill = rfkill_alloc("bcm43241 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm43241_bt_rfkill_ops,
				NULL);

	if (unlikely(!bt_rfkill)) {
		pr_err("[BT] bt_rfkill alloc failed.\n");
		gpio_free(GPIO_BT_HOST_WAKE);
		gpio_free(GPIO_BT_WAKE);
		gpio_free(GPIO_BTREG_ON);
		return -ENOMEM;
	}

	rfkill_init_sw_state(bt_rfkill, 0);

	rc = rfkill_register(bt_rfkill);

	if (unlikely(rc)) {
		pr_err("[BT] bt_rfkill register failed.\n");
		rfkill_destroy(bt_rfkill);
		gpio_free(GPIO_BT_HOST_WAKE);
		gpio_free(GPIO_BT_WAKE);
		gpio_free(GPIO_BTREG_ON);
		return -1;
	}

	rfkill_set_sw_state(bt_rfkill, true);

#ifdef BT_LPM_ENABLE
	ret = bcm_bt_lpm_init(pdev);
	if (ret) {
		rfkill_unregister(bt_rfkill);
		rfkill_destroy(bt_rfkill);

		gpio_free(GPIO_BT_HOST_WAKE);
		gpio_free(GPIO_BT_WAKE);
		gpio_free(GPIO_BTREG_ON);
	}

	hci_register_notifier(&bcm_bt_nblock);
#endif
	return rc;
}

static int bcm43241_bluetooth_remove(struct platform_device *pdev)
{
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);

	gpio_free(GPIO_BTREG_ON);
	gpio_free(GPIO_BT_WAKE);
	gpio_free(GPIO_BT_HOST_WAKE);

	wake_lock_destroy(&bt_lpm.wake_lock);

#ifdef BT_LPM_ENABLE
	hci_unregister_notifier(&bcm_bt_nblock);
#endif
	return 0;
}

static struct platform_driver bcm43241_bluetooth_platform_driver = {
	.probe = bcm43241_bluetooth_probe,
	.remove = bcm43241_bluetooth_remove,
	.driver = {
		   .name = "bcm43241_bluetooth",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcm43241_bluetooth_init(void)
{
	return platform_driver_register(&bcm43241_bluetooth_platform_driver);
}

static void __exit bcm43241_bluetooth_exit(void)
{
	platform_driver_unregister(&bcm43241_bluetooth_platform_driver);
}


module_init(bcm43241_bluetooth_init);
module_exit(bcm43241_bluetooth_exit);

MODULE_ALIAS("platform:bcm43241");
MODULE_DESCRIPTION("bcm43241_bluetooth");
MODULE_LICENSE("GPL");
