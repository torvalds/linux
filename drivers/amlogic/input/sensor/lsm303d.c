/******************** (C) COPYRIGHT 2013 STMicroelectronics *******************
*
* File Name          : lsm303d.h
* Authors            : AMS - MSH Div - Application Team
*		     : Matteo Dameno (matteo.dameno@st.com)
*		     : Denis Ciocca (denis.ciocca@st.com)
*		     : Both authors are willing to be considered the contact
*		     : and update points for the driver.
* Version            : V.1.0.5
* Date               : 2013/Oct/23
* Description        : LSM303D accelerometer & magnetometer driver
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
******************************************************************************/
/******************************************************************************
Version History.

Revision 1-0-0 2012/05/04
 first revision
Revision 1-0-1 2012/05/07
 New sysfs architecture
 Support antialiasing filter
Revision 1-0-2 2012/10/15
 I2C address bugfix
Revision 1-0-3 2013/01/21
 Move CTLREG7 resume write from acc_power_on to magn_power_on
Revision 1-0-4 2013/05/09
 Added rotation matrix
Revision 1-0-5 2013/10/23
 Corrects Mag Enable bug, Corrects missing BDU enable
******************************************************************************/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/workqueue.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/kobject.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>

#include <linux/sensor/lsm303d.h>
#include <linux/sensor/sensor_common.h>
/*#include "lsm303d.h"*/


#define	I2C_AUTO_INCREMENT	(0x80)
#define MS_TO_NS(x)		(x*1000000L)

#define	ACC_G_MAX_POS		1495040	/** max positive value acc [ug] */
#define	ACC_G_MAX_NEG		1495770	/** max negative value acc [ug] */
#define	MAG_G_MAX_POS		983520	/** max positive value mag [ugauss] */
#define	MAG_G_MAX_NEG		983040	/** max negative value mag [ugauss] */

#define FUZZ			0
#define FLAT			0

/* Address registers */
#define REG_WHOAMI_ADDR		(0x0F)	/** Who am i address register */
#define REG_CNTRL0_ADDR		(0x1F)	/** CNTRL0 address register */
#define REG_CNTRL1_ADDR		(0x20)	/** CNTRL1 address register */
#define REG_CNTRL2_ADDR		(0x21)	/** CNTRL2 address register */
#define REG_CNTRL3_ADDR		(0x22)	/** CNTRL3 address register */
#define REG_CNTRL4_ADDR		(0x23)	/** CNTRL4 address register */
#define REG_CNTRL5_ADDR		(0x24)	/** CNTRL5 address register */
#define REG_CNTRL6_ADDR		(0x25)	/** CNTRL6 address register */
#define REG_CNTRL7_ADDR		(0x26)	/** CNTRL7 address register */

#define REG_ACC_DATA_ADDR	(0x28)	/** Acc. data low address register */
#define REG_MAG_DATA_ADDR	(0x08)	/** Mag. data low address register */
#define REG_TEMP_DATA_ADDR	(0x05)	/** Temp. data low address register */

#define REG_GEN_MAG_ADDR	(0x12)	/** INT_CTRL_REG_M address register */
#define INT_SRC_REG_M_ADDR	(0x13)	/** INT_SRC_REG_M address register */
#define REG_GEN_MAG_THR_ADDR	(0x14)	/** INT_THS_L_M address register */
#define MIG_THRESHOLD_ADDR_H	(0x15)	/** INT_THS_H_M address register */
#define REG_GEN1_AXIS_ADDR	(0x30)	/** INT_GEN1_REG address register */
#define INT_GEN1_SRC_ADDR	(0x31)	/** INT_GEN1_SRC address register */
#define REG_GEN1_THR_ADDR	(0x32)	/** INT_GEN1_THS address register */
#define REG_GEN1_DUR_ADDR	(0x33)	/** INT_GEN1_DUR address register */
#define REG_GEN2_AXIS_ADDR	(0x34)	/** INT_GEN2_REG address register */
#define INT_GEN2_SRC_ADDR	(0x35)	/** INT_GEN2_SRC address register */
#define REG_GEN2_THR_ADDR	(0x36)	/** INT_GEN2_THS address register */
#define REG_GEN2_DUR_ADDR	(0x37)	/** INT_GEN2_DUR address register */

/* Sensitivity */
#define SENSITIVITY_ACC_2G	60	/**	ug/LSB	*/
#define SENSITIVITY_ACC_4G	120	/**	ug/LSB	*/
#define SENSITIVITY_ACC_8G	240	/**	ug/LSB	*/
#define SENSITIVITY_ACC_16G	730	/**	ug/LSB	*/

#define SENSITIVITY_MAG_2G	80	/**	ugauss/LSB	*/
#define SENSITIVITY_MAG_4G	160	/**	ugauss/LSB	*/
#define SENSITIVITY_MAG_8G	320	/**	ugauss/LSB	*/
#define SENSITIVITY_MAG_12G	480	/**	ugauss/LSB	*/

/* ODR */
#define ODR_ACC_MASK		(0XF0)	/* Mask for odr change on acc */
#define LSM303D_ACC_ODR_OFF	(0x00)  /* Power down */
#define LSM303D_ACC_ODR3_125	(0x10)  /* 3.25Hz output data rate */
#define LSM303D_ACC_ODR6_25	(0x20)  /* 6.25Hz output data rate */
#define LSM303D_ACC_ODR12_5	(0x30)  /* 12.5Hz output data rate */
#define LSM303D_ACC_ODR25	(0x40)  /* 25Hz output data rate */
#define LSM303D_ACC_ODR50	(0x50)  /* 50Hz output data rate */
#define LSM303D_ACC_ODR100	(0x60)  /* 100Hz output data rate */
#define LSM303D_ACC_ODR200	(0x70)  /* 200Hz output data rate */
#define LSM303D_ACC_ODR400	(0x80)  /* 400Hz output data rate */
#define LSM303D_ACC_ODR800	(0x90)  /* 800Hz output data rate */
#define LSM303D_ACC_ODR1600	(0xA0)  /* 1600Hz output data rate */

#define ODR_MAG_MASK		(0X1C)	/* Mask for odr change on mag */
#define LSM303D_MAG_ODR3_125	(0x00)  /* 3.25Hz output data rate */
#define LSM303D_MAG_ODR6_25	(0x04)  /* 6.25Hz output data rate */
#define LSM303D_MAG_ODR12_5	(0x08)  /* 12.5Hz output data rate */
#define LSM303D_MAG_ODR25	(0x0C)  /* 25Hz output data rate */
#define LSM303D_MAG_ODR50	(0x10)  /* 50Hz output data rate */
#define LSM303D_MAG_ODR100	(0x14)  /* 100Hz output data rate */

/* Magnetic sensor mode */
#define MSMS_MASK		(0x03)	/* Mask magnetic sensor mode */
#define POWEROFF_MAG		(0x02)	/* Power Down */
#define CONTINUOS_CONVERSION	(0x00)	/* Continuos Conversion */

/* Default values loaded in probe function */
#define WHOIAM_VALUE		(0x49)	/** Who Am I default value */
#define REG_DEF_CNTRL0		(0x00)	/** CNTRL0 default value */
#define REG_DEF_CNTRL1		(0x0F)	/** CNTRL1 default value */
#define REG_DEF_CNTRL2		(0x00)	/** CNTRL2 default value */
#define REG_DEF_CNTRL3		(0x00)	/** CNTRL3 default value */
#define REG_DEF_CNTRL4		(0x00)	/** CNTRL4 default value */
#define REG_DEF_CNTRL5		(0x18)	/** CNTRL5 default value */
#define REG_DEF_CNTRL6		(0x20)	/** CNTRL6 default value */
#define REG_DEF_CNTRL7		(0x02)	/** CNTRL7 default value */

#define REG_DEF_INT_CNTRL_MAG	(0x00)	/** INT_CTRL_REG_M default value */
#define REG_DEF_INT_GEN1	(0x00)	/** INT_GEN1_REG default value */
#define REG_DEF_INT_GEN2	(0x00)	/** INT_GEN2_REG default value */
#define REG_DEF_IIG1_DURATION	(0x00)	/** INT_GEN1_DUR default value */
#define REG_DEF_IIG2_DURATION	(0x00)	/** INT_GEN2_DUR default value */
#define REG_DEF_IIG1_THRESHOLD	(0x00)	/** INT_GEN1_THS default value */
#define REG_DEF_IIG2_THRESHOLD	(0x00)	/** INT_GEN2_THS default value */
#define REG_DEF_MIG_THRESHOLD_L	(0x00)	/** INT_THS_L_M default value */
#define REG_DEF_MIG_THRESHOLD_H	(0x00)	/** INT_THS_H_M default value */

#define REG_DEF_ALL_ZEROS	(0x00)

/* Accelerometer Filter */
#define LSM303D_ACC_FILTER_MASK	(0xC0)	/* Mask for filter band change on acc */
#define FILTER_773		773	/* Anti-Aliasing 773 Hz */
#define FILTER_362		362	/* Anti-Aliasing 362 Hz */
#define FILTER_194		194	/* Anti-Aliasing 194 Hz */
#define FILTER_50		50	/* Anti-Aliasing 50 Hz */

/* Temperature */
#define TEMP_MASK		(0x80)	/* Mask for temperature change */
#define TEMP_ON			(0x80)	/* Enable temperature */
#define TEMP_OFF		(0x00)	/* Disable temperature */
#define TEMP_SENSITIVITY	8	/* Sensitivity temperature */
#define OFFSET_TEMP		25	/* Offset temperature */
#define NDTEMP			1000	/* Not Available temperature */

/* Interrupt */
#define GEN1_PIN1_MASK		(0x20)
#define GEN1_PIN2_MASK		(0x40)
#define GEN2_PIN1_MASK		(0x10)
#define GEN2_PIN2_MASK		(0x20)
#define GEN_MAG_PIN1_MASK	(0x08)
#define GEN_MAG_PIN2_MASK	(0x10)
#define GEN_MAG_EN_MASK		(0x01)
#define MAX_DUR_TH		127
#define MAX_TH_MAG		131071
#define GEN_X_HIGH_MASK		(0x02)
#define GEN_X_LOW_MASK		(0x01)
#define GEN_Y_HIGH_MASK		(0x08)
#define GEN_Y_LOW_MASK		(0x04)
#define GEN_Z_HIGH_MASK		(0x20)
#define GEN_Z_LOW_MASK		(0x10)
#define GEN_X_MAG_MASK		(0x80)
#define GEN_Y_MAG_MASK		(0x40)
#define GEN_Z_MAG_MASK		(0x20)

#define GEN1_AND_OR_MASK	(0x80)
#define GEN2_AND_OR_MASK	(0x83)

#define INT_PIN_CONF_MASK	(0x10)
#define INT_POLARITY_MASK	(0x80)

#define to_dev(obj) container_of(obj, struct device, kobj)
#define to_dev_attr(_attr) container_of(_attr, struct device_attribute, attr)

//static struct kobject *acc_kobj;
//static struct kobject *mag_kobj;

struct workqueue_struct *lsm303d_workqueue = 0;

struct {
	unsigned int cutoff_us;
	u8 value;
} lsm303d_acc_odr_table[] = {
		{   1, LSM303D_ACC_ODR800  },
		{   2, LSM303D_ACC_ODR400  },
		{   5, LSM303D_ACC_ODR200  },
		{  10, LSM303D_ACC_ODR100  },
		{  20, LSM303D_ACC_ODR50   },
		{  40, LSM303D_ACC_ODR25   },
		{  80, LSM303D_ACC_ODR12_5 },
		{ 160, LSM303D_ACC_ODR6_25 },
		{ 320, LSM303D_ACC_ODR3_125},
};

struct {
	unsigned int cutoff_us;
	u8 value;
} lsm303d_mag_odr_table[] = {
		{  10, LSM303D_MAG_ODR100  },
		{  20, LSM303D_MAG_ODR50   },
		{  40, LSM303D_MAG_ODR25   },
		{  80, LSM303D_MAG_ODR12_5 },
		{ 160, LSM303D_MAG_ODR6_25 },
		{ 320, LSM303D_MAG_ODR3_125},
};

struct interrupt_enable {
	atomic_t enable;
	u8 address;
	u8 mask;
};

struct interrupt_value {
	int value;
	u8 address;
};

struct lsm303d_interrupt {
	struct interrupt_enable gen1_pin1;
	struct interrupt_enable gen1_pin2;
	struct interrupt_enable gen2_pin1;
	struct interrupt_enable gen2_pin2;
	struct interrupt_value gen1_threshold;
	struct interrupt_value gen2_threshold;
	struct interrupt_value gen1_duration;
	struct interrupt_value gen2_duration;
	struct interrupt_enable gen_mag_pin1;
	struct interrupt_enable gen_mag_pin2;
	struct interrupt_enable gen_mag;
	struct interrupt_value gen_mag_threshold;
	struct interrupt_enable gen1_axis[6];
	struct interrupt_enable gen2_axis[6];
	struct interrupt_enable gen_mag_axis[3];
	struct interrupt_enable gen1_and_or;
	struct interrupt_enable gen2_and_or;
	struct interrupt_enable interrupt_pin_conf;
	struct interrupt_enable interrupt_polarity;
};

