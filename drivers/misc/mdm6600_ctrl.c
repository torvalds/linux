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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kobject.h>

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

#define LOOP_DELAY_TIME_MS          500

static const char *mdmctrl = "mdm6600_ctrl";

static const char *bp_status[8] = {
	[BP_STATUS_PANIC] = "panic",
	[BP_STATUS_PANIC_BUSY_WAIT] = "panic busy wait",
	[BP_STATUS_QC_DLOAD] = "qc dload",
	[BP_STATUS_RAM_DOWNLOADER] = "ram downloader",
	[BP_STATUS_PHONE_CODE_AWAKE] = "awake",
	[BP_STATUS_PHONE_CODE_ASLEEP] = "asleep",
	[BP_STATUS_SHUTDOWN_ACK] = "shutdown ack",
	[BP_STATUS_UNDEFINED] = "undefined",
};

static const char *bp_power_state[2] = {
	"off",
	"on",
};

#define BP_STATUS_MAX_LENGTH        32
#define BP_COMMAND_MAX_LENGTH       32

/* structure to keep track of gpio, irq, and irq enabled info */
struct gpio_info {
	int irq;
	struct work_struct work;
};

struct mdm_ctrl_info {
	struct mdm_ctrl_platform_data *pdata;
	struct gpio_info gpios[MDM_CTRL_NUM_GPIOS];
};

static struct mdm_ctrl_info mdm_ctrl;

static DEFINE_MUTEX(mdm_ctrl_info_lock);

struct workqueue_struct *working_queue = NULL;

static dev_t dev_number;
struct class *radio_cls = NULL;
struct device *mdm_dev = NULL;

static unsigned int bp_status_idx = BP_STATUS_UNDEFINED;
static unsigned int bp_power_idx = 0;

static const char *bp_status_string(unsigned int stat)
{
	if (stat < ARRAY_SIZE(bp_status))
		return bp_status[stat];
	else
		return "status out of range";
}

static const char *bp_power_state_string(unsigned int stat)
{
	if (stat < ARRAY_SIZE(bp_power_state))
		return bp_power_state[stat];
	else
		return "status out of range";
}

static ssize_t mdm_status_show(struct device *dev,
			       struct device_attribute *attr, char *buff)
{
	ssize_t status = 0;
	status = snprintf(buff, BP_STATUS_MAX_LENGTH, "%s\n",
			  bp_status_string(bp_status_idx));

	return status;
}

static ssize_t mdm_power_show(struct device *dev,
			      struct device_attribute *attr, char *buff)
{
	ssize_t status = 0;
	status = snprintf(buff, BP_STATUS_MAX_LENGTH, "%s\n",
			  bp_power_state_string(bp_power_idx));

	return status;
}

static ssize_t mdm_user_command(struct device *dev,
				struct device_attribute *attr, const char *buff,
				size_t size)
{
	char tmp[BP_COMMAND_MAX_LENGTH];
	char *post_strip = NULL;

	if (size > BP_COMMAND_MAX_LENGTH - 1) {
		return size;
	}

	/* strip whitespaces if any */
	memcpy(tmp, buff, size);
	tmp[size] = '\0';
	post_strip = strim(tmp);

	pr_info("%s: user command = %s\n", mdmctrl, post_strip);

	/* TODO : real handlers of user commands will be added later */

	return size;
}

static DEVICE_ATTR(status, 0444, mdm_status_show, NULL);
static DEVICE_ATTR(power_status, 0444, mdm_power_show, NULL);
static DEVICE_ATTR(command, 0200, NULL, mdm_user_command);

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
	if (gpio_request(gpio->number, gpio->name))  {
		printk(KERN_ERR "failed to aquire gpio %s", gpio->name);
		return -1;
	}
	gpio->allocated = 1;
	gpio_export(gpio->number, false);
	if (gpio->direction == MDM_GPIO_DIRECTION_IN)
		gpio_direction_input(gpio->number);
	else if (gpio->direction == MDM_GPIO_DIRECTION_OUT)
		gpio_direction_output(gpio->number, gpio->default_value);
	return 0;
}

