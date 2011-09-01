/******************** (C) COPYRIGHT 2010 STMicroelectronics ********************
 *
 * File Name          : lis3dh_acc.c
 * Authors            : MSH - Motion Mems BU - Application Team
 *		      : Carmine Iascone (carmine.iascone@st.com)
 *		      : Matteo Dameno (matteo.dameno@st.com)
 * Version            : V.1.0.5
 * Date               : 16/08/2010
 * Description        : LIS3DH accelerometer sensor API
 *
 *******************************************************************************
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THE PRESENT SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES
 * OR CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, FOR THE SOLE
 * PURPOSE TO SUPPORT YOUR APPLICATION DEVELOPMENT.
 * AS A RESULT, STMICROELECTRONICS SHALL NOT BE HELD LIABLE FOR ANY DIRECT,
 * INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING FROM THE
 * CONTENT OF SUCH SOFTWARE AND/OR THE USE MADE BY CUSTOMERS OF THE CODING
 * INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
 *
 * THIS SOFTWARE IS SPECIFICALLY DESIGNED FOR EXCLUSIVE USE WITH ST PARTS.
 *
 ******************************************************************************
 Revision 1.0.0 05/11/09
 First Release
 Revision 1.0.3 22/01/2010
  Linux K&R Compliant Release;
 Revision 1.0.5 16/08/2010
 modified _get_acceleration_data function
 modified _update_odr function
 manages 2 interrupts

 ******************************************************************************/

#include	<linux/err.h>
#include	<linux/errno.h>
#include	<linux/delay.h>
#include	<linux/fs.h>
#include	<linux/i2c.h>

#include	<linux/input.h>
#include	<linux/input-polldev.h>
#include	<linux/miscdevice.h>
#include	<linux/uaccess.h>

#include	<linux/workqueue.h>
#include	<linux/irq.h>
#include	<linux/gpio.h>
#include	<linux/interrupt.h>

//#include	<linux/i2c/lis3dh.h>
#include "lis3dh_acc_misc.h"


#define	DEBUG	1

#define	INTERRUPT_MANAGEMENT 1

#define	G_MAX		16000	/** Maximum polled-device-reported g value */

/*
#define	SHIFT_ADJ_2G		4
#define	SHIFT_ADJ_4G		3
#define	SHIFT_ADJ_8G		2
#define	SHIFT_ADJ_16G		1
*/

#define SENSITIVITY_2G		1	/**	mg/LSB	*/
#define SENSITIVITY_4G		2	/**	mg/LSB	*/
#define SENSITIVITY_8G		4	/**	mg/LSB	*/
#define SENSITIVITY_16G		12	/**	mg/LSB	*/


#define	HIGH_RESOLUTION		0x08

#define	AXISDATA_REG		0x28
#define WHOAMI_LIS3DH_ACC	0x33	/*	Expctd content for WAI	*/

/*	CONTROL REGISTERS	*/
#define WHO_AM_I		0x0F	/*	WhoAmI register		*/
#define	TEMP_CFG_REG		0x1F	/*	temper sens control reg	*/
/* ctrl 1: ODR3 ODR2 ODR ODR0 LPen Zenable Yenable Zenable */
#define	CTRL_REG1		0x20	/*	control reg 1		*/
#define	CTRL_REG2		0x21	/*	control reg 2		*/
#define	CTRL_REG3		0x22	/*	control reg 3		*/
#define	CTRL_REG4		0x23	/*	control reg 4		*/
#define	CTRL_REG5		0x24	/*	control reg 5		*/
#define	CTRL_REG6		0x25	/*	control reg 6		*/

#define	FIFO_CTRL_REG		0x2E	/*	FiFo control reg	*/

#define	INT_CFG1		0x30	/*	interrupt 1 config	*/
#define	INT_SRC1		0x31	/*	interrupt 1 source	*/
#define	INT_THS1		0x32	/*	interrupt 1 threshold	*/
#define	INT_DUR1		0x33	/*	interrupt 1 duration	*/

#define	INT_CFG2		0x34	/*	interrupt 2 config	*/
#define	INT_SRC2		0x35	/*	interrupt 2 source	*/
#define	INT_THS2		0x36	/*	interrupt 2 threshold	*/
#define	INT_DUR2		0x37	/*	interrupt 2 duration	*/

#define	TT_CFG			0x38	/*	tap config		*/
#define	TT_SRC			0x39	/*	tap source		*/
#define	TT_THS			0x3A	/*	tap threshold		*/
#define	TT_LIM			0x3B	/*	tap time limit		*/
#define	TT_TLAT			0x3C	/*	tap time latency	*/
#define	TT_TW			0x3D	/*	tap time window		*/
/*	end CONTROL REGISTRES	*/


#define ENABLE_HIGH_RESOLUTION	1

#define LIS3DH_ACC_PM_OFF		0x00
#define LIS3DH_ACC_ENABLE_ALL_AXES	0x07

#define PMODE_MASK			0x08
#define ODR_MASK			0XF0

#define ODR1		0x10  /* 1Hz output data rate */
#define ODR10		0x20  /* 10Hz output data rate */
#define ODR25		0x30  /* 25Hz output data rate */
#define ODR50		0x40  /* 50Hz output data rate */
#define ODR100		0x50  /* 100Hz output data rate */
#define ODR200		0x60  /* 200Hz output data rate */
#define ODR400		0x70  /* 400Hz output data rate */
#define ODR1250		0x90  /* 1250Hz output data rate */



#define	IA			0x40
#define	ZH			0x20
#define	ZL			0x10
#define	YH			0x08
#define	YL			0x04
#define	XH			0x02
#define	XL			0x01
/* */
/* CTRL REG BITS*/
#define	CTRL_REG3_I1_AOI1	0x40
#define	CTRL_REG6_I2_TAPEN	0x80
#define	CTRL_REG6_HLACTIVE	0x02
/* */

