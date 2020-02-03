// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/smiapp/smiapp-regs.c
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 */

#include <asm/unaligned.h>

#include <linux/delay.h>
#include <linux/i2c.h>

#include "smiapp.h"
#include "smiapp-regs.h"

static uint32_t float_to_u32_mul_1000000(struct i2c_client *client,
					 uint32_t phloat)
{
	int32_t exp;
	uint64_t man;

	if (phloat >= 0x80000000) {
		dev_err(&client->dev, "this is a negative number\n");
		return 0;
	}

	if (phloat == 0x7f800000)
		return ~0; /* Inf. */

	if ((phloat & 0x7f800000) == 0x7f800000) {
		dev_err(&client->dev, "NaN or other special number\n");
		return 0;
	}

	/* Valid cases begin here */
	if (phloat == 0)
		return 0; /* Valid zero */

	if (phloat > 0x4f800000)
		return ~0; /* larger than 4294967295 */

	/*
	 * Unbias exponent (note how phloat is now guaranteed to
	 * have 0 in the high bit)
	 */
	exp = ((int32_t)phloat >> 23) - 127;

	/* Extract mantissa, add missing '1' bit and it's in MHz */
	man = ((phloat & 0x7fffff) | 0x800000) * 1000000ULL;

	if (exp < 0)
		man >>= -exp;
	else
		man <<= exp;

	man >>= 23; /* Remove mantissa bias */

	return man & 0xffffffff;
}


/*
 * Read a 8/16/32-bit i2c register.  The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int ____smiapp_read(struct smiapp_sensor *sensor, u16 reg,
			   u16 len, u32 *val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct i2c_msg msg;
	unsigned char data_buf[sizeof(u32)] = { 0 };
	unsigned char offset_buf[sizeof(u16)];
	int r;

	if (len > sizeof(data_buf))
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.len = sizeof(offset_buf);
	msg.buf = offset_buf;
	put_unaligned_be16(reg, offset_buf);

	r = i2c_transfer(client->adapter, &msg, 1);
	if (r != 1) {
		if (r >= 0)
			r = -EBUSY;
		goto err;
	}

	msg.len = len;
	msg.flags = I2C_M_RD;
	msg.buf = &data_buf[sizeof(data_buf) - len];

	r = i2c_transfer(client->adapter, &msg, 1);
	if (r != 1) {
		if (r >= 0)
			r = -EBUSY;
		goto err;
	}

	*val = get_unaligned_be32(data_buf);

	return 0;

err:
	dev_err(&client->dev, "read from offset 0x%x error %d\n", reg, r);

	return r;
}

/* Read a register using 8-bit access only. */
static int ____smiapp_read_8only(struct smiapp_sensor *sensor, u16 reg,
				 u16 len, u32 *val)
{
	unsigned int i;
	int rval;

	*val = 0;

	for (i = 0; i < len; i++) {
		u32 val8;

		rval = ____smiapp_read(sensor, reg + i, 1, &val8);
		if (rval < 0)
			return rval;
		*val |= val8 << ((len - i - 1) << 3);
	}

	return 0;
}

/*
 * Read a 8/16/32-bit i2c register.  The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int __smiapp_read(struct smiapp_sensor *sensor, u32 reg, u32 *val,
			 bool only8)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	u8 len = SMIAPP_REG_WIDTH(reg);
	int rval;

	if (len != SMIAPP_REG_8BIT && len != SMIAPP_REG_16BIT
	    && len != SMIAPP_REG_32BIT)
		return -EINVAL;

	if (!only8)
		rval = ____smiapp_read(sensor, SMIAPP_REG_ADDR(reg), len, val);
	else
		rval = ____smiapp_read_8only(sensor, SMIAPP_REG_ADDR(reg), len,
					     val);
	if (rval < 0)
		return rval;

	if (reg & SMIAPP_REG_FLAG_FLOAT)
		*val = float_to_u32_mul_1000000(client, *val);

	return 0;
}

int smiapp_read_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 *val)
{
	return __smiapp_read(
		sensor, reg, val,
		smiapp_needs_quirk(sensor,
				   SMIAPP_QUIRK_FLAG_8BIT_READ_ONLY));
}

static int smiapp_read_quirk(struct smiapp_sensor *sensor, u32 reg, u32 *val,
			     bool force8)
{
	int rval;

	*val = 0;
	rval = smiapp_call_quirk(sensor, reg_access, false, &reg, val);
	if (rval == -ENOIOCTLCMD)
		return 0;
	if (rval < 0)
		return rval;

	if (force8)
		return __smiapp_read(sensor, reg, val, true);

	return smiapp_read_no_quirk(sensor, reg, val);
}

int smiapp_read(struct smiapp_sensor *sensor, u32 reg, u32 *val)
{
	return smiapp_read_quirk(sensor, reg, val, false);
}

int smiapp_read_8only(struct smiapp_sensor *sensor, u32 reg, u32 *val)
{
	return smiapp_read_quirk(sensor, reg, val, true);
}

int smiapp_write_no_quirk(struct smiapp_sensor *sensor, u32 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct i2c_msg msg;
	unsigned char data[6];
	unsigned int retries;
	u8 len = SMIAPP_REG_WIDTH(reg);
	int r;

	if (len > sizeof(data) - 2)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0; /* Write */
	msg.len = 2 + len;
	msg.buf = data;

	put_unaligned_be16(SMIAPP_REG_ADDR(reg), data);
	put_unaligned_be32(val << (8 * (sizeof(val) - len)), data + 2);

	for (retries = 0; retries < 5; retries++) {
		/*
		 * Due to unknown reason sensor stops responding. This
		 * loop is a temporaty solution until the root cause
		 * is found.
		 */
		r = i2c_transfer(client->adapter, &msg, 1);
		if (r == 1) {
			if (retries)
				dev_err(&client->dev,
					"sensor i2c stall encountered. retries: %d\n",
					retries);
			return 0;
		}

		usleep_range(2000, 2000);
	}

	dev_err(&client->dev,
		"wrote 0x%x to offset 0x%x error %d\n", val,
		SMIAPP_REG_ADDR(reg), r);

	return r;
}

/*
 * Write to a 8/16-bit register.
 * Returns zero if successful, or non-zero otherwise.
 */
int smiapp_write(struct smiapp_sensor *sensor, u32 reg, u32 val)
{
	int rval;

	rval = smiapp_call_quirk(sensor, reg_access, true, &reg, &val);
	if (rval == -ENOIOCTLCMD)
		return 0;
	if (rval < 0)
		return rval;

	return smiapp_write_no_quirk(sensor, reg, val);
}
