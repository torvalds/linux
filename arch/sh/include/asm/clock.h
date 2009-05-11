#ifndef __ASM_SH_CLOCK_H
#define __ASM_SH_CLOCK_H

#include <linux/list.h>
#include <linux/seq_file.h>
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
	unsigned long		arch_flags;
};

struct clk_lookup {
	struct list_head	node;
	const char		*dev_id;
	const char		*con_id;
	struct clk		*clk;
};

#define CLK_ENABLE_ON_INIT	(1 << 0)

/* Should be defined by processor-specific code */
void arch_init_clk_ops(struct clk_ops **, int type);
int __init arch_clk_init(void);

/* arch/sh/kernel/cpu/clock.c */
int clk_init(void);
unsigned long followparent_recalc(struct clk *);
void recalculate_root_clocks(void);
void propagate_rate(struct clk *);
int clk_reparent(struct clk *child, struct clk *parent);
int clk_register(struct clk *);
void clk_unregister(struct clk *);

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

#endif /* __ASM_SH_CLOCK_H */
