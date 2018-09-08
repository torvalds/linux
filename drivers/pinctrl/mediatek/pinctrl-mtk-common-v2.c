// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio.h>
#include <linux/io.h>

#include "pinctrl-mtk-common-v2.h"

/**
 * struct mtk_drive_desc - the structure that holds the information
 *			    of the driving current
 * @min:	the minimum current of this group
 * @max:	the maximum current of this group
 * @step:	the step current of this group
 * @scal:	the weight factor
 *
 * formula: output = ((input) / step - 1) * scal
 */
struct mtk_drive_desc {
	u8 min;
	u8 max;
	u8 step;
	u8 scal;
};

/* The groups of drive strength */
const struct mtk_drive_desc mtk_drive[] = {
	[DRV_GRP0] = { 4, 16, 4, 1 },
	[DRV_GRP1] = { 4, 16, 4, 2 },
	[DRV_GRP2] = { 2, 8, 2, 1 },
	[DRV_GRP3] = { 2, 8, 2, 2 },
	[DRV_GRP4] = { 2, 16, 2, 1 },
};

static void mtk_w32(struct mtk_pinctrl *pctl, u32 reg, u32 val)
{
	writel_relaxed(val, pctl->base + reg);
}

static u32 mtk_r32(struct mtk_pinctrl *pctl, u32 reg)
{
	return readl_relaxed(pctl->base + reg);
}

void mtk_rmw(struct mtk_pinctrl *pctl, u32 reg, u32 mask, u32 set)
{
	u32 val;

	val = mtk_r32(pctl, reg);
	val &= ~mask;
	val |= set;
	mtk_w32(pctl, reg, val);
}

static int mtk_hw_pin_field_lookup(struct mtk_pinctrl *hw, int pin,
				   const struct mtk_pin_reg_calc *rc,
				   struct mtk_pin_field *pfd)
{
	const struct mtk_pin_field_calc *c, *e;
	u32 bits;

	c = rc->range;
	e = c + rc->nranges;

	while (c < e) {
		if (pin >= c->s_pin && pin <= c->e_pin)
			break;
		c++;
	}

	if (c >= e) {
		dev_err(hw->dev, "Out of range for pin = %d\n", pin);
		return -EINVAL;
	}

	/* Calculated bits as the overall offset the pin is located at,
	 * if c->fixed is held, that determines the all the pins in the
	 * range use the same field with the s_pin.
	 */
	bits = c->fixed ? c->s_bit : c->s_bit + (pin - c->s_pin) * (c->x_bits);

	/* Fill pfd from bits. For example 32-bit register applied is assumed
	 * when c->sz_reg is equal to 32.
	 */
	pfd->offset = c->s_addr + c->x_addrs * (bits / c->sz_reg);
	pfd->bitpos = bits % c->sz_reg;
	pfd->mask = (1 << c->x_bits) - 1;

	/* pfd->next is used for indicating that bit wrapping-around happens
	 * which requires the manipulation for bit 0 starting in the next
	 * register to form the complete field read/write.
	 */
	pfd->next = pfd->bitpos + c->x_bits > c->sz_reg ? c->x_addrs : 0;

	return 0;
}

static int mtk_hw_pin_field_get(struct mtk_pinctrl *hw, int pin,
				int field, struct mtk_pin_field *pfd)
{
	const struct mtk_pin_reg_calc *rc;

	if (field < 0 || field >= PINCTRL_PIN_REG_MAX) {
		dev_err(hw->dev, "Invalid Field %d\n", field);
		return -EINVAL;
	}

	if (hw->soc->reg_cal && hw->soc->reg_cal[field].range) {
		rc = &hw->soc->reg_cal[field];
	} else {
		dev_err(hw->dev, "Undefined range for field %d\n", field);
		return -EINVAL;
	}

	return mtk_hw_pin_field_lookup(hw, pin, rc, pfd);
}

static void mtk_hw_bits_part(struct mtk_pin_field *pf, int *h, int *l)
{
	*l = 32 - pf->bitpos;
	*h = get_count_order(pf->mask) - *l;
}

static void mtk_hw_write_cross_field(struct mtk_pinctrl *hw,
				     struct mtk_pin_field *pf, int value)
{
	int nbits_l, nbits_h;

	mtk_hw_bits_part(pf, &nbits_h, &nbits_l);

	mtk_rmw(hw, pf->offset, pf->mask << pf->bitpos,
		(value & pf->mask) << pf->bitpos);

	mtk_rmw(hw, pf->offset + pf->next, BIT(nbits_h) - 1,
		(value & pf->mask) >> nbits_l);
}

static void mtk_hw_read_cross_field(struct mtk_pinctrl *hw,
				    struct mtk_pin_field *pf, int *value)
{
	int nbits_l, nbits_h, h, l;

	mtk_hw_bits_part(pf, &nbits_h, &nbits_l);

	l  = (mtk_r32(hw, pf->offset) >> pf->bitpos) & (BIT(nbits_l) - 1);
	h  = (mtk_r32(hw, pf->offset + pf->next)) & (BIT(nbits_h) - 1);

	*value = (h << nbits_l) | l;
}