static unsigned int get_bp_status(void)
{
	unsigned int status = BP_STATUS_UNDEFINED;
	unsigned int bp_status[3] = {0};

	mutex_lock(&mdm_ctrl_info_lock);
	if (mdm_ctrl.pdata) {
		bp_status[0] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_0]);
		bp_status[1] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_1]);
		bp_status[2] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_BP_STATUS_2]);
	}
	mutex_unlock(&mdm_ctrl_info_lock);

	status = ((bp_status[2] & 0x1) << 2) |
		 ((bp_status[1] & 0x1) << 1) |
		  (bp_status[0] & 0x1);

	return status;
}

static unsigned int get_bp_power_status(void)
{
	unsigned int status = 0;

	mutex_lock(&mdm_ctrl_info_lock);
	if (mdm_ctrl.pdata) {
		status = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_BP_RESOUT]);
	}

	mutex_unlock(&mdm_ctrl_info_lock);

	return status & 0x1;
}

static unsigned int get_ap_status(void)
{
	unsigned int status = AP_STATUS_UNDEFINED;
	unsigned int ap_status[3] =  {0};

	mutex_lock(&mdm_ctrl_info_lock);
	if (mdm_ctrl.pdata) {
		ap_status[0] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_0]);
		ap_status[1] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_1]);
		ap_status[2] = mdm_gpio_get_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_2]);
	}
	mutex_unlock(&mdm_ctrl_info_lock);

	status = ((ap_status[2] & 0x1) << 2) |
		 ((ap_status[1] & 0x1) << 1) |
		  (ap_status[0] & 0x1);

	return status;
}

static void set_ap_status(unsigned int status)
{
	mutex_lock(&mdm_ctrl_info_lock);
	if (mdm_ctrl.pdata) {
		mdm_gpio_set_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_0],
			(status & 0x1));
		mdm_gpio_set_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_1],
			(status >> 1) & 0x1);
		mdm_gpio_set_value(
			mdm_ctrl.pdata->gpios[MDM_CTRL_GPIO_AP_STATUS_2],
			(status >> 2) & 0x1);
	}
	mutex_unlock(&mdm_ctrl_info_lock);
}

static void update_bp_status(void) {

	static int bp_status_prev_idx = BP_STATUS_UNDEFINED;

	bp_status_prev_idx = bp_status_idx;
	bp_status_idx = get_bp_status();
	bp_power_idx = get_bp_power_status();

	pr_info("%s: modem status: %s -> %s [power %s]", mdmctrl,
		bp_status_string(bp_status_prev_idx),
		bp_status_string(bp_status_idx),
		bp_power_state_string(bp_power_idx));

	kobject_uevent(&mdm_dev->kobj, KOBJ_CHANGE);
}

static void irq_worker(struct work_struct *work)
{
	struct gpio_info *gpio = container_of(work, struct gpio_info, work);
	update_bp_status();
	enable_irq(gpio->irq);
}

static irqreturn_t irq_handler(int irq, void *data)
{
	struct gpio_info *gpio = (struct gpio_info *) data;

	disable_irq_nosync(irq);
	queue_work(working_queue, &gpio->work);

	return IRQ_HANDLED;
}

