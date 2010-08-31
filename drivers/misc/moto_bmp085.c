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
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/regulator/consumer.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>

#include <linux/moto_bmp085.h>

#define DEBUG

#define NO_CYCLE 0
#define TEMP_CYCLE 1
#define PRESSURE_CYCLE 2

/* Register definitions */
#define BMP085_TAKE_MEAS_REG		0xf4
#define BMP085_READ_MEAS_REG_U		0xf6
#define BMP085_READ_MEAS_REG_L		0xf7
#define BMP085_READ_MEAS_REG_XL		0xf8

/* Bytes defined by the spec to take measurements
Temperature will take 4.5ms before EOC */
#define BMP085_MEAS_TEMP	0x2e
/* 4.5ms wait for measurement */
#define BMP085_MEAS_PRESS_OVERSAMP_0	0x34
/* 7.5ms wait for measurement */
#define BMP085_MEAS_PRESS_OVERSAMP_1	0x74
/* 13.5ms wait for measurement */
#define BMP085_MEAS_PRESS_OVERSAMP_2	0xb4
/* 25.5ms wait for measurement */
#define BMP085_MEAS_PRESS_OVERSAMP_3	0xf4


/* EEPROM registers each is a two byte value so there is
an upper byte and a lower byte */
#define BMP085_EEPROM_AC1_U	0xaa
#define BMP085_EEPROM_AC1_L	0xab
#define BMP085_EEPROM_AC2_U	0xac
#define BMP085_EEPROM_AC2_L	0xad
#define BMP085_EEPROM_AC3_U	0xae
#define BMP085_EEPROM_AC3_L	0xaf
#define BMP085_EEPROM_AC4_U	0xb0
#define BMP085_EEPROM_AC4_L	0xb1
#define BMP085_EEPROM_AC5_U	0xb2
#define BMP085_EEPROM_AC5_L	0xb3
#define BMP085_EEPROM_AC6_U	0xb4
#define BMP085_EEPROM_AC6_L	0xb5
#define BMP085_EEPROM_B1_U	0xb6
#define BMP085_EEPROM_B1_L	0xb7
#define BMP085_EEPROM_B2_U	0xb8
#define BMP085_EEPROM_B2_L	0xb9
#define BMP085_EEPROM_MB_U	0xba
#define BMP085_EEPROM_MB_L	0xbb
#define BMP085_EEPROM_MC_U	0xbc
#define BMP085_EEPROM_MC_L	0xbd
#define BMP085_EEPROM_MD_U	0xbe
#define BMP085_EEPROM_MD_L	0xbf

#ifdef DEBUG
struct bmp085_reg {
	const char *name;
	uint8_t reg;
} bmp085_regs[] = {
	{"MEASURE_REG", BMP085_TAKE_MEAS_REG},
	{"CNTRL_1", BMP085_READ_MEAS_REG_U},
	{"CNTRL_2", BMP085_READ_MEAS_REG_L},
	{"CNTRL_3", BMP085_READ_MEAS_REG_XL},
	{"EE_AC1_U", BMP085_EEPROM_AC1_U},
	{"EE_AC1_U", BMP085_EEPROM_AC1_L},
	{"EE_AC2_U", BMP085_EEPROM_AC2_U},
	{"EE_AC2_L", BMP085_EEPROM_AC2_L},
	{"EE_AC3_U", BMP085_EEPROM_AC3_U},
	{"EE_AC3_L", BMP085_EEPROM_AC3_L},
	{"EE_AC4_U", BMP085_EEPROM_AC4_U},
	{"EE_AC4_L", BMP085_EEPROM_AC4_L},
	{"EE_AC5_U", BMP085_EEPROM_AC5_U},
	{"EE_AC5_L", BMP085_EEPROM_AC5_L},
	{"EE_AC6_U", BMP085_EEPROM_AC6_U},
	{"EE_AC6_L", BMP085_EEPROM_AC6_L},
	{"EE_B1_U", BMP085_EEPROM_B1_U},
	{"EE_B1_L", BMP085_EEPROM_B1_L},
	{"EE_B2_U", BMP085_EEPROM_B2_U},
	{"EE_B2_L", BMP085_EEPROM_B2_L},
	{"EE_MB_U", BMP085_EEPROM_MB_U},
	{"EE_MB_L", BMP085_EEPROM_MB_L},
	{"EE_MC_U", BMP085_EEPROM_MC_U},
	{"EE_MC_L", BMP085_EEPROM_MC_L},
	{"EE_MD_U", BMP085_EEPROM_MD_U},
	{"EE_MD_L", BMP085_EEPROM_MD_L},
};
#endif
static uint32_t bmp085_debug = 0x01;
module_param_named(baro_debug, bmp085_debug, uint, 0664);

