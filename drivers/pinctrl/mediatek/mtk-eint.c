// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014-2025 MediaTek Inc.

/*
 * Library for MediaTek External Interrupt Support
 *
 * Author: Maoguang Meng <maoguang.meng@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 *	   Hao Chang <ot_chhao.chang@mediatek.com>
 *	   Qingliang Li <qingliang.li@mediatek.com>
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtk-eint.h"
#include "pinctrl-mtk-common-v2.h"

#define MTK_EINT_EDGE_SENSITIVE           0
#define MTK_EINT_LEVEL_SENSITIVE          1
#define MTK_EINT_DBNC_SET_DBNC_BITS	  4
#define MTK_EINT_DBNC_MAX		  16
#define MTK_EINT_DBNC_RST_BIT		  (0x1 << 1)
#define MTK_EINT_DBNC_SET_EN		  (0x1 << 0)

static const struct mtk_eint_regs mtk_generic_eint_regs = {
	.stat      = 0x000,
	.ack       = 0x040,
	.mask      = 0x080,
	.mask_set  = 0x0c0,
	.mask_clr  = 0x100,
	.sens      = 0x140,
	.sens_set  = 0x180,
	.sens_clr  = 0x1c0,
	.soft      = 0x200,
	.soft_set  = 0x240,
	.soft_clr  = 0x280,
	.pol       = 0x300,
	.pol_set   = 0x340,
	.pol_clr   = 0x380,
	.dom_en    = 0x400,
	.dbnc_ctrl = 0x500,
	.dbnc_set  = 0x600,
	.dbnc_clr  = 0x700,
};

const unsigned int debounce_time_mt2701[] = {
	500, 1000, 16000, 32000, 64000, 128000, 256000, 0
};
EXPORT_SYMBOL_GPL(debounce_time_mt2701);

const unsigned int debounce_time_mt6765[] = {
	125, 250, 500, 1000, 16000, 32000, 64000, 128000, 256000, 512000, 0
};
EXPORT_SYMBOL_GPL(debounce_time_mt6765);

const unsigned int debounce_time_mt6795[] = {
	500, 1000, 16000, 32000, 64000, 128000, 256000, 512000, 0
};
EXPORT_SYMBOL_GPL(debounce_time_mt6795);

static void __iomem *mtk_eint_get_offset(struct mtk_eint *eint,
					 unsigned int eint_num,
					 unsigned int offset)
{
	unsigned int idx = eint->pins[eint_num].index;
	unsigned int inst = eint->pins[eint_num].instance;
	void __iomem *reg;

	reg = eint->base[inst] + offset + (idx / 32 * 4);

	return reg;
}

static unsigned int mtk_eint_can_en_debounce(struct mtk_eint *eint,
					     unsigned int eint_num)
{
	unsigned int sens;
	unsigned int bit = BIT(eint->pins[eint_num].index % 32);
	void __iomem *reg = mtk_eint_get_offset(eint, eint_num,
						eint->regs->sens);

	if (readl(reg) & bit)
		sens = MTK_EINT_LEVEL_SENSITIVE;
	else
		sens = MTK_EINT_EDGE_SENSITIVE;

	if (eint->pins[eint_num].debounce && sens != MTK_EINT_EDGE_SENSITIVE)
		return 1;
	else
		return 0;
}

static int mtk_eint_flip_edge(struct mtk_eint *eint, int hwirq)
{
	int start_level, curr_level;
	unsigned int reg_offset;
	unsigned int mask = BIT(eint->pins[hwirq].index & 0x1f);
	unsigned int port = (eint->pins[hwirq].index >> 5) & eint->hw->port_mask;
	void __iomem *reg = eint->base[eint->pins[hwirq].instance] + (port << 2);

	curr_level = eint->gpio_xlate->get_gpio_state(eint->pctl, hwirq);

	do {
		start_level = curr_level;
		if (start_level)
			reg_offset = eint->regs->pol_clr;
		else
			reg_offset = eint->regs->pol_set;
		writel(mask, reg + reg_offset);

		curr_level = eint->gpio_xlate->get_gpio_state(eint->pctl,
							      hwirq);
	} while (start_level != curr_level);

	return start_level;
}

static void mtk_eint_mask(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	unsigned int idx = eint->pins[d->hwirq].index;
	unsigned int inst = eint->pins[d->hwirq].instance;
	unsigned int mask = BIT(idx & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->mask_set);

	eint->cur_mask[inst][idx >> 5] &= ~mask;

	writel(mask, reg);
}

static void mtk_eint_unmask(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	unsigned int idx = eint->pins[d->hwirq].index;
	unsigned int inst = eint->pins[d->hwirq].instance;
	unsigned int mask = BIT(idx & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->mask_clr);

	eint->cur_mask[inst][idx >> 5] |= mask;

	writel(mask, reg);

	if (eint->pins[d->hwirq].dual_edge)
		mtk_eint_flip_edge(eint, d->hwirq);
}

static unsigned int mtk_eint_get_mask(struct mtk_eint *eint,
				      unsigned int eint_num)
{
	unsigned int bit = BIT(eint->pins[eint_num].index % 32);
	void __iomem *reg = mtk_eint_get_offset(eint, eint_num,
						eint->regs->mask);

	return !!(readl(reg) & bit);
}

static void mtk_eint_ack(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	unsigned int mask = BIT(eint->pins[d->hwirq].index & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->ack);

	writel(mask, reg);
}

static int mtk_eint_set_type(struct irq_data *d, unsigned int type)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	bool masked;
	unsigned int mask = BIT(eint->pins[d->hwirq].index & 0x1f);
	void __iomem *reg;

	if (((type & IRQ_TYPE_EDGE_BOTH) && (type & IRQ_TYPE_LEVEL_MASK)) ||
	    ((type & IRQ_TYPE_LEVEL_MASK) == IRQ_TYPE_LEVEL_MASK)) {
		dev_err(eint->dev,
			"Can't configure IRQ%d (EINT%lu) for type 0x%X\n",
			d->irq, d->hwirq, type);
		return -EINVAL;
	}

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		eint->pins[d->hwirq].dual_edge = 1;
	else
		eint->pins[d->hwirq].dual_edge = 0;

	if (!mtk_eint_get_mask(eint, d->hwirq)) {
		mtk_eint_mask(d);
		masked = false;
	} else {
		masked = true;
	}

	if (type & (IRQ_TYPE_LEVEL_LOW | IRQ_TYPE_EDGE_FALLING)) {
		reg = mtk_eint_get_offset(eint, d->hwirq, eint->regs->pol_clr);
		writel(mask, reg);
	} else {
		reg = mtk_eint_get_offset(eint, d->hwirq, eint->regs->pol_set);
		writel(mask, reg);
	}

	if (type & (IRQ_TYPE_EDGE_RISING | IRQ_TYPE_EDGE_FALLING)) {
		reg = mtk_eint_get_offset(eint, d->hwirq, eint->regs->sens_clr);
		writel(mask, reg);
	} else {
		reg = mtk_eint_get_offset(eint, d->hwirq, eint->regs->sens_set);
		writel(mask, reg);
	}

	mtk_eint_ack(d);
	if (!masked)
		mtk_eint_unmask(d);

	return 0;
}

static int mtk_eint_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	unsigned int idx = eint->pins[d->hwirq].index;
	unsigned int inst = eint->pins[d->hwirq].instance;
	unsigned int shift = idx & 0x1f;
	unsigned int port = idx >> 5;

	if (on)
		eint->wake_mask[inst][port] |= BIT(shift);
	else
		eint->wake_mask[inst][port] &= ~BIT(shift);

	return 0;
}

static void mtk_eint_chip_write_mask(const struct mtk_eint *eint,
				     void __iomem *base, unsigned int **buf)
{
	int inst, port, port_num;
	void __iomem *reg;

	for (inst = 0; inst < eint->nbase; inst++) {
		port_num = DIV_ROUND_UP(eint->base_pin_num[inst], 32);
		for (port = 0; port < port_num; port++) {
			reg = eint->base[inst] + (port << 2);
			writel_relaxed(~buf[inst][port], reg + eint->regs->mask_set);
			writel_relaxed(buf[inst][port], reg + eint->regs->mask_clr);
		}
	}
}

static int mtk_eint_irq_request_resources(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	struct gpio_chip *gpio_c;
	unsigned int gpio_n;
	int err;

	err = eint->gpio_xlate->get_gpio_n(eint->pctl, d->hwirq,
					   &gpio_n, &gpio_c);
	if (err < 0) {
		dev_err(eint->dev, "Can not find pin\n");
		return err;
	}

	err = gpiochip_lock_as_irq(gpio_c, gpio_n);
	if (err < 0) {
		dev_err(eint->dev, "unable to lock HW IRQ %lu for IRQ\n",
			irqd_to_hwirq(d));
		return err;
	}

	err = eint->gpio_xlate->set_gpio_as_eint(eint->pctl, d->hwirq);
	if (err < 0) {
		dev_err(eint->dev, "Can not eint mode\n");
		return err;
	}

	return 0;
}

static void mtk_eint_irq_release_resources(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	struct gpio_chip *gpio_c;
	unsigned int gpio_n;

	eint->gpio_xlate->get_gpio_n(eint->pctl, d->hwirq, &gpio_n,
				     &gpio_c);

	gpiochip_unlock_as_irq(gpio_c, gpio_n);
}

static struct irq_chip mtk_eint_irq_chip = {
	.name = "mt-eint",
	.irq_disable = mtk_eint_mask,
	.irq_mask = mtk_eint_mask,
	.irq_unmask = mtk_eint_unmask,
	.irq_ack = mtk_eint_ack,
	.irq_set_type = mtk_eint_set_type,
	.irq_set_wake = mtk_eint_irq_set_wake,
	.irq_request_resources = mtk_eint_irq_request_resources,
	.irq_release_resources = mtk_eint_irq_release_resources,
};

static unsigned int mtk_eint_hw_init(struct mtk_eint *eint)
{
	void __iomem *dom_reg, *mask_reg;
	unsigned int i, j;

	for (i = 0; i < eint->nbase; i++) {
		dom_reg = eint->base[i] + eint->regs->dom_en;
		mask_reg = eint->base[i] + eint->regs->mask_set;
		for (j = 0; j < eint->base_pin_num[i]; j += 32) {
			writel(0xffffffff, dom_reg);
			writel(0xffffffff, mask_reg);
			dom_reg += 4;
			mask_reg += 4;
		}
	}

	return 0;
}

static inline void
mtk_eint_debounce_process(struct mtk_eint *eint, int index)
{
	unsigned int rst, ctrl_offset;
	unsigned int bit, dbnc;
	unsigned int inst = eint->pins[index].instance;
	unsigned int idx = eint->pins[index].index;

	ctrl_offset = (idx / 4) * 4 + eint->regs->dbnc_ctrl;
	dbnc = readl(eint->base[inst] + ctrl_offset);
	bit = MTK_EINT_DBNC_SET_EN << ((idx % 4) * 8);
	if ((bit & dbnc) > 0) {
		ctrl_offset = (idx / 4) * 4 + eint->regs->dbnc_set;
		rst = MTK_EINT_DBNC_RST_BIT << ((idx % 4) * 8);
		writel(rst, eint->base[inst] + ctrl_offset);
	}
}

static void mtk_eint_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct mtk_eint *eint = irq_desc_get_handler_data(desc);
	unsigned int i, j, port, status, shift, mask, eint_num;
	void __iomem *reg;
	int dual_edge, start_level, curr_level;

	chained_irq_enter(chip, desc);
	for (i = 0; i < eint->nbase; i++) {
		for (j = 0; j < eint->base_pin_num[i]; j += 32) {
			port = j >> 5;
			status = readl(eint->base[i] + port * 4 + eint->regs->stat);
			while (status) {
				shift = __ffs(status);
				status &= ~BIT(shift);
				mask = BIT(shift);
				eint_num = eint->pin_list[i][shift + j];

				/*
				 * If we get an interrupt on pin that was only required
				 * for wake (but no real interrupt requested), mask the
				 * interrupt (as would mtk_eint_resume do anyway later
				 * in the resume sequence).
				 */
				if (eint->wake_mask[i][port] & mask &&
				    !(eint->cur_mask[i][port] & mask)) {
					reg = mtk_eint_get_offset(eint, eint_num,
								  eint->regs->mask_set);
					writel_relaxed(mask, reg);
				}

				dual_edge = eint->pins[eint_num].dual_edge;
				if (dual_edge) {
					/*
					 * Clear soft-irq in case we raised it last
					 * time.
					 */
					reg = mtk_eint_get_offset(eint, eint_num,
								  eint->regs->soft_clr);
					writel(mask, reg);

					start_level =
					eint->gpio_xlate->get_gpio_state(eint->pctl,
									 eint_num);
				}

				generic_handle_domain_irq(eint->domain, eint_num);

				if (dual_edge) {
					curr_level = mtk_eint_flip_edge(eint, eint_num);

					/*
					 * If level changed, we might lost one edge
					 * interrupt, raised it through soft-irq.
					 */
					if (start_level != curr_level) {
						reg = mtk_eint_get_offset(eint, eint_num,
									  eint->regs->soft_set);
						writel(mask, reg);
					}
				}

				if (eint->pins[eint_num].debounce)
					mtk_eint_debounce_process(eint, eint_num);
			}
		}
	}
	chained_irq_exit(chip, desc);
}

