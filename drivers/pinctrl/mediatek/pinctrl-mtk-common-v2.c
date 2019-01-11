// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2018 MediaTek Inc.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of_irq.h>

#include "mtk-eint.h"
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
static const struct mtk_drive_desc mtk_drive[] = {
	[DRV_GRP0] = { 4, 16, 4, 1 },
	[DRV_GRP1] = { 4, 16, 4, 2 },
	[DRV_GRP2] = { 2, 8, 2, 1 },
	[DRV_GRP3] = { 2, 8, 2, 2 },
	[DRV_GRP4] = { 2, 16, 2, 1 },
};

static void mtk_w32(struct mtk_pinctrl *pctl, u8 i, u32 reg, u32 val)
{
	writel_relaxed(val, pctl->base[i] + reg);
}

static u32 mtk_r32(struct mtk_pinctrl *pctl, u8 i, u32 reg)
{
	return readl_relaxed(pctl->base[i] + reg);
}

void mtk_rmw(struct mtk_pinctrl *pctl, u8 i, u32 reg, u32 mask, u32 set)
{
	u32 val;

	val = mtk_r32(pctl, i, reg);
	val &= ~mask;
	val |= set;
	mtk_w32(pctl, i, reg, val);
}

static int mtk_hw_pin_field_lookup(struct mtk_pinctrl *hw,
				   const struct mtk_pin_desc *desc,
				   int field, struct mtk_pin_field *pfd)
{
	const struct mtk_pin_field_calc *c, *e;
	const struct mtk_pin_reg_calc *rc;
	u32 bits;

	if (hw->soc->reg_cal && hw->soc->reg_cal[field].range) {
		rc = &hw->soc->reg_cal[field];
	} else {
		dev_dbg(hw->dev,
			"Not support field %d for pin %d (%s)\n",
			field, desc->number, desc->name);
		return -ENOTSUPP;
	}

	c = rc->range;
	e = c + rc->nranges;

	while (c < e) {
		if (desc->number >= c->s_pin && desc->number <= c->e_pin)
			break;
		c++;
	}

	if (c >= e) {
		dev_dbg(hw->dev, "Not support field %d for pin = %d (%s)\n",
			field, desc->number, desc->name);
		return -ENOTSUPP;
	}

	if (c->i_base > hw->nbase - 1) {
		dev_err(hw->dev,
			"Invalid base for field %d for pin = %d (%s)\n",
			field, desc->number, desc->name);
		return -EINVAL;
	}

	/* Calculated bits as the overall offset the pin is located at,
	 * if c->fixed is held, that determines the all the pins in the
	 * range use the same field with the s_pin.
	 */
	bits = c->fixed ? c->s_bit : c->s_bit +
	       (desc->number - c->s_pin) * (c->x_bits);

	/* Fill pfd from bits. For example 32-bit register applied is assumed
	 * when c->sz_reg is equal to 32.
	 */
	pfd->index = c->i_base;
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

static int mtk_hw_pin_field_get(struct mtk_pinctrl *hw,
				const struct mtk_pin_desc *desc,
				int field, struct mtk_pin_field *pfd)
{
	if (field < 0 || field >= PINCTRL_PIN_REG_MAX) {
		dev_err(hw->dev, "Invalid Field %d\n", field);
		return -EINVAL;
	}

	return mtk_hw_pin_field_lookup(hw, desc, field, pfd);
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

	mtk_rmw(hw, pf->index, pf->offset, pf->mask << pf->bitpos,
		(value & pf->mask) << pf->bitpos);

	mtk_rmw(hw, pf->index, pf->offset + pf->next, BIT(nbits_h) - 1,
		(value & pf->mask) >> nbits_l);
}

static void mtk_hw_read_cross_field(struct mtk_pinctrl *hw,
				    struct mtk_pin_field *pf, int *value)
{
	int nbits_l, nbits_h, h, l;

	mtk_hw_bits_part(pf, &nbits_h, &nbits_l);

	l  = (mtk_r32(hw, pf->index, pf->offset)
	      >> pf->bitpos) & (BIT(nbits_l) - 1);
	h  = (mtk_r32(hw, pf->index, pf->offset + pf->next))
	      & (BIT(nbits_h) - 1);

	*value = (h << nbits_l) | l;
}

int mtk_hw_set_value(struct mtk_pinctrl *hw, const struct mtk_pin_desc *desc,
		     int field, int value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, desc, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		mtk_rmw(hw, pf.index, pf.offset, pf.mask << pf.bitpos,
			(value & pf.mask) << pf.bitpos);
	else
		mtk_hw_write_cross_field(hw, &pf, value);

	return 0;
}

int mtk_hw_get_value(struct mtk_pinctrl *hw, const struct mtk_pin_desc *desc,
		     int field, int *value)
{
	struct mtk_pin_field pf;
	int err;

	err = mtk_hw_pin_field_get(hw, desc, field, &pf);
	if (err)
		return err;

	if (!pf.next)
		*value = (mtk_r32(hw, pf.index, pf.offset)
			  >> pf.bitpos) & pf.mask;
	else
		mtk_hw_read_cross_field(hw, &pf, value);

	return 0;
}

static int mtk_xt_find_eint_num(struct mtk_pinctrl *hw, unsigned long eint_n)
{
	const struct mtk_pin_desc *desc;
	int i = 0;

	desc = (const struct mtk_pin_desc *)hw->soc->pins;

	while (i < hw->soc->npins) {
		if (desc[i].eint.eint_n == eint_n)
			return desc[i].number;
		i++;
	}

	return EINT_NA;
}

static int mtk_xt_get_gpio_n(void *data, unsigned long eint_n,
			     unsigned int *gpio_n,
			     struct gpio_chip **gpio_chip)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;
	const struct mtk_pin_desc *desc;

	desc = (const struct mtk_pin_desc *)hw->soc->pins;
	*gpio_chip = &hw->chip;

	/* Be greedy to guess first gpio_n is equal to eint_n */
	if (desc[eint_n].eint.eint_n == eint_n)
		*gpio_n = eint_n;
	else
		*gpio_n = mtk_xt_find_eint_num(hw, eint_n);

	return *gpio_n == EINT_NA ? -EINVAL : 0;
}

