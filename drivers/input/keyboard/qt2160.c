/*
 *  qt2160.c - Atmel AT42QT2160 Touch Sense Controller
 *
 *  Copyright (C) 2009 Raphael Derosso Pereira <raphaelpereira@gmail.com>
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
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/input.h>

#define QT2160_VALID_CHIPID  0x11

#define QT2160_CMD_CHIPID     0
#define QT2160_CMD_CODEVER    1
#define QT2160_CMD_GSTAT      2
#define QT2160_CMD_KEYS3      3
#define QT2160_CMD_KEYS4      4
#define QT2160_CMD_SLIDE      5
#define QT2160_CMD_GPIOS      6
#define QT2160_CMD_SUBVER     7
#define QT2160_CMD_CALIBRATE  10

#define QT2160_CYCLE_INTERVAL	(2*HZ)

static unsigned char qt2160_key2code[] = {
	KEY_0, KEY_1, KEY_2, KEY_3,
	KEY_4, KEY_5, KEY_6, KEY_7,
	KEY_8, KEY_9, KEY_A, KEY_B,
	KEY_C, KEY_D, KEY_E, KEY_F,
};

struct qt2160_data {
	struct i2c_client *client;
	struct input_dev *input;
	struct delayed_work dwork;
	spinlock_t lock;        /* Protects canceling/rescheduling of dwork */
	unsigned short keycodes[ARRAY_SIZE(qt2160_key2code)];
	u16 key_matrix;
};

static int qt2160_read_block(struct i2c_client *client,
			     u8 inireg, u8 *buffer, unsigned int count)
{
	int error, idx = 0;

	/*
	 * Can't use SMBus block data read. Check for I2C functionality to speed
	 * things up whenever possible. Otherwise we will be forced to read
	 * sequentially.
	 */
	if (i2c_check_functionality(client->adapter, I2C_FUNC_I2C))	{

		error = i2c_smbus_write_byte(client, inireg + idx);
		if (error) {
			dev_err(&client->dev,
				"couldn't send request. Returned %d\n", error);
			return error;
		}

		error = i2c_master_recv(client, buffer, count);
		if (error != count) {
			dev_err(&client->dev,
				"couldn't read registers. Returned %d bytes\n", error);
			return error;
		}
	} else {

		while (count--) {
			int data;

			error = i2c_smbus_write_byte(client, inireg + idx);
			if (error) {
				dev_err(&client->dev,
					"couldn't send request. Returned %d\n", error);
				return error;
			}

			data = i2c_smbus_read_byte(client);
			if (data < 0) {
				dev_err(&client->dev,
					"couldn't read register. Returned %d\n", data);
				return data;
			}

			buffer[idx++] = data;
		}
	}

	return 0;
}

static int qt2160_get_key_matrix(struct qt2160_data *qt2160)
{
	struct i2c_client *client = qt2160->client;
	struct input_dev *input = qt2160->input;
	u8 regs[6];
	u16 old_matrix, new_matrix;
	int ret, i, mask;

	dev_dbg(&client->dev, "requesting keys...\n");

	/*
	 * Read all registers from General Status Register
	 * to GPIOs register
	 */
	ret = qt2160_read_block(client, QT2160_CMD_GSTAT, regs, 6);
	if (ret) {
		dev_err(&client->dev,
			"could not perform chip read.\n");
		return ret;
	}

	old_matrix = qt2160->key_matrix;
	qt2160->key_matrix = new_matrix = (regs[2] << 8) | regs[1];

	mask = 0x01;
	for (i = 0; i < 16; ++i, mask <<= 1) {
		int keyval = new_matrix & mask;

		if ((old_matrix & mask) != keyval) {
			input_report_key(input, qt2160->keycodes[i], keyval);
			dev_dbg(&client->dev, "key %d %s\n",
				i, keyval ? "pressed" : "released");
		}
	}

	input_sync(input);

	return 0;
}

static irqreturn_t qt2160_irq(int irq, void *_qt2160)
{
	struct qt2160_data *qt2160 = _qt2160;
	unsigned long flags;

	spin_lock_irqsave(&qt2160->lock, flags);

	mod_delayed_work(system_wq, &qt2160->dwork, 0);

	spin_unlock_irqrestore(&qt2160->lock, flags);

	return IRQ_HANDLED;
}

static void qt2160_schedule_read(struct qt2160_data *qt2160)
{
	spin_lock_irq(&qt2160->lock);
	schedule_delayed_work(&qt2160->dwork, QT2160_CYCLE_INTERVAL);
	spin_unlock_irq(&qt2160->lock);
}

static void qt2160_worker(struct work_struct *work)
{
	struct qt2160_data *qt2160 =
		container_of(work, struct qt2160_data, dwork.work);

	dev_dbg(&qt2160->client->dev, "worker\n");

	qt2160_get_key_matrix(qt2160);

	/* Avoid device lock up by checking every so often */
	qt2160_schedule_read(qt2160);
}

static int qt2160_read(struct i2c_client *client, u8 reg)
{
	int ret;

	ret = i2c_smbus_write_byte(client, reg);
	if (ret) {
		dev_err(&client->dev,
			"couldn't send request. Returned %d\n", ret);
		return ret;
	}

	ret = i2c_smbus_read_byte(client);
	if (ret < 0) {
		dev_err(&client->dev,
			"couldn't read register. Returned %d\n", ret);
		return ret;
	}

	return ret;
}

