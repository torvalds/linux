// SPDX-License-Identifier: GPL-2.0-only
/*
 * drivers/media/i2c/ccs/ccs-reg-access.c
 *
 * Generic driver for MIPI CCS/SMIA/SMIA++ compliant camera sensors
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#include <asm/unaligned.h>

#include <linux/delay.h>
#include <linux/i2c.h>

#include "ccs.h"
#include "ccs-limits.h"

static u32 float_to_u32_mul_1000000(struct i2c_client *client, u32 phloat)
{
	s32 exp;
	u64 man;

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
static int ____ccs_read_addr(struct ccs_sensor *sensor, u16 reg, u16 len,
			     u32 *val)
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
static int ____ccs_read_addr_8only(struct ccs_sensor *sensor, u16 reg,
				   u16 len, u32 *val)
{
	unsigned int i;
	int rval;

	*val = 0;

	for (i = 0; i < len; i++) {
		u32 val8;

		rval = ____ccs_read_addr(sensor, reg + i, 1, &val8);
		if (rval < 0)
			return rval;
		*val |= val8 << ((len - i - 1) << 3);
	}

	return 0;
}

unsigned int ccs_reg_width(u32 reg)
{
	if (reg & CCS_FL_16BIT)
		return sizeof(u16);
	if (reg & CCS_FL_32BIT)
		return sizeof(u32);

	return sizeof(u8);
}

static u32 ireal32_to_u32_mul_1000000(struct i2c_client *client, u32 val)
{
	if (val >> 10 > U32_MAX / 15625) {
		dev_warn(&client->dev, "value %u overflows!\n", val);
		return U32_MAX;
	}

	return ((val >> 10) * 15625) +
		(val & GENMASK(9, 0)) * 15625 / 1024;
}

u32 ccs_reg_conv(struct ccs_sensor *sensor, u32 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);

	if (reg & CCS_FL_FLOAT_IREAL) {
		if (CCS_LIM(sensor, CLOCK_CAPA_TYPE_CAPABILITY) &
		    CCS_CLOCK_CAPA_TYPE_CAPABILITY_IREAL)
			val = ireal32_to_u32_mul_1000000(client, val);
		else
			val = float_to_u32_mul_1000000(client, val);
	} else if (reg & CCS_FL_IREAL) {
		val = ireal32_to_u32_mul_1000000(client, val);
	}

	return val;
}

/*
 * Read a 8/16/32-bit i2c register.  The value is returned in 'val'.
 * Returns zero if successful, or non-zero otherwise.
 */
static int __ccs_read_addr(struct ccs_sensor *sensor, u32 reg, u32 *val,
			   bool only8, bool conv)
{
	unsigned int len = ccs_reg_width(reg);
	int rval;

	if (!only8)
		rval = ____ccs_read_addr(sensor, CCS_REG_ADDR(reg), len, val);
	else
		rval = ____ccs_read_addr_8only(sensor, CCS_REG_ADDR(reg), len,
					       val);
	if (rval < 0)
		return rval;

	if (!conv)
		return 0;

	*val = ccs_reg_conv(sensor, reg, *val);

	return 0;
}

static int __ccs_read_data(struct ccs_reg *regs, size_t num_regs,
			   u32 reg, u32 *val)
{
	unsigned int width = ccs_reg_width(reg);
	size_t i;

	for (i = 0; i < num_regs; i++, regs++) {
		u8 *data;

		if (regs->addr + regs->len < CCS_REG_ADDR(reg) + width)
			continue;

		if (regs->addr > CCS_REG_ADDR(reg))
			break;

		data = &regs->value[CCS_REG_ADDR(reg) - regs->addr];

		switch (width) {
		case sizeof(u8):
			*val = *data;
			break;
		case sizeof(u16):
			*val = get_unaligned_be16(data);
			break;
		case sizeof(u32):
			*val = get_unaligned_be32(data);
			break;
		default:
			WARN_ON(1);
			return -EINVAL;
		}

		return 0;
	}

	return -ENOENT;
}

static int ccs_read_data(struct ccs_sensor *sensor, u32 reg, u32 *val)
{
	if (!__ccs_read_data(sensor->sdata.sensor_read_only_regs,
			     sensor->sdata.num_sensor_read_only_regs,
			     reg, val))
		return 0;

	return __ccs_read_data(sensor->mdata.module_read_only_regs,
			       sensor->mdata.num_module_read_only_regs,
			       reg, val);
}