/* TAP_SOURCE_REG BIT */
#define	DTAP			0x20
#define	STAP			0x10
#define	SIGNTAP			0x08
#define	ZTAP			0x04
#define	YTAP			0x02
#define	XTAZ			0x01


#define	FUZZ			32
#define	FLAT			32
#define	I2C_RETRY_DELAY		5
#define	I2C_RETRIES		5
#define	I2C_AUTO_INCREMENT	0x80

/* RESUME STATE INDICES */
#define	RES_CTRL_REG1		0
#define	RES_CTRL_REG2		1
#define	RES_CTRL_REG3		2
#define	RES_CTRL_REG4		3
#define	RES_CTRL_REG5		4
#define	RES_CTRL_REG6		5

#define	RES_INT_CFG1		6
#define	RES_INT_THS1		7
#define	RES_INT_DUR1		8
#define	RES_INT_CFG2		9
#define	RES_INT_THS2		10
#define	RES_INT_DUR2		11

#define	RES_TT_CFG		12
#define	RES_TT_THS		13
#define	RES_TT_LIM		14
#define	RES_TT_TLAT		15
#define	RES_TT_TW		16

#define	RES_TEMP_CFG_REG	17
#define	RES_REFERENCE_REG	18
#define	RES_FIFO_CTRL_REG	19

#define	RESUME_ENTRIES		20
/* end RESUME STATE INDICES */

struct {
	unsigned int cutoff_ms;
	unsigned int mask;
} lis3dh_acc_odr_table[] = {
			{ 1, ODR1250 },
			{ 3, ODR400 },
			{ 5, ODR200 },
			{ 10, ODR100 },
			{ 20, ODR50 },
			{ 40, ODR25 },
			{ 100, ODR10 },
			{ 1000, ODR1 },
};

struct lis3dh_acc_data {
	struct i2c_client *client;
	struct lis3dh_acc_platform_data *pdata;

	struct mutex lock;
	struct delayed_work input_work;

	struct input_dev *input_dev;

	int hw_initialized;
	/* hw_working=-1 means not tested yet */
	int hw_working;
	atomic_t enabled;
	int on_before_suspend;

	u8 sensitivity;

	u8 resume_state[RESUME_ENTRIES];

	int irq1;
	struct work_struct irq1_work;
	struct workqueue_struct *irq1_work_queue;
	int irq2;
	struct work_struct irq2_work;
	struct workqueue_struct *irq2_work_queue;
};

/*
 * Because misc devices can not carry a pointer from driver register to
 * open, we keep this global.  This limits the driver to a single instance.
 */
struct lis3dh_acc_data *lis3dh_acc_misc_data;

