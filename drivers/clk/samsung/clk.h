/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 * Copyright (c) 2013 Linaro Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Common Clock Framework support for all Samsung platforms
*/

#ifndef __SAMSUNG_CLK_H
#define __SAMSUNG_CLK_H

#include <linux/clk-provider.h>
#include "clk-pll.h"

struct clk;

/**
 * struct samsung_clk_provider: information about clock provider
 * @reg_base: virtual address for the register base.
 * @clk_data: holds clock related data like clk* and number of clocks.
 * @lock: maintains exclusion between callbacks for a given clock-provider.
 */
struct samsung_clk_provider {
	void __iomem *reg_base;
	struct clk_onecell_data clk_data;
	spinlock_t lock;
};

/**
 * struct samsung_clock_alias: information about mux clock
 * @id: platform specific id of the clock.
 * @dev_name: name of the device to which this clock belongs.
 * @alias: optional clock alias name to be assigned to this clock.
 */
struct samsung_clock_alias {
	unsigned int		id;
	const char		*dev_name;
	const char		*alias;
};

#define ALIAS(_id, dname, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.alias		= a,				\
	}

#define MHZ (1000 * 1000)

/**
 * struct samsung_fixed_rate_clock: information about fixed-rate clock
 * @id: platform specific id of the clock.
 * @name: name of this fixed-rate clock.
 * @parent_name: optional parent clock name.
 * @flags: optional fixed-rate clock flags.
 * @fixed-rate: fixed clock rate of this clock.
 */
struct samsung_fixed_rate_clock {
	unsigned int		id;
	char			*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		fixed_rate;
};

#define FRATE(_id, cname, pname, f, frate)		\
	{						\
		.id		= _id,			\
		.name		= cname,		\
		.parent_name	= pname,		\
		.flags		= f,			\
		.fixed_rate	= frate,		\
	}

/*
 * struct samsung_fixed_factor_clock: information about fixed-factor clock
 * @id: platform specific id of the clock.
 * @name: name of this fixed-factor clock.
 * @parent_name: parent clock name.
 * @mult: fixed multiplication factor.
 * @div: fixed division factor.
 * @flags: optional fixed-factor clock flags.
 */
struct samsung_fixed_factor_clock {
	unsigned int		id;
	char			*name;
	const char		*parent_name;
	unsigned long		mult;
	unsigned long		div;
	unsigned long		flags;
};

#define FFACTOR(_id, cname, pname, m, d, f)		\
	{						\
		.id		= _id,			\
		.name		= cname,		\
		.parent_name	= pname,		\
		.mult		= m,			\
		.div		= d,			\
		.flags		= f,			\
	}

/**
 * struct samsung_mux_clock: information about mux clock
 * @id: platform specific id of the clock.
 * @dev_name: name of the device to which this clock belongs.
 * @name: name of this mux clock.
 * @parent_names: array of pointer to parent clock names.
 * @num_parents: number of parents listed in @parent_names.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the mux.
 * @shift: starting bit location of the mux control bit-field in @reg.
 * @width: width of the mux control bit-field in @reg.
 * @mux_flags: flags for mux-type clock.
 * @alias: optional clock alias name to be assigned to this clock.
 */
struct samsung_mux_clock {
	unsigned int		id;
	const char		*dev_name;
	const char		*name;
	const char		*const *parent_names;
	u8			num_parents;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			mux_flags;
	const char		*alias;
};

#define __MUX(_id, dname, cname, pnames, o, s, w, f, mf, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_names	= pnames,			\
		.num_parents	= ARRAY_SIZE(pnames),		\
		.flags		= (f) | CLK_SET_RATE_NO_REPARENT, \
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.mux_flags	= mf,				\
		.alias		= a,				\
	}

#define MUX(_id, cname, pnames, o, s, w)			\
	__MUX(_id, NULL, cname, pnames, o, s, w, 0, 0, NULL)

#define MUX_A(_id, cname, pnames, o, s, w, a)			\
	__MUX(_id, NULL, cname, pnames, o, s, w, 0, 0, a)

#define MUX_F(_id, cname, pnames, o, s, w, f, mf)		\
	__MUX(_id, NULL, cname, pnames, o, s, w, f, mf, NULL)

#define MUX_FA(_id, cname, pnames, o, s, w, f, mf, a)		\
	__MUX(_id, NULL, cname, pnames, o, s, w, f, mf, a)

