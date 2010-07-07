/*
     Copyright (C) 2010 Motorola, Inc.

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License version 2 as
     published by the Free Software Foundation.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
     02111-1307  USA
*/
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/mdm6600_ctrl.h>
#include <linux/platform_device.h>

#define AP_STATUS_BP_PANIC_ACK      0x00
#define AP_STATUS_DATA_ONLY_BYPASS  0x01
#define AP_STATUS_FULL_BYPASS       0x02
#define AP_STATUS_NO_BYPASS         0x03
#define AP_STATUS_BP_SHUTDOWN_REQ   0x04
#define AP_STATUS_UNDEFINED         0x07

#define BP_STATUS_PANIC             0x00
#define BP_STATUS_PANIC_BUSY_WAIT   0x01
#define BP_STATUS_QC_DLOAD          0x02
#define BP_STATUS_RAM_DOWNLOADER    0x03
#define BP_STATUS_PHONE_CODE_AWAKE  0x04
#define BP_STATUS_PHONE_CODE_ASLEEP 0x05
#define BP_STATUS_SHUTDOWN_ACK      0x06
#define BP_STATUS_UNDEFINED         0x07

static unsigned int mdm_gpio_get_value(struct mdm_ctrl_gpio gpio)
{
	return gpio_get_value(gpio.number);
}

static void mdm_gpio_set_value(struct mdm_ctrl_gpio gpio,
	unsigned int value)
{
	gpio_set_value(gpio.number, value);
}

static void mdm_gpio_free(struct mdm_ctrl_gpio *gpio)
{
	if (gpio->allocated)
		gpio_free(gpio->number);
	gpio->allocated = 0;
}

static int mdm_gpio_setup(struct mdm_ctrl_gpio *gpio)
{
	snprintf(gpio->name, MAX_GPIO_NAME, "mdm_gpio_%05d", gpio->number);

	if (gpio_request(gpio->number, gpio->name))  {
		printk(KERN_ERR "failed to aquire gpio %s", gpio->name);
		return -1;
	}
	gpio->allocated = 1;
	if (gpio->direction == MDM_GPIO_DIRECTION_IN)
		gpio_direction_input(gpio->number);
	else if (gpio->direction == MDM_GPIO_DIRECTION_OUT)
		gpio_direction_output(gpio->number, gpio->default_value);
	return 0;
}

static unsigned int get_bp_status(struct mdm_ctrl_platform_data *pdata)
{
	unsigned int status = 0;
	unsigned int bp_status[3];

	bp_status[0] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_0]);
	bp_status[1] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_1]);
	bp_status[2] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_2]);

	status = ((bp_status[2] & 0x1) << 2) |
		 ((bp_status[1] & 0x1) << 1) |
		  (bp_status[0] & 0x1);

	return status;
}

static unsigned int get_ap_status(struct mdm_ctrl_platform_data *pdata)
{
	unsigned int status = 0;
	unsigned int ap_status[3];

	ap_status[0] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_0]);
	ap_status[1] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_1]);
	ap_status[2] = mdm_gpio_get_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_2]);

	status = ((ap_status[2] & 0x1) << 2) |
		 ((ap_status[1] & 0x1) << 1) |
		  (ap_status[0] & 0x1);

	return status;
}

static void set_ap_status(struct mdm_ctrl_platform_data *pdata,
			  unsigned int status)
{
	mdm_gpio_set_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_0],
		(status & 0x1));
	mdm_gpio_set_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_1],
		(status >> 1) & 0x1);
	mdm_gpio_set_value(
		pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_2],
		(status >> 2) & 0x1);
}

static int __devinit mdm_ctrl_probe(struct platform_device *pdev)
{
	int i;
	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "mdm_ctrl_probe");

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++) {
		if (mdm_gpio_setup(&pdata->gpios[i])) {
			dev_err(&pdev->dev, "failed to aquire gpio %d\n",
				pdata->gpios[i].number);
			goto probe_cleanup;
		}
	}

	return 0;

