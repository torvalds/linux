/* drivers/i2c/chips/kxtf9.c - KXTF9 accelerometer driver
 *
 * Copyright (C) 2010 Kionix, Inc.
 * Written by Kuching Tan <kuchingtan@kionix.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <linux/err.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/uaccess.h>
#include <linux/workqueue.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/kxtf9.h>
#include <linux/miscdevice.h>

#define NAME			"kxtf9"

#define G_MAX			8000
/* OUTPUT REGISTERS */
#define XOUT_HPF_L		0x00
#define XOUT_L			0x06
#define DCST_RESP		0x0C
#define WHO_AM_I		0x0F
#define TILT_POS_CUR	0x10
#define TILT_POS_PRE	0x11
#define INT_SRC_REG1	0x15
#define INT_SRC_REG2	0x16
#define	STATUS_REG		0x18
#define INT_REL			0x1A
/* CONTROL REGISTERS */
#define CTRL_REG1		0x1B
#define CTRL_REG2		0x1C
#define CTRL_REG3		0x1D
#define INT_CTRL_REG1	0x1E
#define INT_CTRL_REG2	0x1F
#define INT_CTRL_REG3	0x20
#define DATA_CTRL_REG	0x21
#define TILT_TIMER		0x28
#define WUF_TIMER		0x29
#define TDT_TIMER		0x2B
#define	TDT_H_THRESH	0x2C
#define	TDT_L_THRESH	0x2D
#define	TDT_TAP_TIMER	0x2E
#define	TDT_TOTAL_TIMER	0x2F
#define	TDT_LAT_TIMER	0x30
#define	TDT_WIN_TIMER	0x31
#define WUF_THRESH		0x5A
#define TILT_ANGLE		0x5C
#define	HYST_SET		0x5F
/* CTRL_REG1 BITS */
#define PC1_OFF			0x00
#define PC1_ON			0x80
/* INT_SRC_REG2 BITS */
#define TPS				0x01
#define WUFS			0x02
#define TDTS0			0x04
#define TDTS1			0x08
//#define	DRDY			0x10
/* Direction Mask */
/* Used for TILT_POS_CUR, TILT_POS_PRE	*/
/*			INT_SRC_REG1, CTRL_REG2		*/
/*			INT_CTRL_REG3*/
#define	DIR_LE			0x20
#define DIR_RI			0x10
#define DIR_DO			0x08
#define DIR_UP			0x04
#define DIR_FD			0x02
#define DIR_FU			0x01
/* ODR MASKS */
#define OTPM			0x60 // CTRL_REG3
#define	OWUFM			0x03 // CTRL_REG3
#define OTDTM			0x0C // CTRL_REG3
#define	HPFROM			0x30 // DATA_CTRL_REG
#define	OSAM			0x07 // DATA_CTRL_REG
/* INPUT_ABS CONSTANTS */
#define FUZZ			32
#define FLAT			32
/* RESUME STATE INDICES */
#define RES_CTRL_REG1		0
#define RES_CTRL_REG2		1
#define RES_CTRL_REG3		2
#define RES_INT_CTRL_REG1	3
#define RES_INT_CTRL_REG2	4
#define RES_INT_CTRL_REG3	5
#define RES_DATA_CTRL_REG	6
#define RES_TILT_TIMER		7
#define RES_WUF_TIMER		8
#define RES_TDT_TIMER		9
#define RES_TDT_H_THRESH	10
#define RES_TDT_L_THRESH	11
#define RES_TDT_TAP_TIMER	12
#define RES_TDT_TOTAL_TIMER	13
#define RES_TDT_LAT_TIMER	14
#define RES_TDT_WIN_TIMER	15
#define RES_WUF_THRESH		16
#define RES_TILT_ANGLE		17
#define RES_HYST_SET		18
#define RESUME_ENTRIES		19
/* OFFSET and SENSITIVITY */
#define	OFFSET			0
#define SENS			1024

#define IOCTL_BUFFER_SIZE	64

/*
 * The following table lists the maximum appropriate poll interval for each
 * available output data rate.
 */
struct {
	unsigned int cutoff;
	u8 mask;
} kxtf9_odr_table[] = {
	{
	3,	ODR800F}, {
	5,	ODR400F}, {
	10,	ODR200F}, {
	20,	ODR100F}, {
	40,	ODR50F}, {
	0,	ODR25F},
};

struct kxtf9_data {
	struct i2c_client *client;
	struct kxtf9_platform_data *pdata;
	struct mutex lock;
	struct delayed_work input_work;
	struct input_dev *input_dev;
	struct work_struct irq_work;

	int acc_sens[3];
	int hw_initialized;
	atomic_t enabled;
	u8 resume[RESUME_ENTRIES];
	int res_interval;
	int irq;
};

static struct kxtf9_data *tf9 = NULL;
static atomic_t kxtf9_dev_open_count;

// Debug Flag, set to 1 to turn on debugging output
#define DEBUG 0

static int kxtf9_i2c_read(u8 addr, u8 *data, int len)
{
	int err;

	struct i2c_msg msgs[] = {
		{
		 .addr = tf9->client->addr,
		 .flags = tf9->client->flags & I2C_M_TEN,
		 .len = 1,
		 .buf = &addr,
		 },
		{
		 .addr = tf9->client->addr,
		 .flags = (tf9->client->flags & I2C_M_TEN) | I2C_M_RD,
		 .len = len,
		 .buf = data,
		 },
	};
	err = i2c_transfer(tf9->client->adapter, msgs, 2);

	if(err != 2)
		dev_err(&tf9->client->dev, "read transfer error\n");
	else
		err = 0;

	return err;
}

