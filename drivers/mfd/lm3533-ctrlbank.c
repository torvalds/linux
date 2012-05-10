/*
 * lm3533-ctrlbank.c -- LM3533 Generic Control Bank interface
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under  the terms of the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the License, or (at your
 * option) any later version.
 */

#include <linux/device.h>
#include <linux/module.h>

#include <linux/mfd/lm3533.h>


#define LM3533_BRIGHTNESS_MAX		255
#define LM3533_MAX_CURRENT_MAX		31
#define LM3533_PWM_MAX			0x3f

#define LM3533_REG_PWM_BASE		0x14
#define LM3533_REG_MAX_CURRENT_BASE	0x1f
#define LM3533_REG_CTRLBANK_ENABLE	0x27
#define LM3533_REG_BRIGHTNESS_BASE	0x40


static inline u8 lm3533_ctrlbank_get_reg(struct lm3533_ctrlbank *cb, u8 base)
{
	return base + cb->id;
}

int lm3533_ctrlbank_enable(struct lm3533_ctrlbank *cb)
{
	u8 mask;
	int ret;

	dev_dbg(cb->dev, "%s - %d\n", __func__, cb->id);

	mask = 1 << cb->id;
	ret = lm3533_update(cb->lm3533, LM3533_REG_CTRLBANK_ENABLE,
								mask, mask);
	if (ret)
		dev_err(cb->dev, "failed to enable ctrlbank %d\n", cb->id);

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_enable);

int lm3533_ctrlbank_disable(struct lm3533_ctrlbank *cb)
{
	u8 mask;
	int ret;

	dev_dbg(cb->dev, "%s - %d\n", __func__, cb->id);

	mask = 1 << cb->id;
	ret = lm3533_update(cb->lm3533, LM3533_REG_CTRLBANK_ENABLE, 0, mask);
	if (ret)
		dev_err(cb->dev, "failed to disable ctrlbank %d\n", cb->id);

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_disable);

#define lm3533_ctrlbank_set(_name, _NAME)				\
int lm3533_ctrlbank_set_##_name(struct lm3533_ctrlbank *cb, u8 val)	\
{									\
	u8 reg;								\
	int ret;							\
									\
	if (val > LM3533_##_NAME##_MAX)					\
		return -EINVAL;						\
									\
	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_##_NAME##_BASE);	\
	ret = lm3533_write(cb->lm3533, reg, val);			\
	if (ret)							\
		dev_err(cb->dev, "failed to set " #_name "\n");		\
									\
	return ret;							\
}									\
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_set_##_name);

#define lm3533_ctrlbank_get(_name, _NAME)				\
int lm3533_ctrlbank_get_##_name(struct lm3533_ctrlbank *cb, u8 *val)	\
{									\
	u8 reg;								\
	int ret;							\
									\
	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_##_NAME##_BASE);	\
	ret = lm3533_read(cb->lm3533, reg, val);			\
	if (ret)							\
		dev_err(cb->dev, "failed to get " #_name "\n");		\
									\
	return ret;							\
}									\
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_get_##_name);

lm3533_ctrlbank_set(brightness, BRIGHTNESS);
lm3533_ctrlbank_get(brightness, BRIGHTNESS);

/*
 * Full scale current.
 *
 * Imax = 5 + val * 0.8 mA, e.g.:
 *
 *    0 - 5 mA
 *     ...
 *   19 - 20.2 mA (default)
 *     ...
 *   31 - 29.8 mA
 */
lm3533_ctrlbank_set(max_current, MAX_CURRENT);

/*
 * PWM-input control mask:
 *
 *   bit 5 - PWM-input enabled in Zone 4
 *   bit 4 - PWM-input enabled in Zone 3
 *   bit 3 - PWM-input enabled in Zone 2
 *   bit 2 - PWM-input enabled in Zone 1
 *   bit 1 - PWM-input enabled in Zone 0
 *   bit 0 - PWM-input enabled
 */
lm3533_ctrlbank_set(pwm, PWM);
lm3533_ctrlbank_get(pwm, PWM);


MODULE_AUTHOR("Johan Hovold <jhovold@gmail.com>");
MODULE_DESCRIPTION("LM3533 Control Bank interface");
MODULE_LICENSE("GPL");
