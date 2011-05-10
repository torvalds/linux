/*
 * clearpad_tm1217.c - Touch Screen driver for Synaptics Clearpad
 * TM1217 controller
 *
 * Copyright (C) 2008 Intel Corp
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; ifnot, write to the Free Software Foundation, Inc.,
 * 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * Questions/Comments/Bug fixes to Ramesh Agarwal (ramesh.agarwal@intel.com)
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/timer.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "cp_tm1217.h"

#define CPTM1217_DEVICE_NAME		"cptm1217"
#define CPTM1217_DRIVER_NAME		CPTM1217_DEVICE_NAME

#define MAX_TOUCH_SUPPORTED		2
#define TOUCH_SUPPORTED			1
#define SAMPLING_FREQ			80	/* Frequency in HZ */
#define DELAY_BTWIN_SAMPLE		(1000 / SAMPLING_FREQ)
#define WAIT_FOR_RESPONSE		5	/* 5msec just works */
#define MAX_RETRIES			5	/* As above */
#define INCREMENTAL_DELAY		5	/* As above */

/* Regster Definitions */
#define TMA1217_DEV_STATUS		0x13	/* Device Status */
#define TMA1217_INT_STATUS		0x14	/* Interrupt Status */

/* Controller can detect up to 2 possible finger touches.
 * Each finger touch provides  12 bit X Y co-ordinates, the values are split
 * across 2 registers, and an 8 bit  Z value */
#define TMA1217_FINGER_STATE		0x18 /* Finger State */
#define TMA1217_FINGER1_X_HIGHER8	0x19 /* Higher 8 bit of X coordinate */
#define TMA1217_FINGER1_Y_HIGHER8	0x1A /* Higher 8 bit of Y coordinate */
#define TMA1217_FINGER1_XY_LOWER4	0x1B /* Lower 4 bits of X and Y */
#define TMA1217_FINGER1_Z_VALUE		0x1D /* 8 bit Z value for finger 1 */
#define TMA1217_FINGER2_X_HIGHER8	0x1E /* Higher 8 bit of X coordinate */
#define TMA1217_FINGER2_Y_HIGHER8	0x1F /* Higher 8 bit of Y coordinate */
#define TMA1217_FINGER2_XY_LOWER4	0x20 /* Lower 4 bits of X and Y */
#define TMA1217_FINGER2_Z_VALUE		0x22 /* 8 bit Z value for finger 2 */
#define TMA1217_DEVICE_CTRL		0x23 /* Device Control */
#define TMA1217_INTERRUPT_ENABLE	0x24 /* Interrupt Enable */
#define TMA1217_REPORT_MODE		0x2B /* Reporting Mode */
#define TMA1217_MAX_X_LOWER8		0x31 /* Bit 0-7 for Max X */
#define TMA1217_MAX_X_HIGHER4		0x32 /* Bit 8-11 for Max X */
#define TMA1217_MAX_Y_LOWER8		0x33 /* Bit 0-7 for Max Y */
#define TMA1217_MAX_Y_HIGHER4		0x34 /* Bit 8-11 for Max Y */
#define TMA1217_DEVICE_CMD_RESET	0x67 /* Device CMD reg for reset */
#define TMA1217_DEVICE_CMD_REZERO	0x69 /* Device CMD reg for rezero */

#define TMA1217_MANUFACTURER_ID		0x73 /* Manufacturer Id */
#define TMA1217_PRODUCT_FAMILY		0x75 /* Product Family */
#define TMA1217_FIRMWARE_REVISION	0x76 /* Firmware Revision */
#define TMA1217_SERIAL_NO_HIGH		0x7C /* Bit 8-15 of device serial no. */
#define TMA1217_SERIAL_NO_LOW		0x7D /* Bit 0-7 of device serial no. */
#define TMA1217_PRODUCT_ID_START	0x7E /* Start address for 10 byte ID */
#define TMA1217_DEVICE_CAPABILITY	0x8B /* Reporting capability */


/*
 * The touch position structure.
 */
struct touch_state {
	int	x;
	int	y;
	bool button;
};

/* Device Specific info given by the controller */
struct cp_dev_info {
	u16	maxX;
	u16	maxY;
};

/* Vendor related info given by the controller */
struct cp_vendor_info {
	u8	vendor_id;
	u8	product_family;
	u8	firmware_rev;
	u16	serial_no;
};

/*
 * Private structure to store the device details
 */
