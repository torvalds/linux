#ifndef CLOCK_H
#define CLOCK_H

#ifdef CONFIG_COMMON_CLK
/* temporary clock configuration helper for platform devices */

struct clk_name {
	const char *clk;
	const char *con_id;
	const char *dev_id;
};

void shmobile_clk_workaround(const struct clk_name *clks, int nr_clks,
			     bool enable);

#else /* CONFIG_COMMON_CLK */
/* legacy clock implementation */

struct clk;
unsigned long shmobile_fixed_ratio_clk_recalc(struct clk *clk);
extern struct sh_clk_ops shmobile_fixed_ratio_clk_ops;

/* clock ratio */
struct clk_ratio {
	int mul;
	int div;
};

#define SH_CLK_RATIO(name, m, d)		\
static struct clk_ratio name ##_ratio = {	\
	.mul = m,				\
	.div = d,				\
}

#define SH_FIXED_RATIO_CLKg(name, p, r)	\
struct clk name = {			\
	.parent	= &p,				\
	.ops	= &shmobile_fixed_ratio_clk_ops,\
	.priv	= &r ## _ratio,			\
}

#define SH_FIXED_RATIO_CLK(name, p, r)		\
static SH_FIXED_RATIO_CLKg(name, p, r)

#define SH_FIXED_RATIO_CLK_SET(name, p, m, d)	\
	SH_CLK_RATIO(name, m, d);		\
	SH_FIXED_RATIO_CLK(name, p, name)

#define SH_CLK_SET_RATIO(p, m, d)	\
do {			\
	(p)->mul = m;	\
	(p)->div = d;	\
} while (0)

#endif /* CONFIG_COMMON_CLK */
#endif