static int lis3dh_acc_i2c_read(struct lis3dh_acc_data *acc, u8 * buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg	msgs[] = {
		{
			.addr = acc->client->addr,
			.flags = acc->client->flags & I2C_M_TEN,
			.len = 1,
			.buf = buf, },
		{
			.addr = acc->client->addr,
			.flags = (acc->client->flags & I2C_M_TEN) | I2C_M_RD,
			.len = len,
			.buf = buf, },
	};

	do {
		err = i2c_transfer(acc->client->adapter, msgs, 2);
		if (err != 2)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 2) && (++tries < I2C_RETRIES));

	if (err != 2) {
		dev_err(&acc->client->dev, "read transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int lis3dh_acc_i2c_write(struct lis3dh_acc_data *acc, u8 * buf, int len)
{
	int err;
	int tries = 0;

	struct i2c_msg msgs[] = { { .addr = acc->client->addr,
			.flags = acc->client->flags & I2C_M_TEN,
			.len = len + 1, .buf = buf, }, };
	do {
		err = i2c_transfer(acc->client->adapter, msgs, 1);
		if (err != 1)
			msleep_interruptible(I2C_RETRY_DELAY);
	} while ((err != 1) && (++tries < I2C_RETRIES));

	if (err != 1) {
		dev_err(&acc->client->dev, "write transfer error\n");
		err = -EIO;
	} else {
		err = 0;
	}

	return err;
}

static int lis3dh_acc_hw_init(struct lis3dh_acc_data *acc)
{
	int err = -1;
	u8 buf[7];

	printk(KERN_INFO "%s: hw init start\n", LIS3DH_ACC_DEV_NAME);

	buf[0] = WHO_AM_I;
	err = lis3dh_acc_i2c_read(acc, buf, 1);
	if (err < 0)
		goto error_firstread;
	else
		acc->hw_working = 1;
	if (buf[0] != WHOAMI_LIS3DH_ACC) {
		err = -1; /* choose the right coded error */
		goto error_unknown_device;
	}

	buf[0] = CTRL_REG1;
	buf[1] = acc->resume_state[RES_CTRL_REG1];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = TEMP_CFG_REG;
	buf[1] = acc->resume_state[RES_TEMP_CFG_REG];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = FIFO_CTRL_REG;
	buf[1] = acc->resume_state[RES_FIFO_CTRL_REG];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = (I2C_AUTO_INCREMENT | TT_THS);
	buf[1] = acc->resume_state[RES_TT_THS];
	buf[2] = acc->resume_state[RES_TT_LIM];
	buf[3] = acc->resume_state[RES_TT_TLAT];
	buf[4] = acc->resume_state[RES_TT_TW];
	err = lis3dh_acc_i2c_write(acc, buf, 4);
	if (err < 0)
		goto error1;
	buf[0] = TT_CFG;
	buf[1] = acc->resume_state[RES_TT_CFG];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = (I2C_AUTO_INCREMENT | INT_THS1);
	buf[1] = acc->resume_state[RES_INT_THS1];
	buf[2] = acc->resume_state[RES_INT_DUR1];
	err = lis3dh_acc_i2c_write(acc, buf, 2);
	if (err < 0)
		goto error1;
	buf[0] = INT_CFG1;
	buf[1] = acc->resume_state[RES_INT_CFG1];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = (I2C_AUTO_INCREMENT | INT_THS2);
	buf[1] = acc->resume_state[RES_INT_THS2];
	buf[2] = acc->resume_state[RES_INT_DUR2];
	err = lis3dh_acc_i2c_write(acc, buf, 2);
	if (err < 0)
		goto error1;
	buf[0] = INT_CFG2;
	buf[1] = acc->resume_state[RES_INT_CFG2];
	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		goto error1;

	buf[0] = (I2C_AUTO_INCREMENT | CTRL_REG2);
	buf[1] = acc->resume_state[RES_CTRL_REG2];
	buf[2] = acc->resume_state[RES_CTRL_REG3];
	buf[3] = acc->resume_state[RES_CTRL_REG4];
	buf[4] = acc->resume_state[RES_CTRL_REG5];
	buf[5] = acc->resume_state[RES_CTRL_REG6];
	err = lis3dh_acc_i2c_write(acc, buf, 5);
	if (err < 0)
		goto error1;

	acc->hw_initialized = 1;
	printk(KERN_INFO "%s: hw init done\n", LIS3DH_ACC_DEV_NAME);
	return 0;

error_firstread:
	acc->hw_working = 0;
	dev_warn(&acc->client->dev, "Error reading WHO_AM_I: is device "
		"available/working?\n");
	goto error1;
error_unknown_device:
	dev_err(&acc->client->dev,
		"device unknown. Expected: 0x%x,"
		" Replies: 0x%x\n", WHOAMI_LIS3DH_ACC, buf[0]);
error1:
	acc->hw_initialized = 0;
	dev_err(&acc->client->dev, "hw init error 0x%x,0x%x: %d\n", buf[0],
			buf[1], err);
	return err;
}

static void lis3dh_acc_device_power_off(struct lis3dh_acc_data *acc)
{
	int err;
	u8 buf[2] = { CTRL_REG1, LIS3DH_ACC_PM_OFF };

	err = lis3dh_acc_i2c_write(acc, buf, 1);
	if (err < 0)
		dev_err(&acc->client->dev, "soft power off failed: %d\n", err);

	if (acc->pdata->power_off) {
		disable_irq_nosync(acc->irq1);
		disable_irq_nosync(acc->irq2);
		acc->pdata->power_off();
		acc->hw_initialized = 0;
	}
	if (acc->hw_initialized) {
		disable_irq_nosync(acc->irq1);
		disable_irq_nosync(acc->irq2);
		acc->hw_initialized = 0;
	}

}

static int lis3dh_acc_device_power_on(struct lis3dh_acc_data *acc)
{
	int err = -1;

	if (acc->pdata->power_on) {
		err = acc->pdata->power_on();
		if (err < 0) {
			dev_err(&acc->client->dev,
					"power_on failed: %d\n", err);
			return err;
		}
		enable_irq(acc->irq1);
		enable_irq(acc->irq2);
	}

	if (!acc->hw_initialized) {
		err = lis3dh_acc_hw_init(acc);
		if (acc->hw_working == 1 && err < 0) {
			lis3dh_acc_device_power_off(acc);
			return err;
		}
	}

	if (acc->hw_initialized) {
		enable_irq(acc->irq1);
		enable_irq(acc->irq2);
		printk(KERN_INFO "%s: power on: irq enabled\n",
						LIS3DH_ACC_DEV_NAME);
	}
	return 0;
}

static irqreturn_t lis3dh_acc_isr1(int irq, void *dev)
{
	struct lis3dh_acc_data *acc = dev;

	disable_irq_nosync(irq);
	queue_work(acc->irq1_work_queue, &acc->irq1_work);
	printk(KERN_INFO "%s: isr1 queued\n", LIS3DH_ACC_DEV_NAME);

	return IRQ_HANDLED;
}

static irqreturn_t lis3dh_acc_isr2(int irq, void *dev)
{
	struct lis3dh_acc_data *acc = dev;

	disable_irq_nosync(irq);
	queue_work(acc->irq2_work_queue, &acc->irq2_work);
	printk(KERN_INFO "%s: isr2 queued\n", LIS3DH_ACC_DEV_NAME);

	return IRQ_HANDLED;
}



static void lis3dh_acc_irq1_work_func(struct work_struct *work)
{

	struct lis3dh_acc_data *acc =
	container_of(work, struct lis3dh_acc_data, irq1_work);
	/* TODO  add interrupt service procedure.
		 ie:lis3dh_acc_get_int1_source(acc); */
	;
	/*  */
	printk(KERN_INFO "%s: IRQ1 triggered\n", LIS3DH_ACC_DEV_NAME);
exit:
	enable_irq(acc->irq1);
}

static void lis3dh_acc_irq2_work_func(struct work_struct *work)
{

	struct lis3dh_acc_data *acc =
	container_of(work, struct lis3dh_acc_data, irq2_work);
	/* TODO  add interrupt service procedure.
		 ie:lis3dh_acc_get_tap_source(acc); */
	;
	/*  */

	printk(KERN_INFO "%s: IRQ2 triggered\n", LIS3DH_ACC_DEV_NAME);
exit:
	enable_irq(acc->irq2);
}

int lis3dh_acc_update_g_range(struct lis3dh_acc_data *acc, u8 new_g_range)
{
	int err;

	u8 sensitivity;
	u8 buf[2];
	u8 updated_val;
	u8 init_val;
	u8 new_val;
	u8 mask = LIS3DH_ACC_FS_MASK | HIGH_RESOLUTION;

	switch (new_g_range) {
	case LIS3DH_ACC_G_2G:

		sensitivity = SENSITIVITY_2G;
		break;
	case LIS3DH_ACC_G_4G:

		sensitivity = SENSITIVITY_4G;
		break;
	case LIS3DH_ACC_G_8G:

		sensitivity = SENSITIVITY_8G;
		break;
	case LIS3DH_ACC_G_16G:

		sensitivity = SENSITIVITY_16G;
		break;
	default:
		dev_err(&acc->client->dev, "invalid g range requested: %u\n",
				new_g_range);
		return -EINVAL;
	}

	if (atomic_read(&acc->enabled)) {
		/* Set configuration register 4, which contains g range setting
		 *  NOTE: this is a straight overwrite because this driver does
		 *  not use any of the other configuration bits in this
		 *  register.  Should this become untrue, we will have to read
		 *  out the value and only change the relevant bits --XX----
		 *  (marked by X) */
		buf[0] = CTRL_REG4;
		err = lis3dh_acc_i2c_read(acc, buf, 1);
		if (err < 0)
			goto error;
		init_val = buf[0];
		acc->resume_state[RES_CTRL_REG4] = init_val;
		new_val = new_g_range | HIGH_RESOLUTION;
		updated_val = ((mask & new_val) | ((~mask) & init_val));
		buf[1] = updated_val;
		buf[0] = CTRL_REG4;
		err = lis3dh_acc_i2c_write(acc, buf, 1);
		if (err < 0)
			goto error;
		acc->resume_state[RES_CTRL_REG4] = updated_val;
		acc->sensitivity = sensitivity;
	}


	return 0;
error:
	dev_err(&acc->client->dev, "update g range failed 0x%x,0x%x: %d\n",
			buf[0], buf[1], err);

	return err;
}

int lis3dh_acc_update_odr(struct lis3dh_acc_data *acc, int poll_interval_ms)
{
	int err = -1;
	int i;
	u8 config[2];

	/* Convert the poll interval into an output data rate configuration
	 *  that is as low as possible.  The ordering of these checks must be
	 *  maintained due to the cascading cut off values - poll intervals are
	 *  checked from shortest to longest.  At each check, if the next lower
	 *  ODR cannot support the current poll interval, we stop searching */
	for (i = ARRAY_SIZE(lis3dh_acc_odr_table) - 1; i >= 0; i--) {
		if (lis3dh_acc_odr_table[i].cutoff_ms <= poll_interval_ms)
			break;
	}
	config[1] = lis3dh_acc_odr_table[i].mask;

	config[1] |= LIS3DH_ACC_ENABLE_ALL_AXES;

	/* If device is currently enabled, we need to write new
	 *  configuration out to it */
	if (atomic_read(&acc->enabled)) {
		config[0] = CTRL_REG1;
		err = lis3dh_acc_i2c_write(acc, config, 1);
		if (err < 0)
			goto error;
		acc->resume_state[RES_CTRL_REG1] = config[1];
	}

	return 0;

error:
	dev_err(&acc->client->dev, "update odr failed 0x%x,0x%x: %d\n",
			config[0], config[1], err);

	return err;
}

/* */

static int lis3dh_acc_register_write(struct lis3dh_acc_data *acc, u8 *buf,
		u8 reg_address, u8 new_value)
{
	int err = -1;

	if (atomic_read(&acc->enabled)) {
		/* Sets configuration register at reg_address
		 *  NOTE: this is a straight overwrite  */
		buf[0] = reg_address;
		buf[1] = new_value;
		err = lis3dh_acc_i2c_write(acc, buf, 1);
		if (err < 0)
			return err;
	}
	return err;
}

static int lis3dh_acc_register_read(struct lis3dh_acc_data *acc, u8 *buf,
		u8 reg_address)
{

	int err = -1;
	buf[0] = (reg_address);
	err = lis3dh_acc_i2c_read(acc, buf, 1);
	return err;
}

static int lis3dh_acc_register_update(struct lis3dh_acc_data *acc, u8 *buf,
		u8 reg_address, u8 mask, u8 new_bit_values)
{
	int err = -1;
	u8 init_val;
	u8 updated_val;
	err = lis3dh_acc_register_read(acc, buf, reg_address);
	if (!(err < 0)) {
		init_val = buf[1];
		updated_val = ((mask & new_bit_values) | ((~mask) & init_val));
		err = lis3dh_acc_register_write(acc, buf, reg_address,
				updated_val);
	}
	return err;
}

/* */

static int lis3dh_acc_get_acceleration_data(struct lis3dh_acc_data *acc,
		int *xyz)
{
	int err = -1;
	/* Data bytes from hardware xL, xH, yL, yH, zL, zH */
	u8 acc_data[6];
	/* x,y,z hardware data */
	s16 hw_d[3] = { 0 };

	acc_data[0] = (I2C_AUTO_INCREMENT | AXISDATA_REG);
	err = lis3dh_acc_i2c_read(acc, acc_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = (((s16) ((acc_data[1] << 8) | acc_data[0])) >> 4);
	hw_d[1] = (((s16) ((acc_data[3] << 8) | acc_data[2])) >> 4);
	hw_d[2] = (((s16) ((acc_data[5] << 8) | acc_data[4])) >> 4);

	hw_d[0] = hw_d[0] * acc->sensitivity;
	hw_d[1] = hw_d[1] * acc->sensitivity;
	hw_d[2] = hw_d[2] * acc->sensitivity;


	xyz[0] = ((acc->pdata->negate_x) ? (-hw_d[acc->pdata->axis_map_x])
		   : (hw_d[acc->pdata->axis_map_x]));
	xyz[1] = ((acc->pdata->negate_y) ? (-hw_d[acc->pdata->axis_map_y])
		   : (hw_d[acc->pdata->axis_map_y]));
	xyz[2] = ((acc->pdata->negate_z) ? (-hw_d[acc->pdata->axis_map_z])
		   : (hw_d[acc->pdata->axis_map_z]));

	#ifdef DEBUG
	/*
		printk(KERN_INFO "%s read x=%d, y=%d, z=%d\n",
			LIS3DH_ACC_DEV_NAME, xyz[0], xyz[1], xyz[2]);
	*/
	#endif
	return err;
}

static void lis3dh_acc_report_values(struct lis3dh_acc_data *acc, int *xyz)
{
	input_report_abs(acc->input_dev, ABS_X, xyz[0]);
	input_report_abs(acc->input_dev, ABS_Y, xyz[1]);
	input_report_abs(acc->input_dev, ABS_Z, xyz[2]);
	input_sync(acc->input_dev);
}

static int lis3dh_acc_enable(struct lis3dh_acc_data *acc)
{
	int err;

	if (!atomic_cmpxchg(&acc->enabled, 0, 1)) {
		err = lis3dh_acc_device_power_on(acc);
		if (err < 0) {
			atomic_set(&acc->enabled, 0);
			return err;
		}
		schedule_delayed_work(&acc->input_work, msecs_to_jiffies(
				acc->pdata->poll_interval));
	}

	return 0;
}

static int lis3dh_acc_disable(struct lis3dh_acc_data *acc)
{
	if (atomic_cmpxchg(&acc->enabled, 1, 0)) {
		cancel_delayed_work_sync(&acc->input_work);
		lis3dh_acc_device_power_off(acc);
	}

	return 0;
}

static int lis3dh_acc_misc_open(struct inode *inode, struct file *file)
{
	int err;
	err = nonseekable_open(inode, file);
	if (err < 0)
		return err;

	file->private_data = lis3dh_acc_misc_data;

	return 0;
}

static int lis3dh_acc_misc_ioctl(struct inode *inode, struct file *file,
		unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	u8 buf[4];
	u8 mask;
	u8 reg_address;
	u8 bit_values;
	int err;
	int interval;
	struct lis3dh_acc_data *acc = file->private_data;

	printk(KERN_INFO "%s: %s call with cmd 0x%x and arg 0x%x\n",
			LIS3DH_ACC_DEV_NAME, __func__, cmd, (unsigned int)arg);

	switch (cmd) {
	case LIS3DH_ACC_IOCTL_GET_DELAY:
		interval = acc->pdata->poll_interval;
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EFAULT;
		break;

	case LIS3DH_ACC_IOCTL_SET_DELAY:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval < 0 || interval > 1000)
			return -EINVAL;

		acc->pdata->poll_interval = max(interval,
				acc->pdata->min_interval);
		err = lis3dh_acc_update_odr(acc, acc->pdata->poll_interval);
		/* TODO: if update fails poll is still set */
		if (err < 0)
			return err;
		break;

	case LIS3DH_ACC_IOCTL_SET_ENABLE:
		if (copy_from_user(&interval, argp, sizeof(interval)))
			return -EFAULT;
		if (interval > 1)
			return -EINVAL;
		if (interval)
			err = lis3dh_acc_enable(acc);
		else
			err = lis3dh_acc_disable(acc);
		return err;
		break;

	case LIS3DH_ACC_IOCTL_GET_ENABLE:
		interval = atomic_read(&acc->enabled);
		if (copy_to_user(argp, &interval, sizeof(interval)))
			return -EINVAL;
		break;

	case LIS3DH_ACC_IOCTL_SET_G_RANGE:
		if (copy_from_user(buf, argp, 1))
			return -EFAULT;
		bit_values = buf[0];
		err = lis3dh_acc_update_g_range(acc, bit_values);
		if (err < 0)
			return err;
		break;

#ifdef INTERRUPT_MANAGEMENT
	case LIS3DH_ACC_IOCTL_SET_CTRL_REG3:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = CTRL_REG3;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_CTRL_REG3] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_CTRL_REG3]));
		break;

	case LIS3DH_ACC_IOCTL_SET_CTRL_REG6:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = CTRL_REG6;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_CTRL_REG6] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_CTRL_REG6]));
		break;

	case LIS3DH_ACC_IOCTL_SET_DURATION1:
		if (copy_from_user(buf, argp, 1))
			return -EFAULT;
		reg_address = INT_DUR1;
		mask = 0x7F;
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_DUR1] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_DUR1]));
		break;

	case LIS3DH_ACC_IOCTL_SET_THRESHOLD1:
		if (copy_from_user(buf, argp, 1))
			return -EFAULT;
		reg_address = INT_THS1;
		mask = 0x7F;
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_THS1] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_THS1]));
		break;

	case LIS3DH_ACC_IOCTL_SET_CONFIG1:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = INT_CFG1;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_CFG1] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_CFG1]));
		break;

	case LIS3DH_ACC_IOCTL_GET_SOURCE1:
		err = lis3dh_acc_register_read(acc, buf, INT_SRC1);
		if (err < 0)
			return err;