struct cp_tm1217_device {
	struct i2c_client	*client;
	struct device		*dev;
	struct cp_vendor_info	vinfo;
	struct cp_dev_info	dinfo;
	struct input_dev_info {
		char			phys[32];
		char			name[128];
		struct input_dev	*input;
		struct touch_state	touch;
	} cp_input_info[MAX_TOUCH_SUPPORTED];

	int	thread_running;
	struct mutex	thread_mutex;

	int gpio;
};


/* The following functions are used to read/write registers on the device
 * as per the RMI prorocol. Technically, a page select should be written
 * before doing read/write but since the register offsets are below 0xFF
 * we can use the default value of page which is 0x00
 */
static int cp_tm1217_read(struct cp_tm1217_device *ts,
				u8 *req, int size)
{
	int i, retval;

	/* Send the address */
	retval = i2c_master_send(ts->client, &req[0], 1);
	if (retval != 1) {
		dev_err(ts->dev, "cp_tm1217: I2C send failed\n");
		return retval;
	}
	msleep(WAIT_FOR_RESPONSE);
	for (i = 0; i < MAX_RETRIES; i++) {
		retval = i2c_master_recv(ts->client, &req[1], size);
		if (retval == size) {
			break;
		} else {
			msleep(INCREMENTAL_DELAY);
			dev_dbg(ts->dev, "cp_tm1217: Retry count is %d\n", i);
		}
	}
	if (retval != size)
		dev_err(ts->dev, "cp_tm1217: Read from device failed\n");

	return retval;
}

static int cp_tm1217_write(struct cp_tm1217_device *ts,
				u8 *req, int size)
{
	int retval;

	/* Send the address and the data to be written */
	retval = i2c_master_send(ts->client, &req[0], size + 1);
	if (retval != size + 1) {
		dev_err(ts->dev, "cp_tm1217: I2C write  failed: %d\n", retval);
		return retval;
	}
	/* Wait for the write to complete. TBD why this is required */
	msleep(WAIT_FOR_RESPONSE);

	return size;
}

static int cp_tm1217_mask_interrupt(struct cp_tm1217_device *ts)
{
	u8 req[2];
	int retval;

	req[0] = TMA1217_INTERRUPT_ENABLE;
	req[1] = 0x0;
	retval = cp_tm1217_write(ts, req, 1);
	if (retval != 1)
		return -EIO;

	return 0;
}

static int cp_tm1217_unmask_interrupt(struct cp_tm1217_device *ts)
{
	u8 req[2];
	int retval;

	req[0] = TMA1217_INTERRUPT_ENABLE;
	req[1] = 0xa;
	retval = cp_tm1217_write(ts, req, 1);
	if (retval != 1)
		return -EIO;

	return 0;
}

static void process_touch(struct cp_tm1217_device *ts, int index)
{
	int retval;
	struct input_dev_info *input_info =
		(struct input_dev_info *)&ts->cp_input_info[index];
	u8 xy_data[6];

	if (index == 0)
		xy_data[0] = TMA1217_FINGER1_X_HIGHER8;
	else
		xy_data[0] = TMA1217_FINGER2_X_HIGHER8;

	retval = cp_tm1217_read(ts, xy_data, 5);
	if (retval < 5) {
		dev_err(ts->dev, "cp_tm1217: XY read from device failed\n");
		return;
	}

	/* Note: Currently not using the Z values but may be requried in
	   the future. */
	input_info->touch.x = (xy_data[1] << 4)
					| (xy_data[3] & 0x0F);
	input_info->touch.y = (xy_data[2] << 4)
					| ((xy_data[3] & 0xF0) >> 4);
	input_report_abs(input_info->input, ABS_X, input_info->touch.x);
	input_report_abs(input_info->input, ABS_Y, input_info->touch.y);
	input_sync(input_info->input);
}

