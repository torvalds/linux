/*
 *  fxls8471.h - Linux kernel modules for 3-Axis Accel sensor
 *  Copyright (C) 2014-2015 Freescale Semiconductor, Inc. All Rights Reserved.
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
 *
 */

#ifndef _FXSL8471_H
#define _FXSL8471_H
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/input.h>

#define FXSL8471_ID  0x6a

/* register enum for fxls8471 registers */
enum { FXLS8471_STATUS = 0x00, FXLS8471_OUT_X_MSB, FXLS8471_OUT_X_LSB,
	FXLS8471_OUT_Y_MSB, FXLS8471_OUT_Y_LSB, FXLS8471_OUT_Z_MSB,
	FXLS8471_OUT_Z_LSB, FXLS8471_F_SETUP =
	    0x09, FXLS8471_TRIG_CFG, FXLS8471_SYSMOD,
	FXLS8471_INT_SOURCE, FXLS8471_WHO_AM_I,
	FXLS8471_XYZ_DATA_CFG, FXLS8471_HP_FILTER_CUTOFF,
	FXLS8471_PL_STATUS, FXLS8471_PL_CFG, FXLS8471_PL_COUNT,
	FXLS8471_PL_BF_ZCOMP, FXLS8471_P_L_THS_REG,
	FXLS8471_FF_MT_CFG, FXLS8471_FF_MT_SRC, FXLS8471_FF_MT_THS,
	FXLS8471_FF_MT_COUNT, FXLS8471_TRANSIENT_CFG =
	    0x1D, FXLS8471_TRANSIENT_SRC, FXLS8471_TRANSIENT_THS,
	FXLS8471_TRANSIENT_COUNT, FXLS8471_PULSE_CFG,
	FXLS8471_PULSE_SRC, FXLS8471_PULSE_THSX, FXLS8471_PULSE_THSY,
	FXLS8471_PULSE_THSZ, FXLS8471_PULSE_TMLT,
	FXLS8471_PULSE_LTCY, FXLS8471_PULSE_WIND,
	FXLS8471_ASLP_COUNT, FXLS8471_CTRL_REG1, FXLS8471_CTRL_REG2,
	FXLS8471_CTRL_REG3, FXLS8471_CTRL_REG4, FXLS8471_CTRL_REG5,
	FXLS8471_OFF_X, FXLS8471_OFF_Y, FXLS8471_OFF_Z,
	FXLS8471_REG_END,
};

enum { STANDBY = 0, ACTIVED,
};

enum { MODE_2G = 0, MODE_4G, MODE_8G,
};

struct fxls8471_data_axis {
	short x;
	short y;
	short z;
};

struct fxls8471_data {
	void *bus_priv;
	u16 bus_type;
	int irq;
	s32 (*write)(struct fxls8471_data *pdata, u8 reg, u8 val);
	s32 (*read)(struct fxls8471_data *pdata, u8 reg);
	s32 (*read_block)(struct fxls8471_data *pdata, u8 reg, u8 len,
			   u8 *val);
	struct input_dev *idev;
	atomic_t active;
	atomic_t delay;
	atomic_t position;
	u8 chip_id;
};

extern struct fxls8471_data fxls8471_dev;

int fxls8471_driver_init(struct fxls8471_data *pdata);
int fxls8471_driver_remove(struct fxls8471_data *pdata);
int fxls8471_driver_suspend(struct fxls8471_data *pdata);
int fxls8471_driver_resume(struct fxls8471_data *pdata);

#endif /*  */