int mtk_eint_do_suspend(struct mtk_eint *eint)
{
	mtk_eint_chip_write_mask(eint, eint->base, eint->wake_mask);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_eint_do_suspend);

int mtk_eint_do_resume(struct mtk_eint *eint)
{
	mtk_eint_chip_write_mask(eint, eint->base, eint->cur_mask);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_eint_do_resume);

int mtk_eint_set_debounce(struct mtk_eint *eint, unsigned long eint_num,
			  unsigned int debounce)
{
	int virq, eint_offset;
	unsigned int set_offset, bit, clr_bit, clr_offset, rst, i, unmask,
		     dbnc;
	unsigned int inst = eint->pins[eint_num].instance;
	unsigned int idx = eint->pins[eint_num].index;
	struct irq_data *d;

	if (!eint->hw->db_time)
		return -EOPNOTSUPP;

	virq = irq_find_mapping(eint->domain, eint_num);
	eint_offset = (eint_num % 4) * 8;
	d = irq_get_irq_data(virq);

	set_offset = (idx / 4) * 4 + eint->regs->dbnc_set;
	clr_offset = (idx / 4) * 4 + eint->regs->dbnc_clr;

	if (!mtk_eint_can_en_debounce(eint, eint_num))
		return -EINVAL;

	dbnc = eint->num_db_time;
	for (i = 0; i < eint->num_db_time; i++) {
		if (debounce <= eint->hw->db_time[i]) {
			dbnc = i;
			break;
		}
	}

	if (!mtk_eint_get_mask(eint, eint_num)) {
		mtk_eint_mask(d);
		unmask = 1;
	} else {
		unmask = 0;
	}

	clr_bit = 0xff << eint_offset;
	writel(clr_bit, eint->base[inst] + clr_offset);

	bit = ((dbnc << MTK_EINT_DBNC_SET_DBNC_BITS) | MTK_EINT_DBNC_SET_EN) <<
		eint_offset;
	rst = MTK_EINT_DBNC_RST_BIT << eint_offset;
	writel(rst | bit, eint->base[inst] + set_offset);

	/*
	 * Delay a while (more than 2T) to wait for hw debounce counter reset
	 * work correctly.
	 */
	udelay(1);
	if (unmask == 1)
		mtk_eint_unmask(d);

	return 0;
}
EXPORT_SYMBOL_GPL(mtk_eint_set_debounce);

