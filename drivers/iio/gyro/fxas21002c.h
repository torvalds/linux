/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Driver for NXP FXAS21002C Gyroscope - Header
 *
 * Copyright (C) 2019 Linaro Ltd.
 */

#ifndef FXAS21002C_H_
#define FXAS21002C_H_

#include <linux/regmap.h>

#define FXAS21002C_REG_STATUS		0x00
#define FXAS21002C_REG_OUT_X_MSB	0x01
#define FXAS21002C_REG_OUT_X_LSB	0x02
#define FXAS21002C_REG_OUT_Y_MSB	0x03
#define FXAS21002C_REG_OUT_Y_LSB	0x04
#define FXAS21002C_REG_OUT_Z_MSB	0x05
#define FXAS21002C_REG_OUT_Z_LSB	0x06
#define FXAS21002C_REG_DR_STATUS	0x07
#define FXAS21002C_REG_F_STATUS		0x08
#define FXAS21002C_REG_F_SETUP		0x09
#define FXAS21002C_REG_F_EVENT		0x0A
#define FXAS21002C_REG_INT_SRC_FLAG	0x0B
#define FXAS21002C_REG_WHO_AM_I		0x0C
#define FXAS21002C_REG_CTRL0		0x0D
#define FXAS21002C_REG_RT_CFG		0x0E
#define FXAS21002C_REG_RT_SRC		0x0F
#define FXAS21002C_REG_RT_THS		0x10
#define FXAS21002C_REG_RT_COUNT		0x11
#define FXAS21002C_REG_TEMP		0x12
#define FXAS21002C_REG_CTRL1		0x13
#define FXAS21002C_REG_CTRL2		0x14
#define FXAS21002C_REG_CTRL3		0x15

enum fxas21002c_fields {
	F_DR_STATUS,
	F_OUT_X_MSB,
	F_OUT_X_LSB,
	F_OUT_Y_MSB,
	F_OUT_Y_LSB,
	F_OUT_Z_MSB,
	F_OUT_Z_LSB,
	/* DR_STATUS */
	F_ZYX_OW, F_Z_OW, F_Y_OW, F_X_OW, F_ZYX_DR, F_Z_DR, F_Y_DR, F_X_DR,
	/* F_STATUS */
	F_OVF, F_WMKF, F_CNT,
	/* F_SETUP */
	F_MODE, F_WMRK,
	/* F_EVENT */
	F_EVENT, FE_TIME,
	/* INT_SOURCE_FLAG */
	F_BOOTEND, F_SRC_FIFO, F_SRC_RT, F_SRC_DRDY,
	/* WHO_AM_I */
	F_WHO_AM_I,
	/* CTRL_REG0 */
	F_BW, F_SPIW, F_SEL, F_HPF_EN, F_FS,
	/* RT_CFG */
	F_ELE, F_ZTEFE, F_YTEFE, F_XTEFE,
	/* RT_SRC */
	F_EA, F_ZRT, F_ZRT_POL, F_YRT, F_YRT_POL, F_XRT, F_XRT_POL,
	/* RT_THS */
	F_DBCNTM, F_THS,
	/* RT_COUNT */
	F_RT_COUNT,
	/* TEMP */
	F_TEMP,
	/* CTRL_REG1 */
	F_RST, F_ST, F_DR, F_ACTIVE, F_READY,
	/* CTRL_REG2 */
	F_INT_CFG_FIFO, F_INT_EN_FIFO, F_INT_CFG_RT, F_INT_EN_RT,
	F_INT_CFG_DRDY, F_INT_EN_DRDY, F_IPOL, F_PP_OD,
	/* CTRL_REG3 */
	F_WRAPTOONE, F_EXTCTRLEN, F_FS_DOUBLE,
	/* MAX FIELDS */
	F_MAX_FIELDS,
};

extern const struct dev_pm_ops fxas21002c_pm_ops;

int fxas21002c_core_probe(struct device *dev, struct regmap *regmap, int irq,
			  const char *name);
void fxas21002c_core_remove(struct device *dev);
#endif
