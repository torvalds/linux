/*
 * drivers/media/video/smiapp/smiapp-quirk.h
 *
 * Generic driver for SMIA/SMIA++ compliant camera modules
 *
 * Copyright (C) 2011--2012 Nokia Corporation
 * Contact: Sakari Ailus <sakari.ailus@maxwell.research.nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
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
 */
struct smiapp_quirk {
	int (*limits)(struct smiapp_sensor *sensor);
	int (*post_poweron)(struct smiapp_sensor *sensor);
	int (*pre_streamon)(struct smiapp_sensor *sensor);
	int (*post_streamoff)(struct smiapp_sensor *sensor);
	const struct smia_reg *regs;
	unsigned long flags;
};

/* op pix clock is for all lanes in total normally */
#define SMIAPP_QUIRK_FLAG_OP_PIX_CLOCK_PER_LANE			(1 << 0)
#define SMIAPP_QUIRK_FLAG_8BIT_READ_ONLY			(1 << 1)

struct smiapp_reg_8 {
	u16 reg;
	u8 val;
};

void smiapp_replace_limit(struct smiapp_sensor *sensor,
			  u32 limit, u32 val);
bool smiapp_quirk_reg(struct smiapp_sensor *sensor,
		      u32 reg, u32 *val);

#define SMIAPP_MK_QUIRK_REG(_reg, _val) \
	{				\
		.type = (_reg >> 16),	\
		.reg = (u16)_reg,	\
		.val = _val,		\
	}

#define smiapp_call_quirk(_sensor, _quirk, ...)				\
	(_sensor->minfo.quirk &&					\
	 _sensor->minfo.quirk->_quirk ?					\
	 _sensor->minfo.quirk->_quirk(_sensor, ##__VA_ARGS__) : 0)

#define smiapp_needs_quirk(_sensor, _quirk)		\
	(_sensor->minfo.quirk ?				\
	 _sensor->minfo.quirk->flags & _quirk : 0)

extern const struct smiapp_quirk smiapp_jt8ev1_quirk;
extern const struct smiapp_quirk smiapp_imx125es_quirk;
extern const struct smiapp_quirk smiapp_jt8ew9_quirk;
extern const struct smiapp_quirk smiapp_tcm8500md_quirk;

#endif /* __SMIAPP_QUIRK__ */