static int kxtf9_i2c_write(u8 addr, u8 *data, int len)
{
	int err;
	int i;
	u8 buf[len + 1];

	struct i2c_msg msgs[] = {
		{
		 .addr = tf9->client->addr,
		 .flags = tf9->client->flags & I2C_M_TEN,
		 .len = len + 1,
		 .buf = buf,
		 },
	};

	buf[0] = addr;
	for (i = 0; i < len; i++)
		buf[i + 1] = data[i];

	err = i2c_transfer(tf9->client->adapter, msgs, 1);

	if(err != 1)
		dev_err(&tf9->client->dev, "write transfer error\n");
	else
		err = 0;

	return err;
}

int kxtf9_get_bits(u8 reg_addr, u8* bits_value, u8 bits_mask)
{
	int err;
	u8 reg_data;

	err = kxtf9_i2c_read(reg_addr, &reg_data, 1);
	if(err < 0)
		return err;

	*bits_value = reg_data & bits_mask;

	return 1;
}

int kxtf9_get_byte(u8 reg_addr, u8* reg_value)
{
	int err;
	u8 reg_data;

	err = kxtf9_i2c_read(reg_addr, &reg_data, 1);
	if(err < 0)
		return err;

	*reg_value = reg_data;

	return 1;
}

int kxtf9_set_bits(int res_index, u8 reg_addr, u8 bits_value, u8 bits_mask)
{
	int err=0, err1=0, retval=0;
	u8 reg_data = 0x00, reg_bits = 0x00, bits_set = 0x00;

	// Turn off PC1
	reg_data = tf9->resume[RES_CTRL_REG1] & ~PC1_ON;

	err = kxtf9_i2c_write(CTRL_REG1, &reg_data, 1);
	if(err < 0)
		goto exit0;

	// Read from device register
	err = kxtf9_i2c_read(reg_addr, &reg_data, 1);
	if(err < 0)
		goto exit0;

	// Apply mask to device register;
	reg_bits = reg_data & bits_mask;

	// Update resume state data
	bits_set = bits_mask & bits_value;
	tf9->resume[res_index] &= ~bits_mask;
	tf9->resume[res_index] |= bits_set;

	// Return 0 if value in device register and value to be written is the same
	if(reg_bits == bits_set)
		retval = 0;
	// Else, return 1
	else
		retval = 1;

	// Write to device register
	err = kxtf9_i2c_write(reg_addr, &tf9->resume[res_index], 1);
	if(err < 0)
		goto exit0;

exit0:
	// Turn on PC1
	reg_data = tf9->resume[RES_CTRL_REG1] | PC1_ON;

	err1 = kxtf9_i2c_write(CTRL_REG1, &reg_data, 1);
	if(err1 < 0)
		return err1;

	if(err < 0)
		return err;

	return retval;
}

int kxtf9_set_byte(int res_index, u8 reg_addr, u8 reg_value)
{
	int err, err1, retval=0;
	u8 reg_data;

	// Turn off PC1
	reg_data = tf9->resume[RES_CTRL_REG1] & ~PC1_ON;

	err = kxtf9_i2c_write(CTRL_REG1, &reg_data, 1);
	if(err < 0)
		goto exit0;

	// Read from device register
	err = kxtf9_i2c_read(reg_addr, &reg_data, 1);
	if(err < 0)
		goto exit0;

	// Update resume state data
	tf9->resume[res_index] = reg_value;

	// Return 0 if value in device register and value to be written is the same
	if(reg_data == reg_value)
		retval = 0;
	// Else, return 1
	else
		retval = 1;

	// Write to device register
	err = kxtf9_i2c_write(reg_addr, &tf9->resume[res_index], 1);
	if(err < 0)
		goto exit0;

exit0:
	// Turn on PC1
	reg_data = tf9->resume[RES_CTRL_REG1] | PC1_ON;

	err1 = kxtf9_i2c_write(CTRL_REG1, &reg_data, 1);
	if(err1 < 0)
		return err1;

	if(err < 0)
		return err;

	return retval;
}

static int kxtf9_verify(void)
{
	int err;
	u8 buf;

	err = kxtf9_i2c_read(WHO_AM_I, &buf, 1);
	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: WHO_AM_I(Chip ID) = 0x%02x\n", __FUNCTION__, buf);
	#endif
	if(err < 0)
		dev_err(&tf9->client->dev, "read err int source\n");
	if(buf != 1)
		err = -1;
	return err;
}

static int kxtf9_hw_init(void)
{
	int err = -1;
	u8 buf[7];

	buf[0] = PC1_OFF;
	err = kxtf9_i2c_write(CTRL_REG1, buf, 1);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(DATA_CTRL_REG, &tf9->resume[RES_DATA_CTRL_REG], 1);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(CTRL_REG3, &tf9->resume[RES_CTRL_REG3], 1);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(TILT_TIMER, &tf9->resume[RES_TILT_TIMER], 1);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(WUF_TIMER, &tf9->resume[RES_WUF_TIMER], 1);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(WUF_THRESH, &tf9->resume[RES_WUF_THRESH], 1);
	if(err < 0)
		return err;
	buf[0] = tf9->resume[RES_TDT_TIMER];
	buf[1] = tf9->resume[RES_TDT_H_THRESH];
	buf[2] = tf9->resume[RES_TDT_L_THRESH];
	buf[3] = tf9->resume[RES_TDT_TAP_TIMER];
	buf[4] = tf9->resume[RES_TDT_TOTAL_TIMER];
	buf[5] = tf9->resume[RES_TDT_LAT_TIMER];
	buf[6] = tf9->resume[RES_TDT_WIN_TIMER];
	err = kxtf9_i2c_write(TDT_TIMER, buf, 7);
	if(err < 0)
		return err;
	err = kxtf9_i2c_write(INT_CTRL_REG1, &tf9->resume[RES_INT_CTRL_REG1], 1);
	if(err < 0)
		return err;
	buf[0] = (tf9->resume[RES_CTRL_REG1] | PC1_ON);
	err = kxtf9_i2c_write(CTRL_REG1, buf, 1);
	if(err < 0)
		return err;
	tf9->resume[RES_CTRL_REG1] = buf[0];
	tf9->hw_initialized = 1;

	return 0;
}

