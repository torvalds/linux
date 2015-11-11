/*
 * drivers/media/i2c/smiapp/smiapp-quirk.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@iki.fi>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#ifndef __SMIAPP_QUIRK__
#define __SMIAPP_QUIRK__

struct smiapp_sensor;

/**
 * struct smiapp_quirk - quirks for sensors that deviate from SMIA++ standard
 *
 * @limits: Replace sensor->limits with values which can't be read from
 *	    sensor registers. Called the first time the sensor is powered up.
 * @post_poweron: Called always after the sensor has been fully powered on.
 * @pre_streamon: Called just before streaming is enabled.
 * @post_streamon: Called right after stopping streaming.
 * @pll_flags: Return flags for the PLL calculator.
 * @init: Quirk initialisation, called the last in probe(). This is
 *	  also appropriate for adding sensor specific controls, for instance.
 * @reg_access: Register access quirk. The quirk may divert the access
 *		to another register, or no register at all.
 *
 *		@write: Is this read (false) or write (true) access?
 *		@reg: Pointer to the register to access
 *		@value: Register value, set by the caller on write, or
 *			by the quirk on read
 *
 *		@return: 0 on success, -ENOIOCTLCMD if no register
 *			 access may be done by the caller (default read
 *			 value is zero), else negative error code on error
 */
struct smiapp_quirk {
	int (*limits)(struct smiapp_sensor *sensor);
	int (*post_poweron)(struct smiapp_sensor *sensor);
	int (*pre_streamon)(struct smiapp_sensor *sensor);
	int (*post_streamoff)(struct smiapp_sensor *sensor);
	unsigned long (*pll_flags)(struct smiapp_sensor *sensor);
	int (*init)(struct smiapp_sensor *sensor);
	int (*reg_access)(struct smiapp_sensor *sensor, bool write, u32 *reg,
			  u32 *val);
	unsigned long flags;
};

#define SMIAPP_QUIRK_FLAG_8BIT_READ_ONLY			(1 << 0)

struct smiapp_reg_8 {
	u16 reg;
	u8 val;
};

void smiapp_replace_limit(struct smiapp_sensor *sensor,
			  u32 limit, u32 val);

#define SMIAPP_MK_QUIRK_REG_8(_reg, _val) \
	{				\
		.reg = (u16)_reg,	\
		.val = _val,		\
	}

#define smiapp_call_quirk(sensor, _quirk, ...)				\
	((sensor)->minfo.quirk &&					\
	 (sensor)->minfo.quirk->_quirk ?				\
	 (sensor)->minfo.quirk->_quirk(sensor, ##__VA_ARGS__) : 0)

#define smiapp_needs_quirk(sensor, _quirk)		\
	((sensor)->minfo.quirk ?			\
	 (sensor)->minfo.quirk->flags & _quirk : 0)

extern const struct smiapp_quirk smiapp_jt8ev1_quirk;
extern const struct smiapp_quirk smiapp_imx125es_quirk;
extern const struct smiapp_quirk smiapp_jt8ew9_quirk;
extern const struct smiapp_quirk smiapp_tcm8500md_quirk;

#endif /* __SMIAPP_QUIRK__ */
