// SPDX-License-Identifier: GPL-2.0-only
/*
 * PLL clock driver for the Mobileye EyeQ5, EyeQ6L and EyeQ6H platforms.
 *
 * This controller handles:
 *  - Read-only PLLs, all derived from the same main crystal clock.
 *  - It also exposes divider clocks, those are children to PLLs.
 *  - Fixed factor clocks, children to PLLs.
 *
 * Parent clock is expected to be constant. This driver's registers live in a
 * shared region called OLB. Some PLLs and fixed-factors are initialised early
 * by of_clk_init(); if so, two clk providers are registered.
 *
 * We use eqc_ as prefix, as-in "EyeQ Clock", but way shorter.
 *
 * Copyright (C) 2024 Mobileye Vision Technologies Ltd.
 */

/*
 * Set pr_fmt() for printing from eqc_early_init().
 * It is called at of_clk_init() stage (read: really early).
 */
#define pr_fmt(fmt) "clk-eyeq: " fmt

#include <linux/array_size.h>
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/clk-provider.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/io-64-nonatomic-hi-lo.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/overflow.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>

#include <dt-bindings/clock/mobileye,eyeq5-clk.h>
#include <dt-bindings/clock/mobileye,eyeq6lplus-clk.h>

/* In frac mode, it enables fractional noise canceling DAC. Else, no function. */
#define FRACG_PCSR0_DAC_EN			BIT(0)
/* Fractional or integer mode */
#define FRACG_PCSR0_DSM_EN			BIT(1)
#define FRACG_PCSR0_PLL_EN			BIT(2)
/* All clocks output held at 0 */
#define FRACG_PCSR0_FOUTPOSTDIV_EN		BIT(3)
#define FRACG_PCSR0_POST_DIV1			GENMASK(6, 4)
#define FRACG_PCSR0_POST_DIV2			GENMASK(9, 7)
#define FRACG_PCSR0_REF_DIV			GENMASK(15, 10)
#define FRACG_PCSR0_INTIN			GENMASK(27, 16)
#define FRACG_PCSR0_BYPASS			BIT(28)
/* Bits 30..29 are reserved */
#define FRACG_PCSR0_PLL_LOCKED			BIT(31)

#define FRACG_PCSR1_RESET			BIT(0)
#define FRACG_PCSR1_SSGC_DIV			GENMASK(4, 1)
/* Spread amplitude (% = 0.1 * SPREAD[4:0]) */
#define FRACG_PCSR1_SPREAD			GENMASK(9, 5)
#define FRACG_PCSR1_DIS_SSCG			BIT(10)
/* Down-spread or center-spread */
#define FRACG_PCSR1_DOWN_SPREAD			BIT(11)
#define FRACG_PCSR1_FRAC_IN			GENMASK(31, 12)

/*
 * Special index values to lookup a parent clock by its name
 * from the device tree or by its globally unique name.
 */
#define PARENT_BY_FWNAME			(-1)
#define PARENT_BY_NAME				(-2)

struct eqc_clock {
	int		index;
	int		parent_idx;
	const char	*name;
	const char	*parent_name;
	int		(*probe)(struct device *dev, struct device_node *np,
				 const struct eqc_clock *clk, void __iomem *base,
				 struct clk_hw_onecell_data *cells);
	void		(*unregister)(struct clk_hw *hw);
	union {
		struct {
			unsigned int			reg;
			u8				shift;
			u8				width;
			unsigned long			flags;
			const struct clk_div_table	*table;
		} div;
		struct {
			unsigned int			mult;
			unsigned int			div;
		} ff;
		struct {
			unsigned int			reg;
		} pll;
	};
};

struct eqc_match_data {
	unsigned int		clk_count;
	const struct eqc_clock	*clks;

	const char		*reset_auxdev_name;
	const char		*pinctrl_auxdev_name;
	const char		*eth_phy_auxdev_name;

	unsigned int		early_clk_count;
};

struct eqc_early_match_data {
	unsigned int		early_clk_count;
	const struct eqc_clock	*early_clks;

	/*
	 * We want our of_xlate callback to EPROBE_DEFER instead of dev_err()
	 * and EINVAL. For that, we must know the total clock count.
	 */
	unsigned int		late_clk_count;
};

/*
 * Both factors (mult and div) must fit in 32 bits. When an operation overflows,
 * this function throws away low bits so that factors still fit in 32 bits.
 *
 * Precision loss depends on amplitude of mult and div. Worst theoretical
 * loss is: (UINT_MAX+1) / UINT_MAX - 1 = 2.3e-10.
 * This is 1Hz every 4.3GHz.
 */