static void kxtf9_device_power_off(void)
{
	int err;
	u8 buf = PC1_OFF;

	err = kxtf9_i2c_write(CTRL_REG1, &buf, 1);
	if(err < 0)
		dev_err(&tf9->client->dev, "soft power off failed\n");
	disable_irq(tf9->irq);
	if(tf9->pdata->power_off)
		tf9->pdata->power_off();
	tf9->hw_initialized = 0;
}

static int kxtf9_device_power_on(void)
{
	int err;

	if(tf9->pdata->power_on) {
		err = tf9->pdata->power_on();
		if(err < 0)
			return err;
	}
	enable_irq(tf9->irq);
	if(!tf9->hw_initialized) {
		mdelay(100);
		err = kxtf9_hw_init();
		if(err < 0) {
			kxtf9_device_power_off();
			return err;
		}
	}
	return 0;
}

static irqreturn_t kxtf9_isr(int irq, void *dev)
{
	disable_irq_nosync(irq);
	schedule_work(&tf9->irq_work);

	return IRQ_HANDLED;
}

static u8 kxtf9_resolve_dir(u8 dir)
{
	switch (dir) {
	case 0x20:	/* -X */
		if(tf9->pdata->negate_x)
			dir = 0x10;
		if(tf9->pdata->axis_map_y == 0)
			dir >>= 2;
		if(tf9->pdata->axis_map_z == 0)
			dir >>= 4;
		break;
	case 0x10:	/* +X */
		if(tf9->pdata->negate_x)
			dir = 0x20;
		if(tf9->pdata->axis_map_y == 0)
			dir >>= 2;
		if(tf9->pdata->axis_map_z == 0)
			dir >>= 4;
		break;
	case 0x08:	/* -Y */
		if(tf9->pdata->negate_y)
			dir = 0x04;
		if(tf9->pdata->axis_map_x == 1)
			dir <<= 2;
		if(tf9->pdata->axis_map_z == 1)
			dir >>= 2;
		break;
	case 0x04:	/* +Y */
		if(tf9->pdata->negate_y)
			dir = 0x08;
		if(tf9->pdata->axis_map_x == 1)
			dir <<= 2;
		if(tf9->pdata->axis_map_z == 1)
			dir >>= 2;
		break;
	case 0x02:	/* -Z */
		if(tf9->pdata->negate_z)
			dir = 0x01;
		if(tf9->pdata->axis_map_x == 2)
			dir <<= 4;
		if(tf9->pdata->axis_map_y == 2)
			dir <<= 2;
		break;
	case 0x01:	/* +Z */
		if(tf9->pdata->negate_z)
			dir = 0x02;
		if(tf9->pdata->axis_map_x == 2)
			dir <<= 4;
		if(tf9->pdata->axis_map_y == 2)
			dir <<= 2;
		break;
	default:
		return -EINVAL;
	}

	return dir;
}

static void kxtf9_irq_work_func(struct work_struct *work)
{
/*
 *	int_status output:
 *	[INT_SRC_REG2][INT_SRC_REG1][TILT_POS_PRE][TILT_POS_CUR]
 *	INT_SRC_REG1, TILT_POS_PRE, and TILT_POS_CUR directions are translated
 *	based on platform data variables.
 */

	int err;
	int int_status = 0;
	u8 status;
	u8 buf[2];

	err = kxtf9_i2c_read(INT_SRC_REG2, &status, 1);
	if(err < 0)
		dev_err(&tf9->client->dev, "read err int source\n");
	int_status = status << 24;
	if((status & TPS) > 0) {
		err = kxtf9_i2c_read(TILT_POS_CUR, buf, 2);
		if(err < 0)
			dev_err(&tf9->client->dev, "read err tilt dir\n");
		int_status |= kxtf9_resolve_dir(buf[0]);
		int_status |= kxtf9_resolve_dir(buf[1]) << 8;
		#if defined DEBUG && DEBUG == 1
		dev_info(&tf9->client->dev, "%s: IRQ TILT [%x]\n", __FUNCTION__,
				kxtf9_resolve_dir(buf[0]));
		#endif
	}
	if(((status & TDTS0) | (status & TDTS1)) > 0) {
		err = kxtf9_i2c_read(INT_SRC_REG1, buf, 1);
		if(err < 0)
			dev_err(&tf9->client->dev, "read err tap dir\n");
		int_status |= (kxtf9_resolve_dir(buf[0])) << 16;
		#if defined DEBUG && DEBUG == 1
		dev_info(&tf9->client->dev, "%s: IRQ TAP%d [%x]\n", __FUNCTION__,
		((status & TDTS1) ? (2) : (1)), kxtf9_resolve_dir(buf[0]));
		#endif
	}
	#if defined DEBUG && DEBUG == 1
	if((status & 0x02) > 0) {
		if(((status & TDTS0) | (status & TDTS1)) > 0)
			dev_info(&tf9->client->dev, "%s: IRQ WUF + TAP\n", __FUNCTION__);
		else
			dev_info(&tf9->client->dev, "%s: IRQ WUF\n", __FUNCTION__);
	}
	#endif
	if(int_status & 0x2FFF) {
		input_report_abs(tf9->input_dev, ABS_MISC, int_status);
		input_sync(tf9->input_dev);
	}
	err = kxtf9_i2c_read(INT_REL, buf, 1);
	if(err < 0)
		dev_err(&tf9->client->dev,
				"error clearing interrupt status: %d\n", err);

	enable_irq(tf9->irq);
}