static void cp_tm1217_get_data(struct cp_tm1217_device *ts)
{
	u8 req[2];
	int retval, i, finger_touched = 0;

	do {
		req[0] = TMA1217_FINGER_STATE;
		retval = cp_tm1217_read(ts, req, 1);
		if (retval != 1) {
			dev_err(ts->dev,
				"cp_tm1217: Read from device failed\n");
			continue;
		}
		finger_touched = 0;
		/* Start sampling until the pressure is below
		  threshold */
		for (i = 0; i < TOUCH_SUPPORTED; i++) {
			if (req[1] & 0x3) {
				finger_touched++;
				if (ts->cp_input_info[i].touch.button == 0) {
					/* send the button touch event */
					input_report_key(
						ts->cp_input_info[i].input,
						BTN_TOUCH, 1);
					ts->cp_input_info[i].touch.button = 1;
				}
				process_touch(ts, i);
			} else {
				if (ts->cp_input_info[i].touch.button == 1) {
					/* send the button release event */
					input_report_key(
						ts->cp_input_info[i].input,
						BTN_TOUCH, 0);
					input_sync(ts->cp_input_info[i].input);
					ts->cp_input_info[i].touch.button = 0;
				}
			}
			req[1] = req[1] >> 2;
		}
		msleep(DELAY_BTWIN_SAMPLE);
	} while (finger_touched > 0);
}

static irqreturn_t cp_tm1217_sample_thread(int irq, void *handle)
{
	struct cp_tm1217_device *ts = (struct cp_tm1217_device *) handle;
	u8 req[2];
	int retval;

	/* Chedk if another thread is already running */
	mutex_lock(&ts->thread_mutex);
	if (ts->thread_running == 1) {
		mutex_unlock(&ts->thread_mutex);
		return IRQ_HANDLED;
	} else {
		ts->thread_running = 1;
		mutex_unlock(&ts->thread_mutex);
	}

	/* Mask the interrupts */
	retval = cp_tm1217_mask_interrupt(ts);

	/* Read the Interrupt Status register to find the cause of the
	   Interrupt */
	req[0] = TMA1217_INT_STATUS;
	retval = cp_tm1217_read(ts, req, 1);
	if (retval != 1)
		goto exit_thread;

	if (!(req[1] & 0x8))
		goto exit_thread;

	cp_tm1217_get_data(ts);

exit_thread:
	/* Unmask the interrupts before going to sleep */
	retval = cp_tm1217_unmask_interrupt(ts);

	mutex_lock(&ts->thread_mutex);
	ts->thread_running = 0;
	mutex_unlock(&ts->thread_mutex);

	return IRQ_HANDLED;
}

static int cp_tm1217_init_data(struct cp_tm1217_device *ts)
{
	int retval;
	u8	req[2];

	/* Read the vendor id/ fw revision etc. Ignoring return check as this
	   is non critical info  */
	req[0] = TMA1217_MANUFACTURER_ID;
	retval = cp_tm1217_read(ts, req, 1);
	ts->vinfo.vendor_id = req[1];

	req[0] = TMA1217_PRODUCT_FAMILY;
	retval = cp_tm1217_read(ts, req, 1);
	ts->vinfo.product_family = req[1];

	req[0] = TMA1217_FIRMWARE_REVISION;
	retval = cp_tm1217_read(ts, req, 1);
	ts->vinfo.firmware_rev = req[1];

	req[0] = TMA1217_SERIAL_NO_HIGH;
	retval = cp_tm1217_read(ts, req, 1);
	ts->vinfo.serial_no = (req[1] << 8);

	req[0] = TMA1217_SERIAL_NO_LOW;
	retval = cp_tm1217_read(ts, req, 1);
	ts->vinfo.serial_no = ts->vinfo.serial_no | req[1];

	req[0] = TMA1217_MAX_X_HIGHER4;
	retval = cp_tm1217_read(ts, req, 1);
	ts->dinfo.maxX = (req[1] & 0xF) << 8;

	req[0] = TMA1217_MAX_X_LOWER8;
	retval = cp_tm1217_read(ts, req, 1);
	ts->dinfo.maxX = ts->dinfo.maxX | req[1];

	req[0] = TMA1217_MAX_Y_HIGHER4;
	retval = cp_tm1217_read(ts, req, 1);
	ts->dinfo.maxY = (req[1] & 0xF) << 8;

	req[0] = TMA1217_MAX_Y_LOWER8;
	retval = cp_tm1217_read(ts, req, 1);
	ts->dinfo.maxY = ts->dinfo.maxY | req[1];

	return 0;

}

/*
 *	Set up a GPIO for use as the interrupt. We can't simply do this at
 *	boot time because the GPIO drivers themselves may not be around at
 *	boot/firmware set up time to do the work. Instead defer it to driver
 *	detection.
 */

