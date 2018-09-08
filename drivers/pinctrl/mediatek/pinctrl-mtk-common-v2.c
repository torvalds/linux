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

	/* Caculated bits as the overall offset the pin is located at */
	bits = c->s_bit + (pin - c->s_pin) * (c->x_bits);

	/* Fill pfd from bits and 32-bit register applied is assumed */
	pfd->offset = c->s_addr + c->x_addrs * (bits / 32);
	pfd->bitpos = bits % 32;
	pfd->mask = (1 << c->x_bits) - 1;

	/* pfd->next is used for indicating that bit wrapping-around happens
	 * which requires the manipulation for bit 0 starting in the next
	 * register to form the complete field read/write.
	 */
	pfd->next = pfd->bitpos + c->x_bits - 1 > 31 ? c->x_addrs : 0;

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