int kxtf9_update_g_range(u8 new_g_range)
{
	int err;
	u8 shift;
	u8 buf;

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
		dev_err(&tf9->client->dev, "invalid g range request\n");
		return -EINVAL;
	}
	if(shift != tf9->pdata->shift_adj) {
		if(tf9->pdata->shift_adj > shift)
			tf9->resume[RES_WUF_THRESH] >>=
						(tf9->pdata->shift_adj - shift);
		if(tf9->pdata->shift_adj < shift)
			tf9->resume[RES_WUF_THRESH] <<=
						(shift - tf9->pdata->shift_adj);

		if(atomic_read(&tf9->enabled)) {
			buf = PC1_OFF;
			err = kxtf9_i2c_write(CTRL_REG1, &buf, 1);
			if(err < 0)
				return err;
			buf = tf9->resume[RES_WUF_THRESH];
			err = kxtf9_i2c_write(WUF_THRESH, &buf, 1);
			if(err < 0)
				return err;
			buf = (tf9->resume[RES_CTRL_REG1] & 0xE7) | new_g_range;
			err = kxtf9_i2c_write(CTRL_REG1, &buf, 1);
			if(err < 0)
				return err;
			tf9->resume[RES_CTRL_REG1] = buf;
		}
		tf9->pdata->shift_adj = shift;
	}

	return 0;
}

static int kxtf9_update_odr(int poll_interval)
{
	int err = -1;
	int i;
	u8 config;

	/*  Convert the poll interval into an output data rate configuration
	 *  that is as low as possible.  The ordering of these checks must be
	 *  maintained due to the cascading cut off values - poll intervals are
	 *  checked from shortest to longest.  At each check, if the next slower
	 *  ODR cannot support the current poll interval, we stop searching */
	for (i = 0; i < ARRAY_SIZE(kxtf9_odr_table); i++) {
		config = kxtf9_odr_table[i].mask;
		if(poll_interval < kxtf9_odr_table[i].cutoff)
			break;
	}

	tf9->resume[RES_DATA_CTRL_REG] = config;
	if(atomic_read(&tf9->enabled)) {
		err = kxtf9_set_byte(RES_DATA_CTRL_REG, DATA_CTRL_REG, config);
		if(err < 0)
			return err;
	}
	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: poll_interval is %dms, ODR set to 0x%02X\n", __FUNCTION__, poll_interval, config);
	#endif

	return 0;
}

static int kxtf9_get_acceleration_data(int *xyz)
{
	int err;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[6], res12bit;
	/* x,y,z hardware values */
	int hw_d[3];

	err = kxtf9_i2c_read(XOUT_L, acc_data, 6);
	if(err < 0)
		return err;

	err = kxtf9_get_bits(CTRL_REG1, &res12bit, RES_12BIT);
	if(err < 0)
		return err;

	acc_data[0] = (res12bit & RES_12BIT) ? (acc_data[0]) : (0x00);
	acc_data[2] = (res12bit & RES_12BIT) ? (acc_data[2]) : (0x00);
	acc_data[4] = (res12bit & RES_12BIT) ? (acc_data[4]) : (0x00);

	hw_d[0] = (int) (((acc_data[1]) << 8) | acc_data[0]);
	hw_d[1] = (int) (((acc_data[3]) << 8) | acc_data[2]);
	hw_d[2] = (int) (((acc_data[5]) << 8) | acc_data[4]);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= tf9->pdata->shift_adj;
	hw_d[1] >>= tf9->pdata->shift_adj;
	hw_d[2] >>= tf9->pdata->shift_adj;

	xyz[0] = ((tf9->pdata->negate_x) ? (-hw_d[tf9->pdata->axis_map_x])
		  : (hw_d[tf9->pdata->axis_map_x]));
	xyz[1] = ((tf9->pdata->negate_y) ? (-hw_d[tf9->pdata->axis_map_y])
		  : (hw_d[tf9->pdata->axis_map_y]));
	xyz[2] = ((tf9->pdata->negate_z) ? (-hw_d[tf9->pdata->axis_map_z])
		  : (hw_d[tf9->pdata->axis_map_z]));

	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: x:%5d y:%5d z:%5d\n", __FUNCTION__, xyz[0], xyz[1], xyz[2]);
	#endif

	return err;
}

static void kxtf9_report_values(int *xyz)
{
	input_report_abs(tf9->input_dev, ABS_X, xyz[0]);
	input_report_abs(tf9->input_dev, ABS_Y, xyz[1]);
	input_report_abs(tf9->input_dev, ABS_Z, xyz[2]);
	input_sync(tf9->input_dev);
}

static int kxtf9_enable(void)
{
	int err;
	int int_status = 0;
	u8 buf;

	if(!atomic_cmpxchg(&tf9->enabled, 0, 1)) {
		err = kxtf9_device_power_on();
		err = kxtf9_i2c_read(INT_REL, &buf, 1);
		if(err < 0) {
			dev_err(&tf9->client->dev,
					"error clearing interrupt: %d\n", err);
			atomic_set(&tf9->enabled, 0);
			return err;
		}
		if((tf9->resume[RES_CTRL_REG1] & TPE) > 0) {
			err = kxtf9_i2c_read(TILT_POS_CUR, &buf, 1);
			if(err < 0)
				dev_err(&tf9->client->dev,
					"read err current tilt\n");
			int_status |= kxtf9_resolve_dir(buf);
			input_report_abs(tf9->input_dev, ABS_MISC, int_status);
			input_sync(tf9->input_dev);
		}
		schedule_delayed_work(&tf9->input_work,
			msecs_to_jiffies(tf9->res_interval));
		#if defined DEBUG && DEBUG == 1
		dev_info(&tf9->client->dev, "%s: Enabled\n", __FUNCTION__);
		#endif
	}
	return 0;
}