static void eqc_pll_downshift_factors(unsigned long *mult, unsigned long *div)
{
	unsigned long biggest;
	unsigned int shift;

	/* This function can be removed if mult/div switch to unsigned long. */
	static_assert(sizeof_field(struct clk_fixed_factor, mult) == sizeof(unsigned int));
	static_assert(sizeof_field(struct clk_fixed_factor, div) == sizeof(unsigned int));

	/* No overflow, nothing to be done. */
	if (*mult <= UINT_MAX && *div <= UINT_MAX)
		return;

	/*
	 * Compute the shift required to bring the biggest factor into unsigned
	 * int range. That is, shift its highest set bit to the unsigned int
	 * most significant bit.
	 */
	biggest = max(*mult, *div);
	shift = __fls(biggest) - (BITS_PER_BYTE * sizeof(unsigned int)) + 1;

	*mult >>= shift;
	*div >>= shift;
}

static int eqc_pll_parse_fracg(void __iomem *base, unsigned long *mult,
			       unsigned long *div, unsigned long *acc)
{
	unsigned long spread;
	u32 r0, r1;
	u64 val;

	val = readq(base);
	r0 = val;
	r1 = val >> 32;

	if (r0 & FRACG_PCSR0_BYPASS) {
		*mult = 1;
		*div = 1;
		*acc = 0;
		return 0;
	}

	if (!(r0 & FRACG_PCSR0_PLL_LOCKED))
		return -EINVAL;

	*mult = FIELD_GET(FRACG_PCSR0_INTIN, r0);
	*div = FIELD_GET(FRACG_PCSR0_REF_DIV, r0);

	/* Fractional mode, in 2^20 (0x100000) parts. */
	if (r0 & FRACG_PCSR0_DSM_EN) {
		*div *= (1ULL << 20);
		*mult = *mult * (1ULL << 20) + FIELD_GET(FRACG_PCSR1_FRAC_IN, r1);
	}

	if (!*mult || !*div)
		return -EINVAL;

	if (r1 & (FRACG_PCSR1_RESET | FRACG_PCSR1_DIS_SSCG)) {
		*acc = 0;
		return 0;
	}

	/*
	 * Spread spectrum.
	 *
	 * Spread is in 1/1024 parts of frequency. Clock accuracy
	 * is half the spread value expressed in parts per billion.
	 *
	 * accuracy = (spread * 1e9) / (1024 * 2)
	 *
	 * Care is taken to avoid overflowing or losing precision.
	 */
	spread = FIELD_GET(FRACG_PCSR1_SPREAD, r1);
	*acc = DIV_ROUND_CLOSEST(spread * 1000000000, 1024 * 2);

	if (r1 & FRACG_PCSR1_DOWN_SPREAD) {
		/*
		 * Downspreading: the central frequency is half a
		 * spread lower.
		 */
		*mult *= 2048 - spread;
		*div *= 2048;

		/*
		 * Previous operation might overflow 32 bits. If it
		 * does, throw away the least amount of low bits.
		 */
		eqc_pll_downshift_factors(mult, div);
	}

	return 0;
}

static void eqc_auxdev_create_optional(struct device *dev, void __iomem *base,
				       const char *name)
{
	struct auxiliary_device *adev;

	if (name) {
		adev = devm_auxiliary_device_create(dev, name,
						    (void __force *)base);
		if (!adev)
			dev_warn(dev, "failed creating auxiliary device %s.%s\n",
				 KBUILD_MODNAME, name);
	}
}

static int eqc_fill_parent_data(const struct eqc_clock *clk,
				struct clk_hw_onecell_data *cells,
				struct clk_parent_data *parent_data)
{
	int pidx = clk->parent_idx;

	memset(parent_data, 0, sizeof(struct clk_parent_data));

	if (pidx == PARENT_BY_FWNAME) {
		/* lookup the parent clock by its fw_name */
		parent_data->index = -1;
		parent_data->fw_name = clk->parent_name;
	} else if (pidx == PARENT_BY_NAME) {
		/* lookup the parent clock by its global name */
		parent_data->index = -1;
		parent_data->name = clk->parent_name;
	} else if (pidx >= 0 && pidx < cells->num && !IS_ERR(cells->hws[pidx])) {
		/* get the parent hw directly */
		parent_data->hw = cells->hws[pidx];
	} else {
		/* no parent lookup by index: explicitly fail */
		return -EINVAL;
	}

	return 0;
}

static int eqc_probe_divider(struct device *dev, struct device_node *np,
			     const struct eqc_clock *clk, void __iomem *base,
			     struct clk_hw_onecell_data *cells)
{
	struct clk_parent_data parent_data;
	struct clk_hw *hw;
	int ret;

