/*
 * Copyright (C) 2007-2008 HTC Corporation.
 *
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
 *
 * This driver is adapted from elan8232_i2c.c written by Shan-Fu Chiou
 * <sfchiou@gmail.com> and Jay Tu <jay_tu@htc.com>.
 * This driver is also adapted from the ELAN Touch Screen driver
 * written by Stanley Zeng <stanley.zeng@emc.com.tw>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/input.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/interrupt.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/of_gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/gpio.h>

static const char ELAN_TS_NAME[] = "elan-touch";

#define ELAN_TS_X_MAX		1088
#define ELAN_TS_Y_MAX		768
#define ELAN_USER_X_MAX		800
#define ELAN_USER_Y_MAX		600
#define IDX_PACKET_SIZE		8

enum {
	hello_packet = 0x55,
	idx_coordinate_packet = 0x5a,
};

enum {
	idx_finger_state = 7,
};

static struct workqueue_struct *elan_wq;

static struct elan_data {
	int intr_gpio;
	int use_irq;
	struct hrtimer timer;
	struct work_struct work;
	struct i2c_client *client;
	struct input_dev *input;
	wait_queue_head_t wait;
} elan_touch_data;

/*--------------------------------------------------------------*/
static int elan_touch_detect_int_level(void)
{
	int v;
	v = gpio_get_value(elan_touch_data.intr_gpio);

	return v;
}

static int __elan_touch_poll(struct i2c_client *client)
{
	int status = 0, retry = 20;

	do {
		status = elan_touch_detect_int_level();
		retry--;
		mdelay(20);
	} while (status == 1 && retry > 0);

	return status == 0 ? 0 : -ETIMEDOUT;
}

static int elan_touch_poll(struct i2c_client *client)
{
	return __elan_touch_poll(client);
}

static int __hello_packet_handler(struct i2c_client *client)
{
	int rc;
	uint8_t buf_recv[4] = { 0 };

	rc = elan_touch_poll(client);

	if (rc < 0)
		return -EINVAL;

	rc = i2c_master_recv(client, buf_recv, 4);

	if (rc != 4) {
		return rc;
	} else {
		int i;
		pr_info("hello packet: [0x%02x 0x%02x 0x%02x 0x%02x]\n",
		       buf_recv[0], buf_recv[1], buf_recv[2], buf_recv[3]);

		for (i = 0; i < 4; i++)
			if (buf_recv[i] != hello_packet)
				return -EINVAL;
	}

	return 0;
}

static inline int elan_touch_parse_xy(uint8_t *data, uint16_t *x,
				      uint16_t *y)
{
	*x = (data[0] & 0xf0);
	*x <<= 4;
	*x |= data[1];
	if (*x >= ELAN_TS_X_MAX)
		*x = ELAN_TS_X_MAX;
	*x = ((((ELAN_TS_X_MAX -
		 *x) * 1000) / ELAN_TS_X_MAX) * ELAN_USER_X_MAX) / 1000;

	*y = (data[0] & 0x0f);
	*y <<= 8;
	*y |= data[2];
	if (*y >= ELAN_TS_Y_MAX)
		*y = ELAN_TS_Y_MAX;
	*y = ((((ELAN_TS_Y_MAX -
		 *y) * 1000) / ELAN_TS_Y_MAX) * ELAN_USER_Y_MAX) / 1000;

	return 0;
}

/*	__elan_touch_init -- hand shaking with touch panel
 *
 *	1.recv hello packet
 */
static int __elan_touch_init(struct i2c_client *client)
{
	int rc;
	rc = __hello_packet_handler(client);
	if (rc < 0)
		goto hand_shake_failed;

hand_shake_failed:
	return rc;
}

static int elan_touch_recv_data(struct i2c_client *client, uint8_t *buf)
{
	int rc, bytes_to_recv = IDX_PACKET_SIZE;

	if (buf == NULL)
		return -EINVAL;

	memset(buf, 0, bytes_to_recv);
	rc = i2c_master_recv(client, buf, bytes_to_recv);
	if (rc != bytes_to_recv)
		return -EINVAL;

	return rc;
}

