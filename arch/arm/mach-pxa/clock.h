#include <linux/list.h>

struct clk;

struct clkops {
	void			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	unsigned long		(*getrate)(struct clk *);
};

struct clk {
	struct list_head	node;
	const char		*name;
	struct device		*dev;
	const struct clkops	*ops;
	unsigned long		rate;
	unsigned int		cken;
	unsigned int		delay;
	unsigned int		enabled;
	struct clk		*other;
};

#define INIT_CKEN(_name, _cken, _rate, _delay, _dev)	\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= &clk_cken_ops,		\
		.rate	= _rate,			\
		.cken	= CKEN_##_cken,			\
		.delay	= _delay,			\
	}

#define INIT_CK(_name, _cken, _ops, _dev)		\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= _ops,				\
		.cken	= CKEN_##_cken,			\
	}

/*
 * This is a placeholder to alias one clock device+name pair
 * to another struct clk.
 */
#define INIT_CKOTHER(_name, _other, _dev)		\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.other	= _other,			\
	}

#define INIT_CLK(_name, _ops, _rate, _delay, _dev)      \
	{                                               \
		.name   = _name,                        \
		.dev    = _dev,                         \
		.ops    = _ops,                         \
		.rate   = _rate,                        \
		.delay	= _delay,			\
	}

extern const struct clkops clk_cken_ops;

void clk_cken_enable(struct clk *clk);
void clk_cken_disable(struct clk *clk);

#ifdef CONFIG_PXA3xx
#define PXA3xx_CKEN(_name, _cken, _rate, _delay, _dev)	\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= &clk_pxa3xx_cken_ops,		\
		.rate	= _rate,			\
		.cken	= CKEN_##_cken,			\
		.delay	= _delay,			\
	}

#define PXA3xx_CK(_name, _cken, _ops, _dev)		\
	{						\
		.name	= _name,			\
		.dev	= _dev,				\
		.ops	= _ops,				\
		.cken	= CKEN_##_cken,			\
	}

extern const struct clkops clk_pxa3xx_cken_ops;
extern void clk_pxa3xx_cken_enable(struct clk *);
extern void clk_pxa3xx_cken_disable(struct clk *);
#endif

void clks_register(struct clk *clks, size_t num);
int clk_add_alias(char *alias, struct device *alias_dev, char *id,
	struct device *dev);

