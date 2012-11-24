/*
 * Copyright (C) ST-Ericsson SA 2010
 * Author: Naveen Kumar G <naveen.gaddipati@stericsson.com> for ST-Ericsson
 * License terms:GNU General Public License (GPL) version 2
 */

#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/input/bu21013.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/module.h>

#define PEN_DOWN_INTR	0
#define MAX_FINGERS	2
#define RESET_DELAY	30
#define PENUP_TIMEOUT	(10)
#define DELTA_MIN	16
#define MASK_BITS	0x03
#define SHIFT_8		8
#define SHIFT_2		2
#define LENGTH_OF_BUFFER	11
#define I2C_RETRY_COUNT	5

#define BU21013_SENSORS_BTN_0_7_REG	0x70
#define BU21013_SENSORS_BTN_8_15_REG	0x71
#define BU21013_SENSORS_BTN_16_23_REG	0x72
#define BU21013_X1_POS_MSB_REG		0x73
#define BU21013_X1_POS_LSB_REG		0x74
#define BU21013_Y1_POS_MSB_REG		0x75
#define BU21013_Y1_POS_LSB_REG		0x76
#define BU21013_X2_POS_MSB_REG		0x77
#define BU21013_X2_POS_LSB_REG		0x78
#define BU21013_Y2_POS_MSB_REG		0x79
#define BU21013_Y2_POS_LSB_REG		0x7A
#define BU21013_INT_CLR_REG		0xE8
#define BU21013_INT_MODE_REG		0xE9
#define BU21013_GAIN_REG		0xEA
#define BU21013_OFFSET_MODE_REG		0xEB
#define BU21013_XY_EDGE_REG		0xEC
#define BU21013_RESET_REG		0xED
#define BU21013_CALIB_REG		0xEE
#define BU21013_DONE_REG		0xEF
#define BU21013_SENSOR_0_7_REG		0xF0
#define BU21013_SENSOR_8_15_REG		0xF1
#define BU21013_SENSOR_16_23_REG	0xF2
#define BU21013_POS_MODE1_REG		0xF3
#define BU21013_POS_MODE2_REG		0xF4
#define BU21013_CLK_MODE_REG		0xF5
#define BU21013_IDLE_REG		0xFA
#define BU21013_FILTER_REG		0xFB
#define BU21013_TH_ON_REG		0xFC
#define BU21013_TH_OFF_REG		0xFD


#define BU21013_RESET_ENABLE		0x01

#define BU21013_SENSORS_EN_0_7		0x3F
#define BU21013_SENSORS_EN_8_15		0xFC
#define BU21013_SENSORS_EN_16_23	0x1F

#define BU21013_POS_MODE1_0		0x02
#define BU21013_POS_MODE1_1		0x04
#define BU21013_POS_MODE1_2		0x08

#define BU21013_POS_MODE2_ZERO		0x01
#define BU21013_POS_MODE2_AVG1		0x02
#define BU21013_POS_MODE2_AVG2		0x04
#define BU21013_POS_MODE2_EN_XY		0x08
#define BU21013_POS_MODE2_EN_RAW	0x10
#define BU21013_POS_MODE2_MULTI		0x80

#define BU21013_CLK_MODE_DIV		0x01
#define BU21013_CLK_MODE_EXT		0x02
#define BU21013_CLK_MODE_CALIB		0x80

#define BU21013_IDLET_0			0x01
#define BU21013_IDLET_1			0x02
#define BU21013_IDLET_2			0x04
#define BU21013_IDLET_3			0x08
#define BU21013_IDLE_INTERMIT_EN	0x10

#define BU21013_DELTA_0_6	0x7F
#define BU21013_FILTER_EN	0x80

#define BU21013_INT_MODE_LEVEL	0x00
#define BU21013_INT_MODE_EDGE	0x01

#define BU21013_GAIN_0		0x01
#define BU21013_GAIN_1		0x02
#define BU21013_GAIN_2		0x04

#define BU21013_OFFSET_MODE_DEFAULT	0x00
#define BU21013_OFFSET_MODE_MOVE	0x01
#define BU21013_OFFSET_MODE_DISABLE	0x02