static int cp_tm1217_setup_gpio_irq(struct cp_tm1217_device *ts)
{
	int retval;

	/* Hook up the irq handler */
	retval = gpio_request(ts->gpio, "cp_tm1217_touch");
	if (retval < 0) {
		dev_err(ts->dev, "cp_tm1217: GPIO request failed error %d\n",
								retval);
		return retval;
	}

	retval = gpio_direction_input(ts->gpio);
	if (retval < 0) {
		dev_err(ts->dev,
		"cp_tm1217: GPIO direction configuration failed, error %d\n",
								retval);
		gpio_free(ts->gpio);
		return retval;
	}

	retval = gpio_to_irq(ts->gpio);
	if (retval < 0) {
		dev_err(ts->dev, "cp_tm1217: GPIO to IRQ failedi,"
		" error %d\n", retval);
		gpio_free(ts->gpio);
	}
	dev_dbg(ts->dev,
		"cp_tm1217: Got IRQ number is %d for GPIO %d\n",
		retval, ts->gpio);
	return retval;
}

static int cp_tm1217_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct cp_tm1217_device *ts;
	struct input_dev *input_dev;
	struct input_dev_info	*input_info;
	struct cp_tm1217_platform_data *pdata;
	u8 req[2];
	int i, retval;

	/* No pdata is fine - we then use "normal" IRQ mode */

	pdata = client->dev.platform_data;

	ts = kzalloc(sizeof(struct cp_tm1217_device), GFP_KERNEL);
	if (!ts) {
		dev_err(&client->dev,
			"cp_tm1217: Private Device Struct alloc failed\n");
		return -ENOMEM;
	}

	ts->client = client;
	ts->dev = &client->dev;
	i2c_set_clientdata(client, ts);

	ts->thread_running = 0;
	mutex_init(&ts->thread_mutex);

	/* Reset the Controller */
	req[0] = TMA1217_DEVICE_CMD_RESET;
	req[1] = 0x1;
	retval = cp_tm1217_write(ts, req, 1);
	if (retval != 1) {
		dev_err(ts->dev, "cp_tm1217: Controller reset failed\n");
		kfree(ts);
		return -EIO;
	}

	/* Clear up the interrupt status from reset. */
	req[0] = TMA1217_INT_STATUS;
	retval = cp_tm1217_read(ts, req, 1);

	/* Mask all the interrupts */
	retval = cp_tm1217_mask_interrupt(ts);

	/* Read the controller information */
	cp_tm1217_init_data(ts);

	/* The following code will register multiple event devices when
	   multi-pointer is enabled, the code has not been tested
	   with MPX */
	for (i = 0; i < TOUCH_SUPPORTED; i++) {
		input_dev = input_allocate_device();
		if (input_dev == NULL) {
			dev_err(ts->dev,
				"cp_tm1217:Input Device Struct alloc failed\n");
			kfree(ts);
			return -ENOMEM;
		}
		input_info = &ts->cp_input_info[i];
		snprintf(input_info->name, sizeof(input_info->name),
			"cp_tm1217_touchscreen_%d", i);
		input_dev->name = input_info->name;
		snprintf(input_info->phys, sizeof(input_info->phys),
			"%s/input%d", dev_name(&client->dev), i);

		input_dev->phys = input_info->phys;
		input_dev->id.bustype = BUS_I2C;

		input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
		input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

		input_set_abs_params(input_dev, ABS_X, 0, ts->dinfo.maxX, 0, 0);
		input_set_abs_params(input_dev, ABS_Y, 0, ts->dinfo.maxY, 0, 0);

		retval = input_register_device(input_dev);
		if (retval) {
			dev_err(ts->dev,
				"Input dev registration failed for %s\n",
					input_dev->name);
			goto fail;
		}
		input_info->input = input_dev;
	}

	/* Setup the reporting mode to send an interrupt only when
	   finger arrives or departs. */
	req[0] = TMA1217_REPORT_MODE;
	req[1] = 0x02;
	retval = cp_tm1217_write(ts, req, 1);

	/* Setup the device to no sleep mode for now and make it configured */
	req[0] = TMA1217_DEVICE_CTRL;
	req[1] = 0x84;
	retval = cp_tm1217_write(ts, req, 1);

	/* Check for the status of the device */
	req[0] = TMA1217_DEV_STATUS;
	retval = cp_tm1217_read(ts, req, 1);
	if (req[1] != 0) {
		dev_err(ts->dev,
			"cp_tm1217: Device Status 0x%x != 0: config failed\n",
			req[1]);

		retval = -EIO;
		goto fail;
	}

	if (pdata && pdata->gpio) {
		ts->gpio = pdata->gpio;
		retval = cp_tm1217_setup_gpio_irq(ts);
	} else
		retval = client->irq;

	if (retval < 0) {
		dev_err(ts->dev, "cp_tm1217: GPIO request failed error %d\n",
								retval);
		goto fail;
	}

	client->irq = retval;


	retval = request_threaded_irq(client->irq,
		NULL, cp_tm1217_sample_thread,
		IRQF_TRIGGER_FALLING, "cp_tm1217_touch", ts);
	if (retval < 0) {
		dev_err(ts->dev, "cp_tm1217: Request IRQ error %d\n", retval);
		goto fail_gpio;
	}

	/* Unmask the interrupts */
	retval = cp_tm1217_unmask_interrupt(ts);
	if (retval == 0)
		return 0;

	free_irq(client->irq, ts);