static int qt2160_write(struct i2c_client *client, u8 reg, u8 data)
{
	int ret;

	ret = i2c_smbus_write_byte_data(client, reg, data);
	if (ret < 0)
		dev_err(&client->dev,
			"couldn't write data. Returned %d\n", ret);

	return ret;
}


static bool qt2160_identify(struct i2c_client *client)
{
	int id, ver, rev;

	/* Read Chid ID to check if chip is valid */
	id = qt2160_read(client, QT2160_CMD_CHIPID);
	if (id != QT2160_VALID_CHIPID) {
		dev_err(&client->dev, "ID %d not supported\n", id);
		return false;
	}

	/* Read chip firmware version */
	ver = qt2160_read(client, QT2160_CMD_CODEVER);
	if (ver < 0) {
		dev_err(&client->dev, "could not get firmware version\n");
		return false;
	}

	/* Read chip firmware revision */
	rev = qt2160_read(client, QT2160_CMD_SUBVER);
	if (rev < 0) {
		dev_err(&client->dev, "could not get firmware revision\n");
		return false;
	}

	dev_info(&client->dev, "AT42QT2160 firmware version %d.%d.%d\n",
			ver >> 4, ver & 0xf, rev);

	return true;
}

static int qt2160_probe(struct i2c_client *client,
				  const struct i2c_device_id *id)
{
	struct qt2160_data *qt2160;
	struct input_dev *input;
	int i;
	int error;

	/* Check functionality */
	error = i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE);
	if (!error) {
		dev_err(&client->dev, "%s adapter not supported\n",
				dev_driver_string(&client->adapter->dev));
		return -ENODEV;
	}

	if (!qt2160_identify(client))
		return -ENODEV;

	/* Chip is valid and active. Allocate structure */
	qt2160 = kzalloc(sizeof(struct qt2160_data), GFP_KERNEL);
	input = input_allocate_device();
	if (!qt2160 || !input) {
		dev_err(&client->dev, "insufficient memory\n");
		error = -ENOMEM;
		goto err_free_mem;
	}

	qt2160->client = client;
	qt2160->input = input;
	INIT_DELAYED_WORK(&qt2160->dwork, qt2160_worker);
	spin_lock_init(&qt2160->lock);

	input->name = "AT42QT2160 Touch Sense Keyboard";
	input->id.bustype = BUS_I2C;

	input->keycode = qt2160->keycodes;
	input->keycodesize = sizeof(qt2160->keycodes[0]);
	input->keycodemax = ARRAY_SIZE(qt2160_key2code);

	__set_bit(EV_KEY, input->evbit);
	__clear_bit(EV_REP, input->evbit);
	for (i = 0; i < ARRAY_SIZE(qt2160_key2code); i++) {
		qt2160->keycodes[i] = qt2160_key2code[i];
		__set_bit(qt2160_key2code[i], input->keybit);
	}
	__clear_bit(KEY_RESERVED, input->keybit);

	/* Calibrate device */
	error = qt2160_write(client, QT2160_CMD_CALIBRATE, 1);
	if (error) {
		dev_err(&client->dev, "failed to calibrate device\n");
		goto err_free_mem;
	}

	if (client->irq) {
		error = request_irq(client->irq, qt2160_irq,
				    IRQF_TRIGGER_FALLING, "qt2160", qt2160);
		if (error) {
			dev_err(&client->dev,
				"failed to allocate irq %d\n", client->irq);
			goto err_free_mem;
		}
	}

	error = input_register_device(qt2160->input);
	if (error) {
		dev_err(&client->dev,
			"Failed to register input device\n");
		goto err_free_irq;
	}

	i2c_set_clientdata(client, qt2160);
	qt2160_schedule_read(qt2160);

	return 0;

err_free_irq:
	if (client->irq)
		free_irq(client->irq, qt2160);
err_free_mem:
	input_free_device(input);
	kfree(qt2160);
	return error;
}

static int qt2160_remove(struct i2c_client *client)
{
	struct qt2160_data *qt2160 = i2c_get_clientdata(client);

	/* Release IRQ so no queue will be scheduled */
	if (client->irq)
		free_irq(client->irq, qt2160);

	cancel_delayed_work_sync(&qt2160->dwork);

	input_unregister_device(qt2160->input);
	kfree(qt2160);

	return 0;
}

static const struct i2c_device_id qt2160_idtable[] = {
	{ "qt2160", 0, },
	{ }
};

MODULE_DEVICE_TABLE(i2c, qt2160_idtable);

static struct i2c_driver qt2160_driver = {
	.driver = {
		.name	= "qt2160",
		.owner  = THIS_MODULE,
	},

	.id_table	= qt2160_idtable,
	.probe		= qt2160_probe,
	.remove		= qt2160_remove,
};

module_i2c_driver(qt2160_driver);

MODULE_AUTHOR("Raphael Derosso Pereira <raphaelpereira@gmail.com>");
MODULE_DESCRIPTION("Driver for AT42QT2160 Touch Sensor");
MODULE_LICENSE("GPL");