	ret = eqc_fill_parent_data(clk, cells, &parent_data);
	if (ret)
		return ret;

	hw = clk_hw_register_divider_table_parent_data(dev, clk->name,
			&parent_data, 0, base + clk->div.reg, clk->div.shift,
			clk->div.width, clk->div.flags, clk->div.table, NULL);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	cells->hws[clk->index] = hw;
	return 0;
}

static int eqc_probe_fixed_factor(struct device *dev, struct device_node *np,
				  const struct eqc_clock *clk, void __iomem *base,
				  struct clk_hw_onecell_data *cells)
{
	struct clk_parent_data parent_data;
	struct clk_hw *hw;
	int ret;

	ret = eqc_fill_parent_data(clk, cells, &parent_data);
	if (ret)
		return ret;

	hw = clk_hw_register_fixed_factor_pdata(dev, np, clk->name, &parent_data, 0,
						clk->ff.mult, clk->ff.div, 0, 0);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	cells->hws[clk->index] = hw;
	return 0;
}

static int eqc_probe_pll_fracg(struct device *dev, struct device_node *np,
			       const struct eqc_clock *clk, void __iomem *base,
			       struct clk_hw_onecell_data *cells)
{
	struct clk_parent_data parent_data;
	unsigned long mult, div, acc;
	struct clk_hw *hw;
	int ret;

	ret = eqc_pll_parse_fracg(base + clk->pll.reg, &mult, &div, &acc);
	if (ret)
		return ret;

	ret = eqc_fill_parent_data(clk, cells, &parent_data);
	if (ret)
		return ret;

	hw = clk_hw_register_fixed_factor_pdata(dev, np, clk->name, &parent_data, 0, mult,
						div, acc, CLK_FIXED_FACTOR_FIXED_ACCURACY);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	cells->hws[clk->index] = hw;
	return 0;
}

static int eqc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	const struct eqc_match_data *data;
	struct clk_hw_onecell_data *cells;
	unsigned int i, clk_count;
	struct resource *res;
	void __iomem *base;
	int ret;

	data = device_get_match_data(dev);
	if (!data)
		return 0; /* No clocks nor auxdevs, we are done. */

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENODEV;

	base = ioremap(res->start, resource_size(res));
	if (!base)
		return -ENOMEM;

	/* Init optional auxiliary devices. */
	eqc_auxdev_create_optional(dev, base, data->reset_auxdev_name);
	eqc_auxdev_create_optional(dev, base, data->pinctrl_auxdev_name);
	eqc_auxdev_create_optional(dev, base, data->eth_phy_auxdev_name);

	if (data->clk_count == 0)
		return 0; /* Zero clocks, we are done. */

	clk_count = data->clk_count + data->early_clk_count;
	cells = kzalloc_flex(*cells, hws, clk_count);
	if (!cells)
		return -ENOMEM;

	cells->num = clk_count;

	/* Early PLLs are marked as errors: the early provider will get queried. */
	for (i = 0; i < clk_count; i++)
		cells->hws[i] = ERR_PTR(-EINVAL);

	for (i = 0; i < data->clk_count; i++) {
		const struct eqc_clock *clk = &data->clks[i];

		if (clk->probe)
			ret = clk->probe(dev, NULL, clk, base, cells);
		else
			ret = -EINVAL;
		if (ret)
			dev_warn(dev, "failed probing clock %s: %d\n", clk->name, ret);
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, cells);
}

#define DIV(_index, _parent_idx, _name, _parent_name,			\
	    _reg, _shift, _width)					\
	{								\
		.index = _index,					\
		.parent_idx = _parent_idx,				\
		.name = _name,						\
		.parent_name = _parent_name,				\
		.probe = eqc_probe_divider,				\
		.unregister = clk_hw_unregister_divider,		\
		.div.reg = _reg,					\
		.div.shift = _shift,					\
		.div.width = _width,					\
		.div.flags = CLK_DIVIDER_EVEN_INTEGERS,			\
		.div.table = NULL,					\
	}

#define FF(_index, _parent_idx, _name, _parent_name, _mult, _div)	\
	{								\
		.index = _index,					\
		.parent_idx = _parent_idx,				\
		.name = _name,						\
		.parent_name = _parent_name,				\
		.probe = eqc_probe_fixed_factor,			\
		.unregister = clk_hw_unregister_fixed_factor,		\
		.ff.mult = _mult,					\
		.ff.div = _div,						\
	}

#define PLL_FRACG(_index, _parent_idx, _name, _parent_name, _reg)	\
	{								\
		.index = _index,					\
		.parent_idx = _parent_idx,				\
		.name = _name,						\
		.parent_name = _parent_name,				\
		.probe = eqc_probe_pll_fracg,				\
		.unregister = clk_hw_unregister_fixed_factor,		\
		.pll.reg = _reg,					\
	}