int mtk_hw_set_value(struct mtk_pinctrl *hw, int pin, int field, int value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, pin, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		mtk_rmw(hw, pf.offset, pf.mask << pf.bitpos,
			(value & pf.mask) << pf.bitpos);
	else
		mtk_hw_write_cross_field(hw, &pf, value);

	return 0;
}

int mtk_hw_get_value(struct mtk_pinctrl *hw, int pin, int field, int *value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, pin, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		*value = (mtk_r32(hw, pf.offset) >> pf.bitpos) & pf.mask;
	else
		mtk_hw_read_cross_field(hw, &pf, value);

	return 0;
}

int mtk_pinconf_bias_disable_set(struct mtk_pinctrl *hw,
				 const struct mtk_pin_desc *desc)
{
	int err;

	err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_PU,
			       MTK_DISABLE);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_PD,
			       MTK_DISABLE);
	if (err)
		return err;

	return 0;
}

int mtk_pinconf_bias_disable_get(struct mtk_pinctrl *hw,
				 const struct mtk_pin_desc *desc, int *res)
{
	int v, v2;
	int err;

	err = mtk_hw_get_value(hw, desc->number, PINCTRL_PIN_REG_PU, &v);
	if (err)
		return err;

	err = mtk_hw_get_value(hw, desc->number, PINCTRL_PIN_REG_PD, &v2);
	if (err)
		return err;

	if (v == MTK_ENABLE || v2 == MTK_ENABLE)
		return -EINVAL;

	*res = 1;

	return 0;
}

int mtk_pinconf_bias_set(struct mtk_pinctrl *hw,
			 const struct mtk_pin_desc *desc, bool pullup)
{
	int err, arg;

	arg = pullup ? 1 : 2;

	err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_PU, arg & 1);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_PD,
			       !!(arg & 2));
	if (err)
		return err;

	return 0;
}

int mtk_pinconf_bias_get(struct mtk_pinctrl *hw,
			 const struct mtk_pin_desc *desc, bool pullup, int *res)
{
	int reg, err, v;

	reg = pullup ? PINCTRL_PIN_REG_PU : PINCTRL_PIN_REG_PD;

	err = mtk_hw_get_value(hw, desc->number, reg, &v);
	if (err)
		return err;

	if (!v)
		return -EINVAL;

	*res = 1;

	return 0;
}

/* Revision 0 */
int mtk_pinconf_drive_set(struct mtk_pinctrl *hw,
			  const struct mtk_pin_desc *desc, u32 arg)
{
	const struct mtk_drive_desc *tb;
	int err = -ENOTSUPP;

	tb = &mtk_drive[desc->drv_n];
	/* 4mA when (e8, e4) = (0, 0)
	 * 8mA when (e8, e4) = (0, 1)
	 * 12mA when (e8, e4) = (1, 0)
	 * 16mA when (e8, e4) = (1, 1)
	 */
	if ((arg >= tb->min && arg <= tb->max) && !(arg % tb->step)) {
		arg = (arg / tb->step - 1) * tb->scal;
		err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_E4,
				       arg & 0x1);
		if (err)
			return err;

		err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_E8,
				       (arg & 0x2) >> 1);
		if (err)
			return err;
	}

	return err;
}

int mtk_pinconf_drive_get(struct mtk_pinctrl *hw,
			  const struct mtk_pin_desc *desc, int *val)
{
	const struct mtk_drive_desc *tb;
	int err, val1, val2;

	tb = &mtk_drive[desc->drv_n];

	err = mtk_hw_get_value(hw, desc->number, PINCTRL_PIN_REG_E4, &val1);
	if (err)
		return err;

	err = mtk_hw_get_value(hw, desc->number, PINCTRL_PIN_REG_E8, &val2);
	if (err)
		return err;

	/* 4mA when (e8, e4) = (0, 0); 8mA when (e8, e4) = (0, 1)
	 * 12mA when (e8, e4) = (1, 0); 16mA when (e8, e4) = (1, 1)
	 */
	*val = (((val2 << 1) + val1) / tb->scal + 1) * tb->step;

	return 0;
}

/* Revision 1 */
int mtk_pinconf_drive_set_rev1(struct mtk_pinctrl *hw,
			       const struct mtk_pin_desc *desc, u32 arg)
{
	const struct mtk_drive_desc *tb;
	int err = -ENOTSUPP;

	tb = &mtk_drive[desc->drv_n];

	if ((arg >= tb->min && arg <= tb->max) && !(arg % tb->step)) {
		arg = (arg / tb->step - 1) * tb->scal;

		err = mtk_hw_set_value(hw, desc->number, PINCTRL_PIN_REG_DRV,
				       arg);
		if (err)
			return err;
	}

	return err;
}

int mtk_pinconf_drive_get_rev1(struct mtk_pinctrl *hw,
			       const struct mtk_pin_desc *desc, int *val)
{
	const struct mtk_drive_desc *tb;
	int err, val1;

	tb = &mtk_drive[desc->drv_n];

	err = mtk_hw_get_value(hw, desc->number, PINCTRL_PIN_REG_DRV, &val1);
	if (err)
		return err;

	*val = ((val1 & 0x7) / tb->scal + 1) * tb->step;

	return 0;
}
