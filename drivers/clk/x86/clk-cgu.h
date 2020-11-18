/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(c) 2020 Intel Corporation.
 * Zhu YiXin <yixin.zhu@intel.com>
 * Rahul Tanwar <rahul.tanwar@intel.com>
 */

#ifndef __CLK_CGU_H
#define __CLK_CGU_H

#include <linux/io.h>

struct lgm_clk_mux {
	struct clk_hw hw;
	void __iomem *membase;
	unsigned int reg;
	u8 shift;
	u8 width;
	unsigned long flags;
	spinlock_t lock;
};

struct lgm_clk_divider {
	struct clk_hw hw;
	void __iomem *membase;
	unsigned int reg;
	u8 shift;
	u8 width;
	u8 shift_gate;
	u8 width_gate;
	unsigned long flags;
	const struct clk_div_table *table;
	spinlock_t lock;
};

struct lgm_clk_ddiv {
	struct clk_hw hw;
	void __iomem *membase;
	unsigned int reg;
	u8 shift0;
	u8 width0;
	u8 shift1;
	u8 width1;
	u8 shift2;
	u8 width2;
	u8 shift_gate;
	u8 width_gate;
	unsigned int mult;
	unsigned int div;
	unsigned long flags;
	spinlock_t lock;
};

struct lgm_clk_gate {
	struct clk_hw hw;
	void __iomem *membase;
	unsigned int reg;
	u8 shift;
	unsigned long flags;
	spinlock_t lock;
};

enum lgm_clk_type {
	CLK_TYPE_FIXED,
	CLK_TYPE_MUX,
	CLK_TYPE_DIVIDER,
	CLK_TYPE_FIXED_FACTOR,
	CLK_TYPE_GATE,
	CLK_TYPE_NONE,
};

/**
 * struct lgm_clk_provider
 * @membase: IO mem base address for CGU.
 * @np: device node
 * @dev: device
 * @clk_data: array of hw clocks and clk number.
 */
struct lgm_clk_provider {
	void __iomem *membase;
	struct device_node *np;
	struct device *dev;
	struct clk_hw_onecell_data clk_data;
	spinlock_t lock;
};

enum pll_type {
	TYPE_ROPLL,
	TYPE_LJPLL,
	TYPE_NONE,
};

struct lgm_clk_pll {
	struct clk_hw hw;
	void __iomem *membase;
	unsigned int reg;
	unsigned long flags;
	enum pll_type type;
	spinlock_t lock;
};

/**
 * struct lgm_pll_clk_data
 * @id: platform specific id of the clock.
 * @name: name of this pll clock.
 * @parent_data: parent clock data.
 * @num_parents: number of parents.
 * @flags: optional flags for basic clock.
 * @type: platform type of pll.
 * @reg: offset of the register.
 */
struct lgm_pll_clk_data {
	unsigned int id;
	const char *name;
	const struct clk_parent_data *parent_data;
	u8 num_parents;
	unsigned long flags;
	enum pll_type type;
	int reg;
};

#define LGM_PLL(_id, _name, _pdata, _flags,		\
		_reg, _type)				\
	{						\
		.id = _id,				\
		.name = _name,				\
		.parent_data = _pdata,			\
		.num_parents = ARRAY_SIZE(_pdata),	\
		.flags = _flags,			\
		.reg = _reg,				\
		.type = _type,				\
	}

struct lgm_clk_ddiv_data {
	unsigned int id;
	const char *name;
	const struct clk_parent_data *parent_data;
	u8 flags;
	unsigned long div_flags;
	unsigned int reg;
	u8 shift0;
	u8 width0;
	u8 shift1;
	u8 width1;
	u8 shift_gate;
	u8 width_gate;
	u8 ex_shift;
	u8 ex_width;
};

#define LGM_DDIV(_id, _name, _pname, _flags, _reg,		\
		 _shft0, _wdth0, _shft1, _wdth1,		\
		 _shft_gate, _wdth_gate, _xshft, _df)		\
	{							\
		.id = _id,					\
		.name = _name,					\
		.parent_data = &(const struct clk_parent_data){	\
			.fw_name = _pname,			\
			.name = _pname,				\
		},						\
		.flags = _flags,				\
		.reg = _reg,					\
		.shift0 = _shft0,				\
		.width0 = _wdth0,				\
		.shift1 = _shft1,				\
		.width1 = _wdth1,				\
		.shift_gate = _shft_gate,			\
		.width_gate = _wdth_gate,			\
		.ex_shift = _xshft,				\
		.ex_width = 1,					\
		.div_flags = _df,				\
	}