struct lsm303d_status {
	struct i2c_client *client;
	struct lsm303d_acc_platform_data *pdata_acc;
	struct lsm303d_mag_platform_data *pdata_mag;

	struct mutex lock;
	struct work_struct input_work_acc;
	struct work_struct input_work_mag;

	struct hrtimer hr_timer_acc;
	ktime_t ktime_acc;
	struct hrtimer hr_timer_mag;
	ktime_t ktime_mag;

	struct input_dev *input_dev_acc;
	struct input_dev *input_dev_mag;
	struct input_dev *input_dev_temp;

	struct lsm303d_interrupt *interrupt;

	int hw_initialized;
	/* hw_working=-1 means not tested yet */
	int hw_working;

	atomic_t enabled_acc;
	atomic_t enabled_mag;
	atomic_t enabled_temp;

	int temp_value_dec;
	unsigned int temp_value_flo;

	int on_before_suspend;
	int use_smbus;

	u16 sensitivity_acc;
	u16 sensitivity_mag;

	int irq1;
	struct work_struct irq1_work;
	struct workqueue_struct *irq1_work_queue;
	int irq2;
	struct work_struct irq2_work;
	struct workqueue_struct *irq2_work_queue;
};

static const struct lsm303d_acc_platform_data default_lsm303d_acc_pdata = {
	.fs_range = LSM303D_ACC_FS_2G,
	.rot_matrix = {
		{1, 0, 0},
		{0, 1, 0},
		{0, 0, 1},
	},
	.poll_interval = 100,
	.min_interval = LSM303D_ACC_MIN_POLL_PERIOD_MS,
	.aa_filter_bandwidth = ANTI_ALIASING_773,
	.gpio_int1 = DEFAULT_INT1_GPIO,
	.gpio_int2 = DEFAULT_INT2_GPIO,
};

static const struct lsm303d_mag_platform_data default_lsm303d_mag_pdata = {
	.poll_interval = 100,
	.min_interval = LSM303D_MAG_MIN_POLL_PERIOD_MS,
	.fs_range = LSM303D_MAG_FS_2G,
	.rot_matrix = {
		{1, 0, 0},
		{0, 1, 0},
		{0, 0, 1},
	},
};

struct reg_rw {
	u8 address;
	u8 default_value;
	u8 resume_value;
};

struct reg_r {
	u8 address;
	u8 value;
};

static struct status_registers {
	struct reg_r who_am_i;
	struct reg_rw cntrl0;
	struct reg_rw cntrl1;
	struct reg_rw cntrl2;
	struct reg_rw cntrl3;
	struct reg_rw cntrl4;
	struct reg_rw cntrl5;
	struct reg_rw cntrl6;
	struct reg_rw cntrl7;
	struct reg_rw int_ctrl_reg_m;
	struct reg_rw int_mag_threshold_low;
	struct reg_rw int_mag_threshold_high;
	struct reg_rw int_gen1_reg;
	struct reg_rw int_gen2_reg;
	struct reg_rw int_gen1_duration;
	struct reg_rw int_gen2_duration;
	struct reg_rw int_gen1_threshold;
	struct reg_rw int_gen2_threshold;
	struct reg_r int_src_reg_m;
	struct reg_r int_gen1_src;
	struct reg_r int_gen2_src;
	struct reg_r int_gen_mag_src;
} status_registers = {
	.who_am_i.address=REG_WHOAMI_ADDR, .who_am_i.value=WHOIAM_VALUE,
	.cntrl0.address=REG_CNTRL0_ADDR, .cntrl0.default_value=REG_DEF_CNTRL0,
	.cntrl1.address=REG_CNTRL1_ADDR, .cntrl1.default_value=REG_DEF_CNTRL1,
	.cntrl2.address=REG_CNTRL2_ADDR, .cntrl2.default_value=REG_DEF_CNTRL2,
	.cntrl3.address=REG_CNTRL3_ADDR, .cntrl3.default_value=REG_DEF_CNTRL3,
	.cntrl4.address=REG_CNTRL4_ADDR, .cntrl4.default_value=REG_DEF_CNTRL4,
	.cntrl5.address=REG_CNTRL5_ADDR, .cntrl5.default_value=REG_DEF_CNTRL5,
	.cntrl6.address=REG_CNTRL6_ADDR, .cntrl6.default_value=REG_DEF_CNTRL6,
	.cntrl7.address=REG_CNTRL7_ADDR, .cntrl7.default_value=REG_DEF_CNTRL7,
	.int_ctrl_reg_m.address=REG_GEN_MAG_ADDR, 
		.int_ctrl_reg_m.default_value=REG_DEF_INT_CNTRL_MAG,
	.int_mag_threshold_low.address=REG_GEN_MAG_THR_ADDR,
		.int_mag_threshold_low.default_value=REG_DEF_MIG_THRESHOLD_L,
	.int_mag_threshold_low.address=MIG_THRESHOLD_ADDR_H,
		.int_mag_threshold_low.default_value=REG_DEF_MIG_THRESHOLD_H,
	.int_gen1_reg.address=REG_GEN1_AXIS_ADDR, 
		.int_gen1_reg.default_value=REG_DEF_INT_GEN1,
	.int_gen2_reg.address=REG_GEN2_AXIS_ADDR, 
		.int_gen2_reg.default_value=REG_DEF_INT_GEN2,
	.int_gen1_duration.address=REG_GEN1_DUR_ADDR,
		.int_gen1_duration.default_value=REG_DEF_IIG1_DURATION,
	.int_gen2_duration.address=REG_GEN2_DUR_ADDR,
		.int_gen2_duration.default_value=REG_DEF_IIG2_DURATION,
	.int_gen1_threshold.address=REG_GEN1_THR_ADDR,
		.int_gen1_threshold.default_value=REG_DEF_IIG1_THRESHOLD,
	.int_gen2_threshold.address=REG_GEN2_THR_ADDR,
		.int_gen2_threshold.default_value=REG_DEF_IIG2_THRESHOLD,
	.int_src_reg_m.address = INT_SRC_REG_M_ADDR,
				.int_src_reg_m.value = REG_DEF_ALL_ZEROS,
	.int_gen1_src.address = INT_GEN1_SRC_ADDR,
				.int_gen1_src.value = REG_DEF_ALL_ZEROS,
	.int_gen2_src.address = INT_GEN2_SRC_ADDR,
				.int_gen2_src.value = REG_DEF_ALL_ZEROS,
	.int_gen_mag_src.address = INT_SRC_REG_M_ADDR,
				.int_gen_mag_src.value = REG_DEF_ALL_ZEROS,
};

static int lsm303d_i2c_read(struct lsm303d_status *stat, u8 *buf, int len)
{
	int ret;
	u8 reg = buf[0];
	u8 cmd = reg;
#ifdef DEBUG
	unsigned int ii;
#endif


	if (len > 1)
		cmd = (I2C_AUTO_INCREMENT | reg);
	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_read_byte_data(stat->client, cmd);
			buf[0] = ret & 0xff;
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_read_byte_data: ret=0x%02x, len:%d ,"
				"command=0x%02x, buf[0]=0x%02x\n",
				ret, len, cmd , buf[0]);
#endif
		} else if (len > 1) {
			ret = i2c_smbus_read_i2c_block_data(stat->client,
								cmd, len, buf);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_read_i2c_block_data: ret:%d len:%d, "
				"command=0x%02x, ",
				ret, len, cmd);
			for (ii = 0; ii < len; ii++)
				printk(KERN_DEBUG "buf[%d]=0x%02x,",
								ii, buf[ii]);

			printk("\n");
#endif
		} else
			ret = -1;

		if (ret < 0) {
			dev_err(&stat->client->dev,
				"read transfer error: len:%d, command=0x%02x\n",
				len, cmd);
			return 0;
		}
		return len;
	}

	ret = i2c_master_send(stat->client, &cmd, sizeof(cmd));
	if (ret != sizeof(cmd))
		return ret;

	return i2c_master_recv(stat->client, buf, len);
}

static int lsm303d_i2c_write(struct lsm303d_status *stat, u8 *buf, int len)
{
	int ret;
	u8 reg, value;
#ifdef DEBUG
	unsigned int ii;
#endif

	if (len > 1)
		buf[0] = (I2C_AUTO_INCREMENT | buf[0]);

	reg = buf[0];
	value = buf[1];

	if (stat->use_smbus) {
		if (len == 1) {
			ret = i2c_smbus_write_byte_data(stat->client,
								reg, value);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_write_byte_data: ret=%d, len:%d, "
				"command=0x%02x, value=0x%02x\n",
				ret, len, reg , value);
#endif
			return ret;
		} else if (len > 1) {
			ret = i2c_smbus_write_i2c_block_data(stat->client,
							reg, len, buf + 1);
#ifdef DEBUG
			dev_warn(&stat->client->dev,
				"i2c_smbus_write_i2c_block_data: ret=%d, "
				"len:%d, command=0x%02x, ",
				ret, len, reg);
			for (ii = 0; ii < (len + 1); ii++)
				printk(KERN_DEBUG "value[%d]=0x%02x,",
								ii, buf[ii]);

			printk("\n");
#endif
			return ret;
		}
	}

	ret = i2c_master_send(stat->client, buf, len+1);
	return (ret == len+1) ? 0 : ret;
}