enum {
	/*
	 * EQ5C_PLL_CPU children.
	 * EQ5C_PER_OCC_PCI is the last clock exposed in dt-bindings.
	 */
	EQ5C_CPU_OCC = EQ5C_PER_OCC_PCI + 1,
	EQ5C_CPU_SI_CSS0,
	EQ5C_CPU_CPC,
	EQ5C_CPU_CM,
	EQ5C_CPU_MEM,
	EQ5C_CPU_OCC_ISRAM,
	EQ5C_CPU_ISRAM,
	EQ5C_CPU_OCC_DBU,
	EQ5C_CPU_SI_DBU_TP,

	/*
	 * EQ5C_PLL_VDI children.
	 */
	EQ5C_VDI_OCC_VDI,
	EQ5C_VDI_VDI,
	EQ5C_VDI_OCC_CAN_SER,
	EQ5C_VDI_CAN_SER,
	EQ5C_VDI_I2C_SER,

	/*
	 * EQ5C_PLL_PER children.
	 */
	EQ5C_PER_PERIPH,
	EQ5C_PER_CAN,
	EQ5C_PER_TIMER,
	EQ5C_PER_CCF,
	EQ5C_PER_OCC_MJPEG,
	EQ5C_PER_HSM,
	EQ5C_PER_MJPEG,
	EQ5C_PER_FCMU_A,
};

/* Required early for GIC timer (pll-cpu) and UARTs (pll-per). */
static const struct eqc_clock eqc_eyeq5_early_clks[] = {
	PLL_FRACG(EQ5C_PLL_CPU, PARENT_BY_FWNAME, "pll-cpu", "ref", 0x02C),
	PLL_FRACG(EQ5C_PLL_PER, PARENT_BY_FWNAME, "pll-per", "ref", 0x05C),

	FF(EQ5C_CPU_OCC, EQ5C_PLL_CPU, "occ-cpu", NULL, 1, 1),
	FF(EQ5C_CPU_SI_CSS0, EQ5C_CPU_OCC, "si-css0", NULL, 1, 1),
	FF(EQ5C_CPU_CORE0, EQ5C_CPU_SI_CSS0, "core0", NULL, 1, 1),
	FF(EQ5C_CPU_CORE1, EQ5C_CPU_SI_CSS0, "core1", NULL, 1, 1),
	FF(EQ5C_CPU_CORE2, EQ5C_CPU_SI_CSS0, "core2", NULL, 1, 1),
	FF(EQ5C_CPU_CORE3, EQ5C_CPU_SI_CSS0, "core3", NULL, 1, 1),

	FF(EQ5C_PER_OCC, EQ5C_PLL_PER, "occ-periph", NULL, 1, 16),
	FF(EQ5C_PER_UART, EQ5C_PER_OCC, "uart", NULL, 1, 1),
};

