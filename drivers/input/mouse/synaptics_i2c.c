/*
 * Synaptics touchpad with I2C interface
 *
 * Copyright (C) 2009 Compulab, Ltd.
 * Mike Rapoport <mike@compulab.co.il>
 * Igor Grinberg <grinberg@compulab.co.il>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 */

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/slab.h>
#include <linux/pm.h>

#define DRIVER_NAME		"synaptics_i2c"
/* maximum product id is 15 characters */
#define PRODUCT_ID_LENGTH	15
#define REGISTER_LENGTH		8

/*
 * after soft reset, we should wait for 1 ms
 * before the device becomes operational
 */
#define SOFT_RESET_DELAY_MS	3
/* and after hard reset, we should wait for max 500ms */
#define HARD_RESET_DELAY_MS	500

/* Registers by SMBus address */
#define PAGE_SEL_REG		0xff
#define DEVICE_STATUS_REG	0x09

/* Registers by RMI address */
#define DEV_CONTROL_REG		0x0000
#define INTERRUPT_EN_REG	0x0001
#define ERR_STAT_REG		0x0002
#define INT_REQ_STAT_REG	0x0003
#define DEV_COMMAND_REG		0x0004

#define RMI_PROT_VER_REG	0x0200
#define MANUFACT_ID_REG		0x0201
#define PHYS_INT_VER_REG	0x0202
#define PROD_PROPERTY_REG	0x0203
#define INFO_QUERY_REG0		0x0204
#define INFO_QUERY_REG1		(INFO_QUERY_REG0 + 1)
#define INFO_QUERY_REG2		(INFO_QUERY_REG0 + 2)
#define INFO_QUERY_REG3		(INFO_QUERY_REG0 + 3)

#define PRODUCT_ID_REG0		0x0210
#define PRODUCT_ID_REG1		(PRODUCT_ID_REG0 + 1)
#define PRODUCT_ID_REG2		(PRODUCT_ID_REG0 + 2)
#define PRODUCT_ID_REG3		(PRODUCT_ID_REG0 + 3)
#define PRODUCT_ID_REG4		(PRODUCT_ID_REG0 + 4)
#define PRODUCT_ID_REG5		(PRODUCT_ID_REG0 + 5)
#define PRODUCT_ID_REG6		(PRODUCT_ID_REG0 + 6)
#define PRODUCT_ID_REG7		(PRODUCT_ID_REG0 + 7)
#define PRODUCT_ID_REG8		(PRODUCT_ID_REG0 + 8)
#define PRODUCT_ID_REG9		(PRODUCT_ID_REG0 + 9)
#define PRODUCT_ID_REG10	(PRODUCT_ID_REG0 + 10)
#define PRODUCT_ID_REG11	(PRODUCT_ID_REG0 + 11)
#define PRODUCT_ID_REG12	(PRODUCT_ID_REG0 + 12)
#define PRODUCT_ID_REG13	(PRODUCT_ID_REG0 + 13)
#define PRODUCT_ID_REG14	(PRODUCT_ID_REG0 + 14)
#define PRODUCT_ID_REG15	(PRODUCT_ID_REG0 + 15)

#define DATA_REG0		0x0400
#define ABS_PRESSURE_REG	0x0401
#define ABS_MSB_X_REG		0x0402
#define ABS_LSB_X_REG		(ABS_MSB_X_REG + 1)
#define ABS_MSB_Y_REG		0x0404
#define ABS_LSB_Y_REG		(ABS_MSB_Y_REG + 1)
#define REL_X_REG		0x0406
#define REL_Y_REG		0x0407

