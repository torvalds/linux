/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2014-2018 MediaTek Inc.
 *
 * Author: Maoguang Meng <maoguang.meng@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 *
 */
#ifndef __MTK_EINT_H
#define __MTK_EINT_H

#include <linux/irqdomain.h>

struct mtk_eint_regs {
	unsigned int	stat;
	unsigned int	ack;
	unsigned int	mask;
	unsigned int	mask_set;
	unsigned int	mask_clr;
	unsigned int	sens;
	unsigned int	sens_set;
	unsigned int	sens_clr;
	unsigned int	soft;
	unsigned int	soft_set;
	unsigned int	soft_clr;
	unsigned int	pol;
	unsigned int	pol_set;
	unsigned int	pol_clr;
	unsigned int	dom_en;
	unsigned int	dbnc_ctrl;
	unsigned int	dbnc_set;
	unsigned int	dbnc_clr;
};

struct mtk_eint_hw {
	u8		port_mask;
	u8		ports;
	unsigned int	ap_num;
	unsigned int	db_cnt;
};

struct mtk_eint;

struct mtk_eint_xt {
	int (*get_gpio_n)(void *data, unsigned long eint_n,
			  unsigned int *gpio_n,
			  struct gpio_chip **gpio_chip);
	int (*get_gpio_state)(void *data, unsigned long eint_n);
	int (*set_gpio_as_eint)(void *data, unsigned long eint_n);
};

struct mtk_eint {
	struct device *dev;
	void __iomem *base;
	struct irq_domain *domain;
	int irq;

	int *dual_edge;
	u32 *wake_mask;
	u32 *cur_mask;

	/* Used to fit into various EINT device */
	const struct mtk_eint_hw *hw;
	const struct mtk_eint_regs *regs;

	/* Used to fit into various pinctrl device */
	void *pctl;
	const struct mtk_eint_xt *gpio_xlate;
};

#if IS_ENABLED(CONFIG_EINT_MTK)
int mtk_eint_do_init(struct mtk_eint *eint);
int mtk_eint_do_suspend(struct mtk_eint *eint);
int mtk_eint_do_resume(struct mtk_eint *eint);
int mtk_eint_set_debounce(struct mtk_eint *eint, unsigned long eint_n,
			  unsigned int debounce);
int mtk_eint_find_irq(struct mtk_eint *eint, unsigned long eint_n);

#else
static inline int mtk_eint_do_init(struct mtk_eint *eint)
{
	return -EOPNOTSUPP;
}

static inline int mtk_eint_do_suspend(struct mtk_eint *eint)
{
	return -EOPNOTSUPP;
}

static inline int mtk_eint_do_resume(struct mtk_eint *eint)
{
	return -EOPNOTSUPP;
}

int mtk_eint_set_debounce(struct mtk_eint *eint, unsigned long eint_n,
			  unsigned int debounce)
{
	return -EOPNOTSUPP;
}

int mtk_eint_find_irq(struct mtk_eint *eint, unsigned long eint_n)
{
	return -EOPNOTSUPP;
}
#endif
#endif /* __MTK_EINT_H */