#if DEBUG
		printk(KERN_ALERT "INT1_SRC content: %d , 0x%x\n",
				buf[0], buf[0]);
#endif
		if (copy_to_user(argp, buf, 1))
			return -EINVAL;
		break;

	case LIS3DH_ACC_IOCTL_SET_DURATION2:
		if (copy_from_user(buf, argp, 1))
			return -EFAULT;
		reg_address = INT_DUR2;
		mask = 0x7F;
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_DUR2] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_DUR2]));
		break;

	case LIS3DH_ACC_IOCTL_SET_THRESHOLD2:
		if (copy_from_user(buf, argp, 1))
			return -EFAULT;
		reg_address = INT_THS2;
		mask = 0x7F;
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_THS2] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_THS2]));
		break;

	case LIS3DH_ACC_IOCTL_SET_CONFIG2:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = INT_CFG2;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_INT_CFG2] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_INT_CFG2]));
		break;

	case LIS3DH_ACC_IOCTL_GET_SOURCE2:
		err = lis3dh_acc_register_read(acc, buf, INT_SRC2);
		if (err < 0)
			return err;
#if DEBUG
		printk(KERN_ALERT "INT2_SRC content: %d , 0x%x\n",
				buf[0], buf[0]);
#endif
		if (copy_to_user(argp, buf, 1))
			return -EINVAL;
		break;

	case LIS3DH_ACC_IOCTL_GET_TAP_SOURCE:
		err = lis3dh_acc_register_read(acc, buf, TT_SRC);
		if (err < 0)
			return err;