#define DEV_QUERY_REG0		0x1000
#define DEV_QUERY_REG1		(DEV_QUERY_REG0 + 1)
#define DEV_QUERY_REG2		(DEV_QUERY_REG0 + 2)
#define DEV_QUERY_REG3		(DEV_QUERY_REG0 + 3)
#define DEV_QUERY_REG4		(DEV_QUERY_REG0 + 4)
#define DEV_QUERY_REG5		(DEV_QUERY_REG0 + 5)
#define DEV_QUERY_REG6		(DEV_QUERY_REG0 + 6)
#define DEV_QUERY_REG7		(DEV_QUERY_REG0 + 7)
#define DEV_QUERY_REG8		(DEV_QUERY_REG0 + 8)

#define GENERAL_2D_CONTROL_REG	0x1041
#define SENSOR_SENSITIVITY_REG	0x1044
#define SENS_MAX_POS_MSB_REG	0x1046
#define SENS_MAX_POS_LSB_REG	(SENS_MAX_POS_UPPER_REG + 1)

/* Register bits */
/* Device Control Register Bits */
#define REPORT_RATE_1ST_BIT	6

/* Interrupt Enable Register Bits (INTERRUPT_EN_REG) */
#define F10_ABS_INT_ENA		0
#define F10_REL_INT_ENA		1
#define F20_INT_ENA		2

/* Interrupt Request Register Bits (INT_REQ_STAT_REG | DEVICE_STATUS_REG) */
#define F10_ABS_INT_REQ		0
#define F10_REL_INT_REQ		1
#define F20_INT_REQ		2
/* Device Status Register Bits (DEVICE_STATUS_REG) */
#define STAT_CONFIGURED		6
#define STAT_ERROR		7

/* Device Command Register Bits (DEV_COMMAND_REG) */
#define RESET_COMMAND		0x01
#define REZERO_COMMAND		0x02

/* Data Register 0 Bits (DATA_REG0) */
#define GESTURE			3

/* Device Query Registers Bits */
/* DEV_QUERY_REG3 */
#define HAS_PALM_DETECT		1
#define HAS_MULTI_FING		2
#define HAS_SCROLLER		4
#define HAS_2D_SCROLL		5

/* General 2D Control Register Bits (GENERAL_2D_CONTROL_REG) */
#define NO_DECELERATION		1
#define REDUCE_REPORTING	3
#define NO_FILTER		5

/* Function Masks */
/* Device Control Register Masks (DEV_CONTROL_REG) */
#define REPORT_RATE_MSK		0xc0
#define SLEEP_MODE_MSK		0x07

/* Device Sleep Modes */
#define FULL_AWAKE		0x0
#define NORMAL_OP		0x1
#define LOW_PWR_OP		0x2
#define VERY_LOW_PWR_OP		0x3
#define SENS_SLEEP		0x4
#define SLEEP_MOD		0x5
#define DEEP_SLEEP		0x6
#define HIBERNATE		0x7

/* Interrupt Register Mask */
/* (INT_REQ_STAT_REG | DEVICE_STATUS_REG | INTERRUPT_EN_REG) */
#define INT_ENA_REQ_MSK		0x07
#define INT_ENA_ABS_MSK		0x01
#define INT_ENA_REL_MSK		0x02
#define INT_ENA_F20_MSK		0x04

/* Device Status Register Masks (DEVICE_STATUS_REG) */
#define CONFIGURED_MSK		0x40
#define ERROR_MSK		0x80

/* Data Register 0 Masks */
#define FINGER_WIDTH_MSK	0xf0
#define GESTURE_MSK		0x08
#define SENSOR_STATUS_MSK	0x07

/*
 * MSB Position Register Masks
 * ABS_MSB_X_REG | ABS_MSB_Y_REG | SENS_MAX_POS_MSB_REG |
 * DEV_QUERY_REG3 | DEV_QUERY_REG5
 */
#define MSB_POSITION_MSK	0x1f

/* Device Query Registers Masks */

/* DEV_QUERY_REG2 */
#define NUM_EXTRA_POS_MSK	0x07

/* When in IRQ mode read the device every THREAD_IRQ_SLEEP_SECS */
#define THREAD_IRQ_SLEEP_SECS	2
#define THREAD_IRQ_SLEEP_MSECS	(THREAD_IRQ_SLEEP_SECS * MSEC_PER_SEC)

