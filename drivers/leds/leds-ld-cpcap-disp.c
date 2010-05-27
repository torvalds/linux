/*
 * Copyright (C) 2009 Motorola, Inc.
 *
 * This program is free dispware; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free dispware Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free dispware
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307, USA
 */

#include <linux/err.h>
#include <linux/leds.h>
#include <linux/leds-ld-cpcap.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/cpcap.h>
#include <linux/spi/cpcap-regbits.h>

struct disp_button_led_data {
	struct led_classdev disp_button_class_dev;
	struct cpcap_device *cpcap;
	struct regulator *regulator;
	struct work_struct brightness_work;
	enum led_brightness brightness;
	int regulator_state;
};

static void disp_button_set(struct led_classdev *led_cdev,
			    enum led_brightness brightness)
{
	struct disp_button_led_data *disp_button_led_data =
		container_of(led_cdev, struct disp_button_led_data,
		disp_button_class_dev);

	if (brightness > 255)
		brightness = 255;

	disp_button_led_data->brightness = brightness;
	schedule_work(&disp_button_led_data->brightness_work);
}
EXPORT_SYMBOL(disp_button_set);

static void disp_button_brightness_work(struct work_struct *work)
{
	int cpcap_status = 0;
	unsigned short brightness = 0;

	struct disp_button_led_data *disp_button_led_data =
	    container_of(work, struct disp_button_led_data, brightness_work);

	brightness = disp_button_led_data->brightness;

	if (brightness > 0) {
		brightness = (LD_DISP_BUTTON_DUTY_CYCLE |
			LD_DISP_BUTTON_CURRENT | LD_DISP_BUTTON_ON);

		if ((disp_button_led_data->regulator) &&
		    (disp_button_led_data->regulator_state == 0)) {
			regulator_enable(disp_button_led_data->regulator);
			disp_button_led_data->regulator_state = 1;
		}

		cpcap_status = cpcap_regacc_write(disp_button_led_data->cpcap,
						  CPCAP_REG_KLC,
						  brightness,
						  LD_DISP_BUTTON_CPCAP_MASK);

		if (cpcap_status < 0)
			pr_err("%s: Writing to the register failed for %i\n",
			       __func__, cpcap_status);

	} else {
		if ((disp_button_led_data->regulator) &&
		    (disp_button_led_data->regulator_state == 1)) {
			regulator_disable(disp_button_led_data->regulator);
			disp_button_led_data->regulator_state = 0;
		}
		/* Due to a HW issue turn off the current then
		turn off the duty cycle */
		brightness = 0x01;
		cpcap_status = cpcap_regacc_write(disp_button_led_data->cpcap,
					  CPCAP_REG_KLC, brightness,
					  LD_DISP_BUTTON_CPCAP_MASK);

		brightness = 0x00;
		cpcap_status = cpcap_regacc_write(disp_button_led_data->cpcap,
						  CPCAP_REG_KLC, brightness,
						  LD_DISP_BUTTON_CPCAP_MASK);

		if (cpcap_status < 0)
			pr_err("%s: Writing to the register failed for %i\n",
			       __func__, cpcap_status);
	}
}

static int disp_button_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct disp_button_led_data *info;

	if (pdev == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;

	}
	info = kzalloc(sizeof(struct disp_button_led_data), GFP_KERNEL);
	if (info == NULL) {
		ret = -ENOMEM;
		return ret;
	}

	info->cpcap = pdev->dev.platform_data;
	platform_set_drvdata(pdev, info);

	info->regulator = regulator_get(&pdev->dev, LD_SUPPLY);
	if (IS_ERR(info->regulator)) {
		pr_err("%s: Cannot get %s regulator\n", __func__, LD_SUPPLY);
		ret = PTR_ERR(info->regulator);
		goto exit_request_reg_failed;

	}
	info->regulator_state = 0;

	info->disp_button_class_dev.name = "button-backlight";
	info->disp_button_class_dev.brightness_set = disp_button_set;
	ret = led_classdev_register(&pdev->dev, &info->disp_button_class_dev);
	if (ret < 0) {
		pr_err("%s:Register button backlight class failed\n", __func__);
		goto err_reg_button_class_failed;
	}
	INIT_WORK(&info->brightness_work, disp_button_brightness_work);
	return ret;

err_reg_button_class_failed:
	if (info->regulator)
		regulator_put(info->regulator);
exit_request_reg_failed:
	kfree(info);
	return ret;
}

static int disp_button_remove(struct platform_device *pdev)
{
	struct disp_button_led_data *info = platform_get_drvdata(pdev);

	if (info->regulator)
		regulator_put(info->regulator);

	led_classdev_unregister(&info->disp_button_class_dev);
	return 0;
}

static struct platform_driver ld_disp_button_driver = {
	.probe = disp_button_probe,
	.remove = disp_button_remove,
	.driver = {
		   .name = LD_DISP_BUTTON_DEV,
	},
};

static int __init led_disp_button_init(void)
{
	return platform_driver_register(&ld_disp_button_driver);
}

static void __exit led_disp_button_exit(void)
{
	platform_driver_unregister(&ld_disp_button_driver);
}

module_init(led_disp_button_init);
module_exit(led_disp_button_exit);

MODULE_DESCRIPTION("Display Button Lighting driver");
MODULE_AUTHOR("Motorola");
MODULE_LICENSE("GPL");