int mtk_eint_find_irq(struct mtk_eint *eint, unsigned long eint_n)
{
	int irq;

	irq = irq_find_mapping(eint->domain, eint_n);
	if (!irq)
		return -EINVAL;

	return irq;
}
EXPORT_SYMBOL_GPL(mtk_eint_find_irq);

int mtk_eint_do_init(struct mtk_eint *eint)
{
	unsigned int size, i, port, inst = 0;
	struct mtk_pinctrl *hw = (struct mtk_pinctrl *)eint->pctl;

	/* If clients don't assign a specific regs, let's use generic one */
	if (!eint->regs)
		eint->regs = &mtk_generic_eint_regs;

	eint->base_pin_num = devm_kmalloc_array(eint->dev, eint->nbase, sizeof(u16),
						GFP_KERNEL | __GFP_ZERO);
	if (!eint->base_pin_num)
		return -ENOMEM;

	if (eint->nbase == 1) {
		size = eint->hw->ap_num * sizeof(struct mtk_eint_pin);
		eint->pins = devm_kmalloc(eint->dev, size, GFP_KERNEL);
		if (!eint->pins)
			goto err_pins;

		eint->base_pin_num[inst] = eint->hw->ap_num;
		for (i = 0; i < eint->hw->ap_num; i++) {
			eint->pins[i].instance = inst;
			eint->pins[i].index = i;
			eint->pins[i].debounce = (i < eint->hw->db_cnt) ? 1 : 0;
		}
	}

	if (hw && hw->soc && hw->soc->eint_pin) {
		eint->pins = hw->soc->eint_pin;
		for (i = 0; i < eint->hw->ap_num; i++) {
			inst = eint->pins[i].instance;
			if (inst >= eint->nbase)
				continue;
			eint->base_pin_num[inst]++;
		}
	}

	eint->pin_list = devm_kmalloc(eint->dev, eint->nbase * sizeof(u16 *), GFP_KERNEL);
	if (!eint->pin_list)
		goto err_pin_list;

	eint->wake_mask = devm_kmalloc(eint->dev, eint->nbase * sizeof(u32 *), GFP_KERNEL);
	if (!eint->wake_mask)
		goto err_wake_mask;

	eint->cur_mask = devm_kmalloc(eint->dev, eint->nbase * sizeof(u32 *), GFP_KERNEL);
	if (!eint->cur_mask)
		goto err_cur_mask;

	for (i = 0; i < eint->nbase; i++) {
		eint->pin_list[i] = devm_kzalloc(eint->dev, eint->base_pin_num[i] * sizeof(u16),
						 GFP_KERNEL);
		port = DIV_ROUND_UP(eint->base_pin_num[i], 32);
		eint->wake_mask[i] = devm_kzalloc(eint->dev, port * sizeof(u32), GFP_KERNEL);
		eint->cur_mask[i] = devm_kzalloc(eint->dev, port * sizeof(u32), GFP_KERNEL);
		if (!eint->pin_list[i] || !eint->wake_mask[i] || !eint->cur_mask[i])
			goto err_eint;
	}

	eint->domain = irq_domain_add_linear(eint->dev->of_node,
					     eint->hw->ap_num,
					     &irq_domain_simple_ops, NULL);
	if (!eint->domain)
		goto err_eint;

	if (eint->hw->db_time) {
		for (i = 0; i < MTK_EINT_DBNC_MAX; i++)
			if (eint->hw->db_time[i] == 0)
				break;
		eint->num_db_time = i;
	}

	mtk_eint_hw_init(eint);
	for (i = 0; i < eint->hw->ap_num; i++) {
		inst = eint->pins[i].instance;
		if (inst >= eint->nbase)
			continue;
		eint->pin_list[inst][eint->pins[i].index] = i;
		int virq = irq_create_mapping(eint->domain, i);
		irq_set_chip_and_handler(virq, &mtk_eint_irq_chip,
					 handle_level_irq);
		irq_set_chip_data(virq, eint);
	}

	irq_set_chained_handler_and_data(eint->irq, mtk_eint_irq_handler,
					 eint);

	return 0;

err_eint:
	for (i = 0; i < eint->nbase; i++) {
		if (eint->cur_mask[i])
			devm_kfree(eint->dev, eint->cur_mask[i]);
		if (eint->wake_mask[i])
			devm_kfree(eint->dev, eint->wake_mask[i]);
		if (eint->pin_list[i])
			devm_kfree(eint->dev, eint->pin_list[i]);
	}
	devm_kfree(eint->dev, eint->cur_mask);
err_cur_mask:
	devm_kfree(eint->dev, eint->wake_mask);
err_wake_mask:
	devm_kfree(eint->dev, eint->pin_list);
err_pin_list:
	if (eint->nbase == 1)
		devm_kfree(eint->dev, eint->pins);
err_pins:
	devm_kfree(eint->dev, eint->base_pin_num);
	return -ENOMEM;
}
EXPORT_SYMBOL_GPL(mtk_eint_do_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("MediaTek EINT Driver");
