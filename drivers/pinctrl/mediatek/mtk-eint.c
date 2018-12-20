// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2014-2018 MediaTek Inc.

/*
 * Library for MediaTek External Interrupt Support
 *
 * Author: Maoguang Meng <maoguang.meng@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/gpio/driver.h>
#include <linux/io.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>

#include "mtk-eint.h"

#define MTK_EINT_EDGE_SENSITIVE           0
#define MTK_EINT_LEVEL_SENSITIVE          1
#define MTK_EINT_DBNC_SET_DBNC_BITS	  4
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

static void __iomem *mtk_eint_get_offset(struct mtk_eint *eint,
					 unsigned int eint_num,
					 unsigned int offset)
{
	unsigned int eint_base = 0;
	void __iomem *reg;

	if (eint_num >= eint->hw->ap_num)
		eint_base = eint->hw->ap_num;

	reg = eint->base + offset + ((eint_num - eint_base) / 32) * 4;

	return reg;
}

static unsigned int mtk_eint_can_en_debounce(struct mtk_eint *eint,
					     unsigned int eint_num)
{
	unsigned int sens;
	unsigned int bit = BIT(eint_num % 32);
	void __iomem *reg = mtk_eint_get_offset(eint, eint_num,
						eint->regs->sens);

	if (readl(reg) & bit)
		sens = MTK_EINT_LEVEL_SENSITIVE;
	else
		sens = MTK_EINT_EDGE_SENSITIVE;

	if (eint_num < eint->hw->db_cnt && sens != MTK_EINT_EDGE_SENSITIVE)
		return 1;
	else
		return 0;
}

static int mtk_eint_flip_edge(struct mtk_eint *eint, int hwirq)
{
	int start_level, curr_level;
	unsigned int reg_offset;
	u32 mask = BIT(hwirq & 0x1f);
	u32 port = (hwirq >> 5) & eint->hw->port_mask;
	void __iomem *reg = eint->base + (port << 2);

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
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->mask_set);

	writel(mask, reg);
}

static void mtk_eint_unmask(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->mask_clr);

	writel(mask, reg);

	if (eint->dual_edge[d->hwirq])
		mtk_eint_flip_edge(eint, d->hwirq);
}

static unsigned int mtk_eint_get_mask(struct mtk_eint *eint,
				      unsigned int eint_num)
{
	unsigned int bit = BIT(eint_num % 32);
	void __iomem *reg = mtk_eint_get_offset(eint, eint_num,
						eint->regs->mask);

	return !!(readl(reg) & bit);
}

static void mtk_eint_ack(struct irq_data *d)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg = mtk_eint_get_offset(eint, d->hwirq,
						eint->regs->ack);

	writel(mask, reg);
}

static int mtk_eint_set_type(struct irq_data *d, unsigned int type)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	u32 mask = BIT(d->hwirq & 0x1f);
	void __iomem *reg;

	if (((type & IRQ_TYPE_EDGE_BOTH) && (type & IRQ_TYPE_LEVEL_MASK)) ||
	    ((type & IRQ_TYPE_LEVEL_MASK) == IRQ_TYPE_LEVEL_MASK)) {
		dev_err(eint->dev,
			"Can't configure IRQ%d (EINT%lu) for type 0x%X\n",
			d->irq, d->hwirq, type);
		return -EINVAL;
	}

	if ((type & IRQ_TYPE_EDGE_BOTH) == IRQ_TYPE_EDGE_BOTH)
		eint->dual_edge[d->hwirq] = 1;
	else
		eint->dual_edge[d->hwirq] = 0;

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

	if (eint->dual_edge[d->hwirq])
		mtk_eint_flip_edge(eint, d->hwirq);

	return 0;
}

static int mtk_eint_irq_set_wake(struct irq_data *d, unsigned int on)
{
	struct mtk_eint *eint = irq_data_get_irq_chip_data(d);
	int shift = d->hwirq & 0x1f;
	int reg = d->hwirq >> 5;

	if (on)
		eint->wake_mask[reg] |= BIT(shift);
	else
		eint->wake_mask[reg] &= ~BIT(shift);

	return 0;
}

static void mtk_eint_chip_write_mask(const struct mtk_eint *eint,
				     void __iomem *base, u32 *buf)
{
	int port;
	void __iomem *reg;

	for (port = 0; port < eint->hw->ports; port++) {
		reg = base + (port << 2);
		writel_relaxed(~buf[port], reg + eint->regs->mask_set);
		writel_relaxed(buf[port], reg + eint->regs->mask_clr);
	}
}

static void mtk_eint_chip_read_mask(const struct mtk_eint *eint,
				    void __iomem *base, u32 *buf)
{
	int port;
	void __iomem *reg;

	for (port = 0; port < eint->hw->ports; port++) {
		reg = base + eint->regs->mask + (port << 2);
		buf[port] = ~readl_relaxed(reg);
		/* Mask is 0 when irq is enabled, and 1 when disabled. */
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
	void __iomem *reg = eint->base + eint->regs->dom_en;
	unsigned int i;

	for (i = 0; i < eint->hw->ap_num; i += 32) {
		writel(0xffffffff, reg);
		reg += 4;
	}

	return 0;
}

static inline void
mtk_eint_debounce_process(struct mtk_eint *eint, int index)
{
	unsigned int rst, ctrl_offset;
	unsigned int bit, dbnc;

	ctrl_offset = (index / 4) * 4 + eint->regs->dbnc_ctrl;
	dbnc = readl(eint->base + ctrl_offset);
	bit = MTK_EINT_DBNC_SET_EN << ((index % 4) * 8);
	if ((bit & dbnc) > 0) {
		ctrl_offset = (index / 4) * 4 + eint->regs->dbnc_set;
		rst = MTK_EINT_DBNC_RST_BIT << ((index % 4) * 8);
		writel(rst, eint->base + ctrl_offset);
	}
}

