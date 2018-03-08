// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) STMicroelectronics 2018 - All Rights Reserved
 * Author: Olivier Bideau <olivier.bideau@st.com> for STMicroelectronics.
 * Author: Gabriel Fernandez <gabriel.fernandez@st.com> for STMicroelectronics.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#include <dt-bindings/clock/stm32mp1-clks.h>

static DEFINE_SPINLOCK(rlock);

#define RCC_OCENSETR		0x0C
#define RCC_HSICFGR		0x18
#define RCC_RDLSICR		0x144
#define RCC_PLL1CR		0x80
#define RCC_PLL1CFGR1		0x84
#define RCC_PLL1CFGR2		0x88
#define RCC_PLL2CR		0x94
#define RCC_PLL2CFGR1		0x98
#define RCC_PLL2CFGR2		0x9C
#define RCC_PLL3CR		0x880
#define RCC_PLL3CFGR1		0x884
#define RCC_PLL3CFGR2		0x888
#define RCC_PLL4CR		0x894
#define RCC_PLL4CFGR1		0x898
#define RCC_PLL4CFGR2		0x89C
#define RCC_APB1ENSETR		0xA00
#define RCC_APB2ENSETR		0xA08
#define RCC_APB3ENSETR		0xA10
#define RCC_APB4ENSETR		0x200
#define RCC_APB5ENSETR		0x208
#define RCC_AHB2ENSETR		0xA18
#define RCC_AHB3ENSETR		0xA20
#define RCC_AHB4ENSETR		0xA28
#define RCC_AHB5ENSETR		0x210
#define RCC_AHB6ENSETR		0x218
#define RCC_AHB6LPENSETR	0x318
#define RCC_RCK12SELR		0x28
#define RCC_RCK3SELR		0x820
#define RCC_RCK4SELR		0x824
#define RCC_MPCKSELR		0x20
#define RCC_ASSCKSELR		0x24
#define RCC_MSSCKSELR		0x48
#define RCC_SPI6CKSELR		0xC4
#define RCC_SDMMC12CKSELR	0x8F4
#define RCC_SDMMC3CKSELR	0x8F8
#define RCC_FMCCKSELR		0x904
#define RCC_I2C46CKSELR		0xC0
#define RCC_I2C12CKSELR		0x8C0
#define RCC_I2C35CKSELR		0x8C4
#define RCC_UART1CKSELR		0xC8
#define RCC_QSPICKSELR		0x900
#define RCC_ETHCKSELR		0x8FC
#define RCC_RNG1CKSELR		0xCC
#define RCC_RNG2CKSELR		0x920
#define RCC_GPUCKSELR		0x938
#define RCC_USBCKSELR		0x91C
#define RCC_STGENCKSELR		0xD4
#define RCC_SPDIFCKSELR		0x914
#define RCC_SPI2S1CKSELR	0x8D8
#define RCC_SPI2S23CKSELR	0x8DC
#define RCC_SPI2S45CKSELR	0x8E0
#define RCC_CECCKSELR		0x918
#define RCC_LPTIM1CKSELR	0x934
#define RCC_LPTIM23CKSELR	0x930
#define RCC_LPTIM45CKSELR	0x92C
#define RCC_UART24CKSELR	0x8E8
#define RCC_UART35CKSELR	0x8EC
#define RCC_UART6CKSELR		0x8E4
#define RCC_UART78CKSELR	0x8F0
#define RCC_FDCANCKSELR		0x90C
#define RCC_SAI1CKSELR		0x8C8
#define RCC_SAI2CKSELR		0x8CC
#define RCC_SAI3CKSELR		0x8D0
#define RCC_SAI4CKSELR		0x8D4
#define RCC_ADCCKSELR		0x928
#define RCC_MPCKDIVR		0x2C
#define RCC_DSICKSELR		0x924
#define RCC_CPERCKSELR		0xD0
#define RCC_MCO1CFGR		0x800
#define RCC_MCO2CFGR		0x804
#define RCC_BDCR		0x140
#define RCC_AXIDIVR		0x30
#define RCC_MCUDIVR		0x830
#define RCC_APB1DIVR		0x834
#define RCC_APB2DIVR		0x838
#define RCC_APB3DIVR		0x83C
#define RCC_APB4DIVR		0x3C
#define RCC_APB5DIVR		0x40
#define RCC_TIMG1PRER		0x828
#define RCC_TIMG2PRER		0x82C
#define RCC_RTCDIVR		0x44
#define RCC_DBGCFGR		0x80C

