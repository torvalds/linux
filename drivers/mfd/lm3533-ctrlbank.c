// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * lm3533-ctrlbank.c -- LM3533 Generic Control Bank interface
 *
 * Copyright (C) 2011-2012 Texas Instruments
 *
 * Author: Johan Hovold <jhovold@gmail.com>
 */

#include <linux/device.h>
#include <linux/module.h>

#include <linux/mfd/lm3533.h>


#define LM3533_MAX_CURRENT_MIN		5000
#define LM3533_MAX_CURRENT_MAX		29800
#define LM3533_MAX_CURRENT_STEP		800

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

/*
 * Full-scale current.
 *
 * imax		5000 - 29800 uA (800 uA step)
 */
int lm3533_ctrlbank_set_max_current(struct lm3533_ctrlbank *cb, u16 imax)
{
	u8 reg;
	u8 val;
	int ret;

	if (imax < LM3533_MAX_CURRENT_MIN || imax > LM3533_MAX_CURRENT_MAX)
		return -EINVAL;

	val = (imax - LM3533_MAX_CURRENT_MIN) / LM3533_MAX_CURRENT_STEP;

	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_MAX_CURRENT_BASE);
	ret = lm3533_write(cb->lm3533, reg, val);
	if (ret)
		dev_err(cb->dev, "failed to set max current\n");

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_set_max_current);

int lm3533_ctrlbank_set_brightness(struct lm3533_ctrlbank *cb, u8 val)
{
	u8 reg;
	int ret;

	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_BRIGHTNESS_BASE);
	ret = lm3533_write(cb->lm3533, reg, val);
	if (ret)
		dev_err(cb->dev, "failed to set brightness\n");

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_set_brightness);

int lm3533_ctrlbank_get_brightness(struct lm3533_ctrlbank *cb, u8 *val)
{
	u8 reg;
	int ret;

	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_BRIGHTNESS_BASE);
	ret = lm3533_read(cb->lm3533, reg, val);
	if (ret)
		dev_err(cb->dev, "failed to get brightness\n");

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_get_brightness);

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
int lm3533_ctrlbank_set_pwm(struct lm3533_ctrlbank *cb, u8 val)
{
	u8 reg;
	int ret;

	if (val > LM3533_PWM_MAX)
		return -EINVAL;

	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_PWM_BASE);
	ret = lm3533_write(cb->lm3533, reg, val);
	if (ret)
		dev_err(cb->dev, "failed to set PWM mask\n");

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_set_pwm);

int lm3533_ctrlbank_get_pwm(struct lm3533_ctrlbank *cb, u8 *val)
{
	u8 reg;
	int ret;

	reg = lm3533_ctrlbank_get_reg(cb, LM3533_REG_PWM_BASE);
	ret = lm3533_read(cb->lm3533, reg, val);
	if (ret)
		dev_err(cb->dev, "failed to get PWM mask\n");

	return ret;
}
EXPORT_SYMBOL_GPL(lm3533_ctrlbank_get_pwm);

MODULE_AUTHOR("Johan Hovold <jhovold@gmail.com>");
MODULE_DESCRIPTION("LM3533 Control Bank interface");
MODULE_LICENSE("GPL");