struct lgm_clk_branch {
	unsigned int id;
	enum lgm_clk_type type;
	const char *name;
	const struct clk_parent_data *parent_data;
	u8 num_parents;
	unsigned long flags;
	unsigned int mux_off;
	u8 mux_shift;
	u8 mux_width;
	unsigned long mux_flags;
	unsigned int mux_val;
	unsigned int div_off;
	u8 div_shift;
	u8 div_width;
	u8 div_shift_gate;
	u8 div_width_gate;
	unsigned long div_flags;
	unsigned int div_val;
	const struct clk_div_table *div_table;
	unsigned int gate_off;
	u8 gate_shift;
	unsigned long gate_flags;
	unsigned int gate_val;
	unsigned int mult;
	unsigned int div;
};

/* clock flags definition */
#define CLOCK_FLAG_VAL_INIT	BIT(16)
#define MUX_CLK_SW		BIT(17)

#define LGM_MUX(_id, _name, _pdata, _f, _reg,		\
		_shift, _width, _cf, _v)		\
	{						\
		.id = _id,				\
		.type = CLK_TYPE_MUX,			\
		.name = _name,				\
		.parent_data = _pdata,			\
		.num_parents = ARRAY_SIZE(_pdata),	\
		.flags = _f,				\
		.mux_off = _reg,			\
		.mux_shift = _shift,			\
		.mux_width = _width,			\
		.mux_flags = _cf,			\
		.mux_val = _v,				\
	}

#define LGM_DIV(_id, _name, _pname, _f, _reg, _shift, _width,	\
		_shift_gate, _width_gate, _cf, _v, _dtable)	\
	{							\
		.id = _id,					\
		.type = CLK_TYPE_DIVIDER,			\
		.name = _name,					\
		.parent_data = &(const struct clk_parent_data){	\
			.fw_name = _pname,			\
			.name = _pname,				\
		},						\
		.num_parents = 1,				\
		.flags = _f,					\
		.div_off = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
		.div_shift_gate = _shift_gate,			\
		.div_width_gate = _width_gate,			\
		.div_flags = _cf,				\
		.div_val = _v,					\
		.div_table = _dtable,				\
	}

#define LGM_GATE(_id, _name, _pname, _f, _reg,			\
		 _shift, _cf, _v)				\
	{							\
		.id = _id,					\
		.type = CLK_TYPE_GATE,				\
		.name = _name,					\
		.parent_data = &(const struct clk_parent_data){	\
			.fw_name = _pname,			\
			.name = _pname,				\
		},						\
		.num_parents = !_pname ? 0 : 1,			\
		.flags = _f,					\
		.gate_off = _reg,				\
		.gate_shift = _shift,				\
		.gate_flags = _cf,				\
		.gate_val = _v,					\
	}

#define LGM_FIXED(_id, _name, _pname, _f, _reg,			\
		  _shift, _width, _cf, _freq, _v)		\
	{							\
		.id = _id,					\
		.type = CLK_TYPE_FIXED,				\
		.name = _name,					\
		.parent_data = &(const struct clk_parent_data){	\
			.fw_name = _pname,			\
			.name = _pname,				\
		},						\
		.num_parents = !_pname ? 0 : 1,			\
		.flags = _f,					\
		.div_off = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
		.div_flags = _cf,				\
		.div_val = _v,					\
		.mux_flags = _freq,				\
	}

#define LGM_FIXED_FACTOR(_id, _name, _pname, _f, _reg,		\
			 _shift, _width, _cf, _v, _m, _d)	\
	{							\
		.id = _id,					\
		.type = CLK_TYPE_FIXED_FACTOR,			\
		.name = _name,					\
		.parent_data = &(const struct clk_parent_data){	\
			.fw_name = _pname,			\
			.name = _pname,				\
		},						\
		.num_parents = 1,				\
		.flags = _f,					\
		.div_off = _reg,				\
		.div_shift = _shift,				\
		.div_width = _width,				\
		.div_flags = _cf,				\
		.div_val = _v,					\
		.mult = _m,					\
		.div = _d,					\
	}

static inline void lgm_set_clk_val(void __iomem *membase, u32 reg,
				   u8 shift, u8 width, u32 set_val)
{
	u32 mask = (GENMASK(width - 1, 0) << shift);
	u32 regval;

	regval = readl(membase + reg);
	regval = (regval & ~mask) | ((set_val << shift) & mask);
	writel(regval, membase + reg);
}

static inline u32 lgm_get_clk_val(void __iomem *membase, u32 reg,
				  u8 shift, u8 width)
{
	u32 mask = (GENMASK(width - 1, 0) << shift);
	u32 val;

	val = readl(membase + reg);
	val = (val & mask) >> shift;

	return val;
}

int lgm_clk_register_branches(struct lgm_clk_provider *ctx,
			      const struct lgm_clk_branch *list,
			      unsigned int nr_clk);
int lgm_clk_register_plls(struct lgm_clk_provider *ctx,
			  const struct lgm_pll_clk_data *list,
			  unsigned int nr_clk);
int lgm_clk_register_ddiv(struct lgm_clk_provider *ctx,
			  const struct lgm_clk_ddiv_data *list,
			  unsigned int nr_clk);
#endif	/* __CLK_CGU_H */