static int mtk_xt_get_gpio_state(void *data, unsigned long eint_n)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;
	const struct mtk_pin_desc *desc;
	struct gpio_chip *gpio_chip;
	unsigned int gpio_n;
	int value, err;

	err = mtk_xt_get_gpio_n(hw, eint_n, &gpio_n, &gpio_chip);
	if (err)
		return err;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio_n];

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DI, &value);
	if (err)
		return err;

	return !!value;
}

static int mtk_xt_set_gpio_as_eint(void *data, unsigned long eint_n)
{
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)data;
	const struct mtk_pin_desc *desc;
	struct gpio_chip *gpio_chip;
	unsigned int gpio_n;
	int err;

	err = mtk_xt_get_gpio_n(hw, eint_n, &gpio_n, &gpio_chip);
	if (err)
		return err;

	desc = (const struct mtk_pin_desc *)&hw->soc->pins[gpio_n];

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_MODE,
			       desc->eint.eint_m);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DIR, MTK_INPUT);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_SMT, MTK_ENABLE);
	if (err)
		return err;

	return 0;
}

static const struct mtk_eint_xt mtk_eint_xt = {
	.get_gpio_n = mtk_xt_get_gpio_n,
	.get_gpio_state = mtk_xt_get_gpio_state,
	.set_gpio_as_eint = mtk_xt_set_gpio_as_eint,
};

int mtk_build_eint(struct mtk_pinctrl *hw, struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;

	if (!IS_ENABLED(CONFIG_EINT_MTK))
		return 0;

	if (!of_property_read_bool(np, "interrupt-controller"))
		return -ENODEV;

	hw->eint = devm_kzalloc(hw->dev, sizeof(*hw->eint), GFP_KERNEL);
	if (!hw->eint)
		return -ENOMEM;

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "eint");
	if (!res) {
		dev_err(&pdev->dev, "Unable to get eint resource\n");
		return -ENODEV;
	}

	hw->eint->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(hw->eint->base))
		return PTR_ERR(hw->eint->base);

	hw->eint->irq = irq_of_parse_and_map(np, 0);
	if (!hw->eint->irq)
		return -EINVAL;

	if (!hw->soc->eint_hw)
		return -ENODEV;

	hw->eint->dev = &pdev->dev;
	hw->eint->hw = hw->soc->eint_hw;
	hw->eint->pctl = hw;
	hw->eint->gpio_xlate = &mtk_eint_xt;

	return mtk_eint_do_init(hw->eint);
}