static int ccs_read_addr_raw(struct ccs_sensor *sensor, u32 reg, u32 *val,
			     bool force8, bool quirk, bool conv, bool data)
{
	int rval;

	if (data) {
		rval = ccs_read_data(sensor, reg, val);
		if (!rval)
			return 0;
	}

	if (quirk) {
		*val = 0;
		rval = ccs_call_quirk(sensor, reg_access, false, &reg, val);
		if (rval == -ENOIOCTLCMD)
			return 0;
		if (rval < 0)
			return rval;

		if (force8)
			return __ccs_read_addr(sensor, reg, val, true, conv);
	}

	return __ccs_read_addr(sensor, reg, val,
			       ccs_needs_quirk(sensor,
					       CCS_QUIRK_FLAG_8BIT_READ_ONLY),
			       conv);
}

int ccs_read_addr(struct ccs_sensor *sensor, u32 reg, u32 *val)
{
	return ccs_read_addr_raw(sensor, reg, val, false, true, true, true);
}

int ccs_read_addr_8only(struct ccs_sensor *sensor, u32 reg, u32 *val)
{
	return ccs_read_addr_raw(sensor, reg, val, true, true, true, true);
}

int ccs_read_addr_noconv(struct ccs_sensor *sensor, u32 reg, u32 *val)
{
	return ccs_read_addr_raw(sensor, reg, val, false, true, false, true);
}

static int ccs_write_retry(struct i2c_client *client, struct i2c_msg *msg)
{
	unsigned int retries;
	int r;

	for (retries = 0; retries < 10; retries++) {
		/*
		 * Due to unknown reason sensor stops responding. This
		 * loop is a temporaty solution until the root cause
		 * is found.
		 */
		r = i2c_transfer(client->adapter, msg, 1);
		if (r != 1) {
			usleep_range(1000, 2000);
			continue;
		}

		if (retries)
			dev_err(&client->dev,
				"sensor i2c stall encountered. retries: %d\n",
				retries);
		return 0;
	}

	return r;
}

int ccs_write_addr_no_quirk(struct ccs_sensor *sensor, u32 reg, u32 val)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	struct i2c_msg msg;
	unsigned char data[6];
	unsigned int len = ccs_reg_width(reg);
	int r;

	if (len > sizeof(data) - 2)
		return -EINVAL;

	msg.addr = client->addr;
	msg.flags = 0; /* Write */
	msg.len = 2 + len;
	msg.buf = data;

	put_unaligned_be16(CCS_REG_ADDR(reg), data);
	put_unaligned_be32(val << (8 * (sizeof(val) - len)), data + 2);

	dev_dbg(&client->dev, "writing reg 0x%4.4x value 0x%*.*x (%u)\n",
		CCS_REG_ADDR(reg), ccs_reg_width(reg) << 1,
		ccs_reg_width(reg) << 1, val, val);

	r = ccs_write_retry(client, &msg);
	if (r)
		dev_err(&client->dev,
			"wrote 0x%x to offset 0x%x error %d\n", val,
			CCS_REG_ADDR(reg), r);

	return r;
}

/*
 * Write to a 8/16-bit register.
 * Returns zero if successful, or non-zero otherwise.
 */
int ccs_write_addr(struct ccs_sensor *sensor, u32 reg, u32 val)
{
	int rval;

	rval = ccs_call_quirk(sensor, reg_access, true, &reg, &val);
	if (rval == -ENOIOCTLCMD)
		return 0;
	if (rval < 0)
		return rval;

	return ccs_write_addr_no_quirk(sensor, reg, val);
}

#define MAX_WRITE_LEN	32U

int ccs_write_data_regs(struct ccs_sensor *sensor, struct ccs_reg *regs,
			size_t num_regs)
{
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->src->sd);
	unsigned char buf[2 + MAX_WRITE_LEN];
	struct i2c_msg msg = {
		.addr = client->addr,
		.buf = buf,
	};
	size_t i;

	for (i = 0; i < num_regs; i++, regs++) {
		unsigned char *regdata = regs->value;
		unsigned int j;

		for (j = 0; j < regs->len;
		     j += msg.len - 2, regdata += msg.len - 2) {
			char printbuf[(MAX_WRITE_LEN << 1) +
				      1 /* \0 */] = { 0 };
			int rval;

			msg.len = min(regs->len - j, MAX_WRITE_LEN);

			bin2hex(printbuf, regdata, msg.len);
			dev_dbg(&client->dev,
				"writing msr reg 0x%4.4x value 0x%s\n",
				regs->addr + j, printbuf);

			put_unaligned_be16(regs->addr + j, buf);
			memcpy(buf + 2, regdata, msg.len);

			msg.len += 2;

			rval = ccs_write_retry(client, &msg);
			if (rval) {
				dev_err(&client->dev,
					"error writing %u octets to address 0x%4.4x\n",
					msg.len, regs->addr + j);
				return rval;
			}
		}
	}

	return 0;
}
