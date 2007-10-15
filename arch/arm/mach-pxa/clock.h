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

extern const struct clkops clk_cken_ops;

void clk_cken_enable(struct clk *clk);
void clk_cken_disable(struct clk *clk);

void clks_register(struct clk *clks, size_t num);