#if DEBUG
		printk(KERN_ALERT "TT_SRC content: %d , 0x%x\n",
				buf[0], buf[0]);
#endif
		if (copy_to_user(argp, buf, 1)) {
			printk(KERN_ERR "%s: %s error in copy_to_user \n",
					LIS3DH_ACC_DEV_NAME, __func__);
			return -EINVAL;
		}
		break;

	case LIS3DH_ACC_IOCTL_SET_TAP_CFG:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = TT_CFG;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_TT_CFG] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_TT_CFG]));
		break;

	case LIS3DH_ACC_IOCTL_SET_TAP_TLIM:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = TT_LIM;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_TT_LIM] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_TT_LIM]));
		break;

	case LIS3DH_ACC_IOCTL_SET_TAP_THS:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = TT_THS;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_TT_THS] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_TT_THS]));
		break;

	case LIS3DH_ACC_IOCTL_SET_TAP_TLAT:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = TT_TLAT;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_TT_TLAT] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_TT_TLAT]));
		break;

	case LIS3DH_ACC_IOCTL_SET_TAP_TW:
		if (copy_from_user(buf, argp, 2))
			return -EFAULT;
		reg_address = TT_TW;
		mask = buf[1];
		bit_values = buf[0];
		err = lis3dh_acc_register_update(acc, (u8 *) arg, reg_address,
				mask, bit_values);
		if (err < 0)
			return err;
		acc->resume_state[RES_TT_TW] = ((mask & bit_values) |
				( ~mask & acc->resume_state[RES_TT_TW]));
		break;

