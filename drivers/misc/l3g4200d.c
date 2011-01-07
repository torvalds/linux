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

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/l3g4200d.h>

/** Register map */
#define L3G4200D_WHO_AM_I		0x0f
#define L3G4200D_CTRL_REG1		0x20
#define L3G4200D_CTRL_REG2		0x21
#define L3G4200D_CTRL_REG3		0x22
#define L3G4200D_CTRL_REG4		0x23
#define L3G4200D_CTRL_REG5		0x24

#define L3G4200D_REF_DATA_CAP		0x25
#define L3G4200D_OUT_TEMP		0x26
#define L3G4200D_STATUS_REG		0x27

#define L3G4200D_OUT_X_L		0x28
#define L3G4200D_OUT_X_H		0x29
#define L3G4200D_OUT_Y_L		0x2a
#define L3G4200D_OUT_Y_H		0x2b
#define L3G4200D_OUT_Z_L		0x2c
#define L3G4200D_OUT_Z_H		0x2d

#define L3G4200D_FIFO_CTRL		0x2e
#define L3G4200D_FIFO_SRC		0x2e

#define L3G4200D_INTERRUPT_CFG		0x30
#define L3G4200D_INTERRUPT_SRC		0x31
#define L3G4200D_INTERRUPT_THRESH_X_H	0x32
#define L3G4200D_INTERRUPT_THRESH_X_L	0x33
#define L3G4200D_INTERRUPT_THRESH_Y_H	0x34
#define L3G4200D_INTERRUPT_THRESH_Y_L	0x35
#define L3G4200D_INTERRUPT_THRESH_Z_H	0x36
#define L3G4200D_INTERRUPT_THRESH_Z_L	0x37
#define L3G4200D_INTERRUPT_DURATION	0x38

#define PM_MASK                         0x08
#define ENABLE_ALL_AXES			0x07

#define I2C_RETRY_DELAY			5
#define I2C_RETRIES			5
#define AUTO_INCREMENT			0x80
#define L3G4200D_PU_DELAY               300


struct l3g4200d_data {
	struct i2c_client *client;
	struct l3g4200d_platform_data *pdata;

	struct delayed_work input_work;
	struct input_dev *input_dev;

	int hw_initialized;
	atomic_t enabled;
	int on_before_suspend;
	struct regulator *regulator;

	u8 shift_adj;
	u8 resume_state[5];
};

struct gyro_val {
	s16 x;
	s16 y;
	s16 z;
};

static uint32_t l3g4200d_debug;
module_param_named(gyro_debug, l3g4200d_debug, uint, 0664);

/*
 * Because misc devices can not carry a pointer from driver register to
 * open, we keep this global.  This limits the driver to a single instance.
 */
struct l3g4200d_data *l3g4200d_misc_data;

