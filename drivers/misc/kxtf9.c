/*
 * Copyright (C) 2009 Kionix, Inc.
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

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/input-polldev.h>
#include <linux/irq.h>
#include <linux/miscdevice.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>

#include <linux/kxtf9.h>

#define NAME			"kxtf9"
#define G_MAX			8000
#define SHIFT_ADJ_2G		4
#define SHIFT_ADJ_4G		3
#define SHIFT_ADJ_8G		2
/* OUTPUT REGISTERS */
#define XOUT_L			0x06
#define XOUT_H			0x07
#define YOUT_L			0x08
#define YOUT_H			0x09
#define ZOUT_L			0x0A
#define ZOUT_H			0x0B
/* TILT POSITION REGISTERS */
#define TILT_POS_CUR		0x10
#define TILT_POS_PRE		0x11
/* INT SRC REGISTERS */
#define INT_SRC_REG1		0x15
#define INT_SRC_REG2		0x16
#define STATUS_REG		0x18
#define INT_REL			0x1A
/* CONTROL REGISTERS */
#define CTRL_REG1		0x1B
#define CTRL_REG2		0x1C
#define CTRL_REG3		0x1D
#define INT_CTRL1		0x1E
#define INT_CTRL2		0x1F
#define INT_CTRL3		0x20
#define DATA_CTRL		0x21
#define TILT_TIMER		0x28
#define WUF_TIMER		0x29
#define TDT_TIMER		0x2B
#define TDT_H_THRESH		0x2C
#define TDT_L_THRESH		0x2D
#define TDT_TAP_TIMER		0x2E
#define TDT_TOTAL_TIMER		0x2F
#define TDT_LAT_TIMER		0x30
#define TDT_WIND_TIMER		0x31
#define SELF_TEST_REG		0x3A
#define WUF_THRESH		0x5A
#define TILT_ANGLE		0x5C
#define HYST_SET		0x5F
/* CONTROL REGISTER 1 BITS */
#define PC1_OFF			0x00
#define PC1_ON			0x80
/* INTERRUPT SOURCE 2 BITS */
#define TPS			0x01
#define TDTS0			0x04
#define TDTS1			0x08
/* INPUT_ABS CONSTANTS */
#define FUZZ			32
#define FLAT			32
/* RESUME STATE INDICES */
#define RES_DATA_CTRL		0
#define RES_CTRL_REG1		1
#define RES_INT_CTRL1		2
#define RES_TILT_TIMER		3
#define RES_CTRL_REG3		4
#define RES_WUF_TIMER		5
#define RES_WUF_THRESH		6
#define RES_TDT_TIMER		7
#define RES_TDT_H_THRESH	8
#define RES_TDT_L_THRESH	9
#define RES_TAP_TIMER		10
#define RES_TOTAL_TIMER		11
#define RES_LAT_TIMER		12
#define RES_WIN_TIMER		13
#define RES_INT_CTRL3		14
#define RESUME_ENTRIES		15

#define SENSITIVITY_LEVELS           3
#define SENSITIVITY_LOW_OFFSET       0
#define SENSITIVITY_MEDIUM_OFFSET    1
#define SENSITIVITY_HIGH_OFFSET      2

#define I2C_RETRY_DELAY 	5
#define I2C_RETRIES		5
#define AUTO_INCREMENT		0x80

static uint32_t kxtf9_dbg;
module_param(kxtf9_dbg, uint, 0664);

struct {
	unsigned int cutoff;
	u8 mask;
} kxtf9_odr_table[] = {
	{3, ODR800},
	{5, ODR400},
	{10, ODR200},
	{20, ODR100},
	{40, ODR50},
	{80, ODR25},
};

struct tap_sensitivity {
	u8 reg_timer_init;
	u8 reg_h_thresh_init;
	u8 reg_l_thresh_init;
	u8 reg_tap_timer_init;
	u8 reg_total_timer_init;
	u8 reg_latency_timer_init;
	u8 reg_window_timer_init;
};

struct kxtf9_data {
	struct i2c_client *client;
	struct kxtf9_platform_data *pdata;
	struct mutex lock;
	struct delayed_work input_work;
	struct input_dev *input_dev;
	struct work_struct irq_work;
	struct workqueue_struct *irq_work_queue;

	int hw_initialized;
	atomic_t enabled;
	int enabled_b4_suspend;
	u8 shift_adj;
	u8 resume_state[RESUME_ENTRIES];
	int irq;
	struct tap_sensitivity ts_regs[SENSITIVITY_LEVELS];
	struct regulator *regulator;
};

struct kxtf9_data *kxtf9_misc_data;