#define RCC_CLR	0x4

struct clock_config {
	u32 id;
	const char *name;
	union {
		const char *parent_name;
		const char * const *parent_names;
	};
	int num_parents;
	unsigned long flags;
	void *cfg;
	struct clk_hw * (*func)(struct device *dev,
				struct clk_hw_onecell_data *clk_data,
				void __iomem *base, spinlock_t *lock,
				const struct clock_config *cfg);
};

#define NO_ID ~0

struct gate_cfg {
	u32 reg_off;
	u8 bit_idx;
	u8 gate_flags;
};

struct fixed_factor_cfg {
	unsigned int mult;
	unsigned int div;
};

struct div_cfg {
	u32 reg_off;
	u8 shift;
	u8 width;
	u8 div_flags;
	const struct clk_div_table *table;
};

static struct clk_hw *
_clk_hw_register_gate(struct device *dev,
		      struct clk_hw_onecell_data *clk_data,
		      void __iomem *base, spinlock_t *lock,
		      const struct clock_config *cfg)
{
	struct gate_cfg *gate_cfg = cfg->cfg;

	return clk_hw_register_gate(dev,
				    cfg->name,
				    cfg->parent_name,
				    cfg->flags,
				    gate_cfg->reg_off + base,
				    gate_cfg->bit_idx,
				    gate_cfg->gate_flags,
				    lock);
}

static struct clk_hw *
_clk_hw_register_fixed_factor(struct device *dev,
			      struct clk_hw_onecell_data *clk_data,
			      void __iomem *base, spinlock_t *lock,
			      const struct clock_config *cfg)
{
	struct fixed_factor_cfg *ff_cfg = cfg->cfg;

	return clk_hw_register_fixed_factor(dev, cfg->name, cfg->parent_name,
					    cfg->flags, ff_cfg->mult,
					    ff_cfg->div);
}

static struct clk_hw *
_clk_hw_register_divider_table(struct device *dev,
			       struct clk_hw_onecell_data *clk_data,
			       void __iomem *base, spinlock_t *lock,
			       const struct clock_config *cfg)
{
	struct div_cfg *div_cfg = cfg->cfg;

	return clk_hw_register_divider_table(dev,
					     cfg->name,
					     cfg->parent_name,
					     cfg->flags,
					     div_cfg->reg_off + base,
					     div_cfg->shift,
					     div_cfg->width,
					     div_cfg->div_flags,
					     div_cfg->table,
					     lock);
}

#define GATE(_id, _name, _parent, _flags, _offset, _bit_idx, _gate_flags)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct gate_cfg) {\
		.reg_off	= _offset,\
		.bit_idx	= _bit_idx,\
		.gate_flags	= _gate_flags,\
	},\
	.func		= _clk_hw_register_gate,\
}

#define FIXED_FACTOR(_id, _name, _parent, _flags, _mult, _div)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct fixed_factor_cfg) {\
		.mult = _mult,\
		.div = _div,\
	},\
	.func		= _clk_hw_register_fixed_factor,\
}

#define DIV_TABLE(_id, _name, _parent, _flags, _offset, _shift, _width,\
		  _div_flags, _div_table)\
{\
	.id		= _id,\
	.name		= _name,\
	.parent_name	= _parent,\
	.flags		= _flags,\
	.cfg		=  &(struct div_cfg) {\
		.reg_off	= _offset,\
		.shift		= _shift,\
		.width		= _width,\
		.div_flags	= _div_flags,\
		.table		= _div_table,\
	},\
	.func		= _clk_hw_register_divider_table,\
}