#define BU21013_TH_ON_0		0x01
#define BU21013_TH_ON_1		0x02
#define BU21013_TH_ON_2		0x04
#define BU21013_TH_ON_3		0x08
#define BU21013_TH_ON_4		0x10
#define BU21013_TH_ON_5		0x20
#define BU21013_TH_ON_6		0x40
#define BU21013_TH_ON_7		0x80
#define BU21013_TH_ON_MAX	0xFF

#define BU21013_TH_OFF_0	0x01
#define BU21013_TH_OFF_1	0x02
#define BU21013_TH_OFF_2	0x04
#define BU21013_TH_OFF_3	0x08
#define BU21013_TH_OFF_4	0x10
#define BU21013_TH_OFF_5	0x20
#define BU21013_TH_OFF_6	0x40
#define BU21013_TH_OFF_7	0x80
#define BU21013_TH_OFF_MAX	0xFF

#define BU21013_X_EDGE_0	0x01
#define BU21013_X_EDGE_1	0x02
#define BU21013_X_EDGE_2	0x04
#define BU21013_X_EDGE_3	0x08
#define BU21013_Y_EDGE_0	0x10
#define BU21013_Y_EDGE_1	0x20
#define BU21013_Y_EDGE_2	0x40
#define BU21013_Y_EDGE_3	0x80

#define BU21013_DONE	0x01
#define BU21013_NUMBER_OF_X_SENSORS	(6)
#define BU21013_NUMBER_OF_Y_SENSORS	(11)

#define DRIVER_TP	"bu21013_tp"

/**
 * struct bu21013_ts_data - touch panel data structure
 * @client: pointer to the i2c client
 * @wait: variable to wait_queue_head_t structure
 * @touch_stopped: touch stop flag
 * @chip: pointer to the touch panel controller
 * @in_dev: pointer to the input device structure
 * @intr_pin: interrupt pin value
 * @regulator: pointer to the Regulator used for touch screen
 *
 * Touch panel device data structure
 */
struct bu21013_ts_data {
	struct i2c_client *client;
	wait_queue_head_t wait;
	bool touch_stopped;
	const struct bu21013_platform_device *chip;
	struct input_dev *in_dev;
	unsigned int intr_pin;
	struct regulator *regulator;
};

/**
 * bu21013_read_block_data(): read the touch co-ordinates
 * @data: bu21013_ts_data structure pointer
 * @buf: byte pointer
 *
 * Read the touch co-ordinates using i2c read block into buffer
 * and returns integer.
 */
static int bu21013_read_block_data(struct bu21013_ts_data *data, u8 *buf)
{
	int ret, i;

	for (i = 0; i < I2C_RETRY_COUNT; i++) {
		ret = i2c_smbus_read_i2c_block_data
			(data->client, BU21013_SENSORS_BTN_0_7_REG,
				LENGTH_OF_BUFFER, buf);
		if (ret == LENGTH_OF_BUFFER)
			return 0;
	}
	return -EINVAL;
}

/**
 * bu21013_do_touch_report(): Get the touch co-ordinates
 * @data: bu21013_ts_data structure pointer
 *
 * Get the touch co-ordinates from touch sensor registers and writes
 * into device structure and returns integer.
 */