/**
 * @id: platform specific id of the clock.
 * struct samsung_div_clock: information about div clock
 * @dev_name: name of the device to which this clock belongs.
 * @name: name of this div clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the div.
 * @shift: starting bit location of the div control bit-field in @reg.
 * @div_flags: flags for div-type clock.
 * @alias: optional clock alias name to be assigned to this clock.
 */
struct samsung_div_clock {
	unsigned int		id;
	const char		*dev_name;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			shift;
	u8			width;
	u8			div_flags;
	const char		*alias;
	struct clk_div_table	*table;
};

#define __DIV(_id, dname, cname, pname, o, s, w, f, df, a, t)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.shift		= s,				\
		.width		= w,				\
		.div_flags	= df,				\
		.alias		= a,				\
		.table		= t,				\
	}

#define DIV(_id, cname, pname, o, s, w)				\
	__DIV(_id, NULL, cname, pname, o, s, w, 0, 0, NULL, NULL)

#define DIV_A(_id, cname, pname, o, s, w, a)			\
	__DIV(_id, NULL, cname, pname, o, s, w, 0, 0, a, NULL)

#define DIV_F(_id, cname, pname, o, s, w, f, df)		\
	__DIV(_id, NULL, cname, pname, o, s, w, f, df, NULL, NULL)

#define DIV_T(_id, cname, pname, o, s, w, t)			\
	__DIV(_id, NULL, cname, pname, o, s, w, 0, 0, NULL, t)

/**
 * struct samsung_gate_clock: information about gate clock
 * @id: platform specific id of the clock.
 * @dev_name: name of the device to which this clock belongs.
 * @name: name of this gate clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @offset: offset of the register for configuring the gate.
 * @bit_idx: bit index of the gate control bit-field in @reg.
 * @gate_flags: flags for gate-type clock.
 * @alias: optional clock alias name to be assigned to this clock.
 */
struct samsung_gate_clock {
	unsigned int		id;
	const char		*dev_name;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	unsigned long		offset;
	u8			bit_idx;
	u8			gate_flags;
	const char		*alias;
};

#define __GATE(_id, dname, cname, pname, o, b, f, gf, a)	\
	{							\
		.id		= _id,				\
		.dev_name	= dname,			\
		.name		= cname,			\
		.parent_name	= pname,			\
		.flags		= f,				\
		.offset		= o,				\
		.bit_idx	= b,				\
		.gate_flags	= gf,				\
		.alias		= a,				\
	}

#define GATE(_id, cname, pname, o, b, f, gf)			\
	__GATE(_id, NULL, cname, pname, o, b, f, gf, NULL)

#define GATE_A(_id, cname, pname, o, b, f, gf, a)		\
	__GATE(_id, NULL, cname, pname, o, b, f, gf, a)

#define GATE_D(_id, dname, cname, pname, o, b, f, gf)		\
	__GATE(_id, dname, cname, pname, o, b, f, gf, NULL)

#define GATE_DA(_id, dname, cname, pname, o, b, f, gf, a)	\
	__GATE(_id, dname, cname, pname, o, b, f, gf, a)

#define PNAME(x) static const char * const x[] __initconst

/**
 * struct samsung_clk_reg_dump: register dump of clock controller registers.
 * @offset: clock register offset from the controller base address.
 * @value: the value to be register at offset.
 */
struct samsung_clk_reg_dump {
	u32	offset;
	u32	value;
};

/**
 * struct samsung_pll_clock: information about pll clock
 * @id: platform specific id of the clock.
 * @dev_name: name of the device to which this clock belongs.
 * @name: name of this pll clock.
 * @parent_name: name of the parent clock.
 * @flags: optional flags for basic clock.
 * @con_offset: offset of the register for configuring the PLL.
 * @lock_offset: offset of the register for locking the PLL.
 * @type: Type of PLL to be registered.
 * @alias: optional clock alias name to be assigned to this clock.
 */
struct samsung_pll_clock {
	unsigned int		id;
	const char		*dev_name;
	const char		*name;
	const char		*parent_name;
	unsigned long		flags;
	int			con_offset;
	int			lock_offset;
	enum samsung_pll_type	type;
	const struct samsung_pll_rate_table *rate_table;
	const char              *alias;
};