static int lsm303d_hw_init(struct lsm303d_status *stat)
{
	int err = -1;
	u8 buf[1];
	int i;

	pr_info("%s: hw init start\n", LSM303D_DEV_NAME);

	buf[0] = status_registers.who_am_i.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	
	if (err < 0) {
		dev_warn(&stat->client->dev, "Error reading WHO_AM_I: is device"
		" available/working?\n");
		goto err_firstread;
	} else
		stat->hw_working = 1;

	if (buf[0] != status_registers.who_am_i.value) {
	dev_err(&stat->client->dev,
		"device unknown. Expected: 0x%02x,"
		" Replies: 0x%02x\n", status_registers.who_am_i.value, buf[0]);
		err = -1;
		goto err_unknown_device;
	}

	status_registers.cntrl1.resume_value = 
					status_registers.cntrl1.default_value;
	status_registers.cntrl2.resume_value = 
					status_registers.cntrl2.default_value;
	status_registers.cntrl3.resume_value = 
					status_registers.cntrl3.default_value;
	status_registers.cntrl4.resume_value = 
					status_registers.cntrl4.default_value;
	status_registers.cntrl5.resume_value = 
					status_registers.cntrl5.default_value;
	status_registers.cntrl6.resume_value = 
					status_registers.cntrl6.default_value;
	status_registers.cntrl7.resume_value = 
					status_registers.cntrl7.default_value;

	status_registers.int_ctrl_reg_m.resume_value = 
			status_registers.int_ctrl_reg_m.default_value;
	status_registers.int_mag_threshold_low.resume_value = 
			status_registers.int_mag_threshold_low.default_value;
	status_registers.int_mag_threshold_high.resume_value = 
			status_registers.int_mag_threshold_high.default_value;
	status_registers.int_gen1_reg.resume_value = 
			status_registers.int_gen1_reg.default_value;
	status_registers.int_gen2_reg.resume_value = 
			status_registers.int_gen2_reg.default_value;
	status_registers.int_gen1_duration.resume_value = 
			status_registers.int_gen1_duration.default_value;
	status_registers.int_gen2_duration.resume_value = 
			status_registers.int_gen2_duration.default_value;
	status_registers.int_gen1_threshold.resume_value = 
			status_registers.int_gen1_threshold.default_value;
	status_registers.int_gen2_threshold.resume_value = 
			status_registers.int_gen2_threshold.default_value;


	stat->temp_value_dec = NDTEMP;

	if((stat->pdata_acc->gpio_int1 >= 0) || 
					(stat->pdata_acc->gpio_int2 >= 0)) {

		stat->interrupt = kmalloc(sizeof(*stat->interrupt), 
								GFP_KERNEL);

		if(stat->interrupt == NULL)
			goto error_interrupt;

		stat->interrupt->gen1_pin1.address = REG_CNTRL3_ADDR;
		stat->interrupt->gen1_pin2.address = REG_CNTRL4_ADDR;
		stat->interrupt->gen2_pin1.address = REG_CNTRL3_ADDR;
		stat->interrupt->gen2_pin2.address = REG_CNTRL4_ADDR;
		stat->interrupt->gen_mag_pin1.address = REG_CNTRL3_ADDR;
		stat->interrupt->gen_mag_pin2.address = REG_CNTRL4_ADDR;
		stat->interrupt->gen_mag.address = REG_GEN_MAG_ADDR;
		stat->interrupt->gen1_duration.address = REG_GEN1_DUR_ADDR;
		stat->interrupt->gen2_duration.address = REG_GEN2_DUR_ADDR;
		stat->interrupt->gen1_threshold.address = REG_GEN1_THR_ADDR;
		stat->interrupt->gen2_threshold.address = REG_GEN2_THR_ADDR;
		stat->interrupt->gen_mag_threshold.address = 
							REG_GEN_MAG_THR_ADDR;

		stat->interrupt->gen1_pin1.mask = GEN1_PIN1_MASK;
		stat->interrupt->gen1_pin2.mask = GEN1_PIN2_MASK;
		stat->interrupt->gen2_pin1.mask = GEN2_PIN1_MASK;
		stat->interrupt->gen2_pin2.mask = GEN2_PIN2_MASK;
		stat->interrupt->gen_mag_pin1.mask = GEN_MAG_PIN1_MASK;
		stat->interrupt->gen_mag_pin2.mask = GEN_MAG_PIN2_MASK;
		stat->interrupt->gen_mag.mask = GEN_MAG_EN_MASK;

		atomic_set(&stat->interrupt->gen1_pin1.enable, 0);
		atomic_set(&stat->interrupt->gen1_pin2.enable, 0);
		atomic_set(&stat->interrupt->gen2_pin1.enable, 0);
		atomic_set(&stat->interrupt->gen2_pin2.enable, 0);
		atomic_set(&stat->interrupt->gen_mag_pin1.enable, 0);
		atomic_set(&stat->interrupt->gen_mag_pin2.enable, 0);
		atomic_set(&stat->interrupt->gen_mag.enable, 0);

		stat->interrupt->gen1_threshold.value = 0;
		stat->interrupt->gen2_threshold.value = 0;
		stat->interrupt->gen1_duration.value = 0;
		stat->interrupt->gen2_duration.value = 0;
		stat->interrupt->gen_mag_threshold.value = 0;

		for(i=0; i<6; i++) {
			stat->interrupt->gen1_axis[i].address = 
							REG_GEN1_AXIS_ADDR;
			stat->interrupt->gen2_axis[i].address = 
							REG_GEN2_AXIS_ADDR;

			atomic_set(&stat->interrupt->gen1_axis[i].enable, 0);
			atomic_set(&stat->interrupt->gen2_axis[i].enable, 0);
		}
		for(i=0; i<3; i++) {
			stat->interrupt->gen_mag_axis[i].address = 
							REG_GEN_MAG_ADDR;
			atomic_set(&stat->interrupt->gen_mag_axis[i].enable, 0);
		}

		stat->interrupt->gen1_axis[0].mask = GEN_X_LOW_MASK;
		stat->interrupt->gen1_axis[1].mask = GEN_Y_LOW_MASK;
		stat->interrupt->gen1_axis[2].mask = GEN_Z_LOW_MASK;
		stat->interrupt->gen1_axis[3].mask = GEN_X_HIGH_MASK;
		stat->interrupt->gen1_axis[4].mask = GEN_Y_HIGH_MASK;
		stat->interrupt->gen1_axis[5].mask = GEN_Z_HIGH_MASK;

		stat->interrupt->gen2_axis[0].mask = GEN_X_LOW_MASK;
		stat->interrupt->gen2_axis[1].mask = GEN_Y_LOW_MASK;
		stat->interrupt->gen2_axis[2].mask = GEN_Z_LOW_MASK;
		stat->interrupt->gen2_axis[3].mask = GEN_X_HIGH_MASK;
		stat->interrupt->gen2_axis[4].mask = GEN_Y_HIGH_MASK;
		stat->interrupt->gen2_axis[5].mask = GEN_Z_HIGH_MASK;

		stat->interrupt->gen_mag_axis[0].mask = GEN_X_MAG_MASK;
		stat->interrupt->gen_mag_axis[1].mask = GEN_Y_MAG_MASK;
		stat->interrupt->gen_mag_axis[2].mask = GEN_Z_MAG_MASK;

		stat->interrupt->gen1_and_or.address = REG_GEN1_AXIS_ADDR;
		stat->interrupt->gen1_and_or.mask = GEN1_AND_OR_MASK;
		atomic_set(&stat->interrupt->gen1_and_or.enable, 0);
		stat->interrupt->gen2_and_or.address = REG_GEN1_DUR_ADDR;
		stat->interrupt->gen2_and_or.mask = GEN2_AND_OR_MASK;
		atomic_set(&stat->interrupt->gen2_and_or.enable, 0);

		stat->interrupt->interrupt_pin_conf.address = REG_GEN_MAG_ADDR;
		stat->interrupt->interrupt_pin_conf.mask = INT_PIN_CONF_MASK;
		atomic_set(&stat->interrupt->interrupt_pin_conf.enable, 0);

		stat->interrupt->interrupt_polarity.address = REG_GEN_MAG_ADDR;
		stat->interrupt->interrupt_polarity.mask = INT_POLARITY_MASK;
		atomic_set(&stat->interrupt->interrupt_polarity.enable, 0);
	}

	stat->hw_initialized = 1;
	pr_info("%s: hw init done\n", LSM303D_DEV_NAME);

	return 0;

error_interrupt:
err_unknown_device:
err_firstread:
	stat->hw_working = 0;
	stat->hw_initialized = 0;
	return err;
}

static irqreturn_t lsm303d_isr1(int irq, void *dev)
{
	struct lsm303d_status *stat = dev;

	disable_irq_nosync(irq);
	queue_work(stat->irq1_work_queue, &stat->irq1_work);
	pr_debug("%s: isr1 queued\n", LSM303D_DEV_NAME);
	return IRQ_HANDLED;
}

static irqreturn_t lsm303d_isr2(int irq, void *dev)
{
	struct lsm303d_status *stat = dev;

	disable_irq_nosync(irq);
	queue_work(stat->irq2_work_queue, &stat->irq2_work);
	pr_debug("%s: isr2 queued\n", LSM303D_DEV_NAME);
	return IRQ_HANDLED;
}

static void lsm303d_interrupt_catch(struct lsm303d_status *stat, int pin ) 
{
	u8 buf[2];
	u8 val;

	if(atomic_read(&stat->interrupt->gen1_pin1.enable) == 1) {
		buf[0] = status_registers.int_gen1_src.address;
		val = lsm303d_i2c_read(stat, buf, 1);
		if(val < 0)
			return;
		status_registers.int_gen1_src.value = buf[0];

		if(((int)status_registers.int_gen1_src.value) > 64)
			pr_info("interrupt send by accelerometer interrupt "
							"generator 1\n");
	}
	if(atomic_read(&stat->interrupt->gen_mag_pin1.enable) == 1) {
		buf[0] = status_registers.int_gen_mag_src.address;
		val = lsm303d_i2c_read(stat, buf, 1);
		if(val < 0)
			return;
		status_registers.int_gen_mag_src.value = buf[0];

		if(((int)status_registers.int_gen_mag_src.value) > 1)
			pr_info("interrupt send by magnetometer interrupt "
								"generator\n");
	}

}

static void lsm303d_irq1_work_func(struct work_struct *work)
{

	struct lsm303d_status *stat =
	container_of(work, struct lsm303d_status, irq1_work);
	/* TODO  add interrupt service procedure.
		 ie:lsm303d_get_int1_source(stat); */
	
	lsm303d_interrupt_catch(stat,1);
	pr_info("%s: IRQ1 triggered\n", LSM303D_DEV_NAME);
	enable_irq(stat->irq1);
}

static void lsm303d_irq2_work_func(struct work_struct *work)
{

	struct lsm303d_status *stat =
	container_of(work, struct lsm303d_status, irq2_work);
	/* TODO  add interrupt service procedure.
		 ie:lsm303d_get_int2_source(stat); */
	
	lsm303d_interrupt_catch(stat,2);
	pr_info("%s: IRQ2 triggered\n", LSM303D_DEV_NAME);
	enable_irq(stat->irq2);
}

static int lsm303d_acc_device_power_off(struct lsm303d_status *stat)
{
	int err;
	u8 buf[2];

	buf[0] = status_registers.cntrl1.address;
	buf[1] = ((ODR_ACC_MASK & LSM303D_ACC_ODR_OFF) | 
		((~ODR_ACC_MASK) & status_registers.cntrl1.resume_value));

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		dev_err(&stat->client->dev, "accelerometer soft power off "
							"failed: %d\n", err);

	if (stat->pdata_acc->power_off) {
		stat->pdata_acc->power_off();
	}
	
	atomic_set(&stat->enabled_acc, 0);

	return 0;
}

static int lsm303d_mag_device_power_off(struct lsm303d_status *stat)
{
	int err;
	u8 buf[2];

	buf[0] = status_registers.cntrl7.address;
	buf[1] = ((MSMS_MASK & POWEROFF_MAG) | 
		((~MSMS_MASK) & status_registers.cntrl7.resume_value));

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		dev_err(&stat->client->dev, "magnetometer soft power off "
							"failed: %d\n", err);

	if (stat->pdata_mag->power_off) {
		stat->pdata_mag->power_off();
	}

	atomic_set(&stat->enabled_mag, 0);

	return 0;
}