static int bu21013_do_touch_report(struct bu21013_ts_data *data)
{
	u8	buf[LENGTH_OF_BUFFER];
	unsigned int pos_x[2], pos_y[2];
	bool	has_x_sensors, has_y_sensors;
	int	finger_down_count = 0;
	int	i;

	if (data == NULL)
		return -EINVAL;

	if (bu21013_read_block_data(data, buf) < 0)
		return -EINVAL;

	has_x_sensors = hweight32(buf[0] & BU21013_SENSORS_EN_0_7);
	has_y_sensors = hweight32(((buf[1] & BU21013_SENSORS_EN_8_15) |
		((buf[2] & BU21013_SENSORS_EN_16_23) << SHIFT_8)) >> SHIFT_2);
	if (!has_x_sensors || !has_y_sensors)
		return 0;

	for (i = 0; i < MAX_FINGERS; i++) {
		const u8 *p = &buf[4 * i + 3];
		unsigned int x = p[0] << SHIFT_2 | (p[1] & MASK_BITS);
		unsigned int y = p[2] << SHIFT_2 | (p[3] & MASK_BITS);
		if (x == 0 || y == 0)
			continue;
		pos_x[finger_down_count] = x;
		pos_y[finger_down_count] = y;
		finger_down_count++;
	}

	if (finger_down_count) {
		if (finger_down_count == 2 &&
		    (abs(pos_x[0] - pos_x[1]) < DELTA_MIN ||
		     abs(pos_y[0] - pos_y[1]) < DELTA_MIN)) {
			return 0;
		}

		for (i = 0; i < finger_down_count; i++) {
			if (data->chip->x_flip)
				pos_x[i] = data->chip->touch_x_max - pos_x[i];
			if (data->chip->y_flip)
				pos_y[i] = data->chip->touch_y_max - pos_y[i];

			input_report_abs(data->in_dev,
					 ABS_MT_POSITION_X, pos_x[i]);
			input_report_abs(data->in_dev,
					 ABS_MT_POSITION_Y, pos_y[i]);
			input_mt_sync(data->in_dev);
		}
	} else
		input_mt_sync(data->in_dev);

	input_sync(data->in_dev);

	return 0;
}
/**
 * bu21013_gpio_irq() - gpio thread function for touch interrupt
 * @irq: irq value
 * @device_data: void pointer
 *
 * This gpio thread function for touch interrupt
 * and returns irqreturn_t.
 */
static irqreturn_t bu21013_gpio_irq(int irq, void *device_data)
{
	struct bu21013_ts_data *data = device_data;
	struct i2c_client *i2c = data->client;
	int retval;

	do {
		retval = bu21013_do_touch_report(data);
		if (retval < 0) {
			dev_err(&i2c->dev, "bu21013_do_touch_report failed\n");
			return IRQ_NONE;
		}

		data->intr_pin = data->chip->irq_read_val();
		if (data->intr_pin == PEN_DOWN_INTR)
			wait_event_timeout(data->wait, data->touch_stopped,
					   msecs_to_jiffies(2));
	} while (!data->intr_pin && !data->touch_stopped);

	return IRQ_HANDLED;
}

/**
 * bu21013_init_chip() - power on sequence for the bu21013 controller
 * @data: device structure pointer
 *
 * This function is used to power on
 * the bu21013 controller and returns integer.
 */
static int bu21013_init_chip(struct bu21013_ts_data *data)
{
	int retval;
	struct i2c_client *i2c = data->client;

	retval = i2c_smbus_write_byte_data(i2c, BU21013_RESET_REG,
					BU21013_RESET_ENABLE);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_RESET reg write failed\n");
		return retval;
	}
	msleep(RESET_DELAY);

	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_0_7_REG,
					BU21013_SENSORS_EN_0_7);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_0_7 reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_8_15_REG,
						BU21013_SENSORS_EN_8_15);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_8_15 reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_SENSOR_16_23_REG,
						BU21013_SENSORS_EN_16_23);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_SENSOR_16_23 reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_POS_MODE1_REG,
				(BU21013_POS_MODE1_0 | BU21013_POS_MODE1_1));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_POS_MODE1 reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_POS_MODE2_REG,
			(BU21013_POS_MODE2_ZERO | BU21013_POS_MODE2_AVG1 |
			BU21013_POS_MODE2_AVG2 | BU21013_POS_MODE2_EN_RAW |
			BU21013_POS_MODE2_MULTI));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_POS_MODE2 reg write failed\n");
		return retval;
	}

	if (data->chip->ext_clk)
		retval = i2c_smbus_write_byte_data(i2c, BU21013_CLK_MODE_REG,
			(BU21013_CLK_MODE_EXT | BU21013_CLK_MODE_CALIB));
	else
		retval = i2c_smbus_write_byte_data(i2c, BU21013_CLK_MODE_REG,
			(BU21013_CLK_MODE_DIV | BU21013_CLK_MODE_CALIB));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_CLK_MODE reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_IDLE_REG,
				(BU21013_IDLET_0 | BU21013_IDLE_INTERMIT_EN));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_IDLE reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_INT_MODE_REG,
						BU21013_INT_MODE_LEVEL);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_INT_MODE reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_FILTER_REG,
						(BU21013_DELTA_0_6 |
							BU21013_FILTER_EN));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_FILTER reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_TH_ON_REG,
					BU21013_TH_ON_5);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_TH_ON reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_TH_OFF_REG,
				BU21013_TH_OFF_4 | BU21013_TH_OFF_3);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_TH_OFF reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_GAIN_REG,
					(BU21013_GAIN_0 | BU21013_GAIN_1));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_GAIN reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_OFFSET_MODE_REG,
					BU21013_OFFSET_MODE_DEFAULT);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_OFFSET_MODE reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_XY_EDGE_REG,
				(BU21013_X_EDGE_0 | BU21013_X_EDGE_2 |
				BU21013_Y_EDGE_1 | BU21013_Y_EDGE_3));
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_XY_EDGE reg write failed\n");
		return retval;
	}

	retval = i2c_smbus_write_byte_data(i2c, BU21013_DONE_REG,
							BU21013_DONE);
	if (retval < 0) {
		dev_err(&i2c->dev, "BU21013_REG_DONE reg write failed\n");
		return retval;
	}

	return 0;
}