static int l3g4200d_i2c_read(struct l3g4200d_data *gyro, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = gyro->client->addr,
			.flags = gyro->client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf,
		},
		{
			.addr = gyro->client->addr,
			.flags = (gyro->client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = len,
			.buf = buf,
		},
	};

	do {
		err = i2c_transfer(gyro->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&gyro->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int l3g4200d_i2c_write(struct l3g4200d_data *gyro, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = gyro->client->addr,
			.flags = gyro->client->flags & I2C_M_TEN,
			.len = len + 1,
			.buf = buf,
		},
	};

	do {
		err = i2c_transfer(gyro->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&gyro->client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}
static int l3g4200d_hw_init(struct l3g4200d_data *gyro)
{
	int err = -1;
	u8 buf[8];

	buf[0] = (AUTO_INCREMENT | L3G4200D_CTRL_REG1);
	buf[1] = gyro->pdata->ctrl_reg1;
	buf[2] = gyro->pdata->ctrl_reg2;
	buf[3] = gyro->pdata->ctrl_reg3;
	buf[4] = gyro->pdata->ctrl_reg4;
	buf[5] = gyro->pdata->ctrl_reg5;
	buf[6] = gyro->pdata->reference;
	err = l3g4200d_i2c_write(gyro, buf, 6);
	if (err < 0)
		return err;

	buf[0] = (L3G4200D_FIFO_CTRL);
	buf[1] = gyro->pdata->fifo_ctrl_reg;
	err = l3g4200d_i2c_write(gyro, buf, 1);
	if (err < 0)
		return err;

	buf[0] = (L3G4200D_INTERRUPT_CFG);
	buf[1] = gyro->pdata->int1_cfg;
	err = l3g4200d_i2c_write(gyro, buf, 1);
	if (err < 0)
		return err;

	buf[0] = (AUTO_INCREMENT | L3G4200D_INTERRUPT_THRESH_X_H);
	buf[1] = gyro->pdata->int1_tsh_xh;
	buf[2] = gyro->pdata->int1_tsh_xl;
	buf[3] = gyro->pdata->int1_tsh_yh;
	buf[4] = gyro->pdata->int1_tsh_yl;
	buf[5] = gyro->pdata->int1_tsh_zh;
	buf[6] = gyro->pdata->int1_tsh_zl;
	buf[7] = gyro->pdata->int1_duration;
	err = l3g4200d_i2c_write(gyro, buf, 7);
	if (err < 0)
		return err;

	gyro->hw_initialized = true;

	return 0;
}

static void l3g4200d_device_power_off(struct l3g4200d_data *gyro)
{
	int err;
	u8 buf[2] = {L3G4200D_CTRL_REG1, 0};

	err = l3g4200d_i2c_read(gyro, buf, 1);
	if (err < 0) {
		dev_err(&gyro->client->dev, "read register control_1 failed\n");
		return;
	}
	buf[1] = buf[0] & ~PM_MASK;
	buf[0] = L3G4200D_CTRL_REG1;

	err = l3g4200d_i2c_write(gyro, buf, 1);
	if (err < 0)
		dev_err(&gyro->client->dev, "soft power off failed\n");

	if (gyro->regulator) {
		regulator_disable(gyro->regulator);
		gyro->hw_initialized = false;
	}
}

static int l3g4200d_device_power_on(struct l3g4200d_data *gyro)
{
	int err;
	u8 buf[2] = {L3G4200D_CTRL_REG1, 0};

	if (gyro->regulator) {
		err = regulator_enable(gyro->regulator);
		if (err < 0)
			return err;
	}

	if (!gyro->hw_initialized) {
		err = l3g4200d_hw_init(gyro);
		if (err < 0) {
			l3g4200d_device_power_off(gyro);
			return err;
		}
	}
	err = l3g4200d_i2c_read(gyro, buf, 1);
	if (err < 0)
		dev_err(&gyro->client->dev, "read register control_1 failed\n");
	buf[1] = buf[0] | PM_MASK;
	buf[0] = L3G4200D_CTRL_REG1;

	err = l3g4200d_i2c_write(gyro, buf, 1);
	if (err < 0)
		dev_err(&gyro->client->dev, "soft power on failed\n");
	return 0;
}

static int l3g4200d_get_gyro_data(struct l3g4200d_data *gyro,
					 struct gyro_val *data)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 gyro_data[6];

	gyro_data[0] = (AUTO_INCREMENT | L3G4200D_OUT_X_L);
	err = l3g4200d_i2c_read(gyro, gyro_data, 6);
	if (err < 0)
		return err;

	data->x = (gyro_data[1] << 8) | gyro_data[0];
	data->y = (gyro_data[3] << 8) | gyro_data[2];
	data->z = (gyro_data[5] << 8) | gyro_data[4];

	return 0;
}

static void l3g4200d_report_values(struct l3g4200d_data *gyro,
					 struct gyro_val *data)
{
	input_report_rel(gyro->input_dev, REL_RX, data->x);
	input_report_rel(gyro->input_dev, REL_RY, data->y);
	input_report_rel(gyro->input_dev, REL_RZ, data->z);

	if (l3g4200d_debug)
		pr_info("%s: Reporting x: %d, y: %d, z: %d\n",
		__func__, data->x, data->y, data->z);
	input_sync(gyro->input_dev);
}

static int l3g4200d_enable(struct l3g4200d_data *gyro)
{
	int err;

	if (!atomic_cmpxchg(&gyro->enabled, 0, 1)) {

		err = l3g4200d_device_power_on(gyro);
		if (err < 0) {
			atomic_set(&gyro->enabled, 0);
			return err;
		}
		schedule_delayed_work(&gyro->input_work,
			msecs_to_jiffies(L3G4200D_PU_DELAY));
	}

	return 0;
}

static int l3g4200d_disable(struct l3g4200d_data *gyro)
{
	if (atomic_cmpxchg(&gyro->enabled, 1, 0)) {
		cancel_delayed_work_sync(&gyro->input_work);
		l3g4200d_device_power_off(gyro);
	}

	return 0;
}

static int l3g4200d_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = l3g4200d_misc_data;

	return 0;
}

