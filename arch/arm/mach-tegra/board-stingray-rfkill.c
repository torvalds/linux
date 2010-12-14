/*
 * Bluetooth Broadcomm rfkill power control via GPIO
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
#include <linux/rfkill.h>
#include <linux/platform_device.h>
#include <asm/mach-types.h>

#include "gpio-names.h"

#define BT_SHUTDOWN_GPIO TEGRA_GPIO_PI7
#define BT_RESET_GPIO TEGRA_GPIO_PU0

static struct rfkill *bt_rfkill;

static int bcm4329_bt_rfkill_set_power(void *data, bool blocked)
{
	if (blocked) {
		gpio_direction_output(BT_SHUTDOWN_GPIO, 0);
		gpio_direction_output(BT_RESET_GPIO, 0);
	} else {
		gpio_direction_output(BT_RESET_GPIO, 1);
		gpio_direction_output(BT_SHUTDOWN_GPIO, 1);
	}

	return 0;
}

static const struct rfkill_ops bcm4329_bt_rfkill_ops = {
	.set_block = bcm4329_bt_rfkill_set_power,
};

static int bcm4329_rfkill_probe(struct platform_device *pdev)
{
	int rc = 0;
	bool default_state = true;  /* off */

	tegra_gpio_enable(BT_RESET_GPIO);
	rc = gpio_request(BT_RESET_GPIO, "bcm4329_nreset_gpip");
	if (unlikely(rc))
		return rc;


	tegra_gpio_enable(BT_SHUTDOWN_GPIO);
	rc = gpio_request(BT_SHUTDOWN_GPIO, "bcm4329_nshutdown_gpio");
	if (unlikely(rc))
		return rc;

	bcm4329_bt_rfkill_set_power(NULL, default_state);

	bt_rfkill = rfkill_alloc("bcm4329 Bluetooth", &pdev->dev,
				RFKILL_TYPE_BLUETOOTH, &bcm4329_bt_rfkill_ops,
				NULL);

	if (unlikely(!bt_rfkill))
		return -ENOMEM;

	rfkill_set_states(bt_rfkill, default_state, false);

	rc = rfkill_register(bt_rfkill);

	if (unlikely(rc))
		rfkill_destroy(bt_rfkill);

	return 0;
}

static int bcm4329_rfkill_remove(struct platform_device *pdev)
{
	rfkill_unregister(bt_rfkill);
	rfkill_destroy(bt_rfkill);
	gpio_free(BT_SHUTDOWN_GPIO);
	gpio_free(BT_RESET_GPIO);

	return 0;
}

static struct platform_driver bcm4329_rfkill_platform_driver = {
	.probe = bcm4329_rfkill_probe,
	.remove = bcm4329_rfkill_remove,
	.driver = {
		   .name = "bcm4329_rfkill",
		   .owner = THIS_MODULE,
		   },
};

static int __init bcm4329_rfkill_init(void)
{
	if (!machine_is_stingray())
		return 0;
	return platform_driver_register(&bcm4329_rfkill_platform_driver);
}

static void __exit bcm4329_rfkill_exit(void)
{
	platform_driver_unregister(&bcm4329_rfkill_platform_driver);
}

module_init(bcm4329_rfkill_init);
module_exit(bcm4329_rfkill_exit);

MODULE_ALIAS("platform:bcm4329");
MODULE_DESCRIPTION("bcm4329_rfkill");
MODULE_AUTHOR("Jaikumar Ganesh <jaikumar@google.com>");
MODULE_LICENSE("GPL");