static int kxtf9_disable(void)
{
	if(atomic_cmpxchg(&tf9->enabled, 1, 0)) {
		cancel_delayed_work_sync(&tf9->input_work);
		kxtf9_device_power_off();
		#if defined DEBUG && DEBUG == 1
		dev_info(&tf9->client->dev, "%s: Disabled\n", __FUNCTION__);
		#endif
	}
	return 0;
}

static void kxtf9_input_work_func(struct work_struct *work)
{
	int xyz[3] = { 0 };
	int err;

	mutex_lock(&tf9->lock);
	err = kxtf9_get_acceleration_data(xyz);
	if(err < 0)
		dev_err(&tf9->client->dev, "get_acceleration_data failed\n");
	else
		kxtf9_report_values(xyz);
	schedule_delayed_work(&tf9->input_work,
			msecs_to_jiffies(tf9->res_interval));
	mutex_unlock(&tf9->lock);
}

int kxtf9_input_open(struct input_dev *input)
{
	return kxtf9_enable();
}

void kxtf9_input_close(struct input_dev *dev)
{
	kxtf9_disable();
}

static int kxtf9_input_init(void)
{
	int err;

	INIT_DELAYED_WORK(&tf9->input_work, kxtf9_input_work_func);
	tf9->input_dev = input_allocate_device();
	if(!tf9->input_dev) {
		err = -ENOMEM;
		dev_err(&tf9->client->dev, "input device allocate failed\n");
		goto err0;
	}
	tf9->input_dev->open = kxtf9_input_open;
	tf9->input_dev->close = kxtf9_input_close;

	input_set_drvdata(tf9->input_dev, tf9);

	set_bit(EV_ABS, tf9->input_dev->evbit);
	set_bit(ABS_MISC, tf9->input_dev->absbit);

	input_set_abs_params(tf9->input_dev, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(tf9->input_dev, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(tf9->input_dev, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);

	tf9->input_dev->name = INPUT_NAME_ACC;

	err = input_register_device(tf9->input_dev);
	if(err) {
		dev_err(&tf9->client->dev,
			"unable to register input polled device %s: %d\n",
			tf9->input_dev->name, err);
		goto err1;
	}

	return 0;
err1:
	input_free_device(tf9->input_dev);
err0:
	return err;
}

static void kxtf9_input_cleanup(void)
{
	input_unregister_device(tf9->input_dev);
}

/* sysfs */
static ssize_t kxtf9_delay_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", tf9->res_interval);
}

static ssize_t kxtf9_delay_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);

	tf9->res_interval = max(val, tf9->pdata->min_interval);
	kxtf9_update_odr(tf9->res_interval);

	return count;
}

static ssize_t kxtf9_enable_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", atomic_read(&tf9->enabled));
}

static ssize_t kxtf9_enable_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);
	if(val)
		kxtf9_enable();
	else
		kxtf9_disable();
	return count;
}

static ssize_t kxtf9_tilt_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 tilt;

	if(tf9->resume[RES_CTRL_REG1] & TPE) {
		kxtf9_i2c_read(TILT_POS_CUR, &tilt, 1);
		return sprintf(buf, "%d\n", kxtf9_resolve_dir(tilt));
	} else {
		return sprintf(buf, "%d\n", 0);
	}
}

static ssize_t kxtf9_tilt_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);
	if(val)
		tf9->resume[RES_CTRL_REG1] |= TPE;
	else
		tf9->resume[RES_CTRL_REG1] &= (~TPE);
	kxtf9_i2c_write(CTRL_REG1, &tf9->resume[RES_CTRL_REG1], 1);
	return count;
}

static ssize_t kxtf9_wake_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 val = tf9->resume[RES_CTRL_REG1] & WUFE;
	if(val)
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t kxtf9_wake_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);
	if(val)
		tf9->resume[RES_CTRL_REG1] |= WUFE;
	else
		tf9->resume[RES_CTRL_REG1] &= (~WUFE);
	kxtf9_i2c_write(CTRL_REG1, &tf9->resume[RES_CTRL_REG1], 1);
	return count;
}

static ssize_t kxtf9_tap_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 val = tf9->resume[RES_CTRL_REG1] & TDTE;
	if(val)
		return sprintf(buf, "%d\n", 1);
	else
		return sprintf(buf, "%d\n", 0);
}

static ssize_t kxtf9_tap_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);
	if(val)
		tf9->resume[RES_CTRL_REG1] |= TDTE;
	else
		tf9->resume[RES_CTRL_REG1] &= (~TDTE);
	kxtf9_i2c_write(CTRL_REG1, &tf9->resume[RES_CTRL_REG1], 1);
	return count;
}

static ssize_t kxtf9_selftest_store(struct device *dev,
					struct device_attribute *attr,
						const char *buf, size_t count)
{
	int val = simple_strtoul(buf, NULL, 10);
	u8 ctrl = 0x00;
	if(val)
		ctrl = 0xCA;
	kxtf9_i2c_write(0x3A, &ctrl, 1);
	return count;
}

static DEVICE_ATTR(delay, S_IRUGO|S_IWUSR, kxtf9_delay_show, kxtf9_delay_store);
static DEVICE_ATTR(enable, S_IRUGO|S_IWUSR, kxtf9_enable_show,
						kxtf9_enable_store);