static void elan_touch_report_data(struct i2c_client *client, uint8_t *buf)
{
	switch (buf[0]) {
	case idx_coordinate_packet:
	{
		uint16_t x1, x2, y1, y2;
		uint8_t finger_stat;

		finger_stat = (buf[idx_finger_state] & 0x06) >> 1;

		if (finger_stat == 0) {
			input_report_key(elan_touch_data.input, BTN_TOUCH, 0);
			input_report_key(elan_touch_data.input, BTN_2, 0);
		} else if (finger_stat == 1) {
			elan_touch_parse_xy(&buf[1], &x1, &y1);
			input_report_abs(elan_touch_data.input, ABS_X, x1);
			input_report_abs(elan_touch_data.input, ABS_Y, y1);
			input_report_key(elan_touch_data.input, BTN_TOUCH, 1);
			input_report_key(elan_touch_data.input, BTN_2, 0);
		} else if (finger_stat == 2) {
			elan_touch_parse_xy(&buf[1], &x1, &y1);
			input_report_abs(elan_touch_data.input, ABS_X, x1);
			input_report_abs(elan_touch_data.input, ABS_Y, y1);
			input_report_key(elan_touch_data.input, BTN_TOUCH, 1);
			elan_touch_parse_xy(&buf[4], &x2, &y2);
			input_report_abs(elan_touch_data.input, ABS_HAT0X, x2);
			input_report_abs(elan_touch_data.input, ABS_HAT0Y, y2);
			input_report_key(elan_touch_data.input, BTN_2, 1);
		}
		input_sync(elan_touch_data.input);
		break;
	}

	default:
		break;
	}
}

static void elan_touch_work_func(struct work_struct *work)
{
	int rc;
	uint8_t buf[IDX_PACKET_SIZE] = { 0 };
	struct i2c_client *client = elan_touch_data.client;

	if (elan_touch_detect_int_level())
		return;

	rc = elan_touch_recv_data(client, buf);
	if (rc < 0)
		return;

	elan_touch_report_data(client, buf);
}

static irqreturn_t elan_touch_ts_interrupt(int irq, void *dev_id)
{
	queue_work(elan_wq, &elan_touch_data.work);

	return IRQ_HANDLED;
}

static enum hrtimer_restart elan_touch_timer_func(struct hrtimer *timer)
{
	queue_work(elan_wq, &elan_touch_data.work);
	hrtimer_start(&elan_touch_data.timer, ktime_set(0, 12500000),
		      HRTIMER_MODE_REL);

	return HRTIMER_NORESTART;
}