static const struct eqc_clock eqc_eyeq5_clks[] = {
	PLL_FRACG(EQ5C_PLL_VMP, PARENT_BY_FWNAME, "pll-vmp", "ref", 0x034),
	PLL_FRACG(EQ5C_PLL_PMA, PARENT_BY_FWNAME, "pll-pma", "ref", 0x03C),
	PLL_FRACG(EQ5C_PLL_VDI, PARENT_BY_FWNAME, "pll-vdi", "ref", 0x044),
	PLL_FRACG(EQ5C_PLL_DDR0, PARENT_BY_FWNAME, "pll-ddr0", "ref", 0x04C),
	PLL_FRACG(EQ5C_PLL_PCI, PARENT_BY_FWNAME, "pll-pci", "ref", 0x054),
	PLL_FRACG(EQ5C_PLL_PMAC, PARENT_BY_FWNAME, "pll-pmac", "ref", 0x064),
	PLL_FRACG(EQ5C_PLL_MPC, PARENT_BY_FWNAME, "pll-mpc", "ref", 0x06C),
	PLL_FRACG(EQ5C_PLL_DDR1, PARENT_BY_FWNAME, "pll-ddr1", "ref", 0x074),

	DIV(EQ5C_DIV_OSPI, PARENT_BY_NAME, "div-ospi", "pll-per", 0x11C, 0, 4),

	FF(EQ5C_CPU_CPC, PARENT_BY_NAME, "cpc", "si-css0", 1, 1),
	FF(EQ5C_CPU_CM, PARENT_BY_NAME, "cm", "si-css0", 1, 1),
	FF(EQ5C_CPU_MEM, PARENT_BY_NAME, "mem", "si-css0", 1, 1),
	FF(EQ5C_CPU_OCC_ISRAM, PARENT_BY_NAME, "occ-isram", "pll-cpu", 1, 2),
	FF(EQ5C_CPU_ISRAM, EQ5C_CPU_OCC_ISRAM, "isram", NULL, 1, 1),
	FF(EQ5C_CPU_OCC_DBU, PARENT_BY_NAME, "occ-dbu", "pll-cpu", 1, 10),
	FF(EQ5C_CPU_SI_DBU_TP, EQ5C_CPU_OCC_DBU, "si-dbu-tp", NULL, 1, 1),

	FF(EQ5C_VDI_OCC_VDI, PARENT_BY_NAME, "occ-vdi", "pll-vdi", 1, 2),
	FF(EQ5C_VDI_VDI, EQ5C_VDI_OCC_VDI, "vdi", NULL, 1, 1),
	FF(EQ5C_VDI_OCC_CAN_SER, PARENT_BY_NAME, "occ-can-ser", "pll-vdi", 1, 16),
	FF(EQ5C_VDI_CAN_SER, EQ5C_VDI_OCC_CAN_SER, "can-ser", NULL, 1, 1),
	FF(EQ5C_VDI_I2C_SER, PARENT_BY_NAME, "i2c-ser", "pll-vdi", 1, 20),

	FF(EQ5C_PER_PERIPH, PARENT_BY_NAME, "periph", "occ-periph", 1, 1),
	FF(EQ5C_PER_CAN, PARENT_BY_NAME, "can", "occ-periph", 1, 1),
	FF(EQ5C_PER_SPI, PARENT_BY_NAME, "spi", "occ-periph", 1, 1),
	FF(EQ5C_PER_I2C, PARENT_BY_NAME, "i2c", "occ-periph", 1, 1),
	FF(EQ5C_PER_TIMER, PARENT_BY_NAME, "timer", "occ-periph", 1, 1),
	FF(EQ5C_PER_GPIO, PARENT_BY_NAME, "gpio", "occ-periph", 1, 1),
	FF(EQ5C_PER_EMMC, PARENT_BY_NAME, "emmc-sys", "pll-per", 1, 10),
	FF(EQ5C_PER_CCF, PARENT_BY_NAME, "ccf-ctrl", "pll-per", 1, 4),
	FF(EQ5C_PER_OCC_MJPEG, PARENT_BY_NAME, "occ-mjpeg", "pll-per", 1, 2),
	FF(EQ5C_PER_HSM, EQ5C_PER_OCC_MJPEG, "hsm", NULL, 1, 1),
	FF(EQ5C_PER_MJPEG, EQ5C_PER_OCC_MJPEG, "mjpeg", NULL, 1, 1),
	FF(EQ5C_PER_FCMU_A, PARENT_BY_NAME, "fcmu-a", "pll-per", 1, 20),
	FF(EQ5C_PER_OCC_PCI, PARENT_BY_NAME, "occ-pci-sys", "pll-per", 1, 8),
};

static const struct eqc_early_match_data eqc_eyeq5_early_match_data __initconst = {
	.early_clk_count	= ARRAY_SIZE(eqc_eyeq5_early_clks),
	.early_clks		= eqc_eyeq5_early_clks,

	.late_clk_count		= ARRAY_SIZE(eqc_eyeq5_clks),
};

static const struct eqc_match_data eqc_eyeq5_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq5_clks),
	.clks		= eqc_eyeq5_clks,

	.reset_auxdev_name = "reset",
	.pinctrl_auxdev_name = "pinctrl",
	.eth_phy_auxdev_name = "phy",

	.early_clk_count = ARRAY_SIZE(eqc_eyeq5_early_clks),
};

static const struct eqc_clock eqc_eyeq6l_clks[] = {
	PLL_FRACG(EQ6LC_PLL_DDR, PARENT_BY_FWNAME, "pll-ddr", "ref", 0x02C),
	PLL_FRACG(EQ6LC_PLL_CPU, PARENT_BY_FWNAME, "pll-cpu", "ref", 0x034),
	PLL_FRACG(EQ6LC_PLL_PER, PARENT_BY_FWNAME, "pll-per", "ref", 0x03C),
	PLL_FRACG(EQ6LC_PLL_VDI, PARENT_BY_FWNAME, "pll-vdi", "ref", 0x044),
};

static const struct eqc_match_data eqc_eyeq6l_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6l_clks),
	.clks		= eqc_eyeq6l_clks,

	.reset_auxdev_name = "reset",
};

static const struct eqc_clock eqc_eyeq6lplus_early_clks[] = {
	PLL_FRACG(EQ6LPC_PLL_CPU, PARENT_BY_FWNAME, "pll-cpu", "ref", 0x058),

	FF(EQ6LPC_CPU_OCC, EQ6LPC_PLL_CPU, "occ-cpu", NULL, 1, 1),
};

