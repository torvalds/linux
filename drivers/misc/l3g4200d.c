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
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

#include <linux/l3g4200d.h>

#define DEBUG 1

#define L3G4200D_G_2G			0x00
#define L3G4200D_G_4G			0x10
#define L3G4200D_G_8G			0x30

/** Register map */
#define L3G4200D_WHO_AM_I		0x0f
#define L3G4200D_CTRL_REG1		0x20
#define L3G4200D_CTRL_REG2		0x21
#define L3G4200D_CTRL_REG3		0x22
#define L3G4200D_CTRL_REG4		0x23
#define L3G4200D_CTRL_REG5		0x24

#define L3G4200D_REF_DATA_CAP		0x25
#define L3G4200D_STATUS_REG		0x27

#define L3G4200D_OUT_X_L		0x28
#define L3G4200D_OUT_X_H		0x29
#define L3G4200D_OUT_Y_L		0x2a
#define L3G4200D_OUT_Y_H		0x2b
#define L3G4200D_OUT_Z_L		0x2c
#define L3G4200D_OUT_Z_H		0x2d

#define L3G4200D_INTERRUPT_CFG		0x30
#define L3G4200D_INTERRUPT_SRC		0x31
#define L3G4200D_INTERRUPT_THRESH_X_H	0x32
#define L3G4200D_INTERRUPT_THRESH_X_L	0x33
#define L3G4200D_INTERRUPT_THRESH_Y_H	0x34
#define L3G4200D_INTERRUPT_THRESH_Y_L	0x35
#define L3G4200D_INTERRUPT_THRESH_Z_H	0x36
#define L3G4200D_INTERRUPT_THRESH_Z_L	0x37
#define L3G4200D_INTERRUPT_DURATION	0x38

/** Maximum polled-device-reported g value */
#define G_MAX				8000

#define SHIFT_ADJ_2G			4
#define SHIFT_ADJ_4G			3
#define SHIFT_ADJ_8G			2

#define PM_OFF				0x00
#define PM_NORMAL			0x20
#define ENABLE_ALL_AXES			0x07

#define FUZZ				32
#define FLAT				32
#define I2C_RETRY_DELAY			5
#define I2C_RETRIES			5
#define AUTO_INCREMENT			0x80

#define ODRHALF				0x40	/* 0.5Hz output data rate */
#define ODR1				0x60	/* 1Hz output data rate */
#define ODR2				0x80	/* 2Hz output data rate */
#define ODR5				0xA0	/* 5Hz output data rate */
#define ODR10				0xC0	/* 10Hz output data rate */
#define ODR50				0x00	/* 50Hz output data rate */
#define ODR100				0x08	/* 100Hz output data rate */
#define ODR400				0x10	/* 400Hz output data rate */
#define ODR1000				0x18	/* 1000Hz output data rate */

struct l3g4200d_data {
	struct i2c_client *client;
	struct l3g4200d_platform_data *pdata;

	struct delayed_work input_work;
	struct input_dev *input_dev;

	int hw_initialized;
	atomic_t enabled;
	int on_before_suspend;

#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend early_suspend;
#endif

	u8 shift_adj;
	u8 resume_state[5];
};
#ifdef DEBUG
struct l3g4200d_reg {
	const char *name;
	uint8_t reg;
} l3g4200d_regs[] = {
	{ "WHO_AM_I",		L3G4200D_WHO_AM_I },
	{ "CNTRL_1",		L3G4200D_CTRL_REG1 },
	{ "CNTRL_2",		L3G4200D_CTRL_REG2 },
	{ "CNTRL_3",		L3G4200D_CTRL_REG3 },
	{ "CNTRL_4",		L3G4200D_CTRL_REG4 },
	{ "CNTRL_5",		L3G4200D_CTRL_REG5 },
	{ "REF_DATA_CAP",	L3G4200D_REF_DATA_CAP },
	{ "STATUS_REG",		L3G4200D_STATUS_REG },
	{ "INT_CFG",		L3G4200D_INTERRUPT_CFG },
	{ "INT_SRC",		L3G4200D_INTERRUPT_SRC },
	{ "INT_TH_X_H",		L3G4200D_INTERRUPT_THRESH_X_H },
	{ "INT_TH_X_L",		L3G4200D_INTERRUPT_THRESH_X_L },
	{ "INT_TH_Y_H",		L3G4200D_INTERRUPT_THRESH_Y_H },
	{ "INT_TH_Y_L",		L3G4200D_INTERRUPT_THRESH_Y_L },
	{ "INT_TH_Z_H",		L3G4200D_INTERRUPT_THRESH_Z_H },
	{ "INT_TH_Z_L",		L3G4200D_INTERRUPT_THRESH_Z_L },
	{ "INT_DUR",		L3G4200D_INTERRUPT_DURATION },
	{ "OUT_X_H",		L3G4200D_OUT_X_H },
	{ "OUT_X_L",		L3G4200D_OUT_X_L },
	{ "OUT_Y_H",		L3G4200D_OUT_Y_H },
	{ "OUT_Y_L",		L3G4200D_OUT_Y_L },
	{ "OUT_Z_H",		L3G4200D_OUT_Z_H },
	{ "OUT_Z_L",		L3G4200D_OUT_Z_L },
};
#endif
static uint32_t l3g4200d_debug = 0xff;
module_param_named(gyro_debug, l3g4200d_debug, uint, 0664);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void l3g4200d_early_suspend(struct early_suspend *handler);
static void l3g4200d_late_resume(struct early_suspend *handler);
#endif

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
	u8 buf[6];

	buf[0] = (AUTO_INCREMENT | L3G4200D_CTRL_REG1);
	buf[1] = gyro->resume_state[0];
	buf[2] = gyro->resume_state[1];
	buf[3] = gyro->resume_state[2];
	buf[4] = gyro->resume_state[3];
	buf[5] = gyro->resume_state[4];
	err = l3g4200d_i2c_write(gyro, buf, 5);
	if (err < 0)
		return err;

	gyro->hw_initialized = 1;

	return 0;
}