/*
 * When in Polling mode and no data received for NO_DATA_THRES msecs
 * reduce the polling rate to NO_DATA_SLEEP_MSECS
 */
#define NO_DATA_THRES		(MSEC_PER_SEC)
#define NO_DATA_SLEEP_MSECS	(MSEC_PER_SEC / 4)

/* Control touchpad's No Deceleration option */
static bool no_decel = 1;
module_param(no_decel, bool, 0644);
MODULE_PARM_DESC(no_decel, "No Deceleration. Default = 1 (on)");

/* Control touchpad's Reduced Reporting option */
static bool reduce_report;
module_param(reduce_report, bool, 0644);
MODULE_PARM_DESC(reduce_report, "Reduced Reporting. Default = 0 (off)");

/* Control touchpad's No Filter option */
static bool no_filter;
module_param(no_filter, bool, 0644);
MODULE_PARM_DESC(no_filter, "No Filter. Default = 0 (off)");

/*
 * touchpad Attention line is Active Low and Open Drain,
 * therefore should be connected to pulled up line
 * and the irq configuration should be set to Falling Edge Trigger
 */
/* Control IRQ / Polling option */
static bool polling_req;
module_param(polling_req, bool, 0444);
MODULE_PARM_DESC(polling_req, "Request Polling. Default = 0 (use irq)");

/* Control Polling Rate */
static int scan_rate = 80;
module_param(scan_rate, int, 0644);
MODULE_PARM_DESC(scan_rate, "Polling rate in times/sec. Default = 80");

/* The main device structure */
struct synaptics_i2c {
	struct i2c_client	*client;
	struct input_dev	*input;
	struct delayed_work	dwork;
	spinlock_t		lock;
	int			no_data_count;
	int			no_decel_param;
	int			reduce_report_param;
	int			no_filter_param;
	int			scan_rate_param;
	int			scan_ms;
};

static inline void set_scan_rate(struct synaptics_i2c *touch, int scan_rate)
{
	touch->scan_ms = MSEC_PER_SEC / scan_rate;
	touch->scan_rate_param = scan_rate;
}

/*
 * Driver's initial design makes no race condition possible on i2c bus,
 * so there is no need in any locking.
 * Keep it in mind, while playing with the code.
 */
static s32 synaptics_i2c_reg_get(struct i2c_client *client, u16 reg)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PAGE_SEL_REG, reg >> 8);
	if (ret == 0)
		ret = i2c_smbus_read_byte_data(client, reg & 0xff);

	return ret;
}

static s32 synaptics_i2c_reg_set(struct i2c_client *client, u16 reg, u8 val)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PAGE_SEL_REG, reg >> 8);
	if (ret == 0)
		ret = i2c_smbus_write_byte_data(client, reg & 0xff, val);

	return ret;
}

static s32 synaptics_i2c_word_get(struct i2c_client *client, u16 reg)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, PAGE_SEL_REG, reg >> 8);
	if (ret == 0)
		ret = i2c_smbus_read_word_data(client, reg & 0xff);

	return ret;
}

static int synaptics_i2c_config(struct i2c_client *client)
{
	int ret, control;
	u8 int_en;

	/* set Report Rate to Device Highest (>=80) and Sleep to normal */
	ret = synaptics_i2c_reg_set(client, DEV_CONTROL_REG, 0xc1);
	if (ret)
		return ret;

	/* set Interrupt Disable to Func20 / Enable to Func10) */
	int_en = (polling_req) ? 0 : INT_ENA_ABS_MSK | INT_ENA_REL_MSK;
	ret = synaptics_i2c_reg_set(client, INTERRUPT_EN_REG, int_en);
	if (ret)
		return ret;

	control = synaptics_i2c_reg_get(client, GENERAL_2D_CONTROL_REG);
	/* No Deceleration */
	control |= no_decel ? 1 << NO_DECELERATION : 0;
	/* Reduced Reporting */
	control |= reduce_report ? 1 << REDUCE_REPORTING : 0;
	/* No Filter */
	control |= no_filter ? 1 << NO_FILTER : 0;
	ret = synaptics_i2c_reg_set(client, GENERAL_2D_CONTROL_REG, control);
	if (ret)
		return ret;

	return 0;
}