static const struct eqc_clock eqc_eyeq6lplus_clks[] = {
	PLL_FRACG(EQ6LPC_PLL_DDR, PARENT_BY_FWNAME, "pll-ddr", "ref", 0x02C),
	PLL_FRACG(EQ6LPC_PLL_ACC, PARENT_BY_FWNAME, "pll-acc", "ref", 0x034),
	PLL_FRACG(EQ6LPC_PLL_PER, PARENT_BY_FWNAME, "pll-per", "ref", 0x03C),
	PLL_FRACG(EQ6LPC_PLL_VDI, PARENT_BY_FWNAME, "pll-vdi", "ref", 0x044),

	FF(EQ6LPC_DDR_OCC, EQ6LPC_PLL_DDR, "occ-ddr", NULL, 1, 1),

	FF(EQ6LPC_ACC_VDI, EQ6LPC_PLL_ACC, "vdi-div", NULL, 1, 10),
	FF(EQ6LPC_ACC_OCC, EQ6LPC_PLL_ACC, "occ-acc", NULL, 1, 1),
	FF(EQ6LPC_ACC_FCMU, EQ6LPC_ACC_OCC, "fcmu-a-clk", NULL, 1, 10),

	FF(EQ6LPC_PER_OCC, EQ6LPC_PLL_PER, "occ-per", NULL, 1, 1),
	FF(EQ6LPC_PER_I2C_SER, EQ6LPC_PER_OCC, "i2c-ser-clk", NULL, 1, 10),
	FF(EQ6LPC_PER_PCLK, EQ6LPC_PER_OCC, "pclk", NULL, 1, 4),
	FF(EQ6LPC_PER_TSU, EQ6LPC_PER_OCC, "tsu-clk", NULL, 1, 8),
	FF(EQ6LPC_PER_OSPI, EQ6LPC_PER_OCC, "ospi-ref-clk", NULL, 1, 10),
	FF(EQ6LPC_PER_GPIO, EQ6LPC_PER_OCC, "gpio-clk", NULL, 1, 4),
	FF(EQ6LPC_PER_TIMER, EQ6LPC_PER_OCC, "timer-clk", NULL, 1, 4),
	FF(EQ6LPC_PER_I2C, EQ6LPC_PER_OCC, "i2c-clk", NULL, 1, 4),
	FF(EQ6LPC_PER_UART, EQ6LPC_PER_OCC, "uart-clk", NULL, 1, 4),
	FF(EQ6LPC_PER_SPI, EQ6LPC_PER_OCC, "spi-clk", NULL, 1, 4),
	FF(EQ6LPC_PER_PERIPH, EQ6LPC_PER_OCC, "periph-clk", NULL, 1, 1),

	FF(EQ6LPC_VDI_OCC, EQ6LPC_PLL_VDI, "occ-vdi", NULL, 1, 1),
};

static const struct eqc_early_match_data eqc_eyeq6lplus_early_match_data __initconst = {
	.early_clk_count	= ARRAY_SIZE(eqc_eyeq6lplus_early_clks),
	.early_clks		= eqc_eyeq6lplus_early_clks,

	.late_clk_count		= ARRAY_SIZE(eqc_eyeq6lplus_clks),
};

static const struct eqc_match_data eqc_eyeq6lplus_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6lplus_clks),
	.clks		= eqc_eyeq6lplus_clks,

	.reset_auxdev_name = "reset",
	.pinctrl_auxdev_name = "pinctrl",

	.early_clk_count = ARRAY_SIZE(eqc_eyeq6lplus_early_clks),
};

static const struct eqc_match_data eqc_eyeq6h_west_match_data = {
	.reset_auxdev_name = "reset_west",
};

static const struct eqc_clock eqc_eyeq6h_east_clks[] = {
	PLL_FRACG(0, PARENT_BY_FWNAME, "pll-east", "ref", 0x074),
};

static const struct eqc_match_data eqc_eyeq6h_east_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6h_east_clks),
	.clks		= eqc_eyeq6h_east_clks,

	.reset_auxdev_name = "reset_east",
};