static DEVICE_ATTR(tilt, S_IRUGO|S_IWUSR, kxtf9_tilt_show, kxtf9_tilt_store);
static DEVICE_ATTR(wake, S_IRUGO|S_IWUSR, kxtf9_wake_show, kxtf9_wake_store);
static DEVICE_ATTR(tap, S_IRUGO|S_IWUSR, kxtf9_tap_show, kxtf9_tap_store);
static DEVICE_ATTR(selftest, S_IWUSR, NULL, kxtf9_selftest_store);

static struct attribute *kxtf9_attributes[] = {
	&dev_attr_delay.attr,
	&dev_attr_enable.attr,
	&dev_attr_tilt.attr,
	&dev_attr_wake.attr,
	&dev_attr_tap.attr,
	&dev_attr_selftest.attr,
	NULL
};

static struct attribute_group kxtf9_attribute_group = {
	.attrs = kxtf9_attributes
};
/* /sysfs */

static int kxtf9_get_count(char *buf, int bufsize)
{
	const char ACC_REG_SIZE = 6;
	int err;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[ACC_REG_SIZE], res12bit;
	/* x,y,z hardware values */
	int hw_d[3], xyz[3];

	if((!buf)||(bufsize<=(sizeof(xyz)*3)))
		return -1;

	err = kxtf9_i2c_read(XOUT_L, acc_data, ACC_REG_SIZE);
	if(err < 0)
		return err;

	err = kxtf9_get_bits(CTRL_REG1, &res12bit, RES_12BIT);
	if(err < 0)
		return err;

	acc_data[0] = (res12bit & RES_12BIT) ? (acc_data[0]) : (0x00);
	acc_data[2] = (res12bit & RES_12BIT) ? (acc_data[2]) : (0x00);
	acc_data[4] = (res12bit & RES_12BIT) ? (acc_data[4]) : (0x00);

	hw_d[0] = (int) (((acc_data[1]) << 8) | acc_data[0]);
	hw_d[1] = (int) (((acc_data[3]) << 8) | acc_data[2]);
	hw_d[2] = (int) (((acc_data[5]) << 8) | acc_data[4]);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= tf9->pdata->shift_adj;
	hw_d[1] >>= tf9->pdata->shift_adj;
	hw_d[2] >>= tf9->pdata->shift_adj;

	xyz[0] = ((tf9->pdata->negate_x) ? (-hw_d[tf9->pdata->axis_map_x])
		  : (hw_d[tf9->pdata->axis_map_x]));
	xyz[1] = ((tf9->pdata->negate_y) ? (-hw_d[tf9->pdata->axis_map_y])
		  : (hw_d[tf9->pdata->axis_map_y]));
	xyz[2] = ((tf9->pdata->negate_z) ? (-hw_d[tf9->pdata->axis_map_z])
		  : (hw_d[tf9->pdata->axis_map_z]));

	sprintf(buf, "%d %d %d %d %d %d %d %d %d",\
			xyz[0], xyz[1], xyz[2],\
			0, 0, 0,\
			((int)SENS), ((int)SENS), ((int)SENS));

	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: [%5d] [%5d] [%5d] [%5d] [%5d] [%5d] [%5d] [%5d] [%5d]\n", __FUNCTION__,\
			xyz[0], xyz[1], xyz[2],\
			0, 0, 0,\
			((int)SENS), ((int)SENS), ((int)SENS));
	#endif

	return err;
}

static int kxtf9_get_mg(char *buf, int bufsize)
{
	const char ACC_REG_SIZE = 6;
	int err;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[ACC_REG_SIZE], res12bit;
	/* x,y,z hardware values */
	int hw_d[3], xyz[3], mg[3];

	if((!buf)||(bufsize<=(sizeof(mg))))
		return -1;

	err = kxtf9_i2c_read(XOUT_L, acc_data, ACC_REG_SIZE);
	if(err < 0)
		return err;

	err = kxtf9_get_bits(CTRL_REG1, &res12bit, RES_12BIT);
	if(err < 0)
		return err;

	acc_data[0] = (res12bit & RES_12BIT) ? (acc_data[0]) : (0x00);
	acc_data[2] = (res12bit & RES_12BIT) ? (acc_data[2]) : (0x00);
	acc_data[4] = (res12bit & RES_12BIT) ? (acc_data[4]) : (0x00);

	hw_d[0] = (int) (((acc_data[1]) << 8) | acc_data[0]);
	hw_d[1] = (int) (((acc_data[3]) << 8) | acc_data[2]);
	hw_d[2] = (int) (((acc_data[5]) << 8) | acc_data[4]);

	hw_d[0] = (hw_d[0] & 0x8000) ? (hw_d[0] | 0xFFFF0000) : (hw_d[0]);
	hw_d[1] = (hw_d[1] & 0x8000) ? (hw_d[1] | 0xFFFF0000) : (hw_d[1]);
	hw_d[2] = (hw_d[2] & 0x8000) ? (hw_d[2] | 0xFFFF0000) : (hw_d[2]);

	hw_d[0] >>= tf9->pdata->shift_adj;
	hw_d[1] >>= tf9->pdata->shift_adj;
	hw_d[2] >>= tf9->pdata->shift_adj;

	xyz[0] = ((tf9->pdata->negate_x) ? (-hw_d[tf9->pdata->axis_map_x])
		  : (hw_d[tf9->pdata->axis_map_x]));
	xyz[1] = ((tf9->pdata->negate_y) ? (-hw_d[tf9->pdata->axis_map_y])
		  : (hw_d[tf9->pdata->axis_map_y]));
	xyz[2] = ((tf9->pdata->negate_z) ? (-hw_d[tf9->pdata->axis_map_z])
		  : (hw_d[tf9->pdata->axis_map_z]));

	mg[0] = xyz[0] * 1000 / SENS;
	mg[1] = xyz[1] * 1000 / SENS;
	mg[2] = xyz[2] * 1000 / SENS;

	sprintf(buf, "%d %d %d",mg[0], mg[1], mg[2]);

	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: [%5d] [%5d] [%5d]\n", __FUNCTION__, mg[0], mg[1], mg[2]);
	#endif

	return err;
}