#endif /* INTERRUPT_MANAGEMENT */

	default:
		return -EINVAL;
	}

	return 0;
}

static const struct file_operations lis3dh_acc_misc_fops = {
		.owner = THIS_MODULE,
		.open = lis3dh_acc_misc_open,
		.ioctl = lis3dh_acc_misc_ioctl,
};

static struct miscdevice lis3dh_acc_misc_device = {
		.minor = MISC_DYNAMIC_MINOR,
		.name = LIS3DH_ACC_DEV_NAME,
		.fops = &lis3dh_acc_misc_fops,
};

static void lis3dh_acc_input_work_func(struct work_struct *work)
{
	struct lis3dh_acc_data *acc;

	int xyz[3] = { 0 };
	int err;

	acc = container_of((struct delayed_work *)work,
			struct lis3dh_acc_data,	input_work);

	mutex_lock(&acc->lock);
	err = lis3dh_acc_get_acceleration_data(acc, xyz);
	if (err < 0)
		dev_err(&acc->client->dev, "get_acceleration_data failed\n");
	else
		lis3dh_acc_report_values(acc, xyz);

	schedule_delayed_work(&acc->input_work, msecs_to_jiffies(
			acc->pdata->poll_interval));
	mutex_unlock(&acc->lock);
}

#ifdef LIS3DH_OPEN_ENABLE
int lis3dh_acc_input_open(struct input_dev *input)
{
	struct lis3dh_acc_data *acc = input_get_drvdata(input);

	return lis3dh_acc_enable(acc);
}

void lis3dh_acc_input_close(struct input_dev *dev)
{
	struct lis3dh_acc_data *acc = input_get_drvdata(dev);

	lis3dh_acc_disable(acc);
}
#endif

static int lis3dh_acc_validate_pdata(struct lis3dh_acc_data *acc)
{
	acc->pdata->poll_interval = max(acc->pdata->poll_interval,
			acc->pdata->min_interval);

	if (acc->pdata->axis_map_x > 2 || acc->pdata->axis_map_y > 2
			|| acc->pdata->axis_map_z > 2) {
		dev_err(&acc->client->dev, "invalid axis_map value "
			"x:%u y:%u z%u\n", acc->pdata->axis_map_x,
				acc->pdata->axis_map_y, acc->pdata->axis_map_z);
		return -EINVAL;
	}

	/* Only allow 0 and 1 for negation boolean flag */
	if (acc->pdata->negate_x > 1 || acc->pdata->negate_y > 1
			|| acc->pdata->negate_z > 1) {
		dev_err(&acc->client->dev, "invalid negate value "
			"x:%u y:%u z:%u\n", acc->pdata->negate_x,
				acc->pdata->negate_y, acc->pdata->negate_z);
		return -EINVAL;
	}

	/* Enforce minimum polling interval */
	if (acc->pdata->poll_interval < acc->pdata->min_interval) {
		dev_err(&acc->client->dev, "minimum poll interval violated\n");
		return -EINVAL;
	}

	return 0;
}