static int kxtf9_i2c_read(struct kxtf9_data *tf9, u8 *buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = tf9->client->addr,
		 .flags = tf9->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = buf,
		 },
		{
		 .addr = tf9->client->addr,
		 .flags = (tf9->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = buf,
		 },
	};
	do {
		err = i2c_transfer(tf9->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&tf9->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int kxtf9_i2c_write(struct kxtf9_data *tf9, u8 * buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = {
		{
		 .addr = tf9->client->addr,
		 .flags = tf9->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};
	do {
		err = i2c_transfer(tf9->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&tf9->client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int kxtf9_hw_init(struct kxtf9_data *tf9)
{
	int err = -1;
	u8 buf[8];

	buf[0] = CTRL_REG1;
	buf[1] = PC1_OFF;
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = DATA_CTRL;
	buf[1] = tf9->resume_state[RES_DATA_CTRL];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = CTRL_REG3;
	buf[1] = tf9->resume_state[RES_CTRL_REG3];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = TILT_TIMER;
	buf[1] = tf9->resume_state[RES_TILT_TIMER];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = WUF_TIMER;
	buf[1] = tf9->resume_state[RES_WUF_TIMER];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = WUF_THRESH;
	buf[1] = tf9->resume_state[RES_WUF_THRESH];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = TDT_TIMER;
	buf[1] = tf9->resume_state[RES_TDT_TIMER];
	buf[2] = tf9->resume_state[RES_TDT_H_THRESH];
	buf[3] = tf9->resume_state[RES_TDT_L_THRESH];
	buf[4] = tf9->resume_state[RES_TAP_TIMER];
	buf[5] = tf9->resume_state[RES_TOTAL_TIMER];
	buf[6] = tf9->resume_state[RES_LAT_TIMER];
	buf[7] = tf9->resume_state[RES_WIN_TIMER];
	err = kxtf9_i2c_write(tf9, buf, 7);
	if (err < 0)
		goto error;
	buf[0] = INT_CTRL1;
	buf[1] = tf9->resume_state[RES_INT_CTRL1];
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	buf[0] = CTRL_REG1;
	buf[1] = (tf9->resume_state[RES_CTRL_REG1] | PC1_ON);
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;
	tf9->resume_state[RES_CTRL_REG1] = buf[1];
	tf9->hw_initialized = 1;

	return 0;
error:
	dev_err(&tf9->client->dev, "hw init error 0x%x,0x%x: %d\n",
		buf[0], buf[1], err);

	return err;
}

static void kxtf9_device_power_off(struct kxtf9_data *tf9)
{
	int err;
	u8 buf[2] = { CTRL_REG1, PC1_OFF };

	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		dev_err(&tf9->client->dev, "soft power off failed: %d\n", err);
	if (tf9->regulator) {
		disable_irq_nosync(tf9->irq);
		regulator_disable(tf9->regulator);
		tf9->hw_initialized = 0;
	}
}

static int kxtf9_device_power_on(struct kxtf9_data *tf9)
{
	int err;

	if (tf9->regulator) {
		err = regulator_enable(tf9->regulator);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"power_on failed: %d\n", err);
			return err;
		}

		if (atomic_read(&tf9->enabled))
			enable_irq(tf9->irq);
	}
	if (!tf9->hw_initialized) {
		mdelay(100);
		err = kxtf9_hw_init(tf9);
		if (err < 0) {
			kxtf9_device_power_off(tf9);
			return err;
		}
	}

	return 0;
}

static irqreturn_t kxtf9_isr(int irq, void *dev)
{
	struct kxtf9_data *tf9 = dev;

	if (atomic_read(&tf9->enabled)) {
		disable_irq_nosync(irq);
		queue_work(tf9->irq_work_queue, &tf9->irq_work);
	}

	return IRQ_HANDLED;
}

static u8 kxtf9_resolve_dir(struct kxtf9_data *tf9, u8 dir)
{
	switch (dir) {
	case 0x20:	/* -X */
		if (tf9->pdata->negate_x)
			dir = 0x10;
		if (tf9->pdata->axis_map_y == 0)
			dir >>= 2;
		if (tf9->pdata->axis_map_z == 0)
			dir >>= 4;
		break;
	case 0x10:	/* +X */
		if (tf9->pdata->negate_x)
			dir = 0x20;
		if (tf9->pdata->axis_map_y == 0)
			dir >>= 2;
		if (tf9->pdata->axis_map_z == 0)
			dir >>= 4;
		break;
	case 0x08:	/* -Y */
		if (tf9->pdata->negate_y)
			dir = 0x04;
		if (tf9->pdata->axis_map_x == 1)
			dir <<= 2;
		if (tf9->pdata->axis_map_z == 1)
			dir >>= 2;
		break;
	case 0x04:	/* +Y */
		if (tf9->pdata->negate_y)
			dir = 0x08;
		if (tf9->pdata->axis_map_x == 1)
			dir <<= 2;
		if (tf9->pdata->axis_map_z == 1)
			dir >>= 2;
		break;
	case 0x02:	/* -Z */
		if (tf9->pdata->negate_z)
			dir = 0x01;
		if (tf9->pdata->axis_map_x == 2)
			dir <<= 4;
		if (tf9->pdata->axis_map_y == 2)
			dir <<= 2;
		break;
	case 0x01:	/* +Z */
		if (tf9->pdata->negate_z)
			dir = 0x02;
		if (tf9->pdata->axis_map_x == 2)
			dir <<= 4;
		if (tf9->pdata->axis_map_y == 2)
			dir <<= 2;
		break;
	default:
		dev_err(&tf9->client->dev,
			"invalid resolve dir: %u\n", dir);
		return 0;
		break;
	}

	return dir;
}

static void kxtf9_irq_work_func(struct work_struct *work)
{
	int err;
	unsigned int int_status = 0;
	u8 status;
	u8 buf[2];

	struct kxtf9_data *tf9 = container_of(work,
						struct kxtf9_data, irq_work);
	if (tf9->pdata->gpio()) {
		status = INT_SRC_REG2;
		err = kxtf9_i2c_read(tf9, &status, 1);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"int source read error: %d\n", err);
			goto exit;
		}
		int_status = status << 24;
		if ((status & TPS) > 0) {
			buf[0] = TILT_POS_CUR;
			err = kxtf9_i2c_read(tf9, buf, 2);
			if (err < 0) {
				dev_err(&tf9->client->dev,
					"tilt read error: %d\n", err);
			} else {
				int_status |= kxtf9_resolve_dir(tf9, buf[0]);
				int_status |=
					kxtf9_resolve_dir(tf9, buf[1]) << 8;
			}
		}
		if (((status & TDTS0) | (status & TDTS1)) > 0) {
			buf[0] = INT_SRC_REG1;
			err = kxtf9_i2c_read(tf9, buf, 1);
			if (err < 0)
				dev_err(&tf9->client->dev,
					"tap read error: %d\n", err);
			else
				int_status |=
					(kxtf9_resolve_dir(tf9, buf[0])) << 16;
		}
		if (int_status & 0x1FFFFFFF) {
			int_status |= (tf9->pdata->gesture++ & 1) << 31;
			input_report_abs(tf9->input_dev, ABS_MISC, int_status);
			input_sync(tf9->input_dev);
		}
		buf[0] = INT_REL;
		err = kxtf9_i2c_read(tf9, buf, 1);
		if (err < 0)
			dev_err(&tf9->client->dev,
				"error clearing interrupt status: %d\n", err);
	}
exit:
	enable_irq(tf9->irq);
}

int kxtf9_update_g_range(struct kxtf9_data *tf9, u8 new_g_range)
{
	int err;
	u8 shift;
	u8 buf[2] = {0,
		(tf9->resume_state[RES_CTRL_REG1] & 0x67) | new_g_range};

	switch (new_g_range) {
	case KXTF9_G_2G:
		shift = SHIFT_ADJ_2G;
		break;
	case KXTF9_G_4G:
		shift = SHIFT_ADJ_4G;
		break;
	case KXTF9_G_8G:
		shift = SHIFT_ADJ_8G;
		break;
	default:
		dev_err(&tf9->client->dev,
			"invalid g range requested: %u\n", new_g_range);
		return -EINVAL;
	}
	if (shift != tf9->shift_adj) {
		if (tf9->shift_adj > shift)
			tf9->resume_state[RES_WUF_THRESH] >>=
						(tf9->shift_adj - shift);
		if (tf9->shift_adj < shift)
			tf9->resume_state[RES_WUF_THRESH] <<=
						(shift - tf9->shift_adj);

		if (atomic_read(&tf9->enabled)) {
			buf[0] = CTRL_REG1;
			buf[1] = PC1_OFF;
			err = kxtf9_i2c_write(tf9, buf, 1);
			if (err < 0)
				goto error;
			buf[0] = WUF_THRESH;
			buf[1] = tf9->resume_state[RES_WUF_THRESH];
			err = kxtf9_i2c_write(tf9, buf, 1);
			if (err < 0)
				goto error;
			buf[0] = CTRL_REG1;
			buf[1] = (tf9->resume_state[RES_CTRL_REG1] & 0xE7) |
					new_g_range;
			err = kxtf9_i2c_write(tf9, buf, 1);
			if (err < 0)
				goto error;
		}
	}

	tf9->resume_state[RES_CTRL_REG1] = buf[1];
	tf9->shift_adj = shift;

	return 0;
error:
	dev_err(&tf9->client->dev, "update g range failed 0x%x,0x%x: %d\n",
		buf[0], buf[1], err);

	return err;
}

int kxtf9_update_odr(struct kxtf9_data *tf9, int poll_interval)
{
	int err = -1;
	int i;
	u8 config[2] = { DATA_CTRL, 0 };
	u8 buf[2] = { CTRL_REG1, PC1_OFF };

	/* Convert the poll interval into an output data rate configuration
	 *  that is as low as possible.  The ordering of these checks must be
	 *  maintained due to the cascading cut off values - poll intervals are
	 *  checked from shortest to longest.  At each check, if the next slower
	 *  ODR cannot support the current poll interval, we stop searching */
	for (i = 0; i < ARRAY_SIZE(kxtf9_odr_table); i++) {
		config[1] = kxtf9_odr_table[i].mask;
		if (poll_interval < kxtf9_odr_table[i].cutoff)
			break;
	}

	if (poll_interval > kxtf9_odr_table[5].cutoff)
		config[1] = kxtf9_odr_table[5].mask;

	if (atomic_read(&tf9->enabled)) {
		err = kxtf9_i2c_write(tf9, buf, 1);
		if (err < 0)
			goto error;
		err = kxtf9_i2c_write(tf9, config, 1);
		if (err < 0) {
			buf[0] = config[0];
			buf[1] = config[1];
			goto error;
		}
		buf[1] = tf9->resume_state[RES_CTRL_REG1];
		err = kxtf9_i2c_write(tf9, buf, 1);
		if (err < 0)
			goto error;
	}
	tf9->resume_state[RES_DATA_CTRL] = config[1];

	return 0;
error:
	dev_err(&tf9->client->dev, "update odr failed 0x%x,0x%x: %d\n",
		buf[0], buf[1], err);

	return err;
}
int kxtf9_update_gesture_sensitivity(struct kxtf9_data *tf9, int index)
{
	int err = -1;
	u8 buf[8], tmp;
	tmp = CTRL_REG1;

	if (index >= SENSITIVITY_LEVELS || index < 0)
		return err;

	err = kxtf9_i2c_read(tf9, &tmp, 1);
	if (err < 0) {
		dev_err(&tf9->client->dev, "CNTRL_REG1 reg read failed: %d\n",
				err);
	return err;
	}

	buf[0] = CTRL_REG1;
	buf[1] = PC1_OFF;
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;

	buf[0] = TDT_TIMER;
	buf[1] = tf9->ts_regs[index].reg_timer_init;
	buf[2] = tf9->ts_regs[index].reg_h_thresh_init;
	buf[3] = tf9->ts_regs[index].reg_l_thresh_init;
	buf[4] = tf9->ts_regs[index].reg_tap_timer_init;
	buf[5] = tf9->ts_regs[index].reg_total_timer_init;
	buf[6] = tf9->ts_regs[index].reg_latency_timer_init;
	buf[7] = tf9->ts_regs[index].reg_window_timer_init;
	err = kxtf9_i2c_write(tf9, buf, 7);
	if (err < 0)
		goto error;

	buf[0] = CTRL_REG1;
	buf[1] = tmp;
	err = kxtf9_i2c_write(tf9, buf, 1);
	if (err < 0)
		goto error;

	return 0;
error:
	dev_err(&tf9->client->dev, "update_gesture_sensitivity 0x%x,0x%x: %d\n",
			buf[0], buf[1], err);

	return err;
}


static int kxtf9_get_acceleration_data(struct kxtf9_data *tf9, int *xyz)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[6] = { XOUT_L };
	/* x,y,z hardware values */
	int hw_d[3] = { 0 };

	err = kxtf9_i2c_read(tf9, acc_data, 6);
	if (err < 0) {
		dev_err(&tf9->client->dev, "accel data read failed: %d\n", err);
		return err;
	}
	hw_d[0] = (int) (((acc_data[1]) << 8) | acc_data[0]);
	hw_d[1] = (int) (((acc_data[3]) << 8) | acc_data[2]);
	hw_d[2] = (int) (((acc_data[5]) << 8) | acc_data[4]);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= tf9->shift_adj;
	hw_d[1] >>= tf9->shift_adj;
	hw_d[2] >>= tf9->shift_adj;

	xyz[0] = ((tf9->pdata->negate_x) ? (-hw_d[tf9->pdata->axis_map_x])
		  : (hw_d[tf9->pdata->axis_map_x]));
	xyz[1] = ((tf9->pdata->negate_y) ? (-hw_d[tf9->pdata->axis_map_y])
		  : (hw_d[tf9->pdata->axis_map_y]));
	xyz[2] = ((tf9->pdata->negate_z) ? (-hw_d[tf9->pdata->axis_map_z])
		  : (hw_d[tf9->pdata->axis_map_z]));

	return err;
}

static void kxtf9_report_values(struct kxtf9_data *tf9, int *xyz)
{
	input_report_abs(tf9->input_dev, ABS_X, xyz[0]);
	input_report_abs(tf9->input_dev, ABS_Y, xyz[1]);
	input_report_abs(tf9->input_dev, ABS_Z, xyz[2]);
	input_report_rel(tf9->input_dev, REL_X, xyz[0]);
	input_report_rel(tf9->input_dev, REL_Y, xyz[1]);
	input_report_rel(tf9->input_dev, REL_Z, xyz[2]);

	if (kxtf9_dbg & 2)
		pr_info("%s: REPORT, X=%d, Y=%d, Z=%d\n",
			__func__, xyz[0], xyz[1], xyz[2]);
	input_sync(tf9->input_dev);
}

static int kxtf9_enable(struct kxtf9_data *tf9)
{
	int err;
	int int_status = 0;
	u8 buf;

	if (!atomic_cmpxchg(&tf9->enabled, 0, 1)) {
		err = kxtf9_device_power_on(tf9);
		if (err < 0) {
			atomic_set(&tf9->enabled, 0);
			return err;
		}
		if ((tf9->resume_state[RES_CTRL_REG1] & TPE) > 0) {
			buf = TILT_POS_CUR;
			err = kxtf9_i2c_read(tf9, &buf, 1);
			if (err < 0) {
				dev_err(&tf9->client->dev,
					"tilt read error: %d\n", err);
			} else {
				int_status |= kxtf9_resolve_dir(tf9, buf);
				int_status |= (tf9->pdata->gesture++ & 1) << 31;
				input_report_abs(tf9->input_dev,
					ABS_MISC, int_status);
				input_sync(tf9->input_dev);
			}
		}
		err = kxtf9_update_odr(tf9, tf9->pdata->poll_interval);
		if (err < 0)
			dev_err(&tf9->client->dev,
				"odr write error: %d\n", err);
		schedule_delayed_work(&tf9->input_work,
				msecs_to_jiffies(tf9->pdata->poll_interval));
	}

	return 0;
}

static int kxtf9_disable(struct kxtf9_data *tf9)
{
	if (atomic_cmpxchg(&tf9->enabled, 1, 0)) {
		cancel_delayed_work_sync(&tf9->input_work);
		kxtf9_device_power_off(tf9);
	}

	return 0;
}

static int kxtf9_misc_open(struct inode *inode, struct file *file)
{
	int err;

	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;
	file->private_data = kxtf9_misc_data;

	return 0;
}

static long kxtf9_misc_ioctl(struct file *file,
			     unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u8 buf[4];
	u8 ctrl[2] = { CTRL_REG1, PC1_OFF };
	int err;
	int interval;
	int int_state;
	struct kxtf9_data *tf9 = file->private_data;
	int sensitivity;

	switch (cmd) {
	case KXTF9_IOCTL_GET_DELAY:
		interval = tf9->pdata->poll_interval;
		if (kxtf9_dbg)
			pr_info("%s: GET_DELAY, interval=%d\n",
				__func__, interval);
		if (copy_to_user(argp, &interval, sizeof(interval))) {
			pr_err("%s: GET_DELAY unable to copy interval to user\n",
					__func__);
			return -EFAULT;
		}
		break;
	case KXTF9_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval))) {
			pr_err("%s: SET_DELAY unable to copy interval from user\n",
					__func__);
			return -EFAULT;
		}
		if (interval < 0) {
			pr_err("%s: SET_DELAY, invalid interval %d\n",
					__func__, interval);
			return -EINVAL;
		}
		if (interval > tf9->pdata->min_interval)
			tf9->pdata->poll_interval = interval;
		else
			tf9->pdata->poll_interval = tf9->pdata->min_interval;
		if (kxtf9_dbg)
			pr_info("%s: SET_DELAY, interval=%d\n",
					__func__, interval);
		err = kxtf9_update_odr(tf9, tf9->pdata->poll_interval);
		if (err < 0)
			return err;
		break;
	case KXTF9_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval))) {
			pr_err("%s: SET_ENABLE unable to copy interval from user\n",
					__func__);
			return -EFAULT;
		}
		if (interval < 0 || interval > 1) {
			pr_err("%s: SET_ENABLE, invalid interval %d\n",
					__func__, interval);
			return -EINVAL;
		}
		if (kxtf9_dbg)
			pr_info("%s: SET_ENABLE, interval=%d\n",
					__func__, interval);
		if (interval)
			kxtf9_enable(tf9);
		else
			kxtf9_disable(tf9);
		break;
	case KXTF9_IOCTL_GET_ENABLE:
		interval = atomic_read(&tf9->enabled);
		if (kxtf9_dbg)
			pr_info("%s: GET_ENABLE, interval=%d\n",
					__func__, interval);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;
	case KXTF9_IOCTL_SET_G_RANGE:
		if (copy_from_user(&buf, argp, 1))
			return -EFAULT;
	if (kxtf9_dbg)
			pr_info("%s: SET_G_RANGE, grange=%d\n",
					__func__, buf[0]);
		err = kxtf9_update_g_range(tf9, buf[0]);
		if (err < 0)
			return err;
		break;
	case KXTF9_IOCTL_SET_TILT_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1)
			return -EINVAL;
		if (kxtf9_dbg)
			pr_info("%s: SET_TILT_ENABLE, interval=%d\n",
					__func__, interval);
		if (interval)
			tf9->resume_state[RES_CTRL_REG1] |= TPE;
		else
			tf9->resume_state[RES_CTRL_REG1] &= (~TPE);
		ctrl[1] = tf9->resume_state[RES_CTRL_REG1];
		err = kxtf9_i2c_write(tf9, ctrl, 1);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"set tilt enable error: %d\n", err);
			return err;
		}
		break;
	case KXTF9_IOCTL_SET_TAP_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1)
			return -EINVAL;
		if (interval)
			tf9->resume_state[RES_CTRL_REG1] |= TDTE;
		else
			tf9->resume_state[RES_CTRL_REG1] &= (~TDTE);
		ctrl[1] = tf9->resume_state[RES_CTRL_REG1];
		err = kxtf9_i2c_write(tf9, ctrl, 1);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"set tap enable error: %d\n", err);
			return err;
		}
		break;
	case KXTF9_IOCTL_SET_WAKE_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1)
			return -EINVAL;
		if (interval)
			tf9->resume_state[RES_CTRL_REG1] |= WUFE;
		else
			tf9->resume_state[RES_CTRL_REG1] &= (~WUFE);
		ctrl[1] = tf9->resume_state[RES_CTRL_REG1];
		err = kxtf9_i2c_write(tf9, ctrl, 1);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"set wake enable error: %d\n", err);
			return err;
		}
		break;
	case KXTF9_IOCTL_SELF_TEST:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1)
			return -EINVAL;
		if (interval) {
			/*disable interrupts in self_test*/
			disable_irq_nosync(tf9->irq);
			/* disable engines and set part to +/-8g with
				12-bit outputs */
			ctrl[0] = CTRL_REG1;
			ctrl[1] = 0xD0;
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* activate self-test function */
			ctrl[0] = SELF_TEST_REG;
			ctrl[1] = 0xCA;
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* toggle physical interrupt pin polarity */
			ctrl[0] = INT_CTRL1;
			if ((tf9->resume_state[RES_INT_CTRL1] & 0x10) > 0)
				ctrl[1] = tf9->resume_state[RES_INT_CTRL1] &
						0xEF;
			else
				ctrl[1] = tf9->resume_state[RES_INT_CTRL1] |
						0x10;
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* read state of gpio pin */
			int_state = tf9->pdata->gpio();
			if (int_state != 1)
				return -ENOENT;
			/* set physical interrupt polarity back to normal */
			ctrl[0] = INT_CTRL1;
			ctrl[1] = tf9->resume_state[RES_INT_CTRL1];
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* read state of gpio pin */
			int_state = tf9->pdata->gpio();
			if (int_state != 0)
				return -ESRCH;
		} else {
			/* set physical interrupt polarity back to normal */
			ctrl[0] = INT_CTRL1;
			ctrl[1] = tf9->resume_state[RES_INT_CTRL1];
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* deactivate self-test function */
			ctrl[0] = SELF_TEST_REG;
			ctrl[1] = 0x00;
			kxtf9_i2c_write(tf9, ctrl, 1);
			/* set part configuration based on last-known state */
			ctrl[0] = CTRL_REG1;
			ctrl[1] = tf9->resume_state[RES_CTRL_REG1];
			kxtf9_i2c_write(tf9, ctrl, 1);
		}
		break;
	case KXTF9_IOCTL_SET_SENSITIVITY:
		if (copy_from_user(&sensitivity, argp, sizeof(sensitivity)))
			return -EFAULT;
		if (sensitivity < 0)
			return -EINVAL;
		if (kxtf9_update_gesture_sensitivity(tf9, sensitivity - 1) < 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations kxtf9_misc_fops = {
	.owner = THIS_MODULE,
	.open = kxtf9_misc_open,
	.unlocked_ioctl = kxtf9_misc_ioctl,
};

static struct miscdevice kxtf9_misc_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = NAME,
	.fops = &kxtf9_misc_fops,
};