static int kxtf9_open(struct inode *inode, struct file *file)
{
	int ret = -1;

	if(kxtf9_enable() < 0)
		return ret;

	atomic_inc(&kxtf9_dev_open_count);

	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: opened %d times\n",\
			__FUNCTION__, atomic_read(&kxtf9_dev_open_count));
	#endif

	return 0;
}

static int kxtf9_release(struct inode *inode, struct file *file)
{
	int open_count;

	atomic_dec(&kxtf9_dev_open_count);
	open_count = (int)atomic_read(&kxtf9_dev_open_count);

	if(open_count == 0)
		kxtf9_disable();

	#if defined DEBUG && DEBUG == 1
	dev_info(&tf9->client->dev, "%s: opened %d times\n",\
			__FUNCTION__, atomic_read(&kxtf9_dev_open_count));
	#endif

	return 0;
}

static int kxtf9_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	char buffer[IOCTL_BUFFER_SIZE];
	void __user *data;
	u8 reg_buffer = 0x00;
	const u8 set = 0xFF, unset = 0x00;
	int retval=0, val_int=0;
	short val_short=0;

	switch (cmd) {
		case KXTF9_IOCTL_GET_COUNT:
			data = (void __user *) arg;
			if(data == NULL){
				retval = -EFAULT;
				goto err_out;
			}
			retval = kxtf9_get_count(buffer, sizeof(buffer));
			if(retval < 0)
				goto err_out;

			if(copy_to_user(data, buffer, sizeof(buffer))) {
				retval = -EFAULT;
				goto err_out;
			}
			break;

		case KXTF9_IOCTL_GET_MG:
			data = (void __user *) arg;
			if(data == NULL){
				retval = -EFAULT;
				goto err_out;
			}
			retval = kxtf9_get_mg(buffer, sizeof(buffer));
			if(retval < 0)
				goto err_out;

			if(copy_to_user(data, buffer, sizeof(buffer))) {
				retval = -EFAULT;
				goto err_out;
			}
			break;

		case KXTF9_IOCTL_ENABLE_OUTPUT:
			retval = kxtf9_enable();
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_DISABLE_OUTPUT:
			retval = kxtf9_disable();
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_GET_ENABLE:
			data = (void __user *) arg;
			if(data == NULL){
				retval = -EFAULT;
				goto err_out;
			}
			retval = kxtf9_get_bits(CTRL_REG1, &reg_buffer, PC1_ON);
			if(retval < 0)
				goto err_out;

			val_short = (short)reg_buffer;

			if(copy_to_user(data, &val_short, sizeof(val_short))) {
				retval = -EFAULT;
				goto err_out;
			}
			break;

		case KXTF9_IOCTL_RESET:
			retval = kxtf9_set_bits(RES_CTRL_REG3, CTRL_REG3, set, SRST);
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_UPDATE_ODR:
			data = (void __user *) arg;
			if(data == NULL){
				retval = -EFAULT;
				goto err_out;
			}
			if(copy_from_user(&val_int, data, sizeof(val_int))) {
				retval = -EFAULT;
				goto err_out;
			}

			mutex_lock(&tf9->lock);
			tf9->res_interval = max(val_int, tf9->pdata->min_interval);
			mutex_unlock(&tf9->lock);

			retval = kxtf9_update_odr(tf9->res_interval);
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_ENABLE_DCST:
			retval = kxtf9_set_bits(RES_CTRL_REG3, CTRL_REG3, set, DCST);
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_DISABLE_DCST:
			retval = kxtf9_set_bits(RES_CTRL_REG3, CTRL_REG3, unset, DCST);
			if(retval < 0)
				goto err_out;
			break;

		case KXTF9_IOCTL_GET_DCST_RESP:
			data = (void __user *) arg;
			if(data == NULL){
				retval = -EFAULT;
				goto err_out;
			}
			retval = kxtf9_get_byte(DCST_RESP, &reg_buffer);
			if(retval < 0)
				goto err_out;

			buffer[0] = (char)reg_buffer;
			if(copy_to_user(data, buffer, sizeof(buffer))) {
				retval = -EFAULT;
				goto err_out;
			}
			break;

		default:
			retval = -ENOIOCTLCMD;
			break;
	}

err_out:
	return retval;
}

static struct file_operations kxtf9_fops = {
	.owner = THIS_MODULE,
	.open = kxtf9_open,
	.release = kxtf9_release,
	.ioctl = kxtf9_ioctl,
};

static struct miscdevice kxtf9_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = NAME_DEV,
	.fops = &kxtf9_fops,
};