/* Revision 0 */
int mtk_pinconf_bias_disable_set(struct mtk_pinctrl *hw,
				 const struct mtk_pin_desc *desc)
{
	int err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PU,
			       MTK_DISABLE);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PD,
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

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PU, &v);
	if (err)
		return err;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PD, &v2);
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

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PU, arg & 1);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PD,
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

	err = mtk_hw_get_value(hw, desc, reg, &v);
	if (err)
		return err;

	if (!v)
		return -EINVAL;

	*res = 1;

	return 0;
}

/* Revision 1 */
int mtk_pinconf_bias_disable_set_rev1(struct mtk_pinctrl *hw,
				      const struct mtk_pin_desc *desc)
{
	int err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PULLEN,
			       MTK_DISABLE);
	if (err)
		return err;

	return 0;
}

int mtk_pinconf_bias_disable_get_rev1(struct mtk_pinctrl *hw,
				      const struct mtk_pin_desc *desc, int *res)
{
	int v, err;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PULLEN, &v);
	if (err)
		return err;

	if (v == MTK_ENABLE)
		return -EINVAL;

	*res = 1;

	return 0;
}

int mtk_pinconf_bias_set_rev1(struct mtk_pinctrl *hw,
			      const struct mtk_pin_desc *desc, bool pullup)
{
	int err, arg;

	arg = pullup ? MTK_PULLUP : MTK_PULLDOWN;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PULLEN,
			       MTK_ENABLE);
	if (err)
		return err;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PULLSEL, arg);
	if (err)
		return err;

	return 0;
}

int mtk_pinconf_bias_get_rev1(struct mtk_pinctrl *hw,
			      const struct mtk_pin_desc *desc, bool pullup,
			      int *res)
{
	int err, v;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PULLEN, &v);
	if (err)
		return err;

	if (v == MTK_DISABLE)
		return -EINVAL;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PULLSEL, &v);
	if (err)
		return err;

	if (pullup ^ (v == MTK_PULLUP))
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
		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_E4,
				       arg & 0x1);
		if (err)
			return err;

		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_E8,
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

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_E4, &val1);
	if (err)
		return err;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_E8, &val2);
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

		err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_DRV,
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

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_DRV, &val1);
	if (err)
		return err;

	*val = ((val1 & 0x7) / tb->scal + 1) * tb->step;

	return 0;
}

int mtk_pinconf_adv_pull_set(struct mtk_pinctrl *hw,
			     const struct mtk_pin_desc *desc, bool pullup,
			     u32 arg)
{
	int err;

	/* 10K off & 50K (75K) off, when (R0, R1) = (0, 0);
	 * 10K off & 50K (75K) on, when (R0, R1) = (0, 1);
	 * 10K on & 50K (75K) off, when (R0, R1) = (1, 0);
	 * 10K on & 50K (75K) on, when (R0, R1) = (1, 1)
	 */
	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_R0, arg & 1);
	if (err)
		return 0;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_R1,
			       !!(arg & 2));
	if (err)
		return 0;

	arg = pullup ? 0 : 1;

	err = mtk_hw_set_value(hw, desc, PINCTRL_PIN_REG_PUPD, arg);

	/* If PUPD register is not supported for that pin, let's fallback to
	 * general bias control.
	 */
	if (err == -ENOTSUPP) {
		if (hw->soc->bias_set) {
			err = hw->soc->bias_set(hw, desc, pullup);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
	}

	return err;
}

int mtk_pinconf_adv_pull_get(struct mtk_pinctrl *hw,
			     const struct mtk_pin_desc *desc, bool pullup,
			     u32 *val)
{
	u32 t, t2;
	int err;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_PUPD, &t);

	/* If PUPD register is not supported for that pin, let's fallback to
	 * general bias control.
	 */
	if (err == -ENOTSUPP) {
		if (hw->soc->bias_get) {
			err = hw->soc->bias_get(hw, desc, pullup, val);
			if (err)
				return err;
		} else {
			return -ENOTSUPP;
		}
	} else {
		/* t == 0 supposes PULLUP for the customized PULL setup */
		if (err)
			return err;

		if (pullup ^ !t)
			return -EINVAL;
	}

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_R0, &t);
	if (err)
		return err;

	err = mtk_hw_get_value(hw, desc, PINCTRL_PIN_REG_R1, &t2);
	if (err)
		return err;

	*val = (t | t2 << 1) & 0x7;

	return 0;
}