static void l3g4200d_device_power_off(struct l3g4200d_data *gyro)
{
	int err;
	u8 buf[2] = {L3G4200D_CTRL_REG1, PM_OFF};

	err = l3g4200d_i2c_write(gyro, buf, 1);
	if (err < 0)
		dev_err(&gyro->client->dev, "soft power off failed\n");

	if (gyro->pdata->power_off) {
		gyro->pdata->power_off();
		gyro->hw_initialized = 0;
	}
}

static int l3g4200d_device_power_on(struct l3g4200d_data *gyro)
{
	int err;

	if (gyro->pdata->power_on) {
		err = gyro->pdata->power_on();
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

	return 0;
}

static int l3g4200d_get_gyro_data(struct l3g4200d_data *gyro, int *xyz)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 gyro_data[6];
	/* x,y,z hardware data */
	int hw_d[3] = { 0 };

	gyro_data[0] = (AUTO_INCREMENT | L3G4200D_OUT_X_L);
	err = l3g4200d_i2c_read(gyro, gyro_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = (int) (((gyro_data[1]) << 8) | gyro_data[0]);
	hw_d[1] = (int) (((gyro_data[3]) << 8) | gyro_data[2]);
	hw_d[2] = (int) (((gyro_data[5]) << 8) | gyro_data[4]);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= gyro->shift_adj;
	hw_d[1] >>= gyro->shift_adj;
	hw_d[2] >>= gyro->shift_adj;

	xyz[0] = ((gyro->pdata->negate_x) ? (-hw_d[gyro->pdata->axis_map_x])
		  : (hw_d[gyro->pdata->axis_map_x]));
	xyz[1] = ((gyro->pdata->negate_y) ? (-hw_d[gyro->pdata->axis_map_y])
		  : (hw_d[gyro->pdata->axis_map_y]));
	xyz[2] = ((gyro->pdata->negate_z) ? (-hw_d[gyro->pdata->axis_map_z])
		  : (hw_d[gyro->pdata->axis_map_z]));

	return err;
}

static void l3g4200d_report_values(struct l3g4200d_data *gyro, int *xyz)
{
	input_report_abs(gyro->input_dev, ABS_X, xyz[0]);
	input_report_abs(gyro->input_dev, ABS_Y, xyz[1]);
	input_report_abs(gyro->input_dev, ABS_Z, xyz[2]);
	if (l3g4200d_debug)
		pr_info("%s: Reporting x: %d, y: %d, z: %d\n",
		__func__, xyz[0], xyz[1], xyz[2]);
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
			msecs_to_jiffies(gyro->pdata->poll_interval));
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

static int l3g4200d_misc_ioctl(struct inode *inode, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int err = 0;
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
		if (interval < 0 || interval > 200)
			return -EINVAL;

		gyro->pdata->poll_interval =
		    max(interval, gyro->pdata->min_interval);
		/* TODO: if update fails poll is still set */
		if (err < 0)
			return err;

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
	.ioctl = l3g4200d_misc_ioctl,
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
	int xyz[3] = { 0 };
	int err;

	err = l3g4200d_get_gyro_data(gyro, xyz);
	if (err < 0)
		dev_err(&gyro->client->dev, "get_acceleration_data failed\n");
	else
		l3g4200d_report_values(gyro, xyz);

	schedule_delayed_work(&gyro->input_work,
			      msecs_to_jiffies(gyro->pdata->poll_interval));
}
#ifdef DEBUG
static ssize_t l3g4200d_registers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct l3g4200d_data *gyro = i2c_get_clientdata(client);
	u8 l3g4200d_buf[2];
	unsigned i, n, reg_count;

	reg_count = sizeof(l3g4200d_regs) / sizeof(l3g4200d_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		l3g4200d_buf[0] = (AUTO_INCREMENT | l3g4200d_regs[i].reg);
		l3g4200d_i2c_read(gyro, l3g4200d_buf, 1);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n",
			       l3g4200d_regs[i].name, l3g4200d_buf[0]);
	}
	return n;
}