static int __devinit kxtf9_probe(struct i2c_client *client,
						const struct i2c_device_id *id)
{
	int err = -1;
	tf9 = kzalloc(sizeof(*tf9), GFP_KERNEL);
	if(tf9 == NULL) {
		dev_err(&client->dev,
			"failed to allocate memory for module data\n");
		err = -ENOMEM;
		goto err0;
	}
	if(client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto err0;
	}
	if(!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto err0;
	}
	mutex_init(&tf9->lock);
	mutex_lock(&tf9->lock);
	tf9->client = client;
	i2c_set_clientdata(client, tf9);

	INIT_WORK(&tf9->irq_work, kxtf9_irq_work_func);
	tf9->pdata = kmalloc(sizeof(*tf9->pdata), GFP_KERNEL);
	if(tf9->pdata == NULL)
		goto err1;

	err = sysfs_create_group(&client->dev.kobj, &kxtf9_attribute_group);
	if(err)
		goto err1;

	memcpy(tf9->pdata, client->dev.platform_data, sizeof(*tf9->pdata));
	if(tf9->pdata->init) {
		err = tf9->pdata->init();
		if(err < 0)
			goto err2;
	}

	tf9->irq = gpio_to_irq(tf9->pdata->gpio);

	memset(tf9->resume, 0, ARRAY_SIZE(tf9->resume));
	tf9->resume[RES_DATA_CTRL_REG] = tf9->pdata->data_odr_init;
	tf9->resume[RES_CTRL_REG1] = tf9->pdata->ctrl_reg1_init;
	tf9->resume[RES_INT_CTRL_REG1] = tf9->pdata->int_ctrl_init;
	tf9->resume[RES_TILT_TIMER] = tf9->pdata->tilt_timer_init;
	tf9->resume[RES_CTRL_REG3] = tf9->pdata->engine_odr_init;
	tf9->resume[RES_WUF_TIMER] = tf9->pdata->wuf_timer_init;
	tf9->resume[RES_WUF_THRESH] = tf9->pdata->wuf_thresh_init;
	tf9->resume[RES_TDT_TIMER] = tf9->pdata->tdt_timer_init;
	tf9->resume[RES_TDT_H_THRESH] = tf9->pdata->tdt_h_thresh_init;
	tf9->resume[RES_TDT_L_THRESH] = tf9->pdata->tdt_l_thresh_init;
	tf9->resume[RES_TDT_TAP_TIMER] = tf9->pdata->tdt_tap_timer_init;
	tf9->resume[RES_TDT_TOTAL_TIMER] = tf9->pdata->tdt_total_timer_init;
	tf9->resume[RES_TDT_LAT_TIMER] = tf9->pdata->tdt_latency_timer_init;
	tf9->resume[RES_TDT_WIN_TIMER]    = tf9->pdata->tdt_window_timer_init;
	tf9->res_interval = tf9->pdata->poll_interval;

	err = kxtf9_device_power_on();
	if(err < 0)
		goto err3;
	atomic_set(&tf9->enabled, 1);

	err = kxtf9_verify();
	if(err < 0) {
		dev_err(&client->dev, "unresolved i2c client\n");
		goto err4;
	}

	err = kxtf9_update_g_range(tf9->pdata->g_range);
	if(err < 0) {
		dev_err(&client->dev, "update_g_range failed\n");
		goto err4;
	}

	err = kxtf9_update_odr(tf9->res_interval);
	if(err < 0) {
		dev_err(&client->dev, "update_odr failed\n");
		goto err4;
	}

	err = kxtf9_input_init();
	if(err < 0)
		goto err4;

	err = misc_register(&kxtf9_device);
	if(err) {
		dev_err(&client->dev, "misc. device failed to register.\n");
		goto err5;
	}

	dev_info(&client->dev, "%s: Registered %s\n", __FUNCTION__, DIR_DEV);

	kxtf9_device_power_off();
	atomic_set(&tf9->enabled, 0);
	err = request_irq(tf9->irq, kxtf9_isr,
			IRQF_TRIGGER_RISING | IRQF_DISABLED, "kxtf9-irq", tf9);
	if(err < 0) {
		pr_err("%s: request irq failed: %d\n", __func__, err);
		goto err6;
	}
	disable_irq_nosync(tf9->irq);

	mutex_unlock(&tf9->lock);

	return 0;

err6:
	misc_deregister(&kxtf9_device);
err5:
	kxtf9_input_cleanup();
err4:
	kxtf9_device_power_off();
err3:
	if(tf9->pdata->exit)
		tf9->pdata->exit();
err2:
	kfree(tf9->pdata);
	sysfs_remove_group(&client->dev.kobj, &kxtf9_attribute_group);
err1:
	mutex_unlock(&tf9->lock);
	kfree(tf9);
err0:
	return err;
}

static int __devexit kxtf9_remove(struct i2c_client *client)
{
	free_irq(tf9->irq, tf9);
	gpio_free(tf9->pdata->gpio);
	kxtf9_input_cleanup();
	misc_deregister(&kxtf9_device);
	kxtf9_device_power_off();
	if(tf9->pdata->exit)
		tf9->pdata->exit();
	kfree(tf9->pdata);
	sysfs_remove_group(&client->dev.kobj, &kxtf9_attribute_group);
	kfree(tf9);

	return 0;
}

#ifdef CONFIG_PM
static int kxtf9_resume(struct i2c_client *client)
{
	return kxtf9_enable();
}

static int kxtf9_suspend(struct i2c_client *client, pm_message_t mesg)
{
	return kxtf9_disable();
}
#endif

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
	atomic_set(&kxtf9_dev_open_count, 0);

	return i2c_add_driver(&kxtf9_driver);
}

static void __exit kxtf9_exit(void)
{
	atomic_set(&kxtf9_dev_open_count, 0);

	i2c_del_driver(&kxtf9_driver);
}

module_init(kxtf9_init);
module_exit(kxtf9_exit);

MODULE_DESCRIPTION("KXTF9 accelerometer driver");
MODULE_AUTHOR("Kuching Tan <kuchingtan@kionix.com>");
MODULE_LICENSE("GPL");
MODULE_VERSION(VERSION_DEV);