static void kxtf9_input_work_func(struct work_struct *work)
{
	struct kxtf9_data *tf9 = container_of((struct delayed_work *)work,
	struct kxtf9_data, input_work);
	int xyz[3] = { 0 };

	mutex_lock(&tf9->lock);
	if (kxtf9_get_acceleration_data(tf9, xyz) == 0)
		kxtf9_report_values(tf9, xyz);
	schedule_delayed_work(&tf9->input_work,
			      msecs_to_jiffies(tf9->pdata->poll_interval));
	mutex_unlock(&tf9->lock);
}

#ifdef KXTF9_OPEN_ENABLE
int kxtf9_input_open(struct input_dev *input)
{
	struct kxtf9_data *tf9 = input_get_drvdata(input);

	return kxtf9_enable(tf9);
}

void kxtf9_input_close(struct input_dev *dev)
{
	struct kxtf9_data *tf9 = input_get_drvdata(dev);

	kxtf9_disable(tf9);
}
#endif

static int kxtf9_validate_pdata(struct kxtf9_data *tf9)
{
	if (tf9->pdata->min_interval > tf9->pdata->poll_interval)
		tf9->pdata->poll_interval = tf9->pdata->min_interval;
	if (tf9->pdata->axis_map_x > 2 || tf9->pdata->axis_map_y > 2 ||
		tf9->pdata->axis_map_z > 2 ||
		tf9->pdata->axis_map_x == tf9->pdata->axis_map_y ||
		tf9->pdata->axis_map_x == tf9->pdata->axis_map_z ||
		tf9->pdata->axis_map_y == tf9->pdata->axis_map_z) {
		dev_err(&tf9->client->dev,
			"invalid axis_map value x:%u y:%u z:%u\n",
			tf9->pdata->axis_map_x, tf9->pdata->axis_map_y,
			tf9->pdata->axis_map_z);
		return -EINVAL;
	}
	if (tf9->pdata->negate_x > 1 || tf9->pdata->negate_y > 1 ||
	    tf9->pdata->negate_z > 1) {
		dev_err(&tf9->client->dev,
			"invalid negate value x:%u y:%u z:%u\n",
			tf9->pdata->negate_x, tf9->pdata->negate_y,
			tf9->pdata->negate_z);
		return -EINVAL;
	}

	return 0;
}