probe_cleanup:
	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++)
		mdm_gpio_free(&pdata->gpios[i]);

	return -1;
}

static int __devexit mdm_ctrl_remove(struct platform_device *pdev)
{
	int i;
	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "cleanup\n");
	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++)
		mdm_gpio_free(&pdata->gpios[i]);

	return 0;
}

static unsigned int __devexit bp_shutdown_wait(struct platform_device *pdev,
		     struct mdm_ctrl_platform_data *pdata)
{
	unsigned int delay;
	unsigned int bp_status;
	unsigned int gpio_value;
	unsigned int pd_failure = 1;

	for (delay = 0; delay < 10; delay++) {
		bp_status = get_bp_status(pdata);
		if (bp_status == BP_STATUS_SHUTDOWN_ACK) {
			dev_info(&pdev->dev, "Modem power down success.\n");
			pd_failure = 0;
			break;
		}
		gpio_value = mdm_gpio_get_value(
			pdata->gpios[MDM_CTRL_GPIO_BP_RESOUT]);

		dev_info(&pdev->dev, "gpio_resout_gpio = %d\n", gpio_value);
		if (!gpio_value) {
			dev_info(&pdev->dev, "Modem reporting Panic.\n");
			pd_failure = 0;
			break;
		} else {
			dev_info(&pdev->dev, "Modem status 0x%x\n", bp_status);
			msleep(500);
		}
	}
	return pd_failure;
}

static void __devexit mdm_ctrl_shutdown(struct platform_device *pdev)
{
	unsigned int pd_failure;

	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "Shutting down modem.\n");

	dev_info(&pdev->dev, "Initial modem status 0x%x\n",
		 get_bp_status(pdata));

	dev_info(&pdev->dev, "Initial ap status 0x%x\n",
		 get_ap_status(pdata));

	set_ap_status(pdata, AP_STATUS_BP_SHUTDOWN_REQ);

	/* Allow modem to process status */
	msleep(100);
	dev_info(&pdev->dev, "ap_status set to %d\n", get_ap_status(pdata));

	/* Toggle the power, delaying to allow modem to respond */
	mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_PWRON], 1);
	msleep(100);
	mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_PWRON], 0);
	msleep(100);

	/* This should be enough to power down the modem */
	/* if this doesn't work, reset the modem and try */
	/* one more time, ultimately the modem will be   */
	/* hard powered off */
	pd_failure = bp_shutdown_wait(pdev, pdata);
	if (pd_failure) {
		mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_PWRON], 1);
		pd_failure = bp_shutdown_wait(pdev, pdata);
	}

	if (pd_failure)
		dev_err(&pdev->dev, "Modem failed to power down.\n");
	else
		dev_info(&pdev->dev, "Modem successfully powered down.\n");
}

static struct platform_driver mdm6x00_ctrl_driver = {
	.probe = mdm_ctrl_probe,
	.remove = __devexit_p(mdm_ctrl_remove),
	.shutdown = __devexit_p(mdm_ctrl_shutdown),
	.driver = {
		.name = MDM_CTRL_MODULE_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init mdm6600_ctrl_init(void)
{
	printk(KERN_DEBUG "mdm6600_ctrl_init\n");
	return platform_driver_register(&mdm6x00_ctrl_driver);
}

static void __exit mdm6600_ctrl_exit(void)
{
	printk(KERN_DEBUG "mdm6600_ctrl_exit\n");
	platform_driver_unregister(&mdm6x00_ctrl_driver);
}

module_init(mdm6600_ctrl_init);
module_exit(mdm6600_ctrl_exit);

MODULE_AUTHOR("Motorola");
MODULE_DESCRIPTION("Modem Control Driver");
MODULE_VERSION("1.1.3");
MODULE_LICENSE("GPL");