static int mdm_gpio_setup_internal(struct mdm_ctrl_platform_data *pdata)
{
	int i;
	int rv = 0;
	struct gpio_info *gpio_data = NULL;

	mutex_lock(&mdm_ctrl_info_lock);
	memset(&mdm_ctrl, 0, sizeof (mdm_ctrl));

	mdm_ctrl.pdata = pdata;

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++) {
		gpio_data = &mdm_ctrl.gpios[i];
		if (pdata->gpios[i].direction == MDM_GPIO_DIRECTION_IN) {
			INIT_WORK(&gpio_data->work, irq_worker);
			gpio_data->irq = gpio_to_irq(pdata->gpios[i].number);
			rv = request_irq(gpio_data->irq, irq_handler,
					 IRQ_TYPE_EDGE_FALLING | IRQ_TYPE_EDGE_RISING,
					 pdata->gpios[i].name, gpio_data);
                        if (rv < 0) {
				pr_err("%s: Cannot request IRQ (%d) from kernel!",
				       mdmctrl, gpio_data->irq);
			} else {
				enable_irq_wake(gpio_data->irq);
			}
		}
        }

	mutex_unlock(&mdm_ctrl_info_lock);
	return rv;
}

static void mdm_gpio_cleanup_internal(void)
{
	int i;
	struct gpio_info *gpio_data = NULL;

	mutex_lock(&mdm_ctrl_info_lock);

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++) {
		gpio_data = &mdm_ctrl.gpios[i];

		if (gpio_data->irq) {
			disable_irq_wake(gpio_data->irq);
			free_irq(gpio_data->irq, gpio_data);
		}
	}
	memset(&mdm_ctrl, 0, sizeof (mdm_ctrl));
	mutex_unlock(&mdm_ctrl_info_lock);
}

static int __devinit mdm_ctrl_probe(struct platform_device *pdev)
{
	int i;
	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "mdm_ctrl_probe");

	if (alloc_chrdev_region(&dev_number, 0, 1, "mdm_ctrl") < 0) {
		dev_err(&pdev->dev, "Can't register new device.");
		return -1;
	}

	/* /sys/class/radio */
	radio_cls = class_create(THIS_MODULE, "radio");
	if (IS_ERR(radio_cls)) {
		dev_err(&pdev->dev, "Failed to create radio class.");
		goto err_cls;
	}

	/* /sys/class/radio/mdm6600 */
	mdm_dev = device_create(radio_cls, NULL, dev_number, NULL, "mdm6600");
	if (IS_ERR(mdm_dev)) {
		dev_err(&pdev->dev, "Failed to create mdm_dev.");
		goto err_mdm;
	}

	/* /sys/class/radio/mdm6600/status */
	if (device_create_file(mdm_dev, &dev_attr_status) > 0) {
		dev_err(&pdev->dev, "Failed to create status sysfile.");
		goto err_status;
	}

	/* /sys/class/radio/mdm6600/power_status */
	if (device_create_file(mdm_dev, &dev_attr_power_status) > 0) {
		dev_err(&pdev->dev, "Failed to create power sysfile .");
		goto err_power;
	}

	/* /sys/class/radio/mdm6600/command */
	if (device_create_file(mdm_dev, &dev_attr_command) > 0) {
		dev_err(&pdev->dev, "Failed to create command sysfile.");
		goto err_command;
	}

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++) {
		if (mdm_gpio_setup(&pdata->gpios[i])) {
			dev_err(&pdev->dev, "failed to aquire gpio %d\n",
				pdata->gpios[i].number);
			goto probe_cleanup;
		}
	}

	working_queue = create_singlethread_workqueue("mdm_ctrl_wq");
	if (!working_queue) {
		dev_err(&pdev->dev, "Cannot create work queue.");
		goto probe_err;
	}

	if (mdm_gpio_setup_internal(pdata) < 0) {
		dev_err(&pdev->dev, "Failed to setup bp  status irq");
		goto err_setup;
	}

	update_bp_status();

	return 0;

err_setup:
	mdm_gpio_cleanup_internal();

probe_err:
	destroy_workqueue(working_queue);

probe_cleanup:
	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++)
		mdm_gpio_free(&pdata->gpios[i]);

err_command:
	device_remove_file(mdm_dev, &dev_attr_command);

err_power:
	device_remove_file(mdm_dev, &dev_attr_power_status);

err_status:
	device_remove_file(mdm_dev, &dev_attr_status);

