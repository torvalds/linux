#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/spinlock.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/seq_file.h>
#include <linux/clkdev.h>

#include <asm/clocks.h>

#define CGU0_CTL_DF (1 << 0)

#define CGU0_CTL_MSEL_SHIFT 8
#define CGU0_CTL_MSEL_MASK (0x7f << 8)

#define CGU0_STAT_PLLEN (1 << 0)
#define CGU0_STAT_PLLBP (1 << 1)
#define CGU0_STAT_PLLLK (1 << 2)
#define CGU0_STAT_CLKSALGN (1 << 3)
#define CGU0_STAT_CCBF0 (1 << 4)
#define CGU0_STAT_CCBF1 (1 << 5)
#define CGU0_STAT_SCBF0 (1 << 6)
#define CGU0_STAT_SCBF1 (1 << 7)
#define CGU0_STAT_DCBF (1 << 8)
#define CGU0_STAT_OCBF (1 << 9)
#define CGU0_STAT_ADDRERR (1 << 16)
#define CGU0_STAT_LWERR (1 << 17)
#define CGU0_STAT_DIVERR (1 << 18)
#define CGU0_STAT_WDFMSERR (1 << 19)
#define CGU0_STAT_WDIVERR (1 << 20)
#define CGU0_STAT_PLOCKERR (1 << 21)

#define CGU0_DIV_CSEL_SHIFT 0
#define CGU0_DIV_CSEL_MASK 0x0000001F
#define CGU0_DIV_S0SEL_SHIFT 5
#define CGU0_DIV_S0SEL_MASK (0x3 << CGU0_DIV_S0SEL_SHIFT)
#define CGU0_DIV_SYSSEL_SHIFT 8
#define CGU0_DIV_SYSSEL_MASK (0x1f << CGU0_DIV_SYSSEL_SHIFT)
#define CGU0_DIV_S1SEL_SHIFT 13
#define CGU0_DIV_S1SEL_MASK (0x3 << CGU0_DIV_S1SEL_SHIFT)
#define CGU0_DIV_DSEL_SHIFT 16
#define CGU0_DIV_DSEL_MASK (0x1f << CGU0_DIV_DSEL_SHIFT)
#define CGU0_DIV_OSEL_SHIFT 22
#define CGU0_DIV_OSEL_MASK (0x7f << CGU0_DIV_OSEL_SHIFT)

#define CLK(_clk, _devname, _conname)                   \
	{                                               \
		.clk    = &_clk,                  \
		.dev_id = _devname,                     \
		.con_id = _conname,                     \
	}

#define NEEDS_INITIALIZATION 0x11

static LIST_HEAD(clk_list);

static void clk_reg_write_mask(u32 reg, uint32_t val, uint32_t mask)
{
	u32 val2;

	val2 = bfin_read32(reg);
	val2 &= ~mask;
	val2 |= val;
	bfin_write32(reg, val2);
}

static void clk_reg_set_bits(u32 reg, uint32_t mask)
{
	u32 val;

	val = bfin_read32(reg);
	val |= mask;
	bfin_write32(reg, val);
}

static void clk_reg_clear_bits(u32 reg, uint32_t mask)
{
	u32 val;

	val = bfin_read32(reg);
	val &= ~mask;
	bfin_write32(reg, val);
}

int wait_for_pll_align(void)
{
	int i = 10000;
	while (i-- && (bfin_read32(CGU0_STAT) & CGU0_STAT_CLKSALGN));

	if (bfin_read32(CGU0_STAT) & CGU0_STAT_CLKSALGN) {
		printk(KERN_DEBUG "fail to align clk\n");
		return -1;
	}
	return 0;
}

