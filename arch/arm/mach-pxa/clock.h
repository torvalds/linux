#include <asm/clkdev.h>

struct clkops {
	void			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
	unsigned long		(*getrate)(struct clk *);
};

struct clk {
	const struct clkops	*ops;
	unsigned long		rate;
	unsigned int		cken;
	unsigned int		delay;
	unsigned int		enabled;
};

#define INIT_CLKREG(_clk,_devname,_conname)		\
	{						\
		.clk		= _clk,			\
		.dev_id		= _devname,		\
		.con_id		= _conname,		\
	}

#define DEFINE_CKEN(_name, _cken, _rate, _delay)	\
struct clk clk_##_name = {				\
		.ops	= &clk_cken_ops,		\
		.rate	= _rate,			\
		.cken	= CKEN_##_cken,			\
		.delay	= _delay,			\
	}

#define DEFINE_CK(_name, _cken, _ops)			\
struct clk clk_##_name = {				\
		.ops	= _ops,				\
		.cken	= CKEN_##_cken,			\
	}

#define DEFINE_CLK(_name, _ops, _rate, _delay)		\
struct clk clk_##_name = {				\
		.ops	= _ops, 			\
		.rate	= _rate,			\
		.delay	= _delay,			\
	}

extern const struct clkops clk_cken_ops;

void clk_cken_enable(struct clk *clk);
void clk_cken_disable(struct clk *clk);

#ifdef CONFIG_PXA3xx
#define DEFINE_PXA3_CKEN(_name, _cken, _rate, _delay)	\
struct clk clk_##_name = {				\
		.ops	= &clk_pxa3xx_cken_ops,		\
		.rate	= _rate,			\
		.cken	= CKEN_##_cken,			\
		.delay	= _delay,			\
	}

#define DEFINE_PXA3_CK(_name, _cken, _ops)		\
struct clk clk_##_name = {				\
		.ops	= _ops,				\
		.cken	= CKEN_##_cken,			\
	}

extern const struct clkops clk_pxa3xx_cken_ops;
extern void clk_pxa3xx_cken_enable(struct clk *);
extern void clk_pxa3xx_cken_disable(struct clk *);
#endif