static int lis3dh_acc_input_init(struct lis3dh_acc_data *acc)
{
	int err;

	INIT_DELAYED_WORK(&acc->input_work, lis3dh_acc_input_work_func);
	acc->input_dev = input_allocate_device();
	if (!acc->input_dev) {
		err = -ENOMEM;
		dev_err(&acc->client->dev, "input device allocate failed\n");
		goto err0;
	}

#ifdef LIS3DH_ACC_OPEN_ENABLE
	acc->input_dev->open = lis3dh_acc_input_open;
	acc->input_dev->close = lis3dh_acc_input_close;
#endif

	input_set_drvdata(acc->input_dev, acc);

	set_bit(EV_ABS, acc->input_dev->evbit);
	/*	next is used for interruptA sources data if the case */
	set_bit(ABS_MISC, acc->input_dev->absbit);
	/*	next is used for interruptB sources data if the case */
	set_bit(ABS_WHEEL, acc->input_dev->absbit);

	input_set_abs_params(acc->input_dev, ABS_X, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(acc->input_dev, ABS_Y, -G_MAX, G_MAX, FUZZ, FLAT);
	input_set_abs_params(acc->input_dev, ABS_Z, -G_MAX, G_MAX, FUZZ, FLAT);
	/*	next is used for interruptA sources data if the case */
	input_set_abs_params(acc->input_dev, ABS_MISC, INT_MIN, INT_MAX, 0, 0);
	/*	next is used for interruptB sources data if the case */
	input_set_abs_params(acc->input_dev, ABS_WHEEL, INT_MIN, INT_MAX, 0, 0);

	acc->input_dev->name = "accelerometer";

	err = input_register_device(acc->input_dev);
	if (err) {
		dev_err(&acc->client->dev,
				"unable to register input polled device %s\n",
				acc->input_dev->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(acc->input_dev);
err0:
	return err;
}

static void lis3dh_acc_input_cleanup(struct lis3dh_acc_data *acc)
{
	input_unregister_device(acc->input_dev);
	input_free_device(acc->input_dev);
}

static int lis3dh_acc_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{

	struct lis3dh_acc_data *acc;

	int err = -1;
	int tempvalue;

	pr_info("%s: probe start.\n", LIS3DH_ACC_DEV_NAME);

	if (client->dev.platform_data == NULL) {
		dev_err(&client->dev, "platform data is NULL. exiting.\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_err(&client->dev, "client not i2c capable\n");
		err = -ENODEV;
		goto exit_check_functionality_failed;
	}

	if (!i2c_check_functionality(client->adapter,
					I2C_FUNC_SMBUS_BYTE |
					I2C_FUNC_SMBUS_BYTE_DATA |
					I2C_FUNC_SMBUS_WORD_DATA)) {
		dev_err(&client->dev, "client not smb-i2c capable:2\n");
		err = -EIO;
		goto exit_check_functionality_failed;
	}


	if (!i2c_check_functionality(client->adapter,
						I2C_FUNC_SMBUS_I2C_BLOCK)){
		dev_err(&client->dev, "client not smb-i2c capable:3\n");
		err = -EIO;
		goto exit_check_functionality_failed;
	}
	/*
	 * OK. From now, we presume we have a valid client. We now create the
	 * client structure, even though we cannot fill it completely yet.
	 */

	acc = kzalloc(sizeof(struct lis3dh_acc_data), GFP_KERNEL);
	if (acc == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto exit_alloc_data_failed;
	}

	mutex_init(&acc->lock);
	mutex_lock(&acc->lock);

	acc->client = client;
	i2c_set_clientdata(client, acc);


	INIT_WORK(&acc->irq1_work, lis3dh_acc_irq1_work_func);
	acc->irq1_work_queue = create_singlethread_workqueue("lis3dh_acc_wq1");
	if (!acc->irq1_work_queue) {
		err = -ENOMEM;
		dev_err(&client->dev, "cannot create work queue1: %d\n", err);
		goto err_mutexunlockfreedata;
	}

	INIT_WORK(&acc->irq2_work, lis3dh_acc_irq2_work_func);
	acc->irq2_work_queue = create_singlethread_workqueue("lis3dh_acc_wq2");
	if (!acc->irq2_work_queue) {
		err = -ENOMEM;
		dev_err(&client->dev, "cannot create work queue2: %d\n", err);
		goto err_destoyworkqueue1;
	}



	if (i2c_smbus_read_byte(client) < 0) {
		printk(KERN_ERR "i2c_smbus_read_byte error!!\n");
		goto err_destoyworkqueue2;
	} else {
		printk(KERN_INFO "%s Device detected!\n", LIS3DH_ACC_DEV_NAME);
	}

	/* read chip id */

	tempvalue = i2c_smbus_read_word_data(client, WHO_AM_I);
	if ((tempvalue & 0x00FF) == WHOAMI_LIS3DH_ACC) {
		printk(KERN_INFO "%s I2C driver registered!\n",
							LIS3DH_ACC_DEV_NAME);
	} else {
		acc->client = NULL;
		printk(KERN_INFO "I2C driver not registered!"
				" Device unknown\n");
		goto err_destoyworkqueue2;
	}

	acc->pdata = kmalloc(sizeof(*acc->pdata), GFP_KERNEL);
	if (acc->pdata == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for pdata: %d\n",
				err);
		goto err_destoyworkqueue2;
	}

	memcpy(acc->pdata, client->dev.platform_data, sizeof(*acc->pdata));

	err = lis3dh_acc_validate_pdata(acc);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data\n");
		goto exit_kfree_pdata;
	}

	i2c_set_clientdata(client, acc);


	if (acc->pdata->init) {
		err = acc->pdata->init();
		if (err < 0) {
			dev_err(&client->dev, "init failed: %d\n", err);
			goto err2;
		}
	}

	memset(acc->resume_state, 0, ARRAY_SIZE(acc->resume_state));

	acc->irq1 = gpio_to_irq(acc->pdata->gpio_int1);
	printk(KERN_INFO "%s: %s has set irq1 to irq: %d mapped on gpio:%d\n",
		LIS3DH_ACC_DEV_NAME, __func__, acc->irq1,
							acc->pdata->gpio_int1);
	acc->irq2 = gpio_to_irq(acc->pdata->gpio_int2);
	printk(KERN_INFO "%s: %s has set irq2 to irq: %d mapped on gpio:%d\n",
		LIS3DH_ACC_DEV_NAME, __func__, acc->irq2,
							acc->pdata->gpio_int2);




	acc->resume_state[RES_CTRL_REG1] = LIS3DH_ACC_ENABLE_ALL_AXES;
	acc->resume_state[RES_CTRL_REG2] = 0x00;
	acc->resume_state[RES_CTRL_REG3] = 0x00;
	acc->resume_state[RES_CTRL_REG4] = 0x00;
	acc->resume_state[RES_CTRL_REG5] = 0x00;
	acc->resume_state[RES_CTRL_REG6] = 0x00;

	acc->resume_state[RES_TEMP_CFG_REG] = 0x00;
	acc->resume_state[RES_FIFO_CTRL_REG] = 0x00;
	acc->resume_state[RES_INT_CFG1] = 0x00;
	acc->resume_state[RES_INT_THS1] = 0x00;
	acc->resume_state[RES_INT_DUR1] = 0x00;
	acc->resume_state[RES_INT_CFG2] = 0x00;
	acc->resume_state[RES_INT_THS2] = 0x00;
	acc->resume_state[RES_INT_DUR2] = 0x00;

	acc->resume_state[RES_TT_CFG] = 0x00;
	acc->resume_state[RES_TT_THS] = 0x00;
	acc->resume_state[RES_TT_LIM] = 0x00;
	acc->resume_state[RES_TT_TLAT] = 0x00;
	acc->resume_state[RES_TT_TW] = 0x00;

	err = lis3dh_acc_device_power_on(acc);
	if (err < 0) {
		dev_err(&client->dev, "power on failed: %d\n", err);
		goto err2;
	}

	atomic_set(&acc->enabled, 1);

	err = lis3dh_acc_update_g_range(acc, acc->pdata->g_range);
	if (err < 0) {
		dev_err(&client->dev, "update_g_range failed\n");
		goto  err_power_off;
	}

	err = lis3dh_acc_update_odr(acc, acc->pdata->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr failed\n");
		goto  err_power_off;
	}

	err = lis3dh_acc_input_init(acc);
	if (err < 0) {
		dev_err(&client->dev, "input init failed\n");
		goto err_power_off;
	}
	lis3dh_acc_misc_data = acc;

	err = misc_register(&lis3dh_acc_misc_device);
	if (err < 0) {
		dev_err(&client->dev,
				"misc LIS3DH_ACC_DEV_NAME register failed\n");
		goto err_input_cleanup;
	}

	lis3dh_acc_device_power_off(acc);

	/* As default, do not report information */
	atomic_set(&acc->enabled, 0);

	err = request_irq(acc->irq1, lis3dh_acc_isr1, IRQF_TRIGGER_RISING,
			"lis3dh_acc_irq1", acc);
	if (err < 0) {
		dev_err(&client->dev, "request irq1 failed: %d\n", err);
		goto err_misc_dereg;
	}
	disable_irq_nosync(acc->irq1);

	err = request_irq(acc->irq2, lis3dh_acc_isr2, IRQF_TRIGGER_RISING,
			"lis3dh_acc_irq2", acc);
	if (err < 0) {
		dev_err(&client->dev, "request irq2 failed: %d\n", err);
		goto err_free_irq1;
	}
	disable_irq_nosync(acc->irq2);

	mutex_unlock(&acc->lock);

	dev_info(&client->dev, "%s: probed\n", LIS3DH_ACC_DEV_NAME);

	return 0;

err_free_irq1:
	free_irq(acc->irq1, acc);
err_misc_dereg:
	misc_deregister(&lis3dh_acc_misc_device);
err_input_cleanup:
	lis3dh_acc_input_cleanup(acc);
err_power_off:
	lis3dh_acc_device_power_off(acc);
err2:
	if (acc->pdata->exit)
		acc->pdata->exit();
exit_kfree_pdata:
	kfree(acc->pdata);
err_destoyworkqueue2:
	destroy_workqueue(acc->irq2_work_queue);
err_destoyworkqueue1:
	destroy_workqueue(acc->irq1_work_queue);
err_mutexunlockfreedata:
	mutex_unlock(&acc->lock);
	kfree(acc);
exit_alloc_data_failed:
exit_check_functionality_failed:
	printk(KERN_ERR "%s: Driver Init failed\n", LIS3DH_ACC_DEV_NAME);
	return err;
}

static int __devexit lis3dh_acc_remove(struct i2c_client *client)
{
	/* TODO: revisit ordering here once _probe order is finalized */
	struct lis3dh_acc_data *acc = i2c_get_clientdata(client);

	free_irq(acc->irq1, acc);
	free_irq(acc->irq2, acc);
	gpio_free(acc->pdata->gpio_int1);
	gpio_free(acc->pdata->gpio_int2);
	destroy_workqueue(acc->irq1_work_queue);
	destroy_workqueue(acc->irq2_work_queue);

	misc_deregister(&lis3dh_acc_misc_device);
	lis3dh_acc_input_cleanup(acc);
	lis3dh_acc_device_power_off(acc);
	if (acc->pdata->exit)
		acc->pdata->exit();
	kfree(acc->pdata);
	kfree(acc);

	return 0;
}

static int lis3dh_acc_resume(struct i2c_client *client)
{
	struct lis3dh_acc_data *acc = i2c_get_clientdata(client);

	if (acc->on_before_suspend)
		return lis3dh_acc_enable(acc);
	return 0;
}

static int lis3dh_acc_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct lis3dh_acc_data *acc = i2c_get_clientdata(client);

	acc->on_before_suspend = atomic_read(&acc->enabled);
	return lis3dh_acc_disable(acc);
}

static const struct i2c_device_id lis3dh_acc_id[]
				= { { LIS3DH_ACC_DEV_NAME, 0 }, { }, };

MODULE_DEVICE_TABLE(i2c, lis3dh_acc_id);

static struct i2c_driver lis3dh_acc_driver = {
	.driver = {
			.name = LIS3DH_ACC_DEV_NAME,
		  },
	.probe = lis3dh_acc_probe,
	.remove = __devexit_p(lis3dh_acc_remove),
	.resume = lis3dh_acc_resume,
	.suspend = lis3dh_acc_suspend,
	.id_table = lis3dh_acc_id,
};

static int __init lis3dh_acc_init(void)
{
	printk(KERN_INFO "%s accelerometer driver: init\n",
						LIS3DH_ACC_DEV_NAME);
	return i2c_add_driver(&lis3dh_acc_driver);
}

static void __exit lis3dh_acc_exit(void)
{
	#if DEBUG
	printk(KERN_INFO "%s accelerometer driver exit\n", LIS3DH_ACC_DEV_NAME);
	#endif
	i2c_del_driver(&lis3dh_acc_driver);
	return;
}

module_init(lis3dh_acc_init);
module_exit(lis3dh_acc_exit);

MODULE_DESCRIPTION("lis3dh accelerometer misc driver");
MODULE_AUTHOR("STMicroelectronics");
MODULE_LICENSE("GPL");