static int synaptics_i2c_reset_config(struct i2c_client *client)
{
	int ret;

	/* Reset the Touchpad */
	ret = synaptics_i2c_reg_set(client, DEV_COMMAND_REG, RESET_COMMAND);
	if (ret) {
		dev_err(&client->dev, "Unable to reset device\n");
	} else {
		msleep(SOFT_RESET_DELAY_MS);
		ret = synaptics_i2c_config(client);
		if (ret)
			dev_err(&client->dev, "Unable to config device\n");
	}

	return ret;
}

static int synaptics_i2c_check_error(struct i2c_client *client)
{
	int status, ret = 0;

	status = i2c_smbus_read_byte_data(client, DEVICE_STATUS_REG) &
		(CONFIGURED_MSK | ERROR_MSK);

	if (status != CONFIGURED_MSK)
		ret = synaptics_i2c_reset_config(client);

	return ret;
}

static bool synaptics_i2c_get_input(struct synaptics_i2c *touch)
{
	struct input_dev *input = touch->input;
	int xy_delta, gesture;
	s32 data;
	s8 x_delta, y_delta;

	/* Deal with spontanious resets and errors */
	if (synaptics_i2c_check_error(touch->client))
		return 0;

	/* Get Gesture Bit */
	data = synaptics_i2c_reg_get(touch->client, DATA_REG0);
	gesture = (data >> GESTURE) & 0x1;

	/*
	 * Get Relative axes. we have to get them in one shot,
	 * so we get 2 bytes starting from REL_X_REG.
	 */
	xy_delta = synaptics_i2c_word_get(touch->client, REL_X_REG) & 0xffff;

	/* Separate X from Y */
	x_delta = xy_delta & 0xff;
	y_delta = (xy_delta >> REGISTER_LENGTH) & 0xff;

	/* Report the button event */
	input_report_key(input, BTN_LEFT, gesture);

	/* Report the deltas */
	input_report_rel(input, REL_X, x_delta);
	input_report_rel(input, REL_Y, -y_delta);
	input_sync(input);

	return xy_delta || gesture;
}

static void synaptics_i2c_reschedule_work(struct synaptics_i2c *touch,
					  unsigned long delay)
{
	unsigned long flags;

	spin_lock_irqsave(&touch->lock, flags);

	/*
	 * If work is already scheduled then subsequent schedules will not
	 * change the scheduled time that's why we have to cancel it first.
	 */
	__cancel_delayed_work(&touch->dwork);
	schedule_delayed_work(&touch->dwork, delay);

	spin_unlock_irqrestore(&touch->lock, flags);
}

static irqreturn_t synaptics_i2c_irq(int irq, void *dev_id)
{
	struct synaptics_i2c *touch = dev_id;

	synaptics_i2c_reschedule_work(touch, 0);

	return IRQ_HANDLED;
}

static void synaptics_i2c_check_params(struct synaptics_i2c *touch)
{
	bool reset = false;

	if (scan_rate != touch->scan_rate_param)
		set_scan_rate(touch, scan_rate);

	if (no_decel != touch->no_decel_param) {
		touch->no_decel_param = no_decel;
		reset = true;
	}

	if (no_filter != touch->no_filter_param) {
		touch->no_filter_param = no_filter;
		reset = true;
	}

	if (reduce_report != touch->reduce_report_param) {
		touch->reduce_report_param = reduce_report;
		reset = true;
	}

	if (reset)
		synaptics_i2c_reset_config(touch->client);
}