static ssize_t l3g4200d_registers_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct l3g4200d_data *gyro = i2c_get_clientdata(client);
	unsigned i, reg_count, value;
	int error;
	u8 l3g4200d_buf[2];
	char name[30];

	if (count >= 30) {
		pr_err("%s:input too long\n", __func__);
		return -1;
	}

	if (sscanf(buf, "%s %x", name, &value) != 2) {
		pr_err("%s:unable to parse input\n", __func__);
		return -1;
	}

	reg_count = sizeof(l3g4200d_regs) / sizeof(l3g4200d_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, l3g4200d_regs[i].name)) {
			l3g4200d_buf[0] = (AUTO_INCREMENT | l3g4200d_regs[i].reg);
			l3g4200d_buf[1] = value;
			error = l3g4200d_i2c_write(gyro, l3g4200d_buf, 2);
			if (error) {
				pr_err("%s:Failed to write register %s\n",
				       __func__, name);
				return -1;
			}
			return count;
		}
	}
	if (!strcmp("Go", name)) {
		l3g4200d_enable(gyro);
		return 0;
	}
	if (!strcmp("Stop", name)) {
		l3g4200d_disable(gyro);
		return 0;
	}
	pr_err("%s:no such register %s\n", __func__, name);
	return -1;
}
static DEVICE_ATTR(registers, 0644, l3g4200d_registers_show,
		   l3g4200d_registers_store);
#endif
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

	if (gyro->pdata->axis_map_x > 2 ||
	    gyro->pdata->axis_map_y > 2 || gyro->pdata->axis_map_z > 2) {
		dev_err(&gyro->client->dev,
			"invalid axis_map value x:%u y:%u z%u\n",
			gyro->pdata->axis_map_x, gyro->pdata->axis_map_y,
			gyro->pdata->axis_map_z);
		return -EINVAL;
	}

	/* Only allow 0 and 1 for negation boolean flag */
	if (gyro->pdata->negate_x > 1 || gyro->pdata->negate_y > 1 ||
	    gyro->pdata->negate_z > 1) {
		dev_err(&gyro->client->dev,
			"invalid negate value x:%u y:%u z:%u\n",
			gyro->pdata->negate_x, gyro->pdata->negate_y,
			gyro->pdata->negate_z);
		return -EINVAL;
	}

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

	set_bit(EV_ABS, gyro->input_dev->evbit);

	input_set_abs_params(gyro->input_dev, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(gyro->input_dev, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(gyro->input_dev, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);

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

	i2c_set_clientdata(client, gyro);

	memset(gyro->resume_state, 0, ARRAY_SIZE(gyro->resume_state));

	gyro->resume_state[0] = gyro->pdata->ctrl_reg_1;
	gyro->resume_state[1] = gyro->pdata->ctrl_reg_2;
	gyro->resume_state[2] = gyro->pdata->ctrl_reg_3;
	gyro->resume_state[3] = gyro->pdata->ctrl_reg_4;
	gyro->resume_state[4] = gyro->pdata->ctrl_reg_5;

	/* As default, do not report information */
	atomic_set(&gyro->enabled, 0);

#ifdef CONFIG_HAS_EARLYSUSPEND
	gyro->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	gyro->early_suspend.suspend = l3g4200d_early_suspend;
	gyro->early_suspend.resume = l3g4200d_late_resume;
	register_early_suspend(&gyro->early_suspend);
#endif

	err = l3g4200d_input_init(gyro);
	if (err < 0)
		goto err3;

	l3g4200d_misc_data = gyro;

	err = misc_register(&l3g4200d_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "l3g4200d_device register failed\n");
		goto err4;
	}
#ifdef DEBUG
	err = device_create_file(&client->dev, &dev_attr_registers);
	if (err < 0)
		pr_err("%s:File device creation failed: %d\n", __func__, err);
#endif

	pr_err("%s:Gyro probed\n", __func__);
	return 0;

err4:
	l3g4200d_input_cleanup(gyro);
err3:
	if (gyro->pdata->exit)
		gyro->pdata->exit();
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

#ifdef DEBUG
	device_remove_file(&client->dev, &dev_attr_registers);
#endif
	misc_deregister(&l3g4200d_misc_device);
	l3g4200d_input_cleanup(gyro);
	l3g4200d_device_power_off(gyro);
	if (gyro->pdata->exit)
		gyro->pdata->exit();
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

#ifdef CONFIG_HAS_EARLYSUSPEND
static void l3g4200d_early_suspend(struct early_suspend *handler)
{
	struct l3g4200d_data *gyro;

	gyro = container_of(handler, struct l3g4200d_data, early_suspend);
	l3g4200d_suspend(gyro->client, PMSG_SUSPEND);
}

static void l3g4200d_late_resume(struct early_suspend *handler)
{
	struct l3g4200d_data *gyro;

	gyro = container_of(handler, struct l3g4200d_data, early_suspend);
	l3g4200d_resume(gyro->client);
}
#endif

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
#ifndef CONFIG_HAS_EARLYSUSPEND
	.resume = l3g4200d_resume,
	.suspend = l3g4200d_suspend,
#endif
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
