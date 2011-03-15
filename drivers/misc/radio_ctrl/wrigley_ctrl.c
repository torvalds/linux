/*
 * Copyright (C) 2011 Motorola, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307  USA
 */
#include <linux/cdev.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/wakelock.h>
#include <linux/radio_ctrl/radio_class.h>
#include <linux/radio_ctrl/wrigley_ctrl.h>

#define GPIO_MAX_NAME 30

enum wrigley_status {
	WRIGLEY_STATUS_NORMAL,
	WRIGLEY_STATUS_FLASH,
	WRIGLEY_STATUS_RESETTING,
	WRIGLEY_STATUS_OFF,
	WRIGLEY_STATUS_UNDEFINED,
};

static const char *wrigley_status_str[] = {
	[WRIGLEY_STATUS_NORMAL] = "normal",
	[WRIGLEY_STATUS_FLASH] = "flash",
	[WRIGLEY_STATUS_RESETTING] = "resetting",
	[WRIGLEY_STATUS_OFF] = "off",
	[WRIGLEY_STATUS_UNDEFINED] = "undefined",
};

struct wrigley_info {
	unsigned int disable_gpio;
	char disable_name[GPIO_MAX_NAME];

	unsigned int flash_gpio;
	char flash_name[GPIO_MAX_NAME];

	unsigned int reset_gpio;
	char reset_name[GPIO_MAX_NAME];

	bool boot_flash;
	enum wrigley_status status;

	struct radio_dev rdev;
};

static ssize_t wrigley_status_show(struct radio_dev *rdev, char *buff)
{
	struct wrigley_info *info =
		container_of(rdev, struct wrigley_info, rdev);

	pr_debug("%s: wrigley_status = %d\n", __func__, info->status);
	if (info->status > WRIGLEY_STATUS_UNDEFINED)
		info->status = WRIGLEY_STATUS_UNDEFINED;

	return snprintf(buff, RADIO_STATUS_MAX_LENGTH, "%s\n",
		wrigley_status_str[info->status]);
}

static ssize_t wrigley_do_powerdown(struct wrigley_info *info)
{
	int i, value, err = -1;

	pr_info("%s: powering down\n", __func__);
	gpio_direction_output(info->disable_gpio, 0);

	for (i = 0; i < 10; i++) {
		value = gpio_get_value(info->reset_gpio);
		pr_debug("%s: reset value = %d\n", __func__, value);
		if (!value) {
			err = 0;
			info->status = WRIGLEY_STATUS_OFF;
			break;
		}
		msleep(100);
	}

	return err;
}

/* hard reset of Wrigley data card
 * recipe is:
 * 1) set force_flash high
 * 2) configure reset as output and drive low for 10ms
 * 3) configure reset as input
 * 4) set force flash low
 * 5) verify data card reset by sampling reset
 */
static ssize_t wrigley_do_reset(struct wrigley_info *info)
{
	int i;
	int value;
	int err = -1;

	gpio_direction_output(info->flash_gpio, 1);

	gpio_direction_output(info->reset_gpio, 0);
	msleep(10);
	gpio_set_value(info->reset_gpio, 1);

	gpio_direction_input(info->reset_gpio);
	gpio_set_value(info->flash_gpio, 0);
	for (i = 0; i < 10; i++) {
		value = gpio_get_value(info->reset_gpio);
		pr_info("%s: reset value = %d\n", __func__, value);
		if (!value) {
			err = 0;
			info->status = WRIGLEY_STATUS_OFF;
			break;
		}
		msleep(100);
	}

	return err;
}