static const struct eqc_clock eqc_eyeq6h_south_clks[] = {
	PLL_FRACG(EQ6HC_SOUTH_PLL_VDI, PARENT_BY_FWNAME, "pll-vdi", "ref", 0x000),
	PLL_FRACG(EQ6HC_SOUTH_PLL_PCIE, PARENT_BY_FWNAME, "pll-pcie", "ref", 0x008),
	PLL_FRACG(EQ6HC_SOUTH_PLL_PER, PARENT_BY_FWNAME, "pll-per", "ref", 0x010),
	PLL_FRACG(EQ6HC_SOUTH_PLL_ISP, PARENT_BY_FWNAME, "pll-isp", "ref", 0x018),

	DIV(EQ6HC_SOUTH_DIV_EMMC, EQ6HC_SOUTH_PLL_PER, "div-emmc", NULL, 0x070, 4, 4),
	DIV(EQ6HC_SOUTH_DIV_OSPI_REF, EQ6HC_SOUTH_PLL_PER, "div-ospi-ref", NULL, 0x090, 4, 4),
	DIV(EQ6HC_SOUTH_DIV_OSPI_SYS, EQ6HC_SOUTH_PLL_PER, "div-ospi-sys", NULL, 0x090, 8, 1),
	DIV(EQ6HC_SOUTH_DIV_TSU, EQ6HC_SOUTH_PLL_PCIE, "div-tsu", NULL, 0x098, 4, 8),
};

static const struct eqc_match_data eqc_eyeq6h_south_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6h_south_clks),
	.clks		= eqc_eyeq6h_south_clks,
};

static const struct eqc_clock eqc_eyeq6h_ddr0_clks[] = {
	PLL_FRACG(0, PARENT_BY_FWNAME, "pll-ddr0", "ref", 0x074),
};

static const struct eqc_match_data eqc_eyeq6h_ddr0_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6h_ddr0_clks),
	.clks		= eqc_eyeq6h_ddr0_clks,
};

static const struct eqc_clock eqc_eyeq6h_ddr1_clks[] = {
	PLL_FRACG(0, PARENT_BY_FWNAME, "pll-ddr1", "ref", 0x074),
};

static const struct eqc_match_data eqc_eyeq6h_ddr1_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6h_ddr1_clks),
	.clks		= eqc_eyeq6h_ddr1_clks,
};

static const struct eqc_clock eqc_eyeq6h_acc_clks[] = {
	PLL_FRACG(EQ6HC_ACC_PLL_XNN, PARENT_BY_FWNAME, "pll-xnn", "ref", 0x040),
	PLL_FRACG(EQ6HC_ACC_PLL_VMP, PARENT_BY_FWNAME, "pll-vmp", "ref", 0x050),
	PLL_FRACG(EQ6HC_ACC_PLL_PMA, PARENT_BY_FWNAME, "pll-pma", "ref", 0x05C),
	PLL_FRACG(EQ6HC_ACC_PLL_MPC, PARENT_BY_FWNAME, "pll-mpc", "ref", 0x068),
	PLL_FRACG(EQ6HC_ACC_PLL_NOC, PARENT_BY_FWNAME, "pll-noc", "ref", 0x070),
};

static const struct eqc_match_data eqc_eyeq6h_acc_match_data = {
	.clk_count	= ARRAY_SIZE(eqc_eyeq6h_acc_clks),
	.clks		= eqc_eyeq6h_acc_clks,

	.reset_auxdev_name = "reset_acc",
};

static const struct of_device_id eqc_match_table[] = {
	{ .compatible = "mobileye,eyeq5-olb", .data = &eqc_eyeq5_match_data },
	{ .compatible = "mobileye,eyeq6l-olb", .data = &eqc_eyeq6l_match_data },
	{ .compatible = "mobileye,eyeq6lplus-olb", .data = &eqc_eyeq6lplus_match_data },
	{ .compatible = "mobileye,eyeq6h-west-olb", .data = &eqc_eyeq6h_west_match_data },
	{ .compatible = "mobileye,eyeq6h-east-olb", .data = &eqc_eyeq6h_east_match_data },
	{ .compatible = "mobileye,eyeq6h-south-olb", .data = &eqc_eyeq6h_south_match_data },
	{ .compatible = "mobileye,eyeq6h-ddr0-olb", .data = &eqc_eyeq6h_ddr0_match_data },
	{ .compatible = "mobileye,eyeq6h-ddr1-olb", .data = &eqc_eyeq6h_ddr1_match_data },
	{ .compatible = "mobileye,eyeq6h-acc-olb", .data = &eqc_eyeq6h_acc_match_data },
	{}
};

