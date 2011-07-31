/*
 * Copyright (C) 2010 Motorola, Inc.
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

struct cpcap_led_data {
	struct led_classdev cpcap_class_dev;
	struct cpcap_device *cpcap;
	struct cpcap_led *pdata;
	struct regulator *regulator;
	struct work_struct brightness_work;
	enum led_brightness brightness;
	int regulator_state;
	short blink_val;
};

static void cpcap_set(struct led_classdev *led_cdev,
			    enum led_brightness brightness)
{
	struct cpcap_led_data *cpcap_led_data =
		container_of(led_cdev, struct cpcap_led_data,
		cpcap_class_dev);

	if (brightness > 255)
		brightness = 255;

	cpcap_led_data->brightness = brightness;
	queue_work(system_nrt_wq, &cpcap_led_data->brightness_work);
}
EXPORT_SYMBOL(cpcap_set);

static void
cpcap_led_set_blink(struct cpcap_led_data *info, unsigned long blink)
{
	info->blink_val = blink;

	if (info->pdata->blink_able) {
		if(info->blink_val) {
			cpcap_uc_start(info->cpcap, CPCAP_MACRO_6);
		} else {
			cpcap_uc_stop(info->cpcap, CPCAP_MACRO_6);
			queue_work(system_nrt_wq, &info->brightness_work);
		}
	}
}

static int cpcap_led_blink(struct led_classdev *led_cdev,
			       unsigned long *delay_on,
			       unsigned long *delay_off)
{
	struct cpcap_led_data *info =
		container_of(led_cdev, struct cpcap_led_data,
			 cpcap_class_dev);

	cpcap_led_set_blink(info, *delay_on);
	return 0;
}

static void cpcap_brightness_work(struct work_struct *work)
{
	int cpcap_status = 0;
	unsigned short brightness = 0;

	struct cpcap_led_data *cpcap_led_data =
	    container_of(work, struct cpcap_led_data, brightness_work);

	brightness = cpcap_led_data->brightness;

	if (brightness > 0) {
		brightness = (cpcap_led_data->pdata->cpcap_reg_period |
				 cpcap_led_data->pdata->cpcap_reg_duty_cycle |
				 cpcap_led_data->pdata->cpcap_reg_current |
				 0x01);

		if ((cpcap_led_data->regulator) &&
		    (cpcap_led_data->regulator_state == 0)) {
			regulator_enable(cpcap_led_data->regulator);
			cpcap_led_data->regulator_state = 1;
		}

		cpcap_status = cpcap_regacc_write(cpcap_led_data->cpcap,
				cpcap_led_data->pdata->cpcap_register,
				brightness,
				cpcap_led_data->pdata->cpcap_reg_mask);

		if (cpcap_status < 0)
			pr_err("%s: Writing to the register failed for %i\n",
			       __func__, cpcap_status);

	} else {
		if ((cpcap_led_data->regulator) &&
		    (cpcap_led_data->regulator_state == 1)) {
			regulator_disable(cpcap_led_data->regulator);
			cpcap_led_data->regulator_state = 0;
		}
		/* Due to a HW issue turn off the current then
		turn off the duty cycle */
		brightness = 0x01;
		cpcap_status = cpcap_regacc_write(cpcap_led_data->cpcap,
				cpcap_led_data->pdata->cpcap_register,
				brightness,
				cpcap_led_data->pdata->cpcap_reg_mask);


		brightness = 0x00;
		cpcap_status = cpcap_regacc_write(cpcap_led_data->cpcap,
				cpcap_led_data->pdata->cpcap_register,
				brightness,
				cpcap_led_data->pdata->cpcap_reg_mask);


		if (cpcap_status < 0)
			pr_err("%s: Writing to the register failed for %i\n",
			       __func__, cpcap_status);

	}
}

static ssize_t blink_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct cpcap_led_data *info =
		container_of(led_cdev, struct cpcap_led_data, cpcap_class_dev);

	return sprintf(buf, "%u\n", info->blink_val);
}