fail_gpio:
	if (ts->gpio)
		gpio_free(ts->gpio);
fail:
	/* Clean up before returning failure */
	for (i = 0; i < TOUCH_SUPPORTED; i++) {
		if (ts->cp_input_info[i].input) {
			input_unregister_device(ts->cp_input_info[i].input);
			input_free_device(ts->cp_input_info[i].input);
		}
	}
	kfree(ts);
	return retval;

}

/*
 * cp_tm1217 suspend
 *
 */
static int cp_tm1217_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct cp_tm1217_device *ts = i2c_get_clientdata(client);
	u8 req[2];
	int retval;

	/* Put the controller to sleep */
	req[0] = TMA1217_DEVICE_CTRL;
	retval = cp_tm1217_read(ts, req, 1);
	req[1] = (req[1] & 0xF8) | 0x1;
	retval = cp_tm1217_write(ts, req, 1);

	if (device_may_wakeup(&client->dev))
		enable_irq_wake(client->irq);

	return 0;
}

/*
 * cp_tm1217_resume
 *
 */
static int cp_tm1217_resume(struct i2c_client *client)
{
	struct cp_tm1217_device *ts = i2c_get_clientdata(client);
	u8 req[2];
	int retval;

	/* Take the controller out of sleep */
	req[0] = TMA1217_DEVICE_CTRL;
	retval = cp_tm1217_read(ts, req, 1);
	req[1] = (req[1] & 0xF8) | 0x4;
	retval = cp_tm1217_write(ts, req, 1);

	/* Restore the register settings sinc the power to the
	   could have been cut off */

	/* Setup the reporting mode to send an interrupt only when
	   finger arrives or departs. */
	req[0] = TMA1217_REPORT_MODE;
	req[1] = 0x02;
	retval = cp_tm1217_write(ts, req, 1);

	/* Setup the device to no sleep mode for now and make it configured */
	req[0] = TMA1217_DEVICE_CTRL;
	req[1] = 0x84;
	retval = cp_tm1217_write(ts, req, 1);

	/* Setup the interrupt mask */
	retval = cp_tm1217_unmask_interrupt(ts);

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(client->irq);

	return 0;
}

/*
 * cp_tm1217_remove
 *
 */
static int cp_tm1217_remove(struct i2c_client *client)
{
	struct cp_tm1217_device *ts = i2c_get_clientdata(client);
	int i;

	free_irq(client->irq, ts);
	if (ts->gpio)
		gpio_free(ts->gpio);
	for (i = 0; i < TOUCH_SUPPORTED; i++)
		input_unregister_device(ts->cp_input_info[i].input);
	kfree(ts);
	return 0;
}

static struct i2c_device_id cp_tm1217_idtable[] = {
	{ CPTM1217_DEVICE_NAME, 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, cp_tm1217_idtable);

static struct i2c_driver cp_tm1217_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= CPTM1217_DRIVER_NAME,
	},
	.id_table	= cp_tm1217_idtable,
	.probe		= cp_tm1217_probe,
	.remove		= cp_tm1217_remove,
	.suspend    = cp_tm1217_suspend,
	.resume     = cp_tm1217_resume,
};

static int __init clearpad_tm1217_init(void)
{
	return i2c_add_driver(&cp_tm1217_driver);
}

static void __exit clearpad_tm1217_exit(void)
{
	i2c_del_driver(&cp_tm1217_driver);
}

module_init(clearpad_tm1217_init);
module_exit(clearpad_tm1217_exit);

MODULE_AUTHOR("Ramesh Agarwal <ramesh.agarwal@intel.com>");
MODULE_DESCRIPTION("Synaptics TM1217 TouchScreen Driver");
MODULE_LICENSE("GPL v2");