static int lsm303d_acc_device_power_on(struct lsm303d_status *stat)
{
	int err = -1;
	u8 buf[5];

	if (stat->pdata_acc->power_on) {
		err = stat->pdata_acc->power_on();
		if (err < 0) {
			dev_err(&stat->client->dev,
				"accelerometer power_on failed: %d\n", err);
			return err;
		}
	}

	buf[0] = status_registers.cntrl0.address;
	buf[1] = status_registers.cntrl0.resume_value;
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.cntrl1.address;
	buf[1] = status_registers.cntrl1.resume_value;
	buf[2] = status_registers.cntrl2.resume_value;
	buf[3] = status_registers.cntrl3.resume_value;
	buf[4] = status_registers.cntrl4.resume_value;
	err = lsm303d_i2c_write(stat, buf, 4);
	if (err < 0)
		goto err_resume_state;	

	buf[0] = status_registers.int_gen1_reg.address;
	buf[1] = status_registers.int_gen1_reg.resume_value;
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.int_gen1_threshold.address;
	buf[1] = status_registers.int_gen1_threshold.resume_value;
	buf[2] = status_registers.int_gen1_duration.resume_value;
	err = lsm303d_i2c_write(stat, buf, 2);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.int_gen2_reg.address;
	buf[1] = status_registers.int_gen2_reg.resume_value;
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.int_gen2_threshold.address;
	buf[1] = status_registers.int_gen2_threshold.resume_value;
	buf[2] = status_registers.int_gen2_duration.resume_value;
	err = lsm303d_i2c_write(stat, buf, 2);
	if (err < 0)
		goto err_resume_state;

	atomic_set(&stat->enabled_acc, 1);

	return 0;

err_resume_state:
	atomic_set(&stat->enabled_acc, 0);
	dev_err(&stat->client->dev, "accelerometer hw power on error "
				"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm303d_mag_device_power_on(struct lsm303d_status *stat)
{
	int err = -1;
	u8 buf[6];

	if (stat->pdata_mag->power_on) {
		err = stat->pdata_mag->power_on();
		if (err < 0) {
			dev_err(&stat->client->dev,
				"magnetometer power_on failed: %d\n", err);
			return err;
		}
	}
	
	buf[0] = status_registers.cntrl0.address;
	buf[1] = status_registers.cntrl0.resume_value;
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.cntrl3.address;
	buf[1] = status_registers.cntrl3.resume_value;
	buf[2] = status_registers.cntrl4.resume_value;
	buf[3] = status_registers.cntrl5.resume_value;
	buf[4] = status_registers.cntrl6.resume_value;

	err = lsm303d_i2c_write(stat, buf, 4);
	if (err < 0)
		goto err_resume_state;	

	buf[0] = status_registers.int_ctrl_reg_m.address;
	buf[1] = status_registers.int_ctrl_reg_m.resume_value;
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.int_mag_threshold_low.address;
	buf[1] = status_registers.int_mag_threshold_low.resume_value;
	buf[2] = status_registers.int_mag_threshold_high.resume_value;
	err = lsm303d_i2c_write(stat, buf, 2);
	if (err < 0)
		goto err_resume_state;

	buf[0] = status_registers.cntrl7.address;
	buf[1] = ((MSMS_MASK & CONTINUOS_CONVERSION) |
			((~MSMS_MASK) & status_registers.cntrl7.resume_value));
	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto err_resume_state;

	atomic_set(&stat->enabled_mag, 1);

	return 0;

err_resume_state:
	atomic_set(&stat->enabled_mag, 0);
	dev_err(&stat->client->dev, "magnetometer hw power on error "
				"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm303d_acc_update_filter(struct lsm303d_status *stat, 
							u8 new_bandwidth)
{
	int err=-1;

	u8 updated_val;
	u8 buf[2];

	switch (new_bandwidth) {
	case ANTI_ALIASING_50:
		break;
	case ANTI_ALIASING_194:
		break;
	case ANTI_ALIASING_362:
		break;
	case ANTI_ALIASING_773:
		break;
	default:
		dev_err(&stat->client->dev, "invalid accelerometer "
			"update bandwidth requested: %u\n", new_bandwidth);
		return -EINVAL;
	}

	buf[0] = status_registers.cntrl2.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.cntrl2.resume_value = buf[0];	
	updated_val = ((LSM303D_ACC_FILTER_MASK & new_bandwidth) | 
					((~LSM303D_ACC_FILTER_MASK) & buf[0]));
	buf[1] = updated_val;
	buf[0] = status_registers.cntrl2.address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.cntrl2.resume_value = updated_val;

	return err;
	
error:
	dev_err(&stat->client->dev, "update accelerometer fs range failed "
		"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm303d_acc_update_fs_range(struct lsm303d_status *stat, 
								u8 new_fs_range)
{
	int err=-1;

	u16 sensitivity;
	u8 updated_val;
	u8 buf[2];

	switch (new_fs_range) {
	case LSM303D_ACC_FS_2G:
		sensitivity = SENSITIVITY_ACC_2G;
		break;
	case LSM303D_ACC_FS_4G:
		sensitivity = SENSITIVITY_ACC_4G;
		break;
	case LSM303D_ACC_FS_8G:
		sensitivity = SENSITIVITY_ACC_8G;
		break;
	case LSM303D_ACC_FS_16G:
		sensitivity = SENSITIVITY_ACC_16G;
		break;
	default:
		dev_err(&stat->client->dev, "invalid accelerometer "
				"fs range requested: %u\n", new_fs_range);
		return -EINVAL;
	}

	buf[0] = status_registers.cntrl2.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.cntrl2.resume_value = buf[0];	
	updated_val = ((LSM303D_ACC_FS_MASK & new_fs_range) | 
					((~LSM303D_ACC_FS_MASK) & buf[0]));
	buf[1] = updated_val;
	buf[0] = status_registers.cntrl2.address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.cntrl2.resume_value = updated_val;
	stat->sensitivity_acc = sensitivity;

	return err;
	
error:
	dev_err(&stat->client->dev, "update accelerometer fs range failed "
		"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm303d_mag_update_fs_range(struct lsm303d_status *stat, 
								u8 new_fs_range)
{
	int err=-1;

	u16 sensitivity;
	u8 updated_val;
	u8 buf[2];

	switch (new_fs_range) {
	case LSM303D_MAG_FS_2G:
		sensitivity = SENSITIVITY_MAG_2G;
		break;
	case LSM303D_MAG_FS_4G:
		sensitivity = SENSITIVITY_MAG_4G;
		break;
	case LSM303D_MAG_FS_8G:
		sensitivity = SENSITIVITY_MAG_8G;
		break;
	case LSM303D_MAG_FS_12G:
		sensitivity = SENSITIVITY_MAG_12G;
		break;
	default:
		dev_err(&stat->client->dev, "invalid magnetometer "
				"fs range requested: %u\n", new_fs_range);
		return -EINVAL;
	}

	buf[0] = status_registers.cntrl6.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.cntrl6.resume_value = buf[0];	
	updated_val = (LSM303D_MAG_FS_MASK & new_fs_range);
	buf[1] = updated_val;
	buf[0] = status_registers.cntrl6.address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.cntrl6.resume_value = updated_val;
	stat->sensitivity_mag = sensitivity;

	return err;
	
error:
	dev_err(&stat->client->dev, "update magnetometer fs range failed "
		"0x%02x,0x%02x: %d\n", buf[0], buf[1], err);
	return err;
}

static int lsm303d_acc_update_odr(struct lsm303d_status *stat,
						unsigned int poll_interval_ms)
{
	int err = -1;
	u8 config[2];
	int i;

	for (i = ARRAY_SIZE(lsm303d_acc_odr_table) - 1; i >= 0; i--) {
		if ((lsm303d_acc_odr_table[i].cutoff_us <= poll_interval_ms)
								|| (i == 0))
			break;
	}

	config[1] = ((ODR_ACC_MASK & lsm303d_acc_odr_table[i].value) | 
		((~ODR_ACC_MASK) & status_registers.cntrl1.resume_value));

	if (atomic_read(&stat->enabled_acc)) {
		config[0] = status_registers.cntrl1.address;
		err = lsm303d_i2c_write(stat, config, 1);
		if (err < 0)
			goto error;
		status_registers.cntrl1.resume_value = config[1];
		stat->ktime_acc = ktime_set(0, MS_TO_NS(poll_interval_ms));
	}

	return err;

error:
	dev_err(&stat->client->dev, "update accelerometer odr failed "
			"0x%02x,0x%02x: %d\n", config[0], config[1], err);

	return err;
}

static int lsm303d_mag_update_odr(struct lsm303d_status *stat,
						unsigned int poll_interval_ms)
{
	int err = -1;
	u8 config[2];
	int i;

	for (i = ARRAY_SIZE(lsm303d_mag_odr_table) - 1; i >= 0; i--) {
		if ((lsm303d_mag_odr_table[i].cutoff_us <= poll_interval_ms)
								|| (i == 0))
			break;
	}

	config[1] = ((ODR_MAG_MASK & lsm303d_mag_odr_table[i].value) | 
		((~ODR_MAG_MASK) & status_registers.cntrl5.resume_value));

	if (atomic_read(&stat->enabled_mag)) {
		config[0] = status_registers.cntrl5.address;
		err = lsm303d_i2c_write(stat, config, 1);
		if (err < 0)
			goto error;
		status_registers.cntrl5.resume_value = config[1];
		stat->ktime_mag = ktime_set(0, MS_TO_NS(poll_interval_ms));
	}

	return err;

error:
	dev_err(&stat->client->dev, "update magnetometer odr failed "
			"0x%02x,0x%02x: %d\n", config[0], config[1], err);

	return err;
}

static void lsm303d_validate_polling(unsigned int *min_interval,
					unsigned int *poll_interval, 
					unsigned int min,
					struct i2c_client *client)
{
	*min_interval = max(min, *min_interval);
	*poll_interval = max(*poll_interval, *min_interval);
}

static int lsm303d_acc_validate_pdata(struct lsm303d_status *stat)
{
	int res = -EINVAL;

	lsm303d_validate_polling(&stat->pdata_acc->min_interval,
				&stat->pdata_acc->poll_interval,
				(unsigned int)LSM303D_ACC_MIN_POLL_PERIOD_MS,
				stat->client);

	switch (stat->pdata_acc->aa_filter_bandwidth) {
		case ANTI_ALIASING_50:
			res = 1;
			break;
		case ANTI_ALIASING_194:
			res = 1;
			break;
		case ANTI_ALIASING_362:
			res = 1;
			break;
		case ANTI_ALIASING_773:
			res = 1;
			break;
		default:
			dev_err(&stat->client->dev, "invalid accelerometer "
				"bandwidth selected: %u\n", 
					stat->pdata_acc->aa_filter_bandwidth);
	}

	return res;
}

static int lsm303d_mag_validate_pdata(struct lsm303d_status *stat)
{
	lsm303d_validate_polling(&stat->pdata_mag->min_interval,
				&stat->pdata_mag->poll_interval,
				(unsigned int)LSM303D_MAG_MIN_POLL_PERIOD_MS,
				stat->client);

	return 0;
}

static int lsm303d_acc_enable(struct lsm303d_status *stat)
{
	int err;

	if (!atomic_cmpxchg(&stat->enabled_acc, 0, 1)) {
		err = lsm303d_acc_device_power_on(stat);
		if (err < 0) {
			atomic_set(&stat->enabled_acc, 0);
			return err;
		}
		hrtimer_start(&stat->hr_timer_acc, stat->ktime_acc, HRTIMER_MODE_REL);
		if(!atomic_read(&stat->enabled_mag)) {
			if(stat->pdata_acc->gpio_int1 >= 0)
				enable_irq(stat->irq1);
			if(stat->pdata_acc->gpio_int2 >= 0)
				enable_irq(stat->irq2);
		}
	}

	return 0;
}

static int lsm303d_acc_disable(struct lsm303d_status *stat)
{
	if (atomic_cmpxchg(&stat->enabled_acc, 1, 0)) {
		cancel_work_sync(&stat->input_work_acc);
		hrtimer_cancel(&stat->hr_timer_acc);
		lsm303d_acc_device_power_off(stat);

		if(!atomic_read(&stat->enabled_mag)) {
			if(stat->pdata_acc->gpio_int1 >= 0)
				disable_irq_nosync(stat->irq1);
			if(stat->pdata_acc->gpio_int2 >= 0)
				disable_irq_nosync(stat->irq2);
		}
	}

	return 0;
}

static int lsm303d_mag_enable(struct lsm303d_status *stat)
{
	int err;

	if (!atomic_cmpxchg(&stat->enabled_mag, 0, 1)) {
		err = lsm303d_mag_device_power_on(stat);
		if (err < 0) {
			atomic_set(&stat->enabled_mag, 0);
			return err;
		}
		if(!atomic_read(&stat->enabled_temp)) {
			hrtimer_start(&stat->hr_timer_mag, stat->ktime_mag, HRTIMER_MODE_REL);
		}
		if(!atomic_read(&stat->enabled_acc)) {
			if(stat->pdata_acc->gpio_int1 >= 0)
				enable_irq(stat->irq1);
			if(stat->pdata_acc->gpio_int2 >= 0)
				enable_irq(stat->irq2);
		}
	}

	return 0;
}

static int lsm303d_mag_disable(struct lsm303d_status *stat)
{
	if (atomic_cmpxchg(&stat->enabled_mag, 1, 0)) {
		if(!atomic_read(&stat->enabled_temp)) {
			cancel_work_sync(&stat->input_work_mag);
			hrtimer_cancel(&stat->hr_timer_mag);
		}
		lsm303d_mag_device_power_off(stat);
		if(!atomic_read(&stat->enabled_acc)) {
			if(stat->pdata_acc->gpio_int1 >= 0)
				disable_irq(stat->irq1);
			if(stat->pdata_acc->gpio_int2 >= 0)
				disable_irq(stat->irq2);
		}
	}

	return 0;
}

static int lsm303d_temperature_enable(struct lsm303d_status *stat)
{
	int err;
	u8 buf[2];
	u8 updated_val;

	buf[0] = status_registers.cntrl5.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.cntrl5.resume_value = buf[0];	
	updated_val = ((TEMP_MASK & TEMP_ON) | 
					((~TEMP_MASK) & buf[0]));
	buf[1] = updated_val;
	buf[0] = status_registers.cntrl5.address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.cntrl5.resume_value = updated_val;

	if(!atomic_read(&stat->enabled_mag)) {
		hrtimer_start(&stat->hr_timer_mag, stat->ktime_mag, HRTIMER_MODE_REL);
	}
	atomic_set(&stat->enabled_temp, 1);
	return 0;

error:
	return -1;
}

static int lsm303d_temperature_disable(struct lsm303d_status *stat)
{
	int err;
	u8 buf[2];
	u8 updated_val;

	buf[0] = status_registers.cntrl5.address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		goto error;

	status_registers.cntrl5.resume_value = buf[0];	
	updated_val = ((TEMP_MASK & TEMP_OFF) | 
					((~TEMP_MASK) & buf[0]));
	buf[1] = updated_val;
	buf[0] = status_registers.cntrl5.address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		goto error;
	status_registers.cntrl5.resume_value = updated_val;

	if(!atomic_read(&stat->enabled_mag)) {
		cancel_work_sync(&stat->input_work_mag);
		hrtimer_cancel(&stat->hr_timer_mag);
	}
	atomic_set(&stat->enabled_temp, 0);
	stat->temp_value_dec = NDTEMP;
	return 0;

error:
	return -1;
}

static void lsm303d_acc_input_cleanup(struct lsm303d_status *stat)
{
	input_unregister_device(stat->input_dev_acc);
	input_free_device(stat->input_dev_acc);
}

static void lsm303d_mag_input_cleanup(struct lsm303d_status *stat)
{
	input_unregister_device(stat->input_dev_mag);
	input_free_device(stat->input_dev_mag);
}

static ssize_t attr_get_polling_rate_acc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int val;
	//struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev); 
	mutex_lock(&stat->lock);
	val = stat->pdata_acc->poll_interval;
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%u\n", val);
}

static ssize_t attr_get_polling_rate_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	unsigned int val;
	//struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	mutex_lock(&stat->lock);
	val = stat->pdata_mag->poll_interval;
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%u\n", val);
}

static ssize_t attr_set_polling_rate_acc(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	//struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long interval_ms;

	if (strict_strtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	interval_ms = (unsigned int)max((unsigned int)interval_ms,
						stat->pdata_acc->min_interval);
	mutex_lock(&stat->lock);
	stat->pdata_acc->poll_interval = (unsigned int)interval_ms;
	lsm303d_acc_update_odr(stat, interval_ms);
	mutex_unlock(&stat->lock);
	return size;
}

static ssize_t attr_set_polling_rate_mag(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	//struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long interval_ms;

	if (strict_strtoul(buf, 10, &interval_ms))
		return -EINVAL;
	if (!interval_ms)
		return -EINVAL;
	interval_ms = (unsigned int)max((unsigned int)interval_ms,
						stat->pdata_mag->min_interval);
	mutex_lock(&stat->lock);
	stat->pdata_mag->poll_interval = (unsigned int)interval_ms;
	lsm303d_mag_update_odr(stat, interval_ms);
	mutex_unlock(&stat->lock);
	return size;
}

static ssize_t attr_get_enable_acc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	int val = (int)atomic_read(&stat->enabled_acc);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_get_enable_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	int val = (int)atomic_read(&stat->enabled_mag);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_enable_acc(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		lsm303d_acc_enable(stat);
	else
		lsm303d_acc_disable(stat);

	return size;
}

static ssize_t attr_set_enable_mag(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		lsm303d_mag_enable(stat);
	else
		lsm303d_mag_disable(stat);

	return size;
}

static ssize_t attr_get_range_acc(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
//	struct device *dev = to_dev(kobj->parent);
	u8 val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	int range = 2;
	mutex_lock(&stat->lock);
	val = stat->pdata_acc->fs_range ;
	switch (val) {
	case LSM303D_ACC_FS_2G:
		range = 2;
		break;
	case LSM303D_ACC_FS_4G:
		range = 4;
		break;
	case LSM303D_ACC_FS_8G:
		range = 8;
		break;
	case LSM303D_ACC_FS_16G:
		range = 16;
		break;
	}
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%d\n", range);
}

static ssize_t attr_get_range_mag(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
//	struct device *dev = to_dev(kobj->parent);
	u8 val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	int range = 2;
	mutex_lock(&stat->lock);
	val = stat->pdata_mag->fs_range ;
	switch (val) {
	case LSM303D_MAG_FS_2G:
		range = 2;
		break;
	case LSM303D_MAG_FS_4G:
		range = 4;
		break;
	case LSM303D_MAG_FS_8G:
		range = 8;
		break;
	case LSM303D_MAG_FS_12G:
		range = 12;
		break;
	}
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%d\n", range);
}

static ssize_t attr_set_range_acc(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	u8 range;
	int err;
	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	switch (val) {
	case 2:
		range = LSM303D_ACC_FS_2G;
		break;
	case 4:
		range = LSM303D_ACC_FS_4G;
		break;
	case 8:
		range = LSM303D_ACC_FS_8G;
		break;
	case 16:
		range = LSM303D_ACC_FS_16G;
		break;
	default:
		dev_err(&stat->client->dev, "accelerometer invalid range "
					"request: %lu, discarded\n", val);
		return -EINVAL;
	}
	mutex_lock(&stat->lock);
	err = lsm303d_acc_update_fs_range(stat, range);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata_acc->fs_range = range;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "accelerometer range set to:"
							" %lu g\n", val);

	return size;
}

static ssize_t attr_set_range_mag(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	u8 range;
	int err;
	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	switch (val) {
	case 2:
		range = LSM303D_MAG_FS_2G;
		break;
	case 4:
		range = LSM303D_MAG_FS_4G;
		break;
	case 8:
		range = LSM303D_MAG_FS_8G;
		break;
	case 12:
		range = LSM303D_MAG_FS_12G;
		break;
	default:
		dev_err(&stat->client->dev, "magnetometer invalid range "
					"request: %lu, discarded\n", val);
		return -EINVAL;
	}
	mutex_lock(&stat->lock);
	err = lsm303d_mag_update_fs_range(stat, range);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata_mag->fs_range = range;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "magnetometer range set to:"
							" %lu g\n", val);

	return size;
}

static ssize_t attr_get_aa_filter(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
//	struct device *dev = to_dev(kobj->parent);
	u8 val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	int frequency=FILTER_773;
	mutex_lock(&stat->lock);
	val = stat->pdata_acc->aa_filter_bandwidth;
	switch (val) {
	case ANTI_ALIASING_50:
		frequency = FILTER_50;
		break;
	case ANTI_ALIASING_194:
		frequency = FILTER_194;
		break;
	case ANTI_ALIASING_362:
		frequency = FILTER_362;
		break;
	case ANTI_ALIASING_773:
		frequency = FILTER_773;
		break;
	}
	mutex_unlock(&stat->lock);
	return sprintf(buf, "%d\n", frequency);
}

static ssize_t attr_set_aa_filter(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t size)
{
//	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	unsigned long val;
	u8 frequency;
	int err;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;
	switch (val) {
	case FILTER_50:
		frequency = ANTI_ALIASING_50;
		break;
	case FILTER_194:
		frequency = ANTI_ALIASING_194;
		break;
	case FILTER_362:
		frequency = ANTI_ALIASING_362;
		break;
	case FILTER_773:
		frequency = ANTI_ALIASING_773;
		break;
	default:
		dev_err(&stat->client->dev, "accelerometer invalid filter "
					"request: %lu, discarded\n", val);
		return -EINVAL;
	}
	mutex_lock(&stat->lock);
	err = lsm303d_acc_update_filter(stat, frequency);
	if (err < 0) {
		mutex_unlock(&stat->lock);
		return err;
	}
	stat->pdata_acc->aa_filter_bandwidth = frequency;
	mutex_unlock(&stat->lock);
	dev_info(&stat->client->dev, "accelerometer anti-aliasing filter "
					"set to: %lu Hz\n", val);

	return size;
}

static ssize_t attr_get_temp_enable(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	int val = (int)atomic_read(&stat->enabled_temp);

	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_temp_enable(struct device *dev, 
					struct device_attribute *attr,
					const char *buf, size_t size)
{
	unsigned long val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	atomic_set(&stat->enabled_temp, (int)val);

	if(val>0) {
		lsm303d_temperature_enable(stat);
	} else {
		lsm303d_temperature_disable(stat);
	}

	return size;
}

static ssize_t attr_get_temp(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int dec;
	unsigned int flo;
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	dec = stat->temp_value_dec;
	flo = stat->temp_value_flo;

	if(dec==NDTEMP)
		return sprintf(buf, "n.d.\n");
	
	return sprintf(buf, "%d.%u\n", dec, flo);
}

static struct device_attribute poll_attr_acc =
__ATTR(pollrate_ms, 0666, attr_get_polling_rate_acc, attr_set_polling_rate_acc);
static struct device_attribute enable_attr_acc =
__ATTR(enable, 0666, attr_get_enable_acc, attr_set_enable_acc);
static struct device_attribute fs_attr_acc = 
__ATTR(full_scale, 0666, attr_get_range_acc, attr_set_range_acc);
static struct device_attribute aa_filter_attr  =
__ATTR(anti_aliasing_frequency, 0666, attr_get_aa_filter, attr_set_aa_filter);


static struct device_attribute poll_attr_mag =
__ATTR(pollrate_ms, 0666, attr_get_polling_rate_mag, attr_set_polling_rate_mag);
static struct device_attribute enable_attr_mag =
__ATTR(enable, 0666, attr_get_enable_mag, attr_set_enable_mag);
static struct device_attribute fs_attr_mag =
__ATTR(full_scale, 0666, attr_get_range_mag, attr_set_range_mag);

static int write_bit_on_register(struct lsm303d_status *stat, u8 address, 
					u8 *resume_value, u8 mask, int value)
{
	int err;
	u8 updated_val;
	u8 buf[2];
	u8 val = 0x00;

	buf[0] = address;
	err = lsm303d_i2c_read(stat, buf, 1);
	if (err < 0)
		return -1;

	if(resume_value != NULL)
		*resume_value = buf[0];	

	if(mask == 0)
		updated_val = (u8)value;
	else {
		if(value>0)
			val = 0xFF;
		updated_val = (mask & val) | ((~mask) & buf[0]);
	}

	buf[1] = updated_val;
	buf[0] = address;

	err = lsm303d_i2c_write(stat, buf, 1);
	if (err < 0)
		return -1;

	if(resume_value != NULL)
		*resume_value = updated_val;

	return err;
}

static int write_gen_int(struct lsm303d_status *stat, 
					struct interrupt_enable *ie, int val) 
{
	int err;

	if(val>0)
		val = 1;
	else
		val = 0;

	err = write_bit_on_register(stat, ie->address, NULL, ie->mask, val);
	if(err < 0)
		return -1;

	atomic_set(&ie->enable, val);
	return err;
}
#if 0
static int write_duration_threshold_int(struct lsm303d_status *stat, 
					struct interrupt_value *ie, int val) 
{
	int err;

	if(val<0)
		return -1;

	if(val>MAX_DUR_TH)
		return -1;

	err = write_bit_on_register(stat, ie->address, NULL, 0, val);
	if(err<0)
		return -1;

	ie->value = val;

	return err;
}

static int write_threshold_mag_int(struct lsm303d_status *stat, 
					struct interrupt_value *ie, int val) 
{
	int err;
	u8 high;
	u8 low;

	if(val<0)
		return -1;

	if(val>MAX_TH_MAG)
		return -1;

	low = (u8)(0xff & val);

	err = write_bit_on_register(stat, ie->address, NULL, 0, low);
	if(err<0)
		return -1;

	high = (u8)(0xff & (val >> 8));

	err = write_bit_on_register(stat, (ie->address)+1, NULL, 0, high);
	if(err<0)
		return -1;

	ie->value = val;

	return err;
}
static ssize_t attr_get_gen1_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val = -1;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen1_pin1.enable);
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen1_pin2.enable);
	}
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen1_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		err = write_gen_int(stat, 
					&stat->interrupt->gen1_pin1, (int)val);
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		err = write_gen_int(stat, 
					&stat->interrupt->gen1_pin2, (int)val);
	}
	return size;
}