static ssize_t wrigley_do_powerup(struct wrigley_info *info)
{
	int i, value, err = -1;

	pr_debug("%s: enter\n", __func__);

	/* power on in normal or flash mode */
	if (info->boot_flash)
		gpio_direction_output(info->flash_gpio, 1);
	else
		gpio_direction_output(info->flash_gpio, 0);

	/* set disable high to actually power on the card */
	pr_debug("%s: set disable high\n", __func__);
	gpio_direction_output(info->disable_gpio, 1);
	info->status = WRIGLEY_STATUS_RESETTING;

	/* verify power up by sampling reset */
	for (i = 0; i < 10; i++) {
		value = gpio_get_value(info->reset_gpio);
		pr_debug("%s: reset value = %d\n", __func__, value);
		if (value) {
			err = 0;
			break;
		}
		msleep(100);
	}

	if (!err) {
		if (info->boot_flash) {
			pr_debug("%s: started wrigley in flash mode\n",
				__func__);
			info->status = WRIGLEY_STATUS_FLASH;
		} else {
			pr_debug("%s: started wrigley in normal mode\n",
					__func__);
			info->status = WRIGLEY_STATUS_NORMAL;
		}
	} else {
		pr_err("%s: failed to start wrigley\n", __func__);
		info->status = WRIGLEY_STATUS_UNDEFINED;
	}

	return err;
}

static ssize_t wrigley_set_flash_mode(struct wrigley_info *info, bool enable)
{
	pr_debug("%s: set boot state to %d\n", __func__, enable);
	info->boot_flash = enable;
	return 0;
}

static ssize_t wrigley_command(struct radio_dev *rdev, char *cmd)
{
	struct wrigley_info *info =
		container_of(rdev, struct wrigley_info, rdev);

	pr_info("%s: user command = %s\n", __func__, cmd);

	if (strcmp(cmd, "shutdown") == 0)
		return wrigley_do_powerdown(info);
	else if (strcmp(cmd, "reset") == 0)
		return wrigley_do_reset(info);
	else if (strcmp(cmd, "powerup") == 0)
		return wrigley_do_powerup(info);
	else if (strcmp(cmd, "bootmode_normal") == 0)
		return wrigley_set_flash_mode(info, 0);
	else if (strcmp(cmd, "bootmode_flash") == 0)
		return wrigley_set_flash_mode(info, 1);

	pr_err("%s: command %s not supported\n", __func__, cmd);
	return -EINVAL;
}

static irqreturn_t wrigley_reset_fn(int irq, void *data)
{
	struct wrigley_info *info = (struct wrigley_info *) data;
	pr_debug("%s:  reset irq (%d) fired\n", __func__, irq);
	if (info->rdev.dev)
		kobject_uevent(&info->rdev.dev->kobj, KOBJ_CHANGE);
	return IRQ_HANDLED;
}

static irqreturn_t wrigley_reset_isr(int irq, void *data)
{
	struct wrigley_info *info = (struct wrigley_info *) data;
	pr_debug("%s:  reset irq (%d) fired\n", __func__, irq);
	info->status = WRIGLEY_STATUS_RESETTING;
	return IRQ_WAKE_THREAD;
}