static int kxtf9_input_init(struct kxtf9_data *tf9)
{
	int err;
	int int_status = 0;
	u8 buf;

	INIT_DELAYED_WORK(&tf9->input_work, kxtf9_input_work_func);
	tf9->input_dev = input_allocate_device();
	if (!tf9->input_dev) {
		err = -ENOMEM;
		dev_err(&tf9->client->dev,
			"input device allocate failed: %d\n", err);
		goto err0;
	}
#ifdef kxtf9_OPEN_ENABLE
	tf9->input_dev->open = kxtf9_input_open;
	tf9->input_dev->close = kxtf9_input_close;
#endif
	input_set_drvdata(tf9->input_dev, tf9);

	set_bit(EV_ABS, tf9->input_dev->evbit);
	set_bit(ABS_MISC, tf9->input_dev->absbit);

	input_set_abs_params(tf9->input_dev, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(tf9->input_dev, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(tf9->input_dev, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(tf9->input_dev, ABS_MISC, INT_MIN, INT_MAX, 0, 0);

	input_set_capability(tf9->input_dev, EV_REL, REL_X);
	input_set_capability(tf9->input_dev, EV_REL, REL_Y);
	input_set_capability(tf9->input_dev, EV_REL, REL_Z);

	tf9->input_dev->name = "accelerometer";

	err = input_register_device(tf9->input_dev);
	if (err) {
		dev_err(&tf9->client->dev,
			"unable to register input polled device %s: %d\n",
			tf9->input_dev->name, err);
		goto err1;
	}
	if ((tf9->resume_state[RES_CTRL_REG1] & TPE) > 0) {
		buf = TILT_POS_CUR;
		err = kxtf9_i2c_read(tf9, &buf, 1);
		if (err < 0) {
			dev_err(&tf9->client->dev,
				"tilt read error: %d\n", err);
		} else {
			int_status |= kxtf9_resolve_dir(tf9, buf);
			int_status |= (tf9->pdata->gesture++ & 1) << 31;
			input_report_abs(tf9->input_dev, ABS_MISC, int_status);
			input_sync(tf9->input_dev);
		}
	}

	return 0;
err1:
	input_free_device(tf9->input_dev);
err0:

	return err;
}

static void kxtf9_sensitivity_init(struct kxtf9_data *tf9)
{
	int buf_size = 0;

	buf_size = sizeof(tf9->pdata->sensitivity_low);
	 memcpy(tf9->ts_regs + SENSITIVITY_LOW_OFFSET,
			tf9->pdata->sensitivity_low, buf_size);
	buf_size = sizeof(tf9->pdata->sensitivity_medium);
	memcpy(tf9->ts_regs + SENSITIVITY_MEDIUM_OFFSET,
			tf9->pdata->sensitivity_medium, buf_size);
	buf_size = sizeof(tf9->pdata->sensitivity_high);
	memcpy(tf9->ts_regs + SENSITIVITY_HIGH_OFFSET,
			tf9->pdata->sensitivity_high, buf_size);

}

static void kxtf9_input_cleanup(struct kxtf9_data *tf9)
{
	input_unregister_device(tf9->input_dev);
	input_free_device(tf9->input_dev);
}

static int kxtf9_probe(struct i2c_client *client,
			   const struct i2c_device_id *id)
{
	struct kxtf9_data *tf9;
	int err = -1;

	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL, exiting\n");
		err = -ENODEV;
		goto err0;
	}
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto err0;
	}
	tf9 = kzalloc(sizeof(*tf9), GFP_KERNEL);
	if (tf9 == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"failed to allocate memory for module data: %d\n", err);
		goto err0;
	}

	mutex_init(&tf9->lock);
	mutex_lock(&tf9->lock);
	tf9->client = client;

	INIT_WORK(&tf9->irq_work, kxtf9_irq_work_func);
	tf9->irq_work_queue = create_singlethread_workqueue("kxtf9_wq");

	if (!tf9->irq_work_queue) {
		err = -ENOMEM;
		dev_err(&client->dev, "cannot create work queue: %d\n", err);
		goto err1;
	}
	tf9->pdata = kzalloc(sizeof(*tf9->pdata), GFP_KERNEL);
	if (tf9->pdata == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"failed to allocate memory for pdata: %d\n", err);
		goto err2;
	}
	memcpy(tf9->pdata, client->dev.platform_data, sizeof(*tf9->pdata));
	err = kxtf9_validate_pdata(tf9);
	if (err < 0) {
		dev_err(&client->dev,
			"failed to validate platform data: %d\n", err);
		goto err3;
	}
	i2c_set_clientdata(client, tf9);

	tf9->regulator = regulator_get(&client->dev, "vcc");
	if (IS_ERR_OR_NULL(tf9->regulator)) {
		dev_err(&client->dev, "unable to get regulator\n");
		tf9->regulator = NULL;
	}

	kxtf9_sensitivity_init(tf9);

	tf9->irq = client->irq;
	tf9->resume_state[RES_DATA_CTRL]    = tf9->pdata->data_odr_init;
	tf9->resume_state[RES_CTRL_REG1]    = tf9->pdata->ctrl_reg1_init;
	tf9->resume_state[RES_INT_CTRL1]    = tf9->pdata->int_ctrl_init;
	tf9->resume_state[RES_TILT_TIMER]   = tf9->pdata->tilt_timer_init;
	tf9->resume_state[RES_CTRL_REG3]    = tf9->pdata->engine_odr_init;
	tf9->resume_state[RES_WUF_TIMER]    = tf9->pdata->wuf_timer_init;
	tf9->resume_state[RES_WUF_THRESH]   = tf9->pdata->wuf_thresh_init;
	tf9->resume_state[RES_TDT_TIMER]    = tf9->pdata->tdt_timer_init;
	tf9->resume_state[RES_TDT_H_THRESH] = tf9->pdata->tdt_h_thresh_init;
	tf9->resume_state[RES_TDT_L_THRESH] = tf9->pdata->tdt_l_thresh_init;
	tf9->resume_state[RES_TAP_TIMER]    = tf9->pdata->tdt_tap_timer_init;
	tf9->resume_state[RES_TOTAL_TIMER]  = tf9->pdata->tdt_total_timer_init;
	tf9->resume_state[RES_LAT_TIMER]   = tf9->pdata->tdt_latency_timer_init;
	tf9->resume_state[RES_WIN_TIMER]    = tf9->pdata->tdt_window_timer_init;
	err = kxtf9_device_power_on(tf9);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err4;
	}

	atomic_set(&tf9->enabled, 1);
	err = kxtf9_update_g_range(tf9, tf9->pdata->g_range);
	if (err < 0)
		goto err5;
	err = kxtf9_update_odr(tf9, tf9->pdata->poll_interval);
	if (err < 0)
		goto err5;
	err = kxtf9_input_init(tf9);
	if (err < 0)
		goto err5;

	kxtf9_misc_data = tf9;
	err = misc_register(&kxtf9_misc_device);
	if (err < 0) {
		dev_err(&client->dev, "misc register failed: %d\n", err);
		goto err6;
	}

	kxtf9_device_power_off(tf9);
	atomic_set(&tf9->enabled, 0);
	tf9->enabled_b4_suspend = 0;

	err = request_irq(tf9->irq, kxtf9_isr, IRQF_TRIGGER_RISING,
		"kxtf9_irq", tf9);
	if (err < 0) {
		dev_err(&client->dev, "request irq failed: %d\n", err);
		goto err7;
	}
	if (kxtf9_dbg)
		pr_info("%s: Request IRQ = %d\n", __func__, tf9->irq);

	disable_irq_nosync(tf9->irq);
	mutex_unlock(&tf9->lock);

	dev_info(&client->dev, "kxtf9 probed\n");

	return 0;