static int elan_touch_register_interrupt(struct i2c_client *client)
{
	int err = 0;

	if (client->irq) {
		elan_touch_data.use_irq = 1;
		err =
		    request_irq(client->irq, elan_touch_ts_interrupt,
				IRQF_TRIGGER_FALLING, ELAN_TS_NAME,
				&elan_touch_data);

		if (err < 0) {
			pr_info("%s(%s): Can't allocate irq %d\n", __FILE__,
			       __func__, client->irq);
			elan_touch_data.use_irq = 0;
		}
	}

	if (!elan_touch_data.use_irq) {
		hrtimer_init(&elan_touch_data.timer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		elan_touch_data.timer.function = elan_touch_timer_func;
		hrtimer_start(&elan_touch_data.timer, ktime_set(1, 0),
			      HRTIMER_MODE_REL);
	}

	pr_info("elan ts starts in %s mode.\n",
	       elan_touch_data.use_irq == 1 ? "interrupt" : "polling");

	return 0;
}

static int elan_touch_probe(struct i2c_client *client,
			    const struct i2c_device_id *id)
{
	struct device_node *np = client->dev.of_node;
	int gpio_elan_cs, gpio_elan_rst, err = 0;

	if (!np)
		return -ENODEV;

	elan_touch_data.intr_gpio = of_get_named_gpio(np, "gpio_intr", 0);
	if (!gpio_is_valid(elan_touch_data.intr_gpio))
		return -ENODEV;

	err = devm_gpio_request_one(&client->dev, elan_touch_data.intr_gpio,
				GPIOF_IN, "gpio_elan_intr");
	if (err < 0) {
		dev_err(&client->dev,
			"request gpio failed: %d\n", err);
		return err;
	}

	/* elan touch init */
	gpio_elan_cs = of_get_named_gpio(np, "gpio_elan_cs", 0);
	if (!gpio_is_valid(gpio_elan_cs))
		return -ENODEV;

	err = devm_gpio_request_one(&client->dev, gpio_elan_cs,
				GPIOF_OUT_INIT_HIGH, "gpio_elan_cs");
	if (err < 0) {
		dev_err(&client->dev,
			"request gpio failed: %d\n", err);
		return err;
	}
	gpio_set_value(gpio_elan_cs, 0);

	gpio_elan_rst = of_get_named_gpio(np, "gpio_elan_rst", 0);
	if (!gpio_is_valid(gpio_elan_rst))
		return -ENODEV;

	err = devm_gpio_request_one(&client->dev, gpio_elan_rst,
				GPIOF_OUT_INIT_HIGH, "gpio_elan_rst");
	if (err < 0) {
		dev_err(&client->dev,
			"request gpio failed: %d\n", err);
		return err;
	}
	gpio_set_value(gpio_elan_rst, 0);
	msleep(10);
	gpio_set_value(gpio_elan_rst, 1);

	gpio_set_value(gpio_elan_cs, 1);
	msleep(100);

	elan_wq = create_singlethread_workqueue("elan_wq");
	if (!elan_wq) {
		err = -ENOMEM;
		goto fail;
	}

	elan_touch_data.client = client;
	strlcpy(client->name, ELAN_TS_NAME, I2C_NAME_SIZE);

	INIT_WORK(&elan_touch_data.work, elan_touch_work_func);

	elan_touch_data.input = input_allocate_device();
	if (elan_touch_data.input == NULL) {
		err = -ENOMEM;
		goto fail;
	}

	err = __elan_touch_init(client);
	if (err < 0) {
		dev_err(&client->dev, "elan - Read Hello Packet Failed\n");
		goto fail;
	}

	elan_touch_data.input->name = ELAN_TS_NAME;
	elan_touch_data.input->id.bustype = BUS_I2C;

	set_bit(EV_SYN, elan_touch_data.input->evbit);

	set_bit(EV_KEY, elan_touch_data.input->evbit);
	set_bit(BTN_TOUCH, elan_touch_data.input->keybit);
	set_bit(BTN_2, elan_touch_data.input->keybit);

	set_bit(EV_ABS, elan_touch_data.input->evbit);
	set_bit(ABS_X, elan_touch_data.input->absbit);
	set_bit(ABS_Y, elan_touch_data.input->absbit);
	set_bit(ABS_HAT0X, elan_touch_data.input->absbit);
	set_bit(ABS_HAT0Y, elan_touch_data.input->absbit);

	input_set_abs_params(elan_touch_data.input, ABS_X, 0, ELAN_USER_X_MAX,
			     0, 0);
	input_set_abs_params(elan_touch_data.input, ABS_Y, 0, ELAN_USER_Y_MAX,
			     0, 0);
	input_set_abs_params(elan_touch_data.input, ABS_HAT0X, 0,
			     ELAN_USER_X_MAX, 0, 0);
	input_set_abs_params(elan_touch_data.input, ABS_HAT0Y, 0,
			     ELAN_USER_Y_MAX, 0, 0);

	err = input_register_device(elan_touch_data.input);
	if (err < 0)
		goto fail;

	elan_touch_register_interrupt(elan_touch_data.client);

	return 0;

fail:
	input_free_device(elan_touch_data.input);
	if (elan_wq)
		destroy_workqueue(elan_wq);
	return err;
}

static int elan_touch_remove(struct i2c_client *client)
{
	if (elan_wq)
		destroy_workqueue(elan_wq);

	input_unregister_device(elan_touch_data.input);

	if (elan_touch_data.use_irq)
		free_irq(client->irq, client);
	else
		hrtimer_cancel(&elan_touch_data.timer);
	return 0;
}

/* -------------------------------------------------------------------- */
static const struct i2c_device_id elan_touch_id[] = {
	{"elan-touch", 0},
	{}
};

static const struct of_device_id elan_dt_ids[] = {
	{
		.compatible = "elan,elan-touch",
	}, {
		/* sentinel */
	}
};
MODULE_DEVICE_TABLE(of, elan_dt_ids);

static int elan_suspend(struct device *dev)
{
	return 0;
}

static int elan_resume(struct device *dev)
{
	uint8_t buf[IDX_PACKET_SIZE] = { 0 };

	if (0 == elan_touch_detect_int_level()) {
		dev_dbg(dev, "Got touch during suspend period.\n");
		/*
		 * if touch screen during suspend, recv and drop the
		 * data, then touch interrupt pin will return high after
		 * receving data.
		 */
		elan_touch_recv_data(elan_touch_data.client, buf);
	}

	return 0;
}

static const struct dev_pm_ops elan_dev_pm_ops = {
	.suspend = elan_suspend,
	.resume  = elan_resume,
};

static struct i2c_driver elan_touch_driver = {
	.probe = elan_touch_probe,
	.remove = elan_touch_remove,
	.id_table = elan_touch_id,
	.driver = {
		   .name = "elan-touch",
		   .owner = THIS_MODULE,
		   .of_match_table = elan_dt_ids,
#ifdef CONFIG_PM
		   .pm = &elan_dev_pm_ops,
#endif
		   },
};

static int __init elan_touch_init(void)
{
	return i2c_add_driver(&elan_touch_driver);
}

static void __exit elan_touch_exit(void)
{
	i2c_del_driver(&elan_touch_driver);
}

module_init(elan_touch_init);
module_exit(elan_touch_exit);

MODULE_AUTHOR("Stanley Zeng <stanley.zeng@emc.com.tw>");
MODULE_DESCRIPTION("ELAN Touch Screen driver");
MODULE_LICENSE("GPL");