static void mtk_eint_irq_handler(struct irq_desc *desc)
{
	struct irq_chip *chip = irq_desc_get_chip(desc);
	struct mtk_eint *eint = irq_desc_get_handler_data(desc);
	unsigned int status, eint_num;
	int offset, index, virq;
	void __iomem *reg =  mtk_eint_get_offset(eint, 0, eint->regs->stat);
	int dual_edge, start_level, curr_level;

	chained_irq_enter(chip, desc);
	for (eint_num = 0; eint_num < eint->hw->ap_num; eint_num += 32,
	     reg += 4) {
		status = readl(reg);
		while (status) {
			offset = __ffs(status);
			index = eint_num + offset;
			virq = irq_find_mapping(eint->domain, index);
			status &= ~BIT(offset);

			dual_edge = eint->dual_edge[index];
			if (dual_edge) {
				/*
				 * Clear soft-irq in case we raised it last
				 * time.
				 */
				writel(BIT(offset), reg - eint->regs->stat +
				       eint->regs->soft_clr);

				start_level =
				eint->gpio_xlate->get_gpio_state(eint->pctl,
								 index);
			}

			generic_handle_irq(virq);

			if (dual_edge) {
				curr_level = mtk_eint_flip_edge(eint, index);

				/*
				 * If level changed, we might lost one edge
				 * interrupt, raised it through soft-irq.
				 */
				if (start_level != curr_level)
					writel(BIT(offset), reg -
					       eint->regs->stat +
					       eint->regs->soft_set);
			}

			if (index < eint->hw->db_cnt)
				mtk_eint_debounce_process(eint, index);
		}
	}
	chained_irq_exit(chip, desc);
}

int mtk_eint_do_suspend(struct mtk_eint *eint)
{
	mtk_eint_chip_read_mask(eint, eint->base, eint->cur_mask);
	mtk_eint_chip_write_mask(eint, eint->base, eint->wake_mask);

	return 0;
}

int mtk_eint_do_resume(struct mtk_eint *eint)
{
	mtk_eint_chip_write_mask(eint, eint->base, eint->cur_mask);

	return 0;
}

int mtk_eint_set_debounce(struct mtk_eint *eint, unsigned long eint_num,
			  unsigned int debounce)
{
	int virq, eint_offset;
	unsigned int set_offset, bit, clr_bit, clr_offset, rst, i, unmask,
		     dbnc;
	static const unsigned int debounce_time[] = {500, 1000, 16000, 32000,
						     64000, 128000, 256000};
	struct irq_data *d;

	virq = irq_find_mapping(eint->domain, eint_num);
	eint_offset = (eint_num % 4) * 8;
	d = irq_get_irq_data(virq);

	set_offset = (eint_num / 4) * 4 + eint->regs->dbnc_set;
	clr_offset = (eint_num / 4) * 4 + eint->regs->dbnc_clr;

	if (!mtk_eint_can_en_debounce(eint, eint_num))
		return -EINVAL;

	dbnc = ARRAY_SIZE(debounce_time);
	for (i = 0; i < ARRAY_SIZE(debounce_time); i++) {
		if (debounce <= debounce_time[i]) {
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
	writel(clr_bit, eint->base + clr_offset);

	bit = ((dbnc << MTK_EINT_DBNC_SET_DBNC_BITS) | MTK_EINT_DBNC_SET_EN) <<
		eint_offset;
	rst = MTK_EINT_DBNC_RST_BIT << eint_offset;
	writel(rst | bit, eint->base + set_offset);

	/*
	 * Delay a while (more than 2T) to wait for hw debounce counter reset
	 * work correctly.
	 */
	udelay(1);
	if (unmask == 1)
		mtk_eint_unmask(d);

	return 0;
}

int mtk_eint_find_irq(struct mtk_eint *eint, unsigned long eint_n)
{
	int irq;

	irq = irq_find_mapping(eint->domain, eint_n);
	if (!irq)
		return -EINVAL;

	return irq;
}

int mtk_eint_do_init(struct mtk_eint *eint)
{
	int i;

	/* If clients don't assign a specific regs, let's use generic one */
	if (!eint->regs)
		eint->regs = &mtk_generic_eint_regs;

	eint->wake_mask = devm_kcalloc(eint->dev, eint->hw->ports,
				       sizeof(*eint->wake_mask), GFP_KERNEL);
	if (!eint->wake_mask)
		return -ENOMEM;

	eint->cur_mask = devm_kcalloc(eint->dev, eint->hw->ports,
				      sizeof(*eint->cur_mask), GFP_KERNEL);
	if (!eint->cur_mask)
		return -ENOMEM;

	eint->dual_edge = devm_kcalloc(eint->dev, eint->hw->ap_num,
				       sizeof(int), GFP_KERNEL);
	if (!eint->dual_edge)
		return -ENOMEM;

	eint->domain = irq_domain_add_linear(eint->dev->of_node,
					     eint->hw->ap_num,
					     &irq_domain_simple_ops, NULL);
	if (!eint->domain)
		return -ENOMEM;

	mtk_eint_hw_init(eint);
	for (i = 0; i < eint->hw->ap_num; i++) {
		int virq = irq_create_mapping(eint->domain, i);

		irq_set_chip_and_handler(virq, &mtk_eint_irq_chip,
					 handle_level_irq);
		irq_set_chip_data(virq, eint);
	}

	irq_set_chained_handler_and_data(eint->irq, mtk_eint_irq_handler,
					 eint);

	return 0;
}