static long l3g4200d_misc_ioctl(struct file *file,
				unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int interval;
	struct l3g4200d_data *gyro = file->private_data;

	switch (cmd) {
	case L3G4200D_IOCTL_GET_DELAY:
		interval = gyro->pdata->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;

	case L3G4200D_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		gyro->pdata->poll_interval =
		    max(interval, gyro->pdata->min_interval);
		break;

	case L3G4200D_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;

		if (interval)
			l3g4200d_enable(gyro);
		else
			l3g4200d_disable(gyro);

		break;

	case L3G4200D_IOCTL_GET_ENABLE:
		interval = atomic_read(&gyro->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations l3g4200d_misc_fops = {
	.owner = THIS_MODULE,
	.open = l3g4200d_misc_open,
	.unlocked_ioctl = l3g4200d_misc_ioctl,
};

static struct miscdevice l3g4200d_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = L3G4200D_NAME,
	.fops = &l3g4200d_misc_fops,
};

static void l3g4200d_input_work_func(struct work_struct *work)
{
	struct l3g4200d_data *gyro = container_of((struct delayed_work *)work,
						  struct l3g4200d_data,
						  input_work);
	struct gyro_val data;
	int err;

	err = l3g4200d_get_gyro_data(gyro, &data);
	if (err < 0)
		dev_err(&gyro->client->dev, "get_acceleration_data failed\n");
	else
		l3g4200d_report_values(gyro, &data);

	schedule_delayed_work(&gyro->input_work,
			      msecs_to_jiffies(gyro->pdata->poll_interval));
}

#ifdef L3G4200D_OPEN_ENABLE
int l3g4200d_input_open(struct input_dev *input)
{
	struct l3g4200d_data *gyro = input_get_drvdata(input);

	return l3g4200d_enable(gyro);
}

void l3g4200d_input_close(struct input_dev *dev)
{
	struct l3g4200d_data *gyro = input_get_drvdata(dev);

	l3g4200d_disable(gyro);
}
#endif

static int l3g4200d_validate_pdata(struct l3g4200d_data *gyro)
{
	gyro->pdata->poll_interval = max(gyro->pdata->poll_interval,
					gyro->pdata->min_interval);

	/* Enforce minimum polling interval */
	if (gyro->pdata->poll_interval < gyro->pdata->min_interval) {
		dev_err(&gyro->client->dev, "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}

static int l3g4200d_input_init(struct l3g4200d_data *gyro)
{
	int err;

	INIT_DELAYED_WORK(&gyro->input_work, l3g4200d_input_work_func);

	gyro->input_dev = input_allocate_device();
	if (!gyro->input_dev) {
		err = -ENOMEM;
		dev_err(&gyro->client->dev, "input device allocate failed\n");
		goto err0;
	}

#ifdef L3G4200D_OPEN_ENABLE
	gyro->input_dev->open = l3g4200d_input_open;
	gyro->input_dev->close = l3g4200d_input_close;
#endif

	input_set_drvdata(gyro->input_dev, gyro);

	input_set_capability(gyro->input_dev, EV_REL, REL_RX);
	input_set_capability(gyro->input_dev, EV_REL, REL_RY);
	input_set_capability(gyro->input_dev, EV_REL, REL_RZ);

	gyro->input_dev->name = "gyroscope";

	err = input_register_device(gyro->input_dev);
	if (err) {
		dev_err(&gyro->client->dev,
			"unable to register input polled device %s\n",
			gyro->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(gyro->input_dev);
err0:
	return err;
}

static void l3g4200d_input_cleanup(struct l3g4200d_data *gyro)
{
	input_unregister_device(gyro->input_dev);
	input_free_device(gyro->input_dev);
}

static int l3g4200d_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct l3g4200d_data *gyro;
	int err = -1;

	pr_err("%s:Enter\n", __func__);
	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto err0;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto err0;
	}

	gyro = kzalloc(sizeof(*gyro), GFP_KERNEL);
	if (gyro == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto err0;
	}

	gyro->client = client;

	gyro->pdata = kzalloc(sizeof(*gyro->pdata), GFP_KERNEL);
	if (gyro->pdata == NULL)
		goto err1;

	memcpy(gyro->pdata, client->dev.platform_data, sizeof(*gyro->pdata));

	err = l3g4200d_validate_pdata(gyro);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data\n");
		goto err2;
	}

	gyro->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR_OR_NULL(gyro->regulator)) {
		dev_err(&client->dev, "unable to get regulator\n");
		gyro->regulator = NULL;
	}

	i2c_set_clientdata(client, gyro);

	/* As default, do not report information */
	atomic_set(&gyro->enabled, 0);

	err = l3g4200d_input_init(gyro);
	if (err < 0)
		goto err3;

	l3g4200d_misc_data = gyro;

	err = misc_register(&l3g4200d_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "l3g4200d_device register failed\n");
		goto err4;
	}

	pr_info("%s:Gyro probed\n", __func__);
	return 0;

err4:
	l3g4200d_input_cleanup(gyro);
err3:
	if (gyro->regulator)
		regulator_put(gyro->regulator);
err2:
	kfree(gyro->pdata);
err1:
	kfree(gyro);
err0:
	return err;
}

static int __devexit l3g4200d_remove(struct i2c_client *client)
{
	struct l3g4200d_data *gyro = i2c_get_clientdata(client);

	misc_deregister(&l3g4200d_misc_device);
	l3g4200d_input_cleanup(gyro);
	l3g4200d_disable(gyro);
	if (gyro->regulator)
		regulator_put(gyro->regulator);
	kfree(gyro->pdata);
	kfree(gyro);

	return 0;
}

static int l3g4200d_resume(struct i2c_client *client)
{
	struct l3g4200d_data *gyro = i2c_get_clientdata(client);

	if (gyro->on_before_suspend)
		return l3g4200d_enable(gyro);
	return 0;
}

static int l3g4200d_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct l3g4200d_data *gyro = i2c_get_clientdata(client);

	gyro->on_before_suspend = atomic_read(&gyro->enabled);
	return l3g4200d_disable(gyro);
}

static const struct i2c_device_id l3g4200d_id[] = {
	{L3G4200D_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, l3g4200d_id);

static struct i2c_driver l3g4200d_driver = {
	.driver = {
		   .name = L3G4200D_NAME,
		   },
	.probe = l3g4200d_probe,
	.remove = __devexit_p(l3g4200d_remove),
	.resume = l3g4200d_resume,
	.suspend = l3g4200d_suspend,
	.id_table = l3g4200d_id,
};

static int __init l3g4200d_init(void)
{
	pr_info("L3G4200D gyroscope driver\n");
	return i2c_add_driver(&l3g4200d_driver);
}

static void __exit l3g4200d_exit(void)
{
	i2c_del_driver(&l3g4200d_driver);
	return;
}

module_init(l3g4200d_init);
module_exit(l3g4200d_exit);

MODULE_DESCRIPTION("l3g4200d gyroscope driver");
MODULE_AUTHOR("Dan Murphy D.Murphy@Motorola.com");
MODULE_LICENSE("GPL");