static int __devinit wrigley_probe(struct platform_device *pdev)
{
	struct wrigley_ctrl_platform_data *pdata = pdev->dev.platform_data;
	struct wrigley_info *info;
	int reset_irq, err = 0;

	pr_info("%s: %s\n", __func__, dev_name(&pdev->dev));

	info = kzalloc(sizeof(struct wrigley_info), GFP_KERNEL);
	if (!info) {
		err = -ENOMEM;
		goto err_exit;
	}

	platform_set_drvdata(pdev, info);

	/* setup radio_class device */
	info->rdev.name = dev_name(&pdev->dev);
	info->rdev.status = wrigley_status_show;
	info->rdev.command = wrigley_command;

	/* disable */
	pr_debug("%s: setup wrigley_disable\n", __func__);
	info->disable_gpio = pdata->gpio_disable;
	snprintf(info->disable_name, GPIO_MAX_NAME, "%s-%s",
		dev_name(&pdev->dev), "disable");
	err = gpio_request(info->disable_gpio, info->disable_name);
	if (err) {
		pr_err("%s: err_disable\n", __func__);
		goto err_disable;
	}
	gpio_export(info->disable_gpio, false);

	/* reset */
	pr_debug("%s: setup wrigley_reset\n", __func__);
	info->reset_gpio = pdata->gpio_reset;
	snprintf(info->reset_name, GPIO_MAX_NAME, "%s-%s",
		dev_name(&pdev->dev), "reset");
	err = gpio_request(info->reset_gpio, info->reset_name);
	if (err) {
		pr_err("%s: err requesting reset gpio\n", __func__);
		goto err_reset;
	}
	gpio_direction_input(info->reset_gpio);
	reset_irq = gpio_to_irq(info->reset_gpio);
	err = request_threaded_irq(reset_irq, wrigley_reset_isr,
		wrigley_reset_fn, IRQ_TYPE_EDGE_FALLING, info->reset_name,
		info);
	if (err) {
		pr_err("%s: request irq (%d) %s failed\n",
			__func__, reset_irq, info->reset_name);
		gpio_free(info->reset_gpio);
		goto err_reset;
	}
	gpio_export(info->reset_gpio, false);

	/* force_flash */
	pr_debug("%s: setup wrigley_force_flash\n", __func__);
	info->flash_gpio = pdata->gpio_force_flash;
	snprintf(info->flash_name, GPIO_MAX_NAME, "%s-%s",
		dev_name(&pdev->dev), "flash");
	err = gpio_request(info->flash_gpio, info->flash_name);
	if (err) {
		pr_err("%s: error requesting flash gpio\n", __func__);
		goto err_flash;
	}
	gpio_export(info->flash_gpio, false);

	/* try to determine the boot up mode of the device */
	info->boot_flash = !!gpio_get_value(info->flash_gpio);
	if (gpio_get_value(info->reset_gpio)) {
		if (info->boot_flash)
			info->status = WRIGLEY_STATUS_FLASH;
		else
			info->status = WRIGLEY_STATUS_NORMAL;
	} else
		info->status = WRIGLEY_STATUS_OFF;

	pr_debug("%s: initial status = %s\n", __func__,
		wrigley_status_str[info->status]);

	err = radio_dev_register(&info->rdev);
	if (err) {
		pr_err("%s: failed to register radio device\n", __func__);
		goto err_dev_register;
	}

	return 0;

err_dev_register:
	gpio_free(info->flash_gpio);
err_flash:
	free_irq(reset_irq, info);
	gpio_free(info->reset_gpio);
err_reset:
	gpio_free(info->disable_gpio);
err_disable:
	platform_set_drvdata(pdev, NULL);
	kfree(info);
err_exit:
	return err;
}

static void __devexit wrigley_shutdown(struct platform_device *pdev)
{
	struct wrigley_info *info = platform_get_drvdata(pdev);
	pr_info("%s: %s\n", __func__, dev_name(&pdev->dev));
	(void) wrigley_do_powerdown(info);
}

static int __devexit wrigley_remove(struct platform_device *pdev)
{
	struct wrigley_info *info = platform_get_drvdata(pdev);

	pr_info("%s: %s\n", __func__, dev_name(&pdev->dev));

	radio_dev_unregister(&info->rdev);

	/* flash */
	gpio_free(info->flash_gpio);

	/* reset */
	free_irq(gpio_to_irq(info->reset_gpio), info);
	gpio_free(info->reset_gpio);

	/* disable */
	gpio_free(info->disable_gpio);

	platform_set_drvdata(pdev, NULL);
	kfree(info);

	return 0;
}

static struct platform_driver wrigley_driver = {
	.probe = wrigley_probe,
	.remove = __devexit_p(wrigley_remove),
	.shutdown = __devexit_p(wrigley_shutdown),
	.driver = {
		.name = "wrigley",
		.owner = THIS_MODULE,
	},
};

static int __init wrigley_init(void)
{

	pr_info("%s: initializing %s\n", __func__, wrigley_driver.driver.name);

	return platform_driver_register(&wrigley_driver);
}

static void __exit wrigley_exit(void)
{
	pr_info("%s: exiting %s\n", __func__, wrigley_driver.driver.name);
	return platform_driver_unregister(&wrigley_driver);
}

module_init(wrigley_init);
module_exit(wrigley_exit);

MODULE_AUTHOR("Jim Wylder <james.wylder@motorola.com>");
MODULE_DESCRIPTION("Wrigley Modem Control");
MODULE_LICENSE("GPL");