static ssize_t attr_get_gen2_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val = -1;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen2_pin1.enable);
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen2_pin2.enable);
	}
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen2_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		err = write_gen_int(stat, 
					&stat->interrupt->gen2_pin1, (int)val);
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		err = write_gen_int(stat, 
					&stat->interrupt->gen2_pin2, (int)val);
	}
	return size;
}

static ssize_t attr_get_gen1_duration(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = stat->interrupt->gen1_duration.value;
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen1_duration(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_duration_threshold_int(stat, 
				&stat->interrupt->gen1_duration, (int)val);
	
	return size;
}

static ssize_t attr_get_gen2_duration(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = stat->interrupt->gen2_duration.value;
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen2_duration(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_duration_threshold_int(stat, 
				&stat->interrupt->gen2_duration, (int)val);
	
	return size;
}

static ssize_t attr_get_gen1_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = stat->interrupt->gen1_threshold.value;
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen1_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_duration_threshold_int(stat, 
				&stat->interrupt->gen1_threshold, (int)val);
	
	return size;
}

static ssize_t attr_get_gen2_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = stat->interrupt->gen2_threshold.value;
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen2_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_duration_threshold_int(stat, 
				&stat->interrupt->gen2_threshold, (int)val);
	
	return size;
}

static ssize_t attr_get_gen_mag_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val = -1;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen_mag_pin1.enable);
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		val = atomic_read(&stat->interrupt->gen_mag_pin2.enable);
	}
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen_mag_status(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if(strcmp(attr->attr.name, "pin1_enable") == 0) {
		err = write_gen_int(stat, 
				&stat->interrupt->gen_mag_pin1, (int)val);
		if(err >= 0) {
		if((atomic_read(&stat->interrupt->gen_mag_pin2.enable))==0)
			write_gen_int(stat, 
					&stat->interrupt->gen_mag, (int)val);
		}
	}
	if(strcmp(attr->attr.name, "pin2_enable") == 0) {
		err = write_gen_int(stat, 
				&stat->interrupt->gen_mag_pin2, (int)val);
		if(err >= 0) {
		if((atomic_read(&stat->interrupt->gen_mag_pin1.enable))==0)
			write_gen_int(stat, 
					&stat->interrupt->gen_mag, (int)val);
		}
	}
	return size;
}

static ssize_t attr_get_gen_mag_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = stat->interrupt->gen_mag_threshold.value;
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen_mag_threshold(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err = -1;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_threshold_mag_int(stat, 
				&stat->interrupt->gen_mag_threshold, (int)val);
	
	return size;
}
#endif
#if 0
static int get_axis(struct lsm303d_status *stat, 
					int generator, const char *name) {

	int val;
	int axis;

	if(strcmp(name, "x_high_enable") == 0) {
		axis = 3;
	}
	if(strcmp(name, "x_low_enable") == 0) {
		axis = 0;
	}
	if(strcmp(name, "y_high_enable") == 0) {
		axis = 4;
	}
	if(strcmp(name, "y_low_enable") == 0) {
		axis = 1;
	}
	if(strcmp(name, "z_high_enable") == 0) {
		axis = 5;
	}
	if(strcmp(name, "z_low_enable") == 0) {
		axis = 2;
	}

	if(generator == 1)
		val = atomic_read(&stat->interrupt->gen1_axis[axis].enable);
	else
		val = atomic_read(&stat->interrupt->gen2_axis[axis].enable);

	return val;
}

