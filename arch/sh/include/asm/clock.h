#ifndef __ASM_SH_CLOCK_H
#define __ASM_SH_CLOCK_H

#include <linux/list.h>
#include <linux/seq_file.h>
#include <linux/cpufreq.h>
#include <linux/clk.h>
#include <linux/err.h>

struct clk;

struct clk_ops {
	void (*init)(struct clk *clk);
	int (*enable)(struct clk *clk);
	void (*disable)(struct clk *clk);
	unsigned long (*recalc)(struct clk *clk);
	int (*set_rate)(struct clk *clk, unsigned long rate, int algo_id);
	int (*set_parent)(struct clk *clk, struct clk *parent);
	long (*round_rate)(struct clk *clk, unsigned long rate);
};

struct clk {
	struct list_head	node;
	const char		*name;
	int			id;
	struct module		*owner;

	struct clk		*parent;
	struct clk_ops		*ops;

	struct list_head	children;
	struct list_head	sibling;	/* node for children */

	int			usecount;

	unsigned long		rate;
	unsigned long		flags;

	void __iomem		*enable_reg;
	unsigned int		enable_bit;

	unsigned long		arch_flags;
	void			*priv;
	struct dentry		*dentry;
	struct cpufreq_frequency_table *freq_table;
};

struct clk_lookup {
	struct list_head	node;
	const char		*dev_id;
	const char		*con_id;
	struct clk		*clk;
};

#define CLK_ENABLE_ON_INIT	(1 << 0)

/* Should be defined by processor-specific code */
void __deprecated arch_init_clk_ops(struct clk_ops **, int type);
int __init arch_clk_init(void);

/* arch/sh/kernel/cpu/clock.c */
int clk_init(void);
unsigned long followparent_recalc(struct clk *);
void recalculate_root_clocks(void);
void propagate_rate(struct clk *);
int clk_reparent(struct clk *child, struct clk *parent);
int clk_register(struct clk *);
void clk_unregister(struct clk *);

/* arch/sh/kernel/cpu/clock-cpg.c */
int __init __deprecated cpg_clk_init(void);

/* the exported API, in addition to clk_set_rate */
/**
 * clk_set_rate_ex - set the clock rate for a clock source, with additional parameter
 * @clk: clock source
 * @rate: desired clock rate in Hz
 * @algo_id: algorithm id to be passed down to ops->set_rate
 *
 * Returns success (0) or negative errno.
 */
int clk_set_rate_ex(struct clk *clk, unsigned long rate, int algo_id);

enum clk_sh_algo_id {
	NO_CHANGE = 0,

	IUS_N1_N1,
	IUS_322,
	IUS_522,
	IUS_N11,

	SB_N1,

	SB3_N1,
	SB3_32,
	SB3_43,
	SB3_54,

	BP_N1,

	IP_N1,
};

struct clk_div_mult_table {
	unsigned int *divisors;
	unsigned int nr_divisors;
	unsigned int *multipliers;
	unsigned int nr_multipliers;
};

struct cpufreq_frequency_table;
void clk_rate_table_build(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  int nr_freqs,
			  struct clk_div_mult_table *src_table,
			  unsigned long *bitmap);

long clk_rate_table_round(struct clk *clk,
			  struct cpufreq_frequency_table *freq_table,
			  unsigned long rate);

int clk_rate_table_find(struct clk *clk,
			struct cpufreq_frequency_table *freq_table,
			unsigned long rate);

#define SH_CLK_MSTP32(_name, _id, _parent, _enable_reg,	\
	    _enable_bit, _flags)			\
{							\
	.name		= _name,			\
	.id		= _id,				\
	.parent		= _parent,			\
	.enable_reg	= (void __iomem *)_enable_reg,	\
	.enable_bit	= _enable_bit,			\
	.flags		= _flags,			\
}

int sh_clk_mstp32_register(struct clk *clks, int nr);

#define SH_CLK_DIV4(_name, _parent, _reg, _shift, _div_bitmap, _flags)	\
{									\
	.name = _name,							\
	.parent = _parent,						\
	.enable_reg = (void __iomem *)_reg,				\
	.enable_bit = _shift,						\
	.arch_flags = _div_bitmap,					\
	.flags = _flags,						\
}

int sh_clk_div4_register(struct clk *clks, int nr,
			 struct clk_div_mult_table *table);

#define SH_CLK_DIV6(_name, _parent, _reg, _flags)	\
{							\
	.name = _name,					\
	.parent = _parent,				\
	.enable_reg = (void __iomem *)_reg,		\
	.flags = _flags,				\
}

int sh_clk_div6_register(struct clk *clks, int nr);

#endif /* __ASM_SH_CLOCK_H */
