/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Clock driver for TI Davinci PSC controllers
 *
 * Copyright (C) 2018 David Lechner <david@lechnology.com>
 */

#ifndef __CLK_DAVINCI_PSC_H__
#define __CLK_DAVINCI_PSC_H__

#include <linux/clk-provider.h>
#include <linux/types.h>

/* PSC quirk flags */
#define LPSC_ALWAYS_ENABLED	BIT(0) /* never disable this clock */
#define LPSC_SET_RATE_PARENT	BIT(1) /* propagate set_rate to parent clock */
#define LPSC_FORCE		BIT(2) /* requires MDCTL FORCE bit */
#define LPSC_LOCAL_RESET	BIT(3) /* acts as reset provider */

struct davinci_lpsc_clkdev_info {
	const char *con_id;
	const char *dev_id;
};

#define LPSC_CLKDEV(c, d) {	\
	.con_id = (c),		\
	.dev_id = (d)		\
}

#define LPSC_CLKDEV1(n, c, d) \
static const struct davinci_lpsc_clkdev_info n[] __initconst = {	\
	LPSC_CLKDEV((c), (d)),						\
	{ }								\
}

#define LPSC_CLKDEV2(n, c1, d1, c2, d2) \
static const struct davinci_lpsc_clkdev_info n[] __initconst = {	\
	LPSC_CLKDEV((c1), (d1)),					\
	LPSC_CLKDEV((c2), (d2)),					\
	{ }								\
}

#define LPSC_CLKDEV3(n, c1, d1, c2, d2, c3, d3) \
static const struct davinci_lpsc_clkdev_info n[] __initconst = {	\
	LPSC_CLKDEV((c1), (d1)),					\
	LPSC_CLKDEV((c2), (d2)),					\
	LPSC_CLKDEV((c3), (d3)),					\
	{ }								\
}

/**
 * davinci_lpsc_clk_info - LPSC module-specific clock information
 * @name: the clock name
 * @parent: the parent clock name
 * @cdevs: optional array of clkdev lookup table info
 * @md: the local module domain (LPSC id)
 * @pd: the power domain id
 * @flags: bitmask of LPSC_* flags
 */
struct davinci_lpsc_clk_info {
	const char *name;
	const char *parent;
	const struct davinci_lpsc_clkdev_info *cdevs;
	u32 md;
	u32 pd;
	unsigned long flags;
};

#define LPSC(m, d, n, p, c, f)	\
{				\
	.name	= #n,		\
	.parent	= #p,		\
	.cdevs	= (c),		\
	.md	= (m),		\
	.pd	= (d),		\
	.flags	= (f),		\
}

int davinci_psc_register_clocks(struct device *dev,
				const struct davinci_lpsc_clk_info *info,
				u8 num_clks,
				void __iomem *base);

int of_davinci_psc_clk_init(struct device *dev,
			    const struct davinci_lpsc_clk_info *info,
			    u8 num_clks,
			    void __iomem *base);

/* Device-specific data */

struct davinci_psc_init_data {
	struct clk_bulk_data *parent_clks;
	int num_parent_clks;
	int (*psc_init)(struct device *dev, void __iomem *base);
};

#ifdef CONFIG_ARCH_DAVINCI_DA830
extern const struct davinci_psc_init_data da830_psc0_init_data;
extern const struct davinci_psc_init_data da830_psc1_init_data;
#endif
#ifdef CONFIG_ARCH_DAVINCI_DA850
extern const struct davinci_psc_init_data da850_psc0_init_data;
extern const struct davinci_psc_init_data da850_psc1_init_data;
extern const struct davinci_psc_init_data of_da850_psc0_init_data;
extern const struct davinci_psc_init_data of_da850_psc1_init_data;
#endif
#endif /* __CLK_DAVINCI_PSC_H__ */
