/*
 * Copyright (C) 2010 Motorola, Inc.
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
 * 02111-1307, USA
 */

#include <linux/leds.h>
#include <linux/leds-auo-panel-backlight.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>

struct auo_panel_data {
	struct led_classdev led_dev;
	struct auo_panel_bl_platform_data *auo_pdata;
	struct mutex lock;
	int led_on;
};

static uint32_t auo_panel_debug;
module_param_named(auo_bl_debug, auo_panel_debug, uint, 0664);

static void ld_auo_panel_brightness_set(struct led_classdev *led_cdev,
				     enum led_brightness value)
{
	struct auo_panel_data *auo_data =
	    container_of(led_cdev, struct auo_panel_data, led_dev);

	mutex_lock(&auo_data->lock);
	if (value == LED_OFF) {
		if (auo_data->led_on == 1) {
			if (auo_data->auo_pdata->bl_disable) {
				auo_data->auo_pdata->bl_disable();
				auo_data->led_on = 0;
			}
		}
	} else {
		if (auo_data->led_on == 0) {
			if (auo_data->auo_pdata->bl_enable) {
				auo_data->auo_pdata->bl_enable();
				auo_data->led_on = 1;
			}
		}
	}
	mutex_unlock(&auo_data->lock);
}

static int __devinit ld_auo_panel_bl_probe(struct platform_device *pdev)
{
	struct auo_panel_data *auo_data;
	int error = 0;

	if (pdev->dev.platform_data == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;
	}
	auo_data = kzalloc(sizeof(struct auo_panel_data), GFP_KERNEL);
	if (auo_data == NULL)
		return -ENOMEM;

	auo_data->led_dev.name = LD_AUO_PANEL_BL_LED_DEV;
	auo_data->led_dev.brightness_set = ld_auo_panel_brightness_set;

	auo_data->auo_pdata = pdev->dev.platform_data;

	error = led_classdev_register(&pdev->dev, &auo_data->led_dev);
	if (error < 0) {
		pr_err("%s: Register led class failed: %d\n", __func__, error);
		error = -ENODEV;
		kfree(auo_data);
		return error;
	}
	mutex_init(&auo_data->lock);

	mutex_lock(&auo_data->lock);
	auo_data->led_on = 1;
	mutex_unlock(&auo_data->lock);

	platform_set_drvdata(pdev, auo_data);

	return 0;
}

static int __devexit ld_auo_panel_remove(struct platform_device *pdev)
{
	struct auo_panel_data *auo_data = pdev->dev.platform_data;
	led_classdev_unregister(&auo_data->led_dev);
	kfree(auo_data);
	return 0;
}

static struct platform_driver auo_led_driver = {
	.probe		= ld_auo_panel_bl_probe,
	.remove		= __devexit_p(ld_auo_panel_remove),
	.driver		= {
		.name	= LD_AUO_PANEL_BL_NAME,
		.owner	= THIS_MODULE,
	},
};
static int __init ld_auo_panel_init(void)
{
	return platform_driver_register(&auo_led_driver);
}

static void __exit ld_auo_panel_exit(void)
{
	platform_driver_unregister(&auo_led_driver);

}

module_init(ld_auo_panel_init);
module_exit(ld_auo_panel_exit);

MODULE_DESCRIPTION("Lighting driver for the AUO display panel");
MODULE_AUTHOR("Dan Murphy <wldm10@Motorola.com>");
MODULE_LICENSE("GPL");