#define DIV(_id, _name, _parent, _flags, _offset, _shift, _width, _div_flags)\
	DIV_TABLE(_id, _name, _parent, _flags, _offset, _shift, _width,\
		  _div_flags, NULL)

static const struct clock_config stm32mp1_clock_cfg[] = {
	/* Oscillator divider */
	DIV(NO_ID, "clk-hsi-div", "clk-hsi", 0, RCC_HSICFGR, 0, 2,
	    CLK_DIVIDER_READ_ONLY),

	/*  External / Internal Oscillators */
	GATE(CK_LSI, "ck_lsi", "clk-lsi", 0, RCC_RDLSICR, 0, 0),
	GATE(CK_LSE, "ck_lse", "clk-lse", 0, RCC_BDCR, 0, 0),

	FIXED_FACTOR(CK_HSE_DIV2, "clk-hse-div2", "ck_hse", 0, 1, 2),
};

struct stm32_clock_match_data {
	const struct clock_config *cfg;
	unsigned int num;
	unsigned int maxbinding;
};

static struct stm32_clock_match_data stm32mp1_data = {
	.cfg		= stm32mp1_clock_cfg,
	.num		= ARRAY_SIZE(stm32mp1_clock_cfg),
	.maxbinding	= STM32MP1_LAST_CLK,
};

static const struct of_device_id stm32mp1_match_data[] = {
	{
		.compatible = "st,stm32mp1-rcc",
		.data = &stm32mp1_data,
	},
	{ }
};

static int stm32_register_hw_clk(struct device *dev,
				 struct clk_hw_onecell_data *clk_data,
				 void __iomem *base, spinlock_t *lock,
				 const struct clock_config *cfg)
{
	static struct clk_hw **hws;
	struct clk_hw *hw = ERR_PTR(-ENOENT);

	hws = clk_data->hws;

	if (cfg->func)
		hw = (*cfg->func)(dev, clk_data, base, lock, cfg);

	if (IS_ERR(hw)) {
		pr_err("Unable to register %s\n", cfg->name);
		return  PTR_ERR(hw);
	}

	if (cfg->id != NO_ID)
		hws[cfg->id] = hw;

	return 0;
}

static int stm32_rcc_init(struct device_node *np,
			  void __iomem *base,
			  const struct of_device_id *match_data)
{
	struct clk_hw_onecell_data *clk_data;
	struct clk_hw **hws;
	const struct of_device_id *match;
	const struct stm32_clock_match_data *data;
	int err, n, max_binding;

	match = of_match_node(match_data, np);
	if (!match) {
		pr_err("%s: match data not found\n", __func__);
		return -ENODEV;
	}

	data = match->data;

	max_binding =  data->maxbinding;

	clk_data = kzalloc(sizeof(*clk_data) +
				  sizeof(*clk_data->hws) * max_binding,
				  GFP_KERNEL);
	if (!clk_data)
		return -ENOMEM;

	clk_data->num = max_binding;

	hws = clk_data->hws;

	for (n = 0; n < max_binding; n++)
		hws[n] = ERR_PTR(-ENOENT);

	for (n = 0; n < data->num; n++) {
		err = stm32_register_hw_clk(NULL, clk_data, base, &rlock,
					    &data->cfg[n]);
		if (err) {
			pr_err("%s: can't register  %s\n", __func__,
			       data->cfg[n].name);

			kfree(clk_data);

			return err;
		}
	}

	return of_clk_add_hw_provider(np, of_clk_hw_onecell_get, clk_data);
}

static void stm32mp1_rcc_init(struct device_node *np)
{
	void __iomem *base;

	base = of_iomap(np, 0);
	if (!base) {
		pr_err("%s: unable to map resource", np->name);
		of_node_put(np);
		return;
	}

	if (stm32_rcc_init(np, base, stm32mp1_match_data)) {
		iounmap(base);
		of_node_put(np);
	}
}

CLK_OF_DECLARE_DRIVER(stm32mp1_rcc, "st,stm32mp1-rcc", stm32mp1_rcc_init);