int clk_enable(struct clk *clk)
{
	int ret = -EIO;
	if (clk->ops && clk->ops->enable)
		ret = clk->ops->enable(clk);
	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (clk->ops && clk->ops->disable)
		clk->ops->disable(clk);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long ret = 0;
	if (clk->ops && clk->ops->get_rate)
		ret = clk->ops->get_rate(clk);
	return ret;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	long ret = -EIO;
	if (clk->ops && clk->ops->round_rate)
		ret = clk->ops->round_rate(clk, rate);
	return ret;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EIO;
	if (clk->ops && clk->ops->set_rate)
		ret = clk->ops->set_rate(clk, rate);
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

unsigned long vco_get_rate(struct clk *clk)
{
	return clk->rate;
}

unsigned long pll_get_rate(struct clk *clk)
{
	u32 df;
	u32 msel;
	u32 ctl = bfin_read32(CGU0_CTL);
	u32 stat = bfin_read32(CGU0_STAT);
	if (stat & CGU0_STAT_PLLBP)
		return 0;
	msel = (ctl & CGU0_CTL_MSEL_MASK) >> CGU0_CTL_MSEL_SHIFT;
	df = (ctl &  CGU0_CTL_DF);
	clk->parent->rate = clk_get_rate(clk->parent);
	return clk->parent->rate / (df + 1) * msel * 2;
}

unsigned long pll_round_rate(struct clk *clk, unsigned long rate)
{
	u32 div;
	div = rate / clk->parent->rate;
	return clk->parent->rate * div;
}

int pll_set_rate(struct clk *clk, unsigned long rate)
{
	u32 msel;
	u32 stat = bfin_read32(CGU0_STAT);
	if (!(stat & CGU0_STAT_PLLEN))
		return -EBUSY;
	if (!(stat & CGU0_STAT_PLLLK))
		return -EBUSY;
	if (wait_for_pll_align())
		return -EBUSY;
	msel = rate / clk->parent->rate / 2;
	clk_reg_write_mask(CGU0_CTL, msel << CGU0_CTL_MSEL_SHIFT,
		CGU0_CTL_MSEL_MASK);
	clk->rate = rate;
	return 0;
}

unsigned long cclk_get_rate(struct clk *clk)
{
	if (clk->parent)
		return clk->parent->rate;
	else
		return 0;
}

unsigned long sys_clk_get_rate(struct clk *clk)
{
	unsigned long drate;
	u32 msel;
	u32 df;
	u32 ctl = bfin_read32(CGU0_CTL);
	u32 div = bfin_read32(CGU0_DIV);
	div = (div & clk->mask) >> clk->shift;
	msel = (ctl & CGU0_CTL_MSEL_MASK) >> CGU0_CTL_MSEL_SHIFT;
	df = (ctl &  CGU0_CTL_DF);

	if (!strcmp(clk->parent->name, "SYS_CLKIN")) {
		drate = clk->parent->rate / (df + 1);
		drate *=  msel;
		drate /= div;
		return drate;
	} else {
		clk->parent->rate = clk_get_rate(clk->parent);
		return clk->parent->rate / div;
	}
}

unsigned long sys_clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long max_rate;
	unsigned long drate;
	int i;
	u32 msel;
	u32 df;
	u32 ctl = bfin_read32(CGU0_CTL);

	msel = (ctl & CGU0_CTL_MSEL_MASK) >> CGU0_CTL_MSEL_SHIFT;
	df = (ctl &  CGU0_CTL_DF);
	max_rate = clk->parent->rate / (df + 1) * msel;

	if (rate > max_rate)
		return 0;

	for (i = 1; i < clk->mask; i++) {
		drate = max_rate / i;
		if (rate >= drate)
			return drate;
	}
	return 0;
}

int sys_clk_set_rate(struct clk *clk, unsigned long rate)
{
	u32 div = bfin_read32(CGU0_DIV);
	div = (div & clk->mask) >> clk->shift;

	rate = clk_round_rate(clk, rate);

	if (!rate)
		return -EINVAL;

	div = (clk_get_rate(clk) * div) / rate;

	if (wait_for_pll_align())
		return -EBUSY;
	clk_reg_write_mask(CGU0_DIV, div << clk->shift,
			clk->mask);
	clk->rate = rate;
	return 0;
}

static struct clk_ops vco_ops = {
	.get_rate = vco_get_rate,
};

