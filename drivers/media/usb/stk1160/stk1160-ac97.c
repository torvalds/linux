// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * STK1160 driver
 *
 * Copyright (C) 2012 Ezequiel Garcia
 * <elezegarcia--a.t--gmail.com>
 *
 * Copyright (C) 2016 Marcel Hasler
 * <mahasler--a.t--gmail.com>
 *
 * Based on Easycap driver by R.M. Thomas
 *	Copyright (C) 2010 R.M. Thomas
 *	<rmthomas--a.t--sciolus.org>
 */

#include <linux/delay.h>

#include "stk1160.h"
#include "stk1160-reg.h"

static int stk1160_ac97_wait_transfer_complete(struct stk1160 *dev)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(STK1160_AC97_TIMEOUT);
	u8 value;

	/* Wait for AC97 transfer to complete */
	while (time_is_after_jiffies(timeout)) {
		stk1160_read_reg(dev, STK1160_AC97CTL_0, &value);

		if (!(value & (STK1160_AC97CTL_0_CR | STK1160_AC97CTL_0_CW)))
			return 0;

		usleep_range(50, 100);
	}

	stk1160_err("AC97 transfer took too long, this should never happen!");
	return -EBUSY;
}

static void stk1160_write_ac97(struct stk1160 *dev, u16 reg, u16 value)
{
	/* Set codec register address */
	stk1160_write_reg(dev, STK1160_AC97_ADDR, reg);

	/* Set codec command */
	stk1160_write_reg(dev, STK1160_AC97_CMD, value & 0xff);
	stk1160_write_reg(dev, STK1160_AC97_CMD + 1, (value & 0xff00) >> 8);

	/* Set command write bit to initiate write operation */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x8c);

	/* Wait for command write bit to be cleared */
	stk1160_ac97_wait_transfer_complete(dev);
}

#ifdef DEBUG
static u16 stk1160_read_ac97(struct stk1160 *dev, u16 reg)
{
	u8 vall = 0;
	u8 valh = 0;

	/* Set codec register address */
	stk1160_write_reg(dev, STK1160_AC97_ADDR, reg);

	/* Set command read bit to initiate read operation */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x8b);

	/* Wait for command read bit to be cleared */
	if (stk1160_ac97_wait_transfer_complete(dev) < 0)
		return 0;


	/* Retrieve register value */
	stk1160_read_reg(dev, STK1160_AC97_CMD, &vall);
	stk1160_read_reg(dev, STK1160_AC97_CMD + 1, &valh);

	return (valh << 8) | vall;
}

void stk1160_ac97_dump_regs(struct stk1160 *dev)
{
	u16 value;

	value = stk1160_read_ac97(dev, 0x12); /* CD volume */
	stk1160_dbg("0x12 == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x10); /* Line-in volume */
	stk1160_dbg("0x10 == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x0e); /* MIC volume (mono) */
	stk1160_dbg("0x0e == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x16); /* Aux volume */
	stk1160_dbg("0x16 == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x1a); /* Record select */
	stk1160_dbg("0x1a == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x02); /* Master volume */
	stk1160_dbg("0x02 == 0x%04x", value);

	value = stk1160_read_ac97(dev, 0x1c); /* Record gain */
	stk1160_dbg("0x1c == 0x%04x", value);
}
#endif

static int stk1160_has_audio(struct stk1160 *dev)
{
	u8 value;

	stk1160_read_reg(dev, STK1160_POSV_L, &value);
	return !(value & STK1160_POSV_L_ACDOUT);
}

static int stk1160_has_ac97(struct stk1160 *dev)
{
	u8 value;

	stk1160_read_reg(dev, STK1160_POSV_L, &value);
	return !(value & STK1160_POSV_L_ACSYNC);
}

void stk1160_ac97_setup(struct stk1160 *dev)
{
	if (!stk1160_has_audio(dev)) {
		stk1160_info("Device doesn't support audio, skipping AC97 setup.");
		return;
	}

	if (!stk1160_has_ac97(dev)) {
		stk1160_info("Device uses internal 8-bit ADC, skipping AC97 setup.");
		return;
	}

	/* Two-step reset AC97 interface and hardware codec */
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x94);
	stk1160_write_reg(dev, STK1160_AC97CTL_0, 0x8c);

	/* Set 16-bit audio data and choose L&R channel*/
	stk1160_write_reg(dev, STK1160_AC97CTL_1 + 2, 0x01);
	stk1160_write_reg(dev, STK1160_AC97CTL_1 + 3, 0x00);

	/* Setup channels */
	stk1160_write_ac97(dev, 0x12, 0x8808); /* CD volume */
	stk1160_write_ac97(dev, 0x10, 0x0808); /* Line-in volume */
	stk1160_write_ac97(dev, 0x0e, 0x0008); /* MIC volume (mono) */
	stk1160_write_ac97(dev, 0x16, 0x0808); /* Aux volume */
	stk1160_write_ac97(dev, 0x1a, 0x0404); /* Record select */
	stk1160_write_ac97(dev, 0x02, 0x0000); /* Master volume */
	stk1160_write_ac97(dev, 0x1c, 0x0808); /* Record gain */

#ifdef DEBUG
	stk1160_ac97_dump_regs(dev);
#endif
}