err7:
	misc_deregister(&kxtf9_misc_device);
err6:
	kxtf9_input_cleanup(tf9);
err5:
	kxtf9_device_power_off(tf9);
err4:
	if (tf9->regulator)
		regulator_put(tf9->regulator);
err3:
	kfree(tf9->pdata);
err2:
	destroy_workqueue(tf9->irq_work_queue);
err1:
	mutex_unlock(&tf9->lock);
	kfree(tf9);
err0:
	return err;
}

static int __devexit kxtf9_remove(struct i2c_client *client)
{
	struct kxtf9_data *tf9 = i2c_get_clientdata(client);

	free_irq(tf9->irq, tf9);
	misc_deregister(&kxtf9_misc_device);
	kxtf9_input_cleanup(tf9);
	kxtf9_device_power_off(tf9);
	if (tf9->regulator)
		regulator_put(tf9->regulator);
	kfree(tf9->pdata);
	destroy_workqueue(tf9->irq_work_queue);
	kfree(tf9);

	return 0;
}

static int kxtf9_resume(struct i2c_client *client)
{
	int err;
	struct kxtf9_data *tf9 = i2c_get_clientdata(client);
	if(!tf9->enabled_b4_suspend)
		return 0;

	err = kxtf9_enable(tf9);
	if(err)
		return err;
	err = kxtf9_update_odr(tf9,tf9->pdata->poll_interval);
	return err;
}

static int kxtf9_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct kxtf9_data *tf9 = i2c_get_clientdata(client);
	tf9->enabled_b4_suspend = atomic_read(&tf9->enabled);
	return kxtf9_disable(tf9);
}

static const struct i2c_device_id kxtf9_id[] = {
	{NAME, 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, kxtf9_id);

static struct i2c_driver kxtf9_driver = {
	.driver = {
		   .name = NAME,
		   },
	.probe = kxtf9_probe,
	.remove = __devexit_p(kxtf9_remove),
	.resume = kxtf9_resume,
	.suspend = kxtf9_suspend,
	.id_table = kxtf9_id,
};

static int __init kxtf9_init(void)
{
	pr_info("kxtf9 accelerometer driver\n");
	return i2c_add_driver(&kxtf9_driver);
}

static void __exit kxtf9_exit(void)
{
	i2c_del_driver(&kxtf9_driver);
	return;
}

module_init(kxtf9_init);
module_exit(kxtf9_exit);

MODULE_DESCRIPTION("KXTF9 accelerometer driver");
MODULE_AUTHOR("Kionix");
MODULE_LICENSE("GPL");