#define __PLL(_typ, _id, _dname, _name, _pname, _flags, _lock, _con,	\
		_rtable, _alias)					\
	{								\
		.id		= _id,					\
		.type		= _typ,					\
		.dev_name	= _dname,				\
		.name		= _name,				\
		.parent_name	= _pname,				\
		.flags		= CLK_GET_RATE_NOCACHE,			\
		.con_offset	= _con,					\
		.lock_offset	= _lock,				\
		.rate_table	= _rtable,				\
		.alias		= _alias,				\
	}

#define PLL(_typ, _id, _name, _pname, _lock, _con, _rtable)	\
	__PLL(_typ, _id, NULL, _name, _pname, CLK_GET_RATE_NOCACHE,	\
		_lock, _con, _rtable, _name)

#define PLL_A(_typ, _id, _name, _pname, _lock, _con, _alias, _rtable) \
	__PLL(_typ, _id, NULL, _name, _pname, CLK_GET_RATE_NOCACHE,	\
		_lock, _con, _rtable, _alias)

struct samsung_clock_reg_cache {
	struct list_head node;
	void __iomem *reg_base;
	struct samsung_clk_reg_dump *rdump;
	unsigned int rd_num;
};

struct samsung_cmu_info {
	/* list of pll clocks and respective count */
	const struct samsung_pll_clock *pll_clks;
	unsigned int nr_pll_clks;
	/* list of mux clocks and respective count */
	const struct samsung_mux_clock *mux_clks;
	unsigned int nr_mux_clks;
	/* list of div clocks and respective count */
	const struct samsung_div_clock *div_clks;
	unsigned int nr_div_clks;
	/* list of gate clocks and respective count */
	const struct samsung_gate_clock *gate_clks;
	unsigned int nr_gate_clks;
	/* list of fixed clocks and respective count */
	const struct samsung_fixed_rate_clock *fixed_clks;
	unsigned int nr_fixed_clks;
	/* list of fixed factor clocks and respective count */
	const struct samsung_fixed_factor_clock *fixed_factor_clks;
	unsigned int nr_fixed_factor_clks;
	/* total number of clocks with IDs assigned*/
	unsigned int nr_clk_ids;

	/* list and number of clocks registers */
	const unsigned long *clk_regs;
	unsigned int nr_clk_regs;
};

extern struct samsung_clk_provider *__init samsung_clk_init(
			struct device_node *np, void __iomem *base,
			unsigned long nr_clks);
extern void __init samsung_clk_of_add_provider(struct device_node *np,
			struct samsung_clk_provider *ctx);
extern void __init samsung_clk_of_register_fixed_ext(
			struct samsung_clk_provider *ctx,
			struct samsung_fixed_rate_clock *fixed_rate_clk,
			unsigned int nr_fixed_rate_clk,
			const struct of_device_id *clk_matches);

extern void samsung_clk_add_lookup(struct samsung_clk_provider *ctx,
			struct clk *clk, unsigned int id);

extern void __init samsung_clk_register_alias(struct samsung_clk_provider *ctx,
			const struct samsung_clock_alias *list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_fixed_rate(
			struct samsung_clk_provider *ctx,
			const struct samsung_fixed_rate_clock *clk_list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_fixed_factor(
			struct samsung_clk_provider *ctx,
			const struct samsung_fixed_factor_clock *list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_mux(struct samsung_clk_provider *ctx,
			const struct samsung_mux_clock *clk_list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_div(struct samsung_clk_provider *ctx,
			const struct samsung_div_clock *clk_list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_gate(struct samsung_clk_provider *ctx,
			const struct samsung_gate_clock *clk_list,
			unsigned int nr_clk);
extern void __init samsung_clk_register_pll(struct samsung_clk_provider *ctx,
			const struct samsung_pll_clock *pll_list,
			unsigned int nr_clk, void __iomem *base);

extern struct samsung_clk_provider __init *samsung_cmu_register_one(
			struct device_node *,
			const struct samsung_cmu_info *);

extern unsigned long _get_rate(const char *clk_name);

extern void samsung_clk_sleep_init(void __iomem *reg_base,
			const unsigned long *rdump,
			unsigned long nr_rdump);

extern void samsung_clk_save(void __iomem *base,
			struct samsung_clk_reg_dump *rd,
			unsigned int num_regs);
extern void samsung_clk_restore(void __iomem *base,
			const struct samsung_clk_reg_dump *rd,
			unsigned int num_regs);
extern struct samsung_clk_reg_dump *samsung_clk_alloc_reg_dump(
			const unsigned long *rdump,
			unsigned long nr_rdump);

#endif /* __SAMSUNG_CLK_H */