err_mdm:
	if (!IS_ERR_OR_NULL(mdm_dev)) {
		device_destroy(radio_cls, dev_number);
		mdm_dev = NULL;
	}

err_cls:
	if (!IS_ERR_OR_NULL(radio_cls)) {
		class_destroy(radio_cls);
		radio_cls = NULL;
	}

	return -1;
}

static int __devexit mdm_ctrl_remove(struct platform_device *pdev)
{
	int i;
	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "cleanup\n");

	mdm_gpio_cleanup_internal();

        if (working_queue)
		destroy_workqueue(working_queue);

	for (i = 0; i < MDM_CTRL_NUM_GPIOS; i++)
		mdm_gpio_free(&pdata->gpios[i]);

	device_remove_file(mdm_dev, &dev_attr_command);
	device_remove_file(mdm_dev, &dev_attr_power_status);
	device_remove_file(mdm_dev, &dev_attr_status);

	if (!IS_ERR_OR_NULL(mdm_dev)) {
		device_destroy(radio_cls, dev_number);
		mdm_dev = NULL;
	}

	if (!IS_ERR_OR_NULL(radio_cls)) {
		class_destroy(radio_cls);
		radio_cls = NULL;
	}

	return 0;
}

static unsigned int __devexit bp_shutdown_wait(struct platform_device *pdev,
		struct mdm_ctrl_platform_data *pdata,
		unsigned int delay_sec)
{
	unsigned int i, loop_count;
	unsigned int bp_status;
	unsigned int gpio_value;
	unsigned int pd_failure = 1;

	loop_count = (delay_sec * 1000) / LOOP_DELAY_TIME_MS;

	for (i = 0; i < loop_count; i++) {
		msleep(LOOP_DELAY_TIME_MS);
		bp_status = get_bp_status();
		if (bp_status == BP_STATUS_SHUTDOWN_ACK) {
			dev_info(&pdev->dev, "Modem powered off (with ack).\n");
			pd_failure = 0;
			break;
		}
		gpio_value = mdm_gpio_get_value(
			pdata->gpios[MDM_CTRL_GPIO_BP_RESOUT]);

		if (gpio_value == 0) {
			dev_info(&pdev->dev, "Modem powered off.\n");
			pd_failure = 0;
			break;
		} else {
			dev_info(&pdev->dev, "Modem status %s [0x%x]\n",
				 bp_status_string(bp_status), bp_status);
		}
	}
	return pd_failure;
}

static void __devexit mdm_ctrl_shutdown(struct platform_device *pdev)
{
	unsigned int pd_failure;
	unsigned int bp_status;

	struct mdm_ctrl_platform_data *pdata = pdev->dev.platform_data;

	dev_info(&pdev->dev, "Shutting down modem.\n");

	bp_status = get_bp_status();
	dev_info(&pdev->dev, "Initial Modem status %s [0x%x]\n",
		 bp_status_string(bp_status), bp_status);

	set_ap_status(AP_STATUS_BP_SHUTDOWN_REQ);

	/* Allow modem to process status */
	msleep(100);
	dev_info(&pdev->dev, "ap_status set to %d\n", get_ap_status());

	/* Toggle the power, delaying to allow modem to respond */
	mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_PWRON], 1);
	msleep(100);
	mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_PWRON], 0);
	msleep(100);

	/* This should be enough to power down the modem */
	/* if this doesn't work, reset the modem and try */
	/* one more time, ultimately the modem will be   */
	/* hard powered off */
	pd_failure = bp_shutdown_wait(pdev, pdata, 5);
	if (pd_failure) {
		dev_info(&pdev->dev, "Resetting unresponsive modem.\n");
		mdm_gpio_set_value(pdata->gpios[MDM_CTRL_GPIO_BP_RESIN], 1);
		pd_failure = bp_shutdown_wait(pdev, pdata, 5);
	}

	if (pd_failure)
		dev_err(&pdev->dev, "Modem failed to power down.\n");
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