static struct platform_driver eqc_driver = {
	.probe = eqc_probe,
	.driver = {
		.name = "clk-eyeq",
		.of_match_table = eqc_match_table,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(eqc_driver);

/* Required early for GIC timer. */
static const struct eqc_clock eqc_eyeq6h_central_early_clks[] = {
	PLL_FRACG(EQ6HC_CENTRAL_PLL_CPU, PARENT_BY_FWNAME, "pll-cpu", "ref", 0x02C),

	FF(EQ6HC_CENTRAL_CPU_OCC, EQ6HC_CENTRAL_PLL_CPU, "occ-cpu", NULL, 1, 1),
};

static const struct eqc_early_match_data eqc_eyeq6h_central_early_match_data __initconst = {
	.early_clk_count	= ARRAY_SIZE(eqc_eyeq6h_central_early_clks),
	.early_clks		= eqc_eyeq6h_central_early_clks,
};

/* Required early for UART. */
static const struct eqc_clock eqc_eyeq6h_west_early_clks[] = {
	PLL_FRACG(EQ6HC_WEST_PLL_PER, PARENT_BY_FWNAME, "pll-west", "ref", 0x074),

	FF(EQ6HC_WEST_PER_OCC, EQ6HC_WEST_PLL_PER, "west-per-occ", NULL, 1, 10),
	FF(EQ6HC_WEST_PER_UART, EQ6HC_WEST_PER_OCC, "west-per-uart", NULL, 1, 1),
};

static const struct eqc_early_match_data eqc_eyeq6h_west_early_match_data __initconst = {
	.early_clk_count	= ARRAY_SIZE(eqc_eyeq6h_west_early_clks),
	.early_clks		= eqc_eyeq6h_west_early_clks,
};

static void __init eqc_early_init(struct device_node *np,
				  const struct eqc_early_match_data *early_data)
{
	struct clk_hw_onecell_data *cells;
	unsigned int i, clk_count;
	void __iomem *base;
	int ret;

	clk_count = early_data->early_clk_count + early_data->late_clk_count;
	cells = kzalloc_flex(*cells, hws, clk_count);
	if (!cells) {
		ret = -ENOMEM;
		goto err;
	}

	cells->num = clk_count;

	/*
	 * Mark all clocks as deferred; some are registered here, the rest at
	 * platform device probe.
	 *
	 * Once the platform device is probed, its provider will take priority
	 * when looking up clocks.
	 */
	for (i = 0; i < clk_count; i++)
		cells->hws[i] = ERR_PTR(-EPROBE_DEFER);

	/* Offsets (reg64) of early PLLs are relative to OLB block. */
	base = of_iomap(np, 0);
	if (!base) {
		ret = -ENODEV;
		goto err;
	}

	for (i = 0; i < early_data->early_clk_count; i++) {
		const struct eqc_clock *clk = &early_data->early_clks[i];

		if (clk->probe)
			ret = clk->probe(NULL, np, clk, base, cells);
		else
			ret = -EINVAL;
		if (ret) {
			pr_err("failed registering %s\n", clk->name);
			goto err;
		}
	}

	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, cells);
	if (ret) {
		pr_err("failed registering clk provider: %d\n", ret);
		goto err;
	}

	return;

err:
	/*
	 * We are doomed. The system will not be able to boot.
	 *
	 * Let's still try to be good citizens by freeing resources and print
	 * a last error message that might help debugging.
	 */

	pr_err("failed clk init: %d\n", ret);

	if (cells) {
		of_clk_del_provider(np);

		for (i = 0; i < early_data->early_clk_count; i++) {
			const struct eqc_clock *clk = &early_data->early_clks[i];
			struct clk_hw *hw = cells->hws[clk->index];

			if (!IS_ERR_OR_NULL(hw) && clk->unregister)
				clk->unregister(hw);
		}

		kfree(cells);
	}
}

static void __init eqc_eyeq5_early_init(struct device_node *np)
{
	eqc_early_init(np, &eqc_eyeq5_early_match_data);
}
CLK_OF_DECLARE_DRIVER(eqc_eyeq5, "mobileye,eyeq5-olb", eqc_eyeq5_early_init);

static void __init eqc_eyeq6h_central_early_init(struct device_node *np)
{
	eqc_early_init(np, &eqc_eyeq6h_central_early_match_data);
}
CLK_OF_DECLARE_DRIVER(eqc_eyeq6h_central, "mobileye,eyeq6h-central-olb",
		      eqc_eyeq6h_central_early_init);

static void __init eqc_eyeq6h_west_early_init(struct device_node *np)
{
	eqc_early_init(np, &eqc_eyeq6h_west_early_match_data);
}
CLK_OF_DECLARE_DRIVER(eqc_eyeq6h_west, "mobileye,eyeq6h-west-olb",
		      eqc_eyeq6h_west_early_init);

static void __init eqc_eyeq6lplus_early_init(struct device_node *np)
{
	eqc_early_init(np, &eqc_eyeq6lplus_early_match_data);
}
CLK_OF_DECLARE_DRIVER(eqc_eyeq6lplus, "mobileye,eyeq6lplus-olb", eqc_eyeq6lplus_early_init);