#define I2C_RETRY_DELAY		5
#define I2C_RETRIES		5
#define AUTO_INCREMENT		0x80

static struct workqueue_struct *barom_wq;

struct bmp085_eeprom_data {
	s16 AC1, AC2, AC3;
	u16 AC4, AC5, AC6;
	s16 B1, B2;
	s16 MB, MC, MD;
};

struct bmp085_data {
	struct i2c_client *client;
	struct bmp085_platform_data *pdata;
	struct mutex lock;
	struct delayed_work input_work;
	struct work_struct wq;
	struct workqueue_struct *working_queue;
	struct input_dev *input_dev;

	u8 oversampling_rate;
	u8 measurement_cycle;

	int uncalib_temperature;
	int uncalib_pressure;
	int calib_temperature;
	long calib_pressure;
	long b5;		/* Needed for pressure calculation */

	struct bmp085_eeprom_data bmp085_eeprom_vals;

	atomic_t enabled;
	int on_before_suspend;
	struct regulator *regulator;
	u8 resume_state[5];
};

/*
 * Because misc devices can not carry a pointer from driver register to
 * open, we keep this global.  This limits the driver to a single instance.
 */
struct bmp085_data *bmp085_misc_data;

static int bmp085_i2c_read(struct bmp085_data *barom, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = barom->client->addr,
		 .flags = barom->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = buf,
		 },
		{
		 .addr = barom->client->addr,
		 .flags = (barom->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(barom->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		pr_err("%s:read transfer error\n", __func__);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int bmp085_i2c_write(struct bmp085_data *barom, u8 * buf, int len)
{
	int err;
	int tries = 0;
	struct i2c_msg msgs[] = {
		{
		 .addr = barom->client->addr,
		 .flags = barom->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	do {
		err = i2c_transfer(barom->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		pr_err("%s:write transfer error\n", __func__);
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int bmp085_update_measurement_accuracy(struct bmp085_data *barom,
				       int accuracy)
{
	if (accuracy > 3)
		accuracy = 3;
	barom->oversampling_rate = accuracy;

	return 0;
}

static void bmp085_schedule_work(struct bmp085_data *barom)
{
	schedule_delayed_work(&barom->input_work,
			      msecs_to_jiffies(barom->pdata->poll_interval));
}

static int bmp085_enable(struct bmp085_data *barom)
{
	int err = 0;

	if (!atomic_cmpxchg(&barom->enabled, 0, 1)) {
		if (barom->regulator)
			err = regulator_enable(barom->regulator);
		if (err < 0) {
			atomic_set(&barom->enabled, 0);
			return err;
		}
		schedule_delayed_work(&barom->input_work,
				      msecs_to_jiffies(barom->pdata->
						       poll_interval));
	}

	return 0;
}

static int bmp085_disable(struct bmp085_data *barom)
{
	if (atomic_cmpxchg(&barom->enabled, 1, 0)) {
		cancel_delayed_work_sync(&barom->input_work);
		if (barom->regulator)
			regulator_disable(barom->regulator);
	}
	barom->measurement_cycle = NO_CYCLE;

	return 0;
}

static int bmp085_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = bmp085_misc_data;

	return 0;
}

static long bmp085_misc_ioctl(struct file *file,
			      unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u8 buf[4];
	int err;
	int interval;
	struct bmp085_data *barom = file->private_data;

	switch (cmd) {
	case BMP085_IOCTL_GET_DELAY:
		interval = barom->pdata->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;

	case BMP085_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 200)
			return -EINVAL;

		barom->pdata->poll_interval =
		    max(interval, barom->pdata->min_interval);
		break;

	case BMP085_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;

		if (interval)
			bmp085_enable(barom);
		else
			bmp085_disable(barom);

		break;

	case BMP085_IOCTL_GET_ENABLE:
		interval = atomic_read(&barom->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;

		break;

	case BMP085_IOCTL_ACCURACY:
		if (copy_from_user(&buf, argp, 1))
			return -EFAULT;
		err = bmp085_update_measurement_accuracy(barom, arg);
		if (err < 0)
			return err;

		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations bmp085_misc_fops = {
	.owner = THIS_MODULE,
	.open = bmp085_misc_open,
	.unlocked_ioctl = bmp085_misc_ioctl,
};

static struct miscdevice bmp085_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = BMP085_NAME,
	.fops = &bmp085_misc_fops,
};

#ifdef BMP085_OPEN_ENABLE
int bmp085_input_open(struct input_dev *input)
{
	struct bmp085_data *barom = input_get_drvdata(input);

	return bmp085_enable(barom);
}

void bmp085_input_close(struct input_dev *dev)
{
	struct bmp085_data *barom = input_get_drvdata(dev);

	bmp085_disable(barom);
}
#endif
#ifdef DEBUG
static ssize_t bmp085_registers_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct bmp085_data *barom_data = i2c_get_clientdata(client);
	u8 barom_reg[2];
	unsigned i, n, reg_count;

	reg_count = sizeof(bmp085_regs) / sizeof(bmp085_regs[0]);
	for (i = 0, n = 0; i < reg_count; i++) {
		barom_reg[0] = (AUTO_INCREMENT | bmp085_regs[i].reg);
		bmp085_i2c_read(barom_data, barom_reg, 1);
		n += scnprintf(buf + n, PAGE_SIZE - n,
			       "%-20s = 0x%02X\n",
			       bmp085_regs[i].name, barom_reg[0]);
	}
	return n;
}

static ssize_t bmp085_registers_store(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct i2c_client *client = container_of(dev, struct i2c_client,
						 dev);
	struct bmp085_data *barom_data = i2c_get_clientdata(client);
	unsigned i, reg_count, value;
	int error;
	u8 barom_reg[2];
	char name[30];

	if (count >= 30) {
		pr_err("%s:input too long\n", __func__);
		return -1;
	}

	if (sscanf(buf, "%s %x", name, &value) != 2) {
		pr_err("%s:unable to parse input\n", __func__);
		return -1;
	}

	reg_count = sizeof(bmp085_regs) / sizeof(bmp085_regs[0]);
	for (i = 0; i < reg_count; i++) {
		if (!strcmp(name, bmp085_regs[i].name)) {
			barom_reg[0] = (AUTO_INCREMENT | bmp085_regs[i].reg);
			barom_reg[1] = value;
			error = bmp085_i2c_write(barom_data, barom_reg, 2);
			if (error) {
				pr_err("%s:Failed to write register %s\n",
				       __func__, name);
				return -1;
			}
			return count;
		}
	}
	if (!strcmp("Go", name)) {
		if (value > 0)
			bmp085_enable(barom_data);
		else
			bmp085_disable(barom_data);

		return 0;
	}
	if (!strcmp("acc", name)) {
		barom_data->oversampling_rate = value;
		return 0;
	}

	pr_err("%s:no such register %s\n", __func__, name);
	return -1;
}

static DEVICE_ATTR(registers, 0644, bmp085_registers_show,
		   bmp085_registers_store);
#endif
static int bmp085_get_temperature_data(struct bmp085_data *barom)
{
	int err = -1;
	u8 buf[2] = { BMP085_READ_MEAS_REG_U, 0 };
	int x1;
	unsigned int x2;

	err = bmp085_i2c_read(barom, buf, 2);
	if (err) {
		pr_err("%s:Cannot read pressure measurement\n", __func__);
		return err;
	}
	if (bmp085_debug & 2)
		pr_err("%s:Read Temp 0x%X 0x%X\n", __func__, buf[0], buf[1]);

	barom->uncalib_temperature = (buf[0] << 8) + buf[1];

	/* The math is derived from the data sheet. */
	x1 = ((barom->uncalib_temperature - barom->bmp085_eeprom_vals.AC6) *
	      barom->bmp085_eeprom_vals.AC5) >> 15;
	x2 = (barom->bmp085_eeprom_vals.MC << 11) /
	    (x1 + barom->bmp085_eeprom_vals.MD);
	barom->b5 = x1 + x2;
	barom->calib_temperature = (barom->b5 + 8) >> 4;
	if (bmp085_debug & 1)
		pr_err("%s:Calibrated Temp %d\n",
		__func__, barom->calib_temperature);

	return err;
}

static int bmp085_get_barometer_data(struct bmp085_data *barom)
{
	int err = -1;
	long x1, x2, x3, b3, b6;
	unsigned long b4, b7;
	long p;
	u8 buf[3] = { BMP085_READ_MEAS_REG_U, 0, 0 };

	err = bmp085_i2c_read(barom, buf, 3);
	if (err) {
		pr_err("%s:Cannot read pressure measurement\n", __func__);
		return err;
	}

	/* Raw data to uncalibrate pressure.  Conversion compliments of the
	data sheet */
	barom->uncalib_pressure = ((buf[0] << 16) | (buf[1] << 8) | buf[2]) >>
		(8 - barom->oversampling_rate);
	if (bmp085_debug & 2)
		pr_err("%s:Uncalibrated pressure %d\n", __func__,
		       barom->uncalib_pressure);

	/* Complicated math compliments of the data sheet */
	b6 = (barom->b5 - 4000);
	x1 = (barom->bmp085_eeprom_vals.B2 * ((b6 * b6) >> 12)) >> 11;
	x2 = (barom->bmp085_eeprom_vals.AC2 * b6) >> 11;
	x3 = x1 + x2;
	b3 = (((((long)barom->bmp085_eeprom_vals.AC1) * 4 +
		x3) << barom->oversampling_rate) + 2) >> 2;
	x1 = (barom->bmp085_eeprom_vals.AC3 * b6) >> 13;
	x2 = (barom->bmp085_eeprom_vals.B1 * (b6 * b6 >> 12)) >> 16;
	x3 = ((x1 + x2) + 2) >> 2;
	b4 = (barom->bmp085_eeprom_vals.AC4 *
	      (unsigned long)(x3 + 32768)) >> 15;
	b7 = ((unsigned long)barom->uncalib_pressure -
	      b3) * (50000 >> barom->oversampling_rate);
	if (b7 < 0x80000000)
		p = (b7 * 2) / b4;
	else
		p = (b7 / b4) * 2;
	x1 = (p >> 8) * (p >> 8);
	x1 = (x1 * 3038) >> 16;
	x2 = (-7357 * p) >> 16;
	barom->calib_pressure = p + ((x1 + x2 + 3791) >> 4);
	if (bmp085_debug & 1)
		pr_info("%s:Calibrated Pressure is %li\n",
		__func__, barom->calib_pressure);

	return err;
}

static void bmp085_input_work_func(struct work_struct *work)
{
	struct bmp085_data *barom = container_of((struct delayed_work *)work,
						 struct bmp085_data,
						 input_work);
	int err;
	u8 buf[2];

	buf[0] = (AUTO_INCREMENT | BMP085_TAKE_MEAS_REG);
	buf[1] = BMP085_MEAS_TEMP;

	if ((barom->measurement_cycle == TEMP_CYCLE) ||
	    (barom->measurement_cycle == PRESSURE_CYCLE)) {
		/* One of the measurements took to long so
		reset the state machine */
		barom->measurement_cycle = NO_CYCLE;
	} else {
		barom->measurement_cycle = TEMP_CYCLE;
		err = bmp085_i2c_write(barom, buf, 2);
		if (err) {
			pr_err("%s:Cannot start temp measurement\n", __func__);
			barom->measurement_cycle = NO_CYCLE;
			return;
		}
	}
	bmp085_schedule_work(barom);
	return;
}

void bmp085_work_queue(struct work_struct *work)
{
	int err = 0;
	struct bmp085_data *barom_data =
	    container_of(work, struct bmp085_data, wq);
	u8 buf[2];

	if (barom_data->measurement_cycle == NO_CYCLE) {
		pr_err("%s:No cycle defined\n", __func__);
	} else if (barom_data->measurement_cycle == TEMP_CYCLE) {

		if (bmp085_debug & 1)
			pr_err("%s:Temp cycle\n", __func__);

		err = bmp085_get_temperature_data(barom_data);
		if (err) {
			pr_err("%s:Cannot read temp measurement\n", __func__);
			return;
		}
		/* Setup for a pressure measurement */
		buf[0] = (AUTO_INCREMENT | BMP085_TAKE_MEAS_REG);
		buf[1] = BMP085_MEAS_PRESS_OVERSAMP_0 |
			(barom_data->oversampling_rate << 6);

		barom_data->measurement_cycle = PRESSURE_CYCLE;

		err = bmp085_i2c_write(barom_data, buf, 2);
		if (err) {
			pr_err("%s:Cannot start temp measurement\n", __func__);
			barom_data->measurement_cycle = NO_CYCLE;
			return;
		}
	} else {
		/* Get and report the pressure */
		if (bmp085_debug & 1)
			pr_err("%s:Pressure cycle\n", __func__);

		err = bmp085_get_barometer_data(barom_data);
		if (err) {
			pr_err("%s:Pressure measurement failed\n", __func__);
			return;
		}

		input_report_abs(barom_data->input_dev, ABS_PRESSURE,
				 barom_data->calib_pressure);
		input_sync(barom_data->input_dev);

		barom_data->measurement_cycle = NO_CYCLE;
	}
	enable_irq(barom_data->client->irq);
	return;
}

irqreturn_t bmp085_irq_handler(int irq, void *dev)
{
	struct bmp085_data *barom_data = dev;
	disable_irq_nosync(barom_data->client->irq);
	queue_work(barom_wq, &barom_data->wq);

	return IRQ_HANDLED;
}

static int bmp085_validate_pdata(struct bmp085_data *barom)
{
	barom->pdata->poll_interval = max(barom->pdata->poll_interval,
					  barom->pdata->min_interval);

	/* Enforce minimum polling interval */
	if (barom->pdata->poll_interval < barom->pdata->min_interval) {
		pr_err("%s:minimum poll interval violated\n", __func__);
		return -EINVAL;
	}

	return 0;
}

static int bmp085_input_init(struct bmp085_data *barom)
{
	int err;

	INIT_DELAYED_WORK(&barom->input_work, bmp085_input_work_func);

	barom->input_dev = input_allocate_device();
	if (!barom->input_dev) {
		err = -ENOMEM;
		dev_err(&barom->client->dev, "input device allocate failed\n");
		goto err0;
	}
#ifdef BMP085_OPEN_ENABLE
	barom->input_dev->open = bmp085_input_open;
	barom->input_dev->close = bmp085_input_close;
#endif

	input_set_drvdata(barom->input_dev, barom);

	set_bit(EV_ABS, barom->input_dev->evbit);

	/* Need to define the correct min and max */
	input_set_abs_params(barom->input_dev, ABS_PRESSURE,
				barom->pdata->min_p, barom->pdata->max_p,
				barom->pdata->fuzz, barom->pdata->flat);

	barom->input_dev->name = "barometer";

	err = input_register_device(barom->input_dev);
	if (err) {
		dev_err(&barom->client->dev,
			"unable to register input polled device %s\n",
			barom->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(barom->input_dev);
err0:
	return err;
}

static void bmp085_input_cleanup(struct bmp085_data *barom)
{
	input_unregister_device(barom->input_dev);
	input_free_device(barom->input_dev);
}

static int bmp085_read_store_eeprom_val(struct bmp085_data *barom)
{
	int err = 0;
	u8 buf[22];

	buf[0] = BMP085_EEPROM_AC1_U;
	err = bmp085_i2c_read(barom, buf, 22);
	if (err) {
		pr_err("%s:Cannot read EEPROM values\n", __func__);
		return err;
	}

	barom->bmp085_eeprom_vals.AC1 = (buf[0] << 8) | buf[1];
	barom->bmp085_eeprom_vals.AC2 = (buf[2] << 8) | buf[3];
	barom->bmp085_eeprom_vals.AC3 = (buf[4] << 8) | buf[5];
	barom->bmp085_eeprom_vals.AC4 = (buf[6] << 8) | buf[7];
	barom->bmp085_eeprom_vals.AC5 = (buf[8] << 8) | buf[9];
	barom->bmp085_eeprom_vals.AC6 = (buf[10] << 8) | buf[11];
	barom->bmp085_eeprom_vals.B1 = (buf[12] << 8) | buf[13];
	barom->bmp085_eeprom_vals.B2 = (buf[14] << 8) | buf[15];
	barom->bmp085_eeprom_vals.MB = (buf[16] << 8) | buf[17];
	barom->bmp085_eeprom_vals.MC = (buf[18] << 8) | buf[19];
	barom->bmp085_eeprom_vals.MD = (buf[20] << 8) | buf[21];

	return 0;
}

static int bmp085_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct bmp085_data *barom;
	int err = -1;

	if (client->dev.platform_data == NULL) {
		pr_err("%s:platform data is NULL. exiting.\n", __func__);
		err = -ENODEV;
		goto err0;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_err("%s:client not i2c capable\n", __func__);
		err = -ENODEV;
		goto err0;
	}

	barom = kzalloc(sizeof(*barom), GFP_KERNEL);
	if (barom == NULL) {
		pr_err("%s:failed to allocate memory for module data\n",
		       __func__);
		err = -ENOMEM;
		goto err0;
	}
	mutex_init(&barom->lock);
	mutex_lock(&barom->lock);
	barom->client = client;
	barom->oversampling_rate = 0;
	barom->b5 = 0;

	barom->pdata = kzalloc(sizeof(*barom->pdata), GFP_KERNEL);
	if (barom->pdata == NULL)
		goto err1;

	memcpy(barom->pdata, client->dev.platform_data, sizeof(*barom->pdata));

	err = bmp085_validate_pdata(barom);
	if (err < 0) {
		pr_err("%s:failed to validate platform data\n", __func__);
		goto err1_1;
	}

	i2c_set_clientdata(client, barom);

	err = bmp085_read_store_eeprom_val(barom);
	if (err) {
		pr_err("%s: Reading the EEPROM failed\n", __func__);
		err = -ENODEV;
		goto err_req_irq_failed;
	}

	INIT_WORK(&barom->wq, bmp085_work_queue);

	err = request_irq(barom->client->irq, bmp085_irq_handler,
			  IRQF_TRIGGER_RISING, BMP085_NAME, barom);
	if (err != 0) {
		pr_err("%s: irq request failed: %d\n", __func__, err);
		err = -ENODEV;
		goto err_req_irq_failed;
	}

	barom->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR_OR_NULL(barom->regulator)) {
		dev_err(&client->dev, "unable to get regulator\n");
		barom->regulator = NULL;
	}

	err = bmp085_input_init(barom);
	if (err < 0)
		goto err3;

	bmp085_misc_data = barom;

	err = misc_register(&bmp085_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "barom_device register failed\n");
		goto err4;
	}
#ifdef DEBUG
	err = device_create_file(&client->dev, &dev_attr_registers);
	if (err < 0)
		pr_err("%s:File device creation failed: %d\n", __func__, err);
#endif

	/* As default, do not report information */
	atomic_set(&barom->enabled, 0);

	mutex_unlock(&barom->lock);

	return 0;

err4:
	bmp085_input_cleanup(barom);
err3:
	if (barom->regulator)
		regulator_put(barom->regulator);
err_req_irq_failed:
err1_1:
	mutex_unlock(&barom->lock);
	kfree(barom->pdata);
err1:
	kfree(barom);
err0:
	return err;
}

static int __devexit bmp085_remove(struct i2c_client *client)
{
	/* TO DO: revisit ordering here once _probe order is finalized */
	struct bmp085_data *barom = i2c_get_clientdata(client);

	misc_deregister(&bmp085_misc_device);
	bmp085_input_cleanup(barom);
	bmp085_disable(barom);
	if (barom->regulator)
		regulator_put(barom->regulator);
#ifdef DEBUG
	device_remove_file(&client->dev, &dev_attr_registers);
#endif
	destroy_workqueue(barom_wq);

	kfree(barom->pdata);
	kfree(barom);

	return 0;
}

static int bmp085_resume(struct i2c_client *client)
{
	struct bmp085_data *barom = i2c_get_clientdata(client);

	if (barom->on_before_suspend)
		return bmp085_enable(barom);
	return 0;
}

static int bmp085_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct bmp085_data *barom = i2c_get_clientdata(client);

	barom->on_before_suspend = atomic_read(&barom->enabled);
	return bmp085_disable(barom);
}

static const struct i2c_device_id bmp085_id[] = {
	{BMP085_NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, bmp085_id);

static struct i2c_driver bmp085_driver = {
	.driver = {
		   .name = BMP085_NAME,
		   },
	.probe = bmp085_probe,
	.remove = __devexit_p(bmp085_remove),
	.resume = bmp085_resume,
	.suspend = bmp085_suspend,
	.id_table = bmp085_id,
};

static int __init bmp085_init(void)
{
	barom_wq = create_singlethread_workqueue("barometer_wq");
	if (!barom_wq) {
		pr_err("%s: Cannot create work queue\n", __func__);
		return -ENOMEM;
	}
	pr_info("BMP085 barometer driver\n");
	return i2c_add_driver(&bmp085_driver);
}

static void __exit bmp085_exit(void)
{
	i2c_del_driver(&bmp085_driver);
	return;
}

module_init(bmp085_init);
module_exit(bmp085_exit);

MODULE_DESCRIPTION("bmp085 barometer driver");
MODULE_AUTHOR("Dan Murphy D.Murphy@Motorola.com");
MODULE_LICENSE("GPL");
