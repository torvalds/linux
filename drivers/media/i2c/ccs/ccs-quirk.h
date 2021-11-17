/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/i2c/ccs/ccs-quirk.h
 *
 * Generic driver for MIPI CCS/SMIA/SMIA++ compliant camera sensors
 *
 * Copyright (C) 2020 Intel Corporation
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@linux.intel.com>
 */

#ifndef __CCS_QUIRK__
#define __CCS_QUIRK__

struct ccs_sensor;

/**
 * struct ccs_quirk - quirks for sensors that deviate from SMIA++ standard
 *
 * @limits: Replace sensor->limits with values which can't be read from
 *	    sensor registers. Called the first time the sensor is powered up.
 * @post_poweron: Called always after the sensor has been fully powered on.
 * @pre_streamon: Called just before streaming is enabled.
 * @post_streamoff: Called right after stopping streaming.
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
 * @flags: Quirk flags
 *
 *		@return: 0 on success, -ENOIOCTLCMD if no register
 *			 access may be done by the caller (default read
 *			 value is zero), else negative error code on error
 */
struct ccs_quirk {
	int (*limits)(struct ccs_sensor *sensor);
	int (*post_poweron)(struct ccs_sensor *sensor);
	int (*pre_streamon)(struct ccs_sensor *sensor);
	int (*post_streamoff)(struct ccs_sensor *sensor);
	unsigned long (*pll_flags)(struct ccs_sensor *sensor);
	int (*init)(struct ccs_sensor *sensor);
	int (*reg_access)(struct ccs_sensor *sensor, bool write, u32 *reg,
			  u32 *val);
	unsigned long flags;
};

#define CCS_QUIRK_FLAG_8BIT_READ_ONLY			(1 << 0)

struct ccs_reg_8 {
	u16 reg;
	u8 val;
};

#define CCS_MK_QUIRK_REG_8(_reg, _val) \
	{				\
		.reg = (u16)_reg,	\
		.val = _val,		\
	}

#define ccs_call_quirk(sensor, _quirk, ...)				\
	((sensor)->minfo.quirk &&					\
	 (sensor)->minfo.quirk->_quirk ?				\
	 (sensor)->minfo.quirk->_quirk(sensor, ##__VA_ARGS__) : 0)

#define ccs_needs_quirk(sensor, _quirk)		\
	((sensor)->minfo.quirk ?			\
	 (sensor)->minfo.quirk->flags & _quirk : 0)

extern const struct ccs_quirk smiapp_jt8ev1_quirk;
extern const struct ccs_quirk smiapp_imx125es_quirk;
extern const struct ccs_quirk smiapp_jt8ew9_quirk;
extern const struct ccs_quirk smiapp_tcm8500md_quirk;

#endif /* __CCS_QUIRK__ */
