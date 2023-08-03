/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Rockchip Electronics Co., Ltd.
 *
 */

#ifndef __MAXIM4C_I2C_H__
#define __MAXIM4C_I2C_H__

#include <linux/i2c.h>

/* register address: 8bit or 16bit */
#define MAXIM4C_I2C_REG_ADDR_08BITS	1
#define MAXIM4C_I2C_REG_ADDR_16BITS	2

/* register value: 8bit or 16bit or 24bit */
#define MAXIM4C_I2C_REG_VALUE_08BITS	1
#define MAXIM4C_I2C_REG_VALUE_16BITS	2
#define MAXIM4C_I2C_REG_VALUE_24BITS	3

/* I2C Device ID */
enum {
	MAXIM4C_I2C_DES_DEF,	/* Deserializer I2C address: Default */

	MAXIM4C_I2C_SER_DEF,	/* Serializer I2C address: Default */
	MAXIM4C_I2C_SER_MAP,	/* Serializer I2C address: Mapping */

	MAXIM4C_I2C_CAM_DEF,	/* Camera I2C address: Default */
	MAXIM4C_I2C_CAM_MAP,	/* Camera I2C address: Mapping */

	MAXIM4C_I2C_DEV_MAX,
};

/* i2c register array end */
#define MAXIM4C_REG_NULL		0xFFFF

struct maxim4c_i2c_regval {
	u16 reg_len;
	u16 reg_addr;
	u32 val_len;
	u32 reg_val;
	u32 val_mask;
	u8 delay;
};

/* seq_item_size = reg_len + val_len * 2 + 1 */
struct maxim4c_i2c_init_seq {
	struct maxim4c_i2c_regval *reg_init_seq;
	u32 reg_seq_size;
	u32 seq_item_size;
	u32 reg_len;
	u32 val_len;
};

#endif /* __MAXIM4C_I2C_H__ */