/* Control the Device polling rate / Work Handler sleep time */
static unsigned long synaptics_i2c_adjust_delay(struct synaptics_i2c *touch,
						bool have_data)
{
	unsigned long delay, nodata_count_thres;

	if (polling_req) {
		delay = touch->scan_ms;
		if (have_data) {
			touch->no_data_count = 0;
		} else {
			nodata_count_thres = NO_DATA_THRES / touch->scan_ms;
			if (touch->no_data_count < nodata_count_thres)
				touch->no_data_count++;
			else
				delay = NO_DATA_SLEEP_MSECS;
		}
		return msecs_to_jiffies(delay);
	} else {
		delay = msecs_to_jiffies(THREAD_IRQ_SLEEP_MSECS);
		return round_jiffies_relative(delay);
	}
}

/* Work Handler */
static void synaptics_i2c_work_handler(struct work_struct *work)
{
	bool have_data;
	struct synaptics_i2c *touch =
			container_of(work, struct synaptics_i2c, dwork.work);
	unsigned long delay;

	synaptics_i2c_check_params(touch);

	have_data = synaptics_i2c_get_input(touch);
	delay = synaptics_i2c_adjust_delay(touch, have_data);

	/*
	 * While interrupt driven, there is no real need to poll the device.
	 * But touchpads are very sensitive, so there could be errors
	 * related to physical environment and the attention line isn't
	 * necessarily asserted. In such case we can lose the touchpad.
	 * We poll the device once in THREAD_IRQ_SLEEP_SECS and
	 * if error is detected, we try to reset and reconfigure the touchpad.
	 */
	synaptics_i2c_reschedule_work(touch, delay);
}

static int synaptics_i2c_open(struct input_dev *input)
{
	struct synaptics_i2c *touch = input_get_drvdata(input);
	int ret;

	ret = synaptics_i2c_reset_config(touch->client);
	if (ret)
		return ret;

	if (polling_req)
		synaptics_i2c_reschedule_work(touch,
				msecs_to_jiffies(NO_DATA_SLEEP_MSECS));

	return 0;
}

static void synaptics_i2c_close(struct input_dev *input)
{
	struct synaptics_i2c *touch = input_get_drvdata(input);

	if (!polling_req)
		synaptics_i2c_reg_set(touch->client, INTERRUPT_EN_REG, 0);

	cancel_delayed_work_sync(&touch->dwork);

	/* Save some power */
	synaptics_i2c_reg_set(touch->client, DEV_CONTROL_REG, DEEP_SLEEP);
}

static void synaptics_i2c_set_input_params(struct synaptics_i2c *touch)
{
	struct input_dev *input = touch->input;

	input->name = touch->client->name;
	input->phys = touch->client->adapter->name;
	input->id.bustype = BUS_I2C;
	input->id.version = synaptics_i2c_word_get(touch->client,
						   INFO_QUERY_REG0);
	input->dev.parent = &touch->client->dev;
	input->open = synaptics_i2c_open;
	input->close = synaptics_i2c_close;
	input_set_drvdata(input, touch);

	/* Register the device as mouse */
	__set_bit(EV_REL, input->evbit);
	__set_bit(REL_X, input->relbit);
	__set_bit(REL_Y, input->relbit);

	/* Register device's buttons and keys */
	__set_bit(EV_KEY, input->evbit);
	__set_bit(BTN_LEFT, input->keybit);
}

static struct synaptics_i2c *synaptics_i2c_touch_create(struct i2c_client *client)
{
	struct synaptics_i2c *touch;

	touch = kzalloc(sizeof(struct synaptics_i2c), GFP_KERNEL);
	if (!touch)
		return NULL;

	touch->client = client;
	touch->no_decel_param = no_decel;
	touch->scan_rate_param = scan_rate;
	set_scan_rate(touch, scan_rate);
	INIT_DELAYED_WORK(&touch->dwork, synaptics_i2c_work_handler);
	spin_lock_init(&touch->lock);

