/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022 ROHM Semiconductors
 *
 * ROHM/KIONIX KX022A accelerometer driver
 */

#ifndef _KX022A_H_
#define _KX022A_H_

#include <linux/bits.h>
#include <linux/regmap.h>

#define KX022A_REG_WHO		0x0f
#define KX022A_ID		0xc8

#define KX022A_REG_CNTL2	0x19
#define KX022A_MASK_SRST	BIT(7)
#define KX022A_REG_CNTL		0x18
#define KX022A_MASK_PC1		BIT(7)
#define KX022A_MASK_RES		BIT(6)
#define KX022A_MASK_DRDY	BIT(5)
#define KX022A_MASK_GSEL	GENMASK(4, 3)
#define KX022A_GSEL_SHIFT	3
#define KX022A_GSEL_2		0x0
#define KX022A_GSEL_4		BIT(3)
#define KX022A_GSEL_8		BIT(4)
#define KX022A_GSEL_16		GENMASK(4, 3)

#define KX022A_REG_INS2		0x13
#define KX022A_MASK_INS2_DRDY	BIT(4)
#define KX122_MASK_INS2_WMI	BIT(5)

#define KX022A_REG_XHP_L	0x0
#define KX022A_REG_XOUT_L	0x06
#define KX022A_REG_YOUT_L	0x08
#define KX022A_REG_ZOUT_L	0x0a
#define KX022A_REG_COTR		0x0c
#define KX022A_REG_TSCP		0x10
#define KX022A_REG_INT_REL	0x17

#define KX022A_REG_ODCNTL	0x1b

#define KX022A_REG_BTS_WUF_TH	0x31
#define KX022A_REG_MAN_WAKE	0x2c

#define KX022A_REG_BUF_CNTL1	0x3a
#define KX022A_MASK_WM_TH	GENMASK(6, 0)
#define KX022A_REG_BUF_CNTL2	0x3b
#define KX022A_MASK_BUF_EN	BIT(7)
#define KX022A_MASK_BRES16	BIT(6)
#define KX022A_REG_BUF_STATUS_1	0x3c
#define KX022A_REG_BUF_STATUS_2	0x3d
#define KX022A_REG_BUF_CLEAR	0x3e
#define KX022A_REG_BUF_READ	0x3f
#define KX022A_MASK_ODR		GENMASK(3, 0)
#define KX022A_ODR_SHIFT	3
#define KX022A_FIFO_MAX_WMI_TH	41

#define KX022A_REG_INC1		0x1c
#define KX022A_REG_INC5		0x20
#define KX022A_REG_INC6		0x21
#define KX022A_MASK_IEN		BIT(5)
#define KX022A_MASK_IPOL	BIT(4)
#define KX022A_IPOL_LOW		0
#define KX022A_IPOL_HIGH	KX022A_MASK_IPOL1
#define KX022A_MASK_ITYP	BIT(3)
#define KX022A_ITYP_PULSE	KX022A_MASK_ITYP
#define KX022A_ITYP_LEVEL	0

#define KX022A_REG_INC4		0x1f
#define KX022A_MASK_WMI		BIT(5)

#define KX022A_REG_SELF_TEST	0x60
#define KX022A_MAX_REGISTER	0x60

struct device;

int kx022a_probe_internal(struct device *dev);
extern const struct regmap_config kx022a_regmap;

#endif