static int set_axis(struct lsm303d_status *stat, int generator, 
					const char *name, unsigned long value) 
{
	int err = -1;
	int axis;

	if(strcmp(name, "x_high_enable") == 0) {
		axis = 3;
	}
	if((strcmp(name, "x_low_enable") == 0) || 
					(strcmp(name, "x_enable") == 0)) {
		axis = 0;
	}
	if(strcmp(name, "y_high_enable") == 0) {
		axis = 4;
	}
	if((strcmp(name, "y_low_enable") == 0) || 
					(strcmp(name, "y_enable") == 0)) {
		axis = 1;
	}
	if(strcmp(name, "z_high_enable") == 0) {
		axis = 5;
	}
	if((strcmp(name, "z_low_enable") == 0) || 
					(strcmp(name, "z_enable") == 0)) {
		axis = 2;
	}

	if(generator == 1)
		err = write_gen_int(stat, 
			&(stat->interrupt->gen1_axis[axis]), (int)value);
	if(generator == 2)
		err = write_gen_int(stat, 
			&(stat->interrupt->gen2_axis[axis]), (int)value);
	if(generator == 3)
		err = write_gen_int(stat, 
			&(stat->interrupt->gen_mag_axis[axis]), (int)value);

	if(err < 0)
		return -1;

	return err;
}
static ssize_t attr_get_gen1_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = get_axis(stat,1,attr->attr.name);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen1_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = set_axis(stat, 1, attr->attr.name, val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_gen2_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = get_axis(stat,2,attr->attr.name);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen2_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = set_axis(stat, 2, attr->attr.name, val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_gen_mag_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = get_axis(stat, 3, attr->attr.name);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen_mag_axis(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = set_axis(stat, 3, attr->attr.name, val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_gen1_and_or(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = atomic_read(&stat->interrupt->gen1_and_or.enable);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen1_and_or(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_gen_int(stat, &(stat->interrupt->gen1_and_or), (int)val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_gen2_and_or(struct kobject *kobj,
						struct kobj_attribute *attr,
						char *buf)
{
	int val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = atomic_read(&stat->interrupt->gen2_and_or.enable);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_gen2_and_or(struct kobject *kobj,
						struct kobj_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;
	struct device *dev = to_dev(kobj->parent);
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_gen_int(stat, &(stat->interrupt->gen2_and_or), (int)val);
	if(err < 0)
		return -1;

	return size;
}
#endif
static ssize_t attr_set_pin_conf(struct device *dev, 
						struct device_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;

	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_gen_int(stat, 
			&(stat->interrupt->interrupt_pin_conf), (int)val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_pin_conf(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = atomic_read(&stat->interrupt->interrupt_pin_conf.enable);
	return sprintf(buf, "%d\n", val);
}

static ssize_t attr_set_interrupt_polarity(struct device *dev, 
						struct device_attribute *attr,
						const char *buf, size_t size)
{
	int err;
	unsigned long val;

	struct lsm303d_status *stat = dev_get_drvdata(dev);

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	err = write_gen_int(stat, 
			&(stat->interrupt->interrupt_polarity), (int)val);
	if(err < 0)
		return -1;

	return size;
}

static ssize_t attr_get_interrupt_polarity(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	int val;
	struct lsm303d_status *stat = dev_get_drvdata(dev);

	val = atomic_read(&stat->interrupt->interrupt_polarity.enable);
	return sprintf(buf, "%d\n", val);
}
#if 0
static struct kobj_attribute gen1_interrupt_pin1_enable =
__ATTR(pin1_enable, 0664, attr_get_gen1_status, attr_set_gen1_status);
static struct kobj_attribute gen1_interrupt_pin2_enable =
__ATTR(pin2_enable, 0664, attr_get_gen1_status, attr_set_gen1_status);

static struct kobj_attribute gen2_interrupt_pin1_enable =
__ATTR(pin1_enable, 0664, attr_get_gen2_status, attr_set_gen2_status);
static struct kobj_attribute gen2_interrupt_pin2_enable =
__ATTR(pin2_enable, 0664, attr_get_gen2_status, attr_set_gen2_status);

static struct kobj_attribute gen1_duration =
__ATTR(duration, 0664, attr_get_gen1_duration, attr_set_gen1_duration);
static struct kobj_attribute gen2_duration =
__ATTR(duration, 0664, attr_get_gen2_duration, attr_set_gen2_duration);

static struct kobj_attribute gen1_threshold =
__ATTR(threshold, 0664, attr_get_gen1_threshold, attr_set_gen1_threshold);
static struct kobj_attribute gen2_threshold =
__ATTR(threshold, 0664, attr_get_gen2_threshold, attr_set_gen2_threshold);

static struct kobj_attribute mag_gen_interrupt_pin1 =
__ATTR(pin1_enable, 0664, attr_get_gen_mag_status, attr_set_gen_mag_status);
static struct kobj_attribute mag_gen_interrupt_pin2 =
__ATTR(pin2_enable, 0664, attr_get_gen_mag_status, attr_set_gen_mag_status);

static struct kobj_attribute mag_gen_threshold =
__ATTR(threshold, 0664, attr_get_gen_mag_threshold, attr_set_gen_mag_threshold);

static struct kobj_attribute gen1_x_high =
__ATTR(x_high_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);
static struct kobj_attribute gen1_x_low =
__ATTR(x_low_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);

static struct kobj_attribute gen2_x_high =
__ATTR(x_high_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);
static struct kobj_attribute gen2_x_low =
__ATTR(x_low_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);

static struct kobj_attribute gen1_y_high =
__ATTR(y_high_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);
static struct kobj_attribute gen1_y_low =
__ATTR(y_low_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);

static struct kobj_attribute gen2_y_high =
__ATTR(y_high_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);
static struct kobj_attribute gen2_y_low =
__ATTR(y_low_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);

static struct kobj_attribute gen1_z_high =
__ATTR(z_high_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);
static struct kobj_attribute gen1_z_low =
__ATTR(z_low_enable, 0664, attr_get_gen1_axis, attr_set_gen1_axis);

static struct kobj_attribute gen2_z_high =
__ATTR(z_high_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);
static struct kobj_attribute gen2_z_low =
__ATTR(z_low_enable, 0664, attr_get_gen2_axis, attr_set_gen2_axis);

static struct kobj_attribute gen_mag_x =
__ATTR(x_enable, 0664, attr_get_gen_mag_axis, attr_set_gen_mag_axis);
static struct kobj_attribute gen_mag_y =
__ATTR(y_enable, 0664, attr_get_gen_mag_axis, attr_set_gen_mag_axis);
static struct kobj_attribute gen_mag_z =
__ATTR(z_enable, 0664, attr_get_gen_mag_axis, attr_set_gen_mag_axis);

static struct kobj_attribute gen1_and_or =
__ATTR(and(1)_or(0)_combination, 0664, attr_get_gen1_and_or, 
							attr_set_gen1_and_or);
static struct kobj_attribute gen2_and_or =
__ATTR(and(1)_or(0)_combination, 0664, attr_get_gen2_and_or, 
							attr_set_gen2_and_or);


static struct attribute *attributes_acc_interrupt1[] = {
	&gen1_interrupt_pin1_enable.attr,
	&gen1_interrupt_pin2_enable.attr,
	&gen1_duration.attr,
	&gen1_threshold.attr,
	&gen1_x_high.attr,
	&gen1_x_low.attr,
	&gen1_y_high.attr,
	&gen1_y_low.attr,
	&gen1_z_high.attr,
	&gen1_z_low.attr,
	&gen1_and_or.attr,
	NULL,
};

static struct attribute *attributes_acc_interrupt2[] = {
	&gen2_interrupt_pin1_enable.attr,
	&gen2_interrupt_pin2_enable.attr,
	&gen2_duration.attr,
	&gen2_threshold.attr,
	&gen2_x_high.attr,
	&gen2_x_low.attr,
	&gen2_y_high.attr,
	&gen2_y_low.attr,
	&gen2_z_high.attr,
	&gen2_z_low.attr,
	&gen2_and_or.attr,
	NULL,
};

static struct attribute *attributes_mag_interrupt[] = {
	&mag_gen_interrupt_pin1.attr,
	&mag_gen_interrupt_pin2.attr,
	&mag_gen_threshold.attr,
	&gen_mag_x.attr,
	&gen_mag_y.attr,
	&gen_mag_z.attr,
	NULL,
};
#endif
static struct attribute *attributes_acc[] = {
	&poll_attr_acc.attr,
	&enable_attr_acc.attr,
	&fs_attr_acc.attr,
	&aa_filter_attr.attr,
	NULL,
};

static struct attribute *attributes_mag[] = {
	&poll_attr_mag.attr,
	&enable_attr_mag.attr,
	&fs_attr_mag.attr,
	NULL,
};

static struct attribute_group attr_group_acc = {
	.attrs = attributes_acc,
};

static struct attribute_group attr_group_mag = {
	.attrs = attributes_mag,
};
#if 0
static struct attribute_group attr_group_int1_acc = {
	.attrs = attributes_acc_interrupt1,
	.name = "interrupt_generator1",
};

static struct attribute_group attr_group_int2_acc = {
	.attrs = attributes_acc_interrupt2,
	.name = "interrupt_generator2",
};

static struct attribute_group attr_group_int_mag = {
	.attrs = attributes_mag_interrupt,
	.name = "interrupt_generator",
};
#endif
static struct device_attribute attributes_com[] = {
	__ATTR(enable_temperature, 0664, attr_get_temp_enable, 
							attr_set_temp_enable),
	__ATTR(read_temperature, 0444, attr_get_temp, NULL),
};

static struct device_attribute attributes_interrupt_com[] = {
	__ATTR(interrupt_pin_configuration, 0664, attr_get_pin_conf, 
						attr_set_pin_conf),
	__ATTR(interrupt_polarity, 0664, attr_get_interrupt_polarity, 
						attr_set_interrupt_polarity),
};

static int create_sysfs_interfaces(struct lsm303d_status *stat)
{
	int err;
	int i=0;
//	struct lsm303d_status *stat = dev_get_drvdata(dev);

	struct kobject *mag_kobj = &stat->input_dev_mag->dev.kobj ;
	struct kobject *acc_kobj = &stat->input_dev_acc->dev.kobj ;
	if(!acc_kobj)
		return -ENOMEM;

	if(!mag_kobj)
		return -ENOMEM;

	err = sysfs_create_group(acc_kobj, &attr_group_acc);
	if (err)
		goto error;

	err = sysfs_create_group(mag_kobj, &attr_group_mag);
	if (err)
		goto error;
/*
	if((stat->pdata_acc->gpio_int1 >= 0)||
					(stat->pdata_acc->gpio_int2 >= 0)) {
		err = sysfs_create_group(acc_kobj, &attr_group_int1_acc);
		if (err)
			kobject_put(acc_kobj);

		err = sysfs_create_group(acc_kobj, &attr_group_int2_acc);
		if (err)
			kobject_put(acc_kobj);

		err = sysfs_create_group(mag_kobj, &attr_group_int_mag);
		if (err)
			kobject_put(mag_kobj);

		for (n = 0; n < ARRAY_SIZE(attributes_interrupt_com); n++)
		if (device_create_file(&stat->client->dev, attributes_interrupt_com + n))
			goto error1;
	}
*/

	for (i = 0; i < ARRAY_SIZE(attributes_com); i++)
		if (device_create_file(&stat->client->dev, attributes_com + i))
			goto error;

	

	return 0;

error:
	for ( ; i >= 0; i--)
		device_remove_file(&stat->client->dev, attributes_com + i);
/*
error1:
	for ( ; n >= 0; n--)
		device_remove_file(&stat->client->dev, attributes_interrupt_com + n);
*/
	dev_err(&stat->client->dev, "%s:Unable to create interface\n", __func__);
	return -1;
}

static void remove_sysfs_interfaces(struct device *dev)
{
	int i;
	struct lsm303d_status *stat = dev_get_drvdata(dev);
	for (i = 0; i < ARRAY_SIZE(attributes_com); i++)
		device_remove_file(dev, attributes_com + i);
	if((stat->pdata_acc->gpio_int1 >= 0)||
					(stat->pdata_acc->gpio_int2 >= 0)) {
		for (i = 0; i < ARRAY_SIZE(attributes_interrupt_com); i++)
			device_remove_file(dev, attributes_interrupt_com + i);
	}
}

int lsm303d_acc_input_open(struct input_dev *input)
{
	struct lsm303d_status *stat = input_get_drvdata(input);

	return lsm303d_acc_enable(stat);
}

void lsm303d_acc_input_close(struct input_dev *dev)
{
	struct lsm303d_status *stat = input_get_drvdata(dev);

	lsm303d_acc_disable(stat);
}

int lsm303d_mag_input_open(struct input_dev *input)
{
	struct lsm303d_status *stat = input_get_drvdata(input);

	return lsm303d_mag_enable(stat);
}

void lsm303d_mag_input_close(struct input_dev *dev)
{
	struct lsm303d_status *stat = input_get_drvdata(dev);

	lsm303d_mag_disable(stat);
}

static int lsm303d_acc_get_data(struct lsm303d_status *stat, int *xyz)
{
	int i, err = -1;
	u8 acc_data[6];
	s32 hw_d[3] = { 0 };

	acc_data[0] = (REG_ACC_DATA_ADDR);
	err = lsm303d_i2c_read(stat, acc_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = ((s32)( (s16)((acc_data[1] << 8) | (acc_data[0]))));
	hw_d[1] = ((s32)( (s16)((acc_data[3] << 8) | (acc_data[2]))));
	hw_d[2] = ((s32)( (s16)((acc_data[5] << 8) | (acc_data[4]))));

#ifdef DEBUG
	pr_debug("%s read x=%X %X(regH regL), x=%d(dec) [ug]\n",
		LSM303D_ACC_DEV_NAME, acc_data[1], acc_data[0], hw_d[0]);
	pr_debug("%s read y=%X %X(regH regL), y=%d(dec) [ug]\n",
		LSM303D_ACC_DEV_NAME, acc_data[3], acc_data[2], hw_d[1]);
	pr_debug("%s read z=%X %X(regH regL), z=%d(dec) [ug]\n",
		LSM303D_ACC_DEV_NAME, acc_data[5], acc_data[4], hw_d[2]);
#endif

	hw_d[0] = hw_d[0] * stat->sensitivity_acc;
	hw_d[1] = hw_d[1] * stat->sensitivity_acc;
	hw_d[2] = hw_d[2] * stat->sensitivity_acc;

	for (i = 0; i < 3; i++) {
		xyz[i] = stat->pdata_acc->rot_matrix[0][i] * hw_d[0] +
				stat->pdata_acc->rot_matrix[1][i] * hw_d[1] +
				stat->pdata_acc->rot_matrix[2][i] * hw_d[2];
	}

	return err;
}

static int lsm303d_mag_get_data(struct lsm303d_status *stat, int *xyz)
{
	int i, err = -1;
	u8 mag_data[6];
	s32 hw_d[3] = { 0 };

	mag_data[0] = (REG_MAG_DATA_ADDR);
	err = lsm303d_i2c_read(stat, mag_data, 6);
	if (err < 0)
		return err;

	hw_d[0] = ((s32)( (s16)((mag_data[1] << 8) | (mag_data[0]))));
	hw_d[1] = ((s32)( (s16)((mag_data[3] << 8) | (mag_data[2]))));
	hw_d[2] = ((s32)( (s16)((mag_data[5] << 8) | (mag_data[4]))));

#ifdef DEBUG
	pr_debug("%s read x=%X %X(regH regL), x=%d(dec) [ug]\n",
		LSM303D_MAG_DEV_NAME, mag_data[1], mag_data[0], hw_d[0]);
	pr_debug("%s read x=%X %X(regH regL), x=%d(dec) [ug]\n",
		LSM303D_MAG_DEV_NAME, mag_data[3], mag_data[2], hw_d[1]);
	pr_debug("%s read x=%X %X(regH regL), x=%d(dec) [ug]\n",
		LSM303D_MAG_DEV_NAME, mag_data[5], mag_data[4], hw_d[2]);
#endif

	hw_d[0] = hw_d[0] * stat->sensitivity_mag;
	hw_d[1] = hw_d[1] * stat->sensitivity_mag;
	hw_d[2] = hw_d[2] * stat->sensitivity_mag;

	for (i = 0; i < 3; i++) {
		xyz[i] = stat->pdata_acc->rot_matrix[0][i] * hw_d[0] +
				stat->pdata_acc->rot_matrix[1][i] * hw_d[1] +
				stat->pdata_acc->rot_matrix[2][i] * hw_d[2];
	}

	return err;
}

static int lsm303d_temp_get_data(struct lsm303d_status *stat, 
							int *dec, int *flo)
{
	int err = -1;
	u8 temp_data[2];
	s16 hw_d = 0;

	temp_data[0] = (REG_TEMP_DATA_ADDR);
	err = lsm303d_i2c_read(stat, temp_data, 2);
	if (err < 0)
		return err;

	hw_d = (s16)((temp_data[1] << 8) | (temp_data[0]));
	

#ifdef DEBUG
	pr_debug("%s read T=%X %X(regH regL), T=%d(dec) [C]\n",
		LSM303D_DEV_NAME, temp_data[1], temp_data[0], hw_d);
#endif

	*dec = (int)(hw_d/TEMP_SENSITIVITY) + OFFSET_TEMP;
	*flo = (((unsigned int)hw_d)%TEMP_SENSITIVITY);

	return err;
}

static void lsm303d_acc_report_values(struct lsm303d_status *stat, int *xyz)
{
	aml_sensor_report_acc(stat->client, stat->input_dev_acc, xyz[0],xyz[1],xyz[2]);
#if 0
	input_report_abs(stat->input_dev_acc, ABS_X, xyz[0]);
	input_report_abs(stat->input_dev_acc, ABS_Y, xyz[1]);
	input_report_abs(stat->input_dev_acc, ABS_Z, xyz[2]);
	input_sync(stat->input_dev_acc);
#endif
}

static void lsm303d_mag_report_values(struct lsm303d_status *stat, int *xyz)
{
	aml_sensor_report_mag(stat->client, stat->input_dev_mag, xyz[0],xyz[1],xyz[2]);
#if 0
	input_report_abs(stat->input_dev_mag, ABS_X, xyz[0]);
	input_report_abs(stat->input_dev_mag, ABS_Y, xyz[1]);
	input_report_abs(stat->input_dev_mag, ABS_Z, xyz[2]);
	input_sync(stat->input_dev_mag);
#endif
}

static int lsm303d_acc_input_init(struct lsm303d_status *stat)
{
	int err;

	stat->input_dev_acc = input_allocate_device();
	if (!stat->input_dev_acc) {
		err = -ENOMEM;
		dev_err(&stat->client->dev, "accelerometer "
					"input device allocation failed\n");
		goto err0;
	}

	stat->input_dev_acc->open = lsm303d_acc_input_open;
	stat->input_dev_acc->close = lsm303d_acc_input_close;
	stat->input_dev_acc->name = LSM303D_ACC_DEV_NAME;
	stat->input_dev_acc->id.bustype = BUS_I2C;
	stat->input_dev_acc->dev.parent = &stat->client->dev;

	input_set_drvdata(stat->input_dev_acc, stat);

	set_bit(EV_ABS, stat->input_dev_acc->evbit);

	input_set_abs_params(stat->input_dev_acc, ABS_X, 
				-ACC_G_MAX_NEG, ACC_G_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_acc, ABS_Y, 
				-ACC_G_MAX_NEG, ACC_G_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_acc, ABS_Z, 
				-ACC_G_MAX_NEG, ACC_G_MAX_POS, FUZZ, FLAT);

	err = input_register_device(stat->input_dev_acc);
	if (err) {
		dev_err(&stat->client->dev,
			"unable to register accelerometer input device %s\n",
				stat->input_dev_acc->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(stat->input_dev_acc);
err0:
	return err;
}

static int lsm303d_mag_input_init(struct lsm303d_status *stat)
{
	int err;

	stat->input_dev_mag = input_allocate_device();
	if (!stat->input_dev_mag) {
		err = -ENOMEM;
		dev_err(&stat->client->dev, "magnetometer "
					"input device allocation failed\n");
		goto err0;
	}

	stat->input_dev_mag->open = lsm303d_mag_input_open;
	stat->input_dev_mag->close = lsm303d_mag_input_close;
	stat->input_dev_mag->name = LSM303D_MAG_DEV_NAME;
	stat->input_dev_mag->id.bustype = BUS_I2C;
	stat->input_dev_mag->dev.parent = &stat->client->dev;

	input_set_drvdata(stat->input_dev_mag, stat);

	set_bit(EV_ABS, stat->input_dev_mag->evbit);

	input_set_abs_params(stat->input_dev_mag, ABS_X, 
				-MAG_G_MAX_NEG, MAG_G_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_mag, ABS_Y, 
				-MAG_G_MAX_NEG, MAG_G_MAX_POS, FUZZ, FLAT);
	input_set_abs_params(stat->input_dev_mag, ABS_Z, 
				-MAG_G_MAX_NEG, MAG_G_MAX_POS, FUZZ, FLAT);

	err = input_register_device(stat->input_dev_mag);
	if (err) {
		dev_err(&stat->client->dev,
			"unable to register magnetometer input device %s\n",
				stat->input_dev_mag->name);
		goto err1;
	}

	return 0;

err1:
	input_free_device(stat->input_dev_mag);
err0:
	return err;
}

static void lsm303d_input_cleanup(struct lsm303d_status *stat)
{
	input_unregister_device(stat->input_dev_acc);
	input_free_device(stat->input_dev_acc);

	input_unregister_device(stat->input_dev_mag);
	input_free_device(stat->input_dev_mag);
}

static void poll_function_work_acc(struct work_struct *input_work_acc)
{
	struct lsm303d_status *stat;
	int xyz[3] = { 0 };
	int err;

	stat = container_of((struct work_struct *)input_work_acc,
			struct lsm303d_status, input_work_acc);

	mutex_lock(&stat->lock);
	err = lsm303d_acc_get_data(stat, xyz);
	if (err < 0)
		dev_err(&stat->client->dev, "get_accelerometer_data failed\n");
	else
		lsm303d_acc_report_values(stat, xyz);

	mutex_unlock(&stat->lock);
	hrtimer_start(&stat->hr_timer_acc, stat->ktime_acc, HRTIMER_MODE_REL);
}

static void poll_function_work_mag(struct work_struct *input_work_mag)
{
	struct lsm303d_status *stat;
	int xyz[3] = { 0 };
	int err;
	int dec;
	int flo;

	stat = container_of((struct work_struct *)input_work_mag,
			struct lsm303d_status, input_work_mag);

	mutex_lock(&stat->lock);

	if(atomic_read(&stat->enabled_temp)) {
		err = lsm303d_temp_get_data(stat, &dec, &flo);
		if (err < 0)
			dev_err(&stat->client->dev, "get_temperature_data"
								" failed\n");
		else {
			stat->temp_value_dec = dec;
			stat->temp_value_flo = flo;
		}
	}

	if(atomic_read(&stat->enabled_mag)) {
		err = lsm303d_mag_get_data(stat, xyz);
		if (err < 0)
			dev_err(&stat->client->dev, "get_magnetometer_data"
								" failed\n");
		else
			lsm303d_mag_report_values(stat, xyz);
	}

	mutex_unlock(&stat->lock);
	hrtimer_start(&stat->hr_timer_mag, stat->ktime_mag, HRTIMER_MODE_REL);
}

enum hrtimer_restart poll_function_read_acc(struct hrtimer *timer)
{
	struct lsm303d_status *stat;


	stat = container_of((struct hrtimer *)timer,
				struct lsm303d_status, hr_timer_acc);

	queue_work(lsm303d_workqueue, &stat->input_work_acc);
	return HRTIMER_NORESTART;
}

enum hrtimer_restart poll_function_read_mag(struct hrtimer *timer)
{
	struct lsm303d_status *stat;


	stat = container_of((struct hrtimer *)timer,
				struct lsm303d_status, hr_timer_mag);

	queue_work(lsm303d_workqueue, &stat->input_work_mag);
	return HRTIMER_NORESTART;
}

static int lsm303d_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct lsm303d_status *stat;

	u32 smbus_func = I2C_FUNC_SMBUS_BYTE_DATA | 
			I2C_FUNC_SMBUS_WORD_DATA | I2C_FUNC_SMBUS_I2C_BLOCK;

	int err = -1;
	dev_info(&client->dev, "probe start.\n");
	stat = kzalloc(sizeof(struct lsm303d_status), GFP_KERNEL);
	if (stat == NULL) {
		err = -ENOMEM;
		dev_err(&client->dev,
				"failed to allocate memory for module data: "
					"%d\n", err);
		goto exit_check_functionality_failed;
	}

	stat->use_smbus = 0;
	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		dev_warn(&client->dev, "client not i2c capable\n");
		if (i2c_check_functionality(client->adapter, smbus_func)){
			stat->use_smbus = 1;
			dev_warn(&client->dev, "client using SMBUS\n");
		} else {			
			err = -ENODEV;
			dev_err(&client->dev, "client nor SMBUS capable\n");
			goto exit_check_functionality_failed;
		}
	}

	if(lsm303d_workqueue == 0)
		lsm303d_workqueue = create_workqueue("lsm303d_workqueue");

	hrtimer_init(&stat->hr_timer_acc, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stat->hr_timer_acc.function = &poll_function_read_acc;
	hrtimer_init(&stat->hr_timer_mag, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	stat->hr_timer_mag.function = &poll_function_read_mag;

	mutex_init(&stat->lock);
	mutex_lock(&stat->lock);

	stat->client = client;
	i2c_set_clientdata(client, stat);

	stat->pdata_acc = kmalloc(sizeof(*stat->pdata_acc), GFP_KERNEL);
	stat->pdata_mag = kmalloc(sizeof(*stat->pdata_mag), GFP_KERNEL);
	if ((stat->pdata_acc == NULL)||(stat->pdata_mag == NULL)) {
		err = -ENOMEM;
		dev_err(&client->dev,
			"failed to allocate memory for pdata: %d\n", err);
		goto err_mutexunlock;
	}

	if (client->dev.platform_data == NULL) {
		memcpy(stat->pdata_acc, &default_lsm303d_acc_pdata,
						sizeof(*stat->pdata_acc));
		memcpy(stat->pdata_mag, &default_lsm303d_mag_pdata,
						sizeof(*stat->pdata_mag));
		dev_info(&client->dev, "using default plaform_data for "
					"accelerometer and magnetometer\n");
	} else {
		struct lsm303d_main_platform_data *tmp;
		tmp = kzalloc(sizeof(struct lsm303d_main_platform_data), 
								GFP_KERNEL);
		if(tmp == NULL)
			goto exit_kfree_pdata;
		memcpy(tmp, client->dev.platform_data, sizeof(*tmp));
		if(tmp->pdata_acc == NULL) {
			memcpy(stat->pdata_acc, &default_lsm303d_acc_pdata,
						sizeof(*stat->pdata_acc));
			dev_info(&client->dev, "using default plaform_data for "
							"accelerometer\n");
		} else {
			memcpy(stat->pdata_acc, tmp->pdata_acc, 
						sizeof(*stat->pdata_acc));
		}
		if(tmp->pdata_mag == NULL) {
			memcpy(stat->pdata_mag, &default_lsm303d_mag_pdata,
						sizeof(*stat->pdata_mag));
			dev_info(&client->dev, "using default plaform_data for "
							"magnetometer\n");
		} else {
			memcpy(stat->pdata_mag, tmp->pdata_mag, 
						sizeof(*stat->pdata_mag));
		}
		kfree(tmp);
	}

	err = lsm303d_acc_validate_pdata(stat);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data for "
							"accelerometer \n");
		goto exit_kfree_pdata;
	}

	err = lsm303d_mag_validate_pdata(stat);
	if (err < 0) {
		dev_err(&client->dev, "failed to validate platform data for "
							"magnetometer\n");
		goto exit_kfree_pdata;
	}

	if (stat->pdata_acc->init) {
		err = stat->pdata_acc->init();
		if (err < 0) {
			dev_err(&client->dev, "accelerometer init failed: "
								"%d\n", err);
			goto err_pdata_acc_init;
		}
	}
	if (stat->pdata_mag->init) {
		err = stat->pdata_mag->init();
		if (err < 0) {
			dev_err(&client->dev, "magnetometer init failed: "
								"%d\n", err);
			goto err_pdata_mag_init;
		}
	}

	if(stat->pdata_acc->gpio_int1 >= 0) {
		if (!gpio_is_valid(stat->pdata_acc->gpio_int1)) {
  			dev_err(&client->dev, "The requested GPIO [%d] is not "
				"available\n", stat->pdata_acc->gpio_int1);
			err = -EINVAL;
  			goto err_gpio1_valid;
		}
		
		err = gpio_request(stat->pdata_acc->gpio_int1, 
						"INTERRUPT_PIN1_LSM303D");
		if(err < 0) {
			dev_err(&client->dev, "Unable to request GPIO [%d].\n",
						stat->pdata_acc->gpio_int1);
  			err = -EINVAL;
			goto err_gpio1_valid;
		}
		gpio_direction_input(stat->pdata_acc->gpio_int1);
		stat->irq1 = gpio_to_irq(stat->pdata_acc->gpio_int1);
		if(stat->irq1 < 0) {
			dev_err(&client->dev, "GPIO [%d] cannot be used as "
				"interrupt.\n",	stat->pdata_acc->gpio_int1);
			err = -EINVAL;
			goto err_gpio1_irq;
		}
		pr_info("%s: %s has set irq1 to irq: %d, mapped on gpio:%d\n",
			LSM303D_DEV_NAME, __func__, stat->irq1,
						stat->pdata_acc->gpio_int1);
	}

	if(stat->pdata_acc->gpio_int2 >= 0) {
		if (!gpio_is_valid(stat->pdata_acc->gpio_int2)) {
  			dev_err(&client->dev, "The requested GPIO [%d] is not "
				"available\n", stat->pdata_acc->gpio_int2);
			err = -EINVAL;
  			goto err_gpio2_valid;
		}
		
		err = gpio_request(stat->pdata_acc->gpio_int2, 
						"INTERRUPT_PIN2_LSM303D");
		if(err < 0) {
			dev_err(&client->dev, "Unable to request GPIO [%d].\n",
						stat->pdata_acc->gpio_int2);
  			err = -EINVAL;
			goto err_gpio2_valid;
		}
		gpio_direction_input(stat->pdata_acc->gpio_int2);
		stat->irq2 = gpio_to_irq(stat->pdata_acc->gpio_int2);
		if(stat->irq2 < 0) {
			dev_err(&client->dev, "GPIO [%d] cannot be used as "
				"interrupt.\n", stat->pdata_acc->gpio_int2);
			err = -EINVAL;
			goto err_gpio2_irq;
		}
		pr_info("%s: %s has set irq2 to irq: %d, "
							"mapped on gpio:%d\n",
			LSM303D_DEV_NAME, __func__, stat->irq2,
						stat->pdata_acc->gpio_int2);
	}

	err = lsm303d_hw_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "hw init failed: %d\n", err);
		goto err_hw_init;
	}

	err = lsm303d_acc_device_power_on(stat);
	if (err < 0) {
		dev_err(&client->dev, "accelerometer power on failed: "
								"%d\n", err);
		goto err_pdata_init;
	}
	err = lsm303d_mag_device_power_on(stat);
	if (err < 0) {
		dev_err(&client->dev, "magnetometer power on failed: "
								"%d\n", err);
		goto err_pdata_init;
	}

	err = lsm303d_acc_update_fs_range(stat, stat->pdata_acc->fs_range);
	if (err < 0) {
		dev_err(&client->dev, "update_fs_range on accelerometer "
								"failed\n");
		goto  err_power_off_acc;
	}

	err = lsm303d_mag_update_fs_range(stat, stat->pdata_mag->fs_range);
	if (err < 0) {
		dev_err(&client->dev, "update_fs_range on magnetometer "
								"failed\n");
		goto  err_power_off_mag;
	}

	err = lsm303d_acc_update_odr(stat, stat->pdata_acc->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr on accelerometer failed\n");
		goto  err_power_off;
	}

	err = lsm303d_mag_update_odr(stat, stat->pdata_mag->poll_interval);
	if (err < 0) {
		dev_err(&client->dev, "update_odr on magnetometer failed\n");
		goto  err_power_off;
	}

	err = lsm303d_acc_update_filter(stat, 
					stat->pdata_acc->aa_filter_bandwidth);
	if (err < 0) {
		dev_err(&client->dev, "update_filter on accelerometer "
								"failed\n");
		goto  err_power_off;
	}

	err = lsm303d_acc_input_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "accelerometer input init failed\n");
		goto err_power_off;
	}

	err = lsm303d_mag_input_init(stat);
	if (err < 0) {
		dev_err(&client->dev, "magnetometer input init failed\n");
		goto err_power_off;
	}

	err = create_sysfs_interfaces(stat);
	if (err < 0) {
		dev_err(&client->dev,
		"device LSM303D_DEV_NAME sysfs register failed\n");
		goto err_input_cleanup;
	}

	lsm303d_acc_device_power_off(stat);
	lsm303d_mag_device_power_off(stat);

	if(stat->pdata_acc->gpio_int1 >= 0){
		INIT_WORK(&stat->irq1_work, lsm303d_irq1_work_func);
		stat->irq1_work_queue =
				create_singlethread_workqueue("lsm303d_wq1");
		if (!stat->irq1_work_queue) {
			err = -ENOMEM;
			dev_err(&client->dev,
					"cannot create work queue1: %d\n", err);
			goto err_remove_sysfs_int;
		}
		err = request_irq(stat->irq1, lsm303d_isr1,
				IRQF_TRIGGER_RISING, "lsm303d_irq1", stat);
		if (err < 0) {
			dev_err(&client->dev, "request irq1 failed: %d\n", err);
			goto err_destoyworkqueue1;
		}
		disable_irq_nosync(stat->irq1);
	}

	if(stat->pdata_acc->gpio_int2 >= 0){
		INIT_WORK(&stat->irq2_work, lsm303d_irq2_work_func);
		stat->irq2_work_queue =
				create_singlethread_workqueue("lsm303d_wq2");
		if (!stat->irq2_work_queue) {
			err = -ENOMEM;
			dev_err(&client->dev,
					"cannot create work queue2: %d\n", err);
			goto err_free_irq1;
		}
		err = request_irq(stat->irq2, lsm303d_isr2,
				IRQF_TRIGGER_RISING, "lsm303d_irq2", stat);
		if (err < 0) {
			dev_err(&client->dev, "request irq2 failed: %d\n", err);
			goto err_destoyworkqueue2;
		}
		disable_irq_nosync(stat->irq2);
	}

	INIT_WORK(&stat->input_work_acc, poll_function_work_acc);
	INIT_WORK(&stat->input_work_mag, poll_function_work_mag);

	mutex_unlock(&stat->lock);
	dev_info(&client->dev, "%s: probed\n", LSM303D_DEV_NAME);
	return 0;

err_destoyworkqueue2:
	destroy_workqueue(stat->irq2_work_queue);
err_free_irq1:
	free_irq(stat->irq1, stat);
err_destoyworkqueue1:
	destroy_workqueue(stat->irq1_work_queue);
err_remove_sysfs_int:
	remove_sysfs_interfaces(&client->dev);
err_input_cleanup:
	lsm303d_input_cleanup(stat);
err_power_off:
err_power_off_mag:
	lsm303d_mag_device_power_off(stat);
err_power_off_acc:
	lsm303d_acc_device_power_off(stat);
	kfree(stat->interrupt);
err_hw_init:
err_gpio2_irq:
	gpio_free(stat->pdata_acc->gpio_int2);
err_gpio2_valid:
err_gpio1_irq:
	gpio_free(stat->pdata_acc->gpio_int1);
err_gpio1_valid:
err_pdata_init:
err_pdata_mag_init:
	if (stat->pdata_mag->exit)
		stat->pdata_mag->exit();
err_pdata_acc_init:
	if (stat->pdata_acc->exit)
		stat->pdata_acc->exit();
exit_kfree_pdata:
	kfree(stat->pdata_acc);
	kfree(stat->pdata_mag);
err_mutexunlock:
	mutex_unlock(&stat->lock);
	kfree(stat);
	if(!lsm303d_workqueue) {
		flush_workqueue(lsm303d_workqueue);
		destroy_workqueue(lsm303d_workqueue);
	}
exit_check_functionality_failed:
	pr_err("%s: Driver Init failed\n", LSM303D_DEV_NAME);
	return err;
}

static int lsm303d_remove(struct i2c_client *client)
{
	struct lsm303d_status *stat = i2c_get_clientdata(client);

	lsm303d_acc_disable(stat);
	lsm303d_mag_disable(stat);
	lsm303d_temperature_disable(stat);

	if(stat->pdata_acc->gpio_int1 >= 0) {
		free_irq(stat->irq1, stat);
		gpio_free(stat->pdata_acc->gpio_int1);
		destroy_workqueue(stat->irq1_work_queue);
	}

	if(stat->pdata_acc->gpio_int2 >= 0) {
		free_irq(stat->irq2, stat);
		gpio_free(stat->pdata_acc->gpio_int2);
		destroy_workqueue(stat->irq2_work_queue);
	}

	lsm303d_acc_input_cleanup(stat);
	lsm303d_mag_input_cleanup(stat);

	remove_sysfs_interfaces(&client->dev);

	if (stat->pdata_acc->exit)
		stat->pdata_acc->exit();

	if (stat->pdata_mag->exit)
		stat->pdata_mag->exit();

	if((stat->pdata_acc->gpio_int1 >= 0)||
					(stat->pdata_acc->gpio_int2 >= 0)) {
		kfree(stat->interrupt);
	}

	if(!lsm303d_workqueue) {
		flush_workqueue(lsm303d_workqueue);
		destroy_workqueue(lsm303d_workqueue);
	}

	kfree(stat->pdata_acc);
	kfree(stat->pdata_mag);
	kfree(stat);
	return 0;
}

static const struct i2c_device_id lsm303d_id[] 
					= { { LSM303D_DEV_NAME, 0 }, { }, };

MODULE_DEVICE_TABLE(i2c, lsm303d_id);

static struct i2c_driver lsm303d_driver = {
	.driver = {
			.owner = THIS_MODULE,
			.name = LSM303D_DEV_NAME,
		  },
	.probe = lsm303d_probe,
	.remove = lsm303d_remove,
	.id_table = lsm303d_id,
};

static int __init lsm303d_init(void)
{
	pr_info("%s driver: init\n", LSM303D_DEV_NAME);
	return i2c_add_driver(&lsm303d_driver);
}

static void __exit lsm303d_exit(void)
{
	pr_info("%s driver exit\n", LSM303D_DEV_NAME);
	i2c_del_driver(&lsm303d_driver);
}

module_init(lsm303d_init);
module_exit(lsm303d_exit);

MODULE_DESCRIPTION("lsm303d accelerometer and magnetometer driver");
MODULE_AUTHOR("Matteo Dameno, Denis Ciocca, STMicroelectronics");
MODULE_LICENSE("GPL");