	return touch;
}

static int __devinit synaptics_i2c_probe(struct i2c_client *client,
			       const struct i2c_device_id *dev_id)
{
	int ret;
	struct synaptics_i2c *touch;

	touch = synaptics_i2c_touch_create(client);
	if (!touch)
		return -ENOMEM;

	ret = synaptics_i2c_reset_config(client);
	if (ret)
		goto err_mem_free;

	if (client->irq < 1)
		polling_req = true;

	touch->input = input_allocate_device();
	if (!touch->input) {
		ret = -ENOMEM;
		goto err_mem_free;
	}

	synaptics_i2c_set_input_params(touch);

	if (!polling_req) {
		dev_dbg(&touch->client->dev,
			 "Requesting IRQ: %d\n", touch->client->irq);

		ret = request_irq(touch->client->irq, synaptics_i2c_irq,
				  IRQ_TYPE_EDGE_FALLING,
				  DRIVER_NAME, touch);
		if (ret) {
			dev_warn(&touch->client->dev,
				  "IRQ request failed: %d, "
				  "falling back to polling\n", ret);
			polling_req = true;
			synaptics_i2c_reg_set(touch->client,
					      INTERRUPT_EN_REG, 0);
		}
	}

	if (polling_req)
		dev_dbg(&touch->client->dev,
			 "Using polling at rate: %d times/sec\n", scan_rate);

	/* Register the device in input subsystem */
	ret = input_register_device(touch->input);
	if (ret) {
		dev_err(&client->dev,
			 "Input device register failed: %d\n", ret);
		goto err_input_free;
	}

	i2c_set_clientdata(client, touch);

	return 0;

err_input_free:
	input_free_device(touch->input);
err_mem_free:
	kfree(touch);

	return ret;
}

static int __devexit synaptics_i2c_remove(struct i2c_client *client)
{
	struct synaptics_i2c *touch = i2c_get_clientdata(client);

	if (!polling_req)
		free_irq(client->irq, touch);

	input_unregister_device(touch->input);
	kfree(touch);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int synaptics_i2c_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct synaptics_i2c *touch = i2c_get_clientdata(client);

	cancel_delayed_work_sync(&touch->dwork);

	/* Save some power */
	synaptics_i2c_reg_set(touch->client, DEV_CONTROL_REG, DEEP_SLEEP);

	return 0;
}

static int synaptics_i2c_resume(struct device *dev)
{
	int ret;
	struct i2c_client *client = to_i2c_client(dev);
	struct synaptics_i2c *touch = i2c_get_clientdata(client);

	ret = synaptics_i2c_reset_config(client);
	if (ret)
		return ret;

	synaptics_i2c_reschedule_work(touch,
				msecs_to_jiffies(NO_DATA_SLEEP_MSECS));

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(synaptics_i2c_pm, synaptics_i2c_suspend,
			 synaptics_i2c_resume);

static const struct i2c_device_id synaptics_i2c_id_table[] = {
	{ "synaptics_i2c", 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, synaptics_i2c_id_table);

static struct i2c_driver synaptics_i2c_driver = {
	.driver = {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
		.pm	= &synaptics_i2c_pm,
	},

	.probe		= synaptics_i2c_probe,
	.remove		= __devexit_p(synaptics_i2c_remove),

	.id_table	= synaptics_i2c_id_table,
};

static int __init synaptics_i2c_init(void)
{
	return i2c_add_driver(&synaptics_i2c_driver);
}

static void __exit synaptics_i2c_exit(void)
{
	i2c_del_driver(&synaptics_i2c_driver);
}

module_init(synaptics_i2c_init);
module_exit(synaptics_i2c_exit);

MODULE_DESCRIPTION("Synaptics I2C touchpad driver");
MODULE_AUTHOR("Mike Rapoport, Igor Grinberg, Compulab");
MODULE_LICENSE("GPL");