static struct clk_ops pll_ops = {
	.get_rate = pll_get_rate,
	.set_rate = pll_set_rate,
};

static struct clk_ops cclk_ops = {
	.get_rate = cclk_get_rate,
};

static struct clk_ops sys_clk_ops = {
	.get_rate = sys_clk_get_rate,
	.set_rate = sys_clk_set_rate,
	.round_rate = sys_clk_round_rate,
};

static struct clk sys_clkin = {
	.name       = "SYS_CLKIN",
	.rate       = CONFIG_CLKIN_HZ,
	.ops        = &vco_ops,
};

static struct clk pll_clk = {
	.name       = "PLLCLK",
	.rate       = 500000000,
	.parent     = &sys_clkin,
	.ops = &pll_ops,
	.flags = NEEDS_INITIALIZATION,
};

static struct clk cclk = {
	.name       = "CCLK",
	.rate       = 500000000,
	.mask       = CGU0_DIV_CSEL_MASK,
	.shift      = CGU0_DIV_CSEL_SHIFT,
	.parent     = &sys_clkin,
	.ops	    = &sys_clk_ops,
	.flags = NEEDS_INITIALIZATION,
};

static struct clk cclk0 = {
	.name       = "CCLK0",
	.parent     = &cclk,
	.ops	    = &cclk_ops,
};

static struct clk cclk1 = {
	.name       = "CCLK1",
	.parent     = &cclk,
	.ops	    = &cclk_ops,
};

static struct clk sysclk = {
	.name       = "SYSCLK",
	.rate       = 500000000,
	.mask       = CGU0_DIV_SYSSEL_MASK,
	.shift      = CGU0_DIV_SYSSEL_SHIFT,
	.parent     = &sys_clkin,
	.ops	    = &sys_clk_ops,
	.flags = NEEDS_INITIALIZATION,
};

static struct clk sclk0 = {
	.name       = "SCLK0",
	.rate       = 500000000,
	.mask       = CGU0_DIV_S0SEL_MASK,
	.shift      = CGU0_DIV_S0SEL_SHIFT,
	.parent     = &sysclk,
	.ops	    = &sys_clk_ops,
};

static struct clk sclk1 = {
	.name       = "SCLK1",
	.rate       = 500000000,
	.mask       = CGU0_DIV_S1SEL_MASK,
	.shift      = CGU0_DIV_S1SEL_SHIFT,
	.parent     = &sysclk,
	.ops	    = &sys_clk_ops,
};

static struct clk dclk = {
	.name       = "DCLK",
	.rate       = 500000000,
	.mask       = CGU0_DIV_DSEL_MASK,
	.shift       = CGU0_DIV_DSEL_SHIFT,
	.parent     = &pll_clk,
	.ops	    = &sys_clk_ops,
};

static struct clk oclk = {
	.name       = "OCLK",
	.rate       = 500000000,
	.mask       = CGU0_DIV_OSEL_MASK,
	.shift      = CGU0_DIV_OSEL_SHIFT,
	.parent     = &pll_clk,
};

static struct clk_lookup bf609_clks[] = {
	CLK(sys_clkin, NULL, "SYS_CLKIN"),
	CLK(pll_clk, NULL, "PLLCLK"),
	CLK(cclk, NULL, "CCLK"),
	CLK(cclk0, NULL, "CCLK0"),
	CLK(cclk1, NULL, "CCLK1"),
	CLK(sysclk, NULL, "SYSCLK"),
	CLK(sclk0, NULL, "SCLK0"),
	CLK(sclk1, NULL, "SCLK1"),
	CLK(dclk, NULL, "DCLK"),
	CLK(oclk, NULL, "OCLK"),
};

int __init clk_init(void)
{
	int i;
	struct clk *clkp;
	for (i = 0; i < ARRAY_SIZE(bf609_clks); i++) {
		clkp = bf609_clks[i].clk;
		if (clkp->flags & NEEDS_INITIALIZATION)
			clk_get_rate(clkp);
	}
	clkdev_add_table(bf609_clks, ARRAY_SIZE(bf609_clks));
	return 0;
}