static ssize_t blink_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t size)
{
	struct led_classdev *led_cdev = dev_get_drvdata(dev);
	struct cpcap_led_data *info =
		container_of(led_cdev, struct cpcap_led_data, cpcap_class_dev);
	unsigned long blink = simple_strtoul(buf, NULL, 10);
	cpcap_led_set_blink(info, blink);
	return size;
}

static DEVICE_ATTR(blink, 0644, blink_show, blink_store);

static int cpcap_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct cpcap_led_data *info;

	if (pdev == NULL) {
		pr_err("%s: platform data required\n", __func__);
		return -ENODEV;

	}
	info = kzalloc(sizeof(struct cpcap_led_data), GFP_KERNEL);
	if (info == NULL) {
		ret = -ENOMEM;
		return ret;
	}

	info->pdata = pdev->dev.platform_data;
	info->cpcap = platform_get_drvdata(pdev);
	platform_set_drvdata(pdev, info);

	if (info->pdata->led_regulator != NULL) {
		info->regulator = regulator_get(&pdev->dev,
				info->pdata->led_regulator);
		if (IS_ERR(info->regulator)) {
			pr_err("%s: Cannot get %s regulator\n",
				__func__, info->pdata->led_regulator);
			ret = PTR_ERR(info->regulator);
			goto exit_request_reg_failed;

		}
	}
	info->regulator_state = 0;

	info->cpcap_class_dev.name = info->pdata->class_name;
	info->cpcap_class_dev.brightness_set = cpcap_set;
	info->cpcap_class_dev.blink_set = cpcap_led_blink;
	info->cpcap_class_dev.brightness = LED_OFF;
	info->cpcap_class_dev.max_brightness = 255;
	if (info->pdata->blink_able)
		info->cpcap_class_dev.default_trigger = "timer";

	ret = led_classdev_register(&pdev->dev, &info->cpcap_class_dev);
	if (ret < 0) {
		pr_err("%s:Register %s class failed\n",
			__func__, info->cpcap_class_dev.name);
		goto err_reg_button_class_failed;
	}

	/* Create a device file to control blinking.
	 * We do this to avoid problems setting permissions on the
	 * timer trigger delay_on and delay_off files.
	 */
	ret = device_create_file(info->cpcap_class_dev.dev, &dev_attr_blink);
	if (ret < 0) {
		pr_err("%s:device_create_file failed for blink\n", __func__);
		goto err_device_create_file_failed;
	}

	INIT_WORK(&info->brightness_work, cpcap_brightness_work);

	return ret;

err_device_create_file_failed:
	led_classdev_unregister(&info->cpcap_class_dev);
err_reg_button_class_failed:
	if (info->regulator)
		regulator_put(info->regulator);
exit_request_reg_failed:
	kfree(info);
	return ret;
}

static int cpcap_remove(struct platform_device *pdev)
{
	struct cpcap_led_data *info = platform_get_drvdata(pdev);

	device_remove_file(info->cpcap_class_dev.dev, &dev_attr_blink);
	if (info->regulator)
		regulator_put(info->regulator);
	led_classdev_unregister(&info->cpcap_class_dev);
	return 0;
}

static struct platform_driver ld_cpcap_driver = {
	.probe = cpcap_probe,
	.remove = cpcap_remove,
	.driver = {
		   .name = LD_CPCAP_LED_DRV,
	},
};

static int __init led_cpcap_init(void)
{
	return platform_driver_register(&ld_cpcap_driver);
}

static void __exit led_cpcap_exit(void)
{
	platform_driver_unregister(&ld_cpcap_driver);
}

module_init(led_cpcap_init);
module_exit(led_cpcap_exit);

MODULE_DESCRIPTION("CPCAP Lighting driver");
MODULE_AUTHOR("Dan Murphy <D.Murphy@Motorola.com>");
MODULE_LICENSE("GPL");