/**
 * bu21013_free_irq() - frees IRQ registered for touchscreen
 * @bu21013_data: device structure pointer
 *
 * This function signals interrupt thread to stop processing and
 * frees interrupt.
 */
static void bu21013_free_irq(struct bu21013_ts_data *bu21013_data)
{
	bu21013_data->touch_stopped = true;
	wake_up(&bu21013_data->wait);
	free_irq(bu21013_data->chip->irq, bu21013_data);
}

/**
 * bu21013_probe() - initializes the i2c-client touchscreen driver
 * @client: i2c client structure pointer
 * @id: i2c device id pointer
 *
 * This function used to initializes the i2c-client touchscreen
 * driver and returns integer.
 */
static int bu21013_probe(struct i2c_client *client,
					const struct i2c_device_id *id)
{
	struct bu21013_ts_data *bu21013_data;
	struct input_dev *in_dev;
	const struct bu21013_platform_device *pdata =
					client->dev.platform_data;
	int error;

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev, "i2c smbus byte data not supported\n");
		return -EIO;
	}

	if (!pdata) {
		dev_err(&client->dev, "platform data not defined\n");
		return -EINVAL;
	}

	bu21013_data = kzalloc(sizeof(struct bu21013_ts_data), GFP_KERNEL);
	in_dev = input_allocate_device();
	if (!bu21013_data || !in_dev) {
		dev_err(&client->dev, "device memory alloc failed\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	bu21013_data->in_dev = in_dev;
	bu21013_data->chip = pdata;
	bu21013_data->client = client;

	bu21013_data->regulator = regulator_get(&client->dev, "V-TOUCH");
	if (IS_ERR(bu21013_data->regulator)) {
		dev_err(&client->dev, "regulator_get failed\n");
		error = PTR_ERR(bu21013_data->regulator);
		goto err_free_mem;
	}

	error = regulator_enable(bu21013_data->regulator);
	if (error < 0) {
		dev_err(&client->dev, "regulator enable failed\n");
		goto err_put_regulator;
	}

	bu21013_data->touch_stopped = false;
	init_waitqueue_head(&bu21013_data->wait);

	/* configure the gpio pins */
	if (pdata->cs_en) {
		error = pdata->cs_en(pdata->cs_pin);
		if (error < 0) {
			dev_err(&client->dev, "chip init failed\n");
			goto err_disable_regulator;
		}
	}

	/* configure the touch panel controller */
	error = bu21013_init_chip(bu21013_data);
	if (error) {
		dev_err(&client->dev, "error in bu21013 config\n");
		goto err_cs_disable;
	}

	/* register the device to input subsystem */
	in_dev->name = DRIVER_TP;
	in_dev->id.bustype = BUS_I2C;
	in_dev->dev.parent = &client->dev;

	__set_bit(EV_SYN, in_dev->evbit);
	__set_bit(EV_KEY, in_dev->evbit);
	__set_bit(EV_ABS, in_dev->evbit);

	input_set_abs_params(in_dev, ABS_MT_POSITION_X, 0,
						pdata->touch_x_max, 0, 0);
	input_set_abs_params(in_dev, ABS_MT_POSITION_Y, 0,
						pdata->touch_y_max, 0, 0);
	input_set_drvdata(in_dev, bu21013_data);

	error = request_threaded_irq(pdata->irq, NULL, bu21013_gpio_irq,
				     IRQF_TRIGGER_FALLING | IRQF_SHARED |
					IRQF_ONESHOT,
				     DRIVER_TP, bu21013_data);
	if (error) {
		dev_err(&client->dev, "request irq %d failed\n", pdata->irq);
		goto err_cs_disable;
	}

	error = input_register_device(in_dev);
	if (error) {
		dev_err(&client->dev, "failed to register input device\n");
		goto err_free_irq;
	}

	device_init_wakeup(&client->dev, pdata->wakeup);
	i2c_set_clientdata(client, bu21013_data);

	return 0;

err_free_irq:
	bu21013_free_irq(bu21013_data);
err_cs_disable:
	pdata->cs_dis(pdata->cs_pin);
err_disable_regulator:
	regulator_disable(bu21013_data->regulator);
err_put_regulator:
	regulator_put(bu21013_data->regulator);
err_free_mem:
	input_free_device(in_dev);
	kfree(bu21013_data);

	return error;
}
/**
 * bu21013_remove() - removes the i2c-client touchscreen driver
 * @client: i2c client structure pointer
 *
 * This function uses to remove the i2c-client
 * touchscreen driver and returns integer.
 */
static int __devexit bu21013_remove(struct i2c_client *client)
{
	struct bu21013_ts_data *bu21013_data = i2c_get_clientdata(client);

	bu21013_free_irq(bu21013_data);

	bu21013_data->chip->cs_dis(bu21013_data->chip->cs_pin);

	input_unregister_device(bu21013_data->in_dev);

	regulator_disable(bu21013_data->regulator);
	regulator_put(bu21013_data->regulator);

	kfree(bu21013_data);

	device_init_wakeup(&client->dev, false);

	return 0;
}

#ifdef CONFIG_PM
/**
 * bu21013_suspend() - suspend the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to suspend the
 * touch panel controller and returns integer
 */
static int bu21013_suspend(struct device *dev)
{
	struct bu21013_ts_data *bu21013_data = dev_get_drvdata(dev);
	struct i2c_client *client = bu21013_data->client;

	bu21013_data->touch_stopped = true;
	if (device_may_wakeup(&client->dev))
		enable_irq_wake(bu21013_data->chip->irq);
	else
		disable_irq(bu21013_data->chip->irq);

	regulator_disable(bu21013_data->regulator);

	return 0;
}

/**
 * bu21013_resume() - resume the touch screen controller
 * @dev: pointer to device structure
 *
 * This function is used to resume the touch panel
 * controller and returns integer.
 */
static int bu21013_resume(struct device *dev)
{
	struct bu21013_ts_data *bu21013_data = dev_get_drvdata(dev);
	struct i2c_client *client = bu21013_data->client;
	int retval;

	retval = regulator_enable(bu21013_data->regulator);
	if (retval < 0) {
		dev_err(&client->dev, "bu21013 regulator enable failed\n");
		return retval;
	}

	retval = bu21013_init_chip(bu21013_data);
	if (retval < 0) {
		dev_err(&client->dev, "bu21013 controller config failed\n");
		return retval;
	}

	bu21013_data->touch_stopped = false;

	if (device_may_wakeup(&client->dev))
		disable_irq_wake(bu21013_data->chip->irq);
	else
		enable_irq(bu21013_data->chip->irq);

	return 0;
}

static const struct dev_pm_ops bu21013_dev_pm_ops = {
	.suspend = bu21013_suspend,
	.resume  = bu21013_resume,
};
#endif

static const struct i2c_device_id bu21013_id[] = {
	{ DRIVER_TP, 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bu21013_id);

static struct i2c_driver bu21013_driver = {
	.driver	= {
		.name	=	DRIVER_TP,
		.owner	=	THIS_MODULE,
#ifdef CONFIG_PM
		.pm	=	&bu21013_dev_pm_ops,
#endif
	},
	.probe		=	bu21013_probe,
	.remove		=	bu21013_remove,
	.id_table	=	bu21013_id,
};

module_i2c_driver(bu21013_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Naveen Kumar G <naveen.gaddipati@stericsson.com>");
MODULE_DESCRIPTION("bu21013 touch screen controller driver");
