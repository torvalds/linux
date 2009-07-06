#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <asm/suspend.h>
#include <asm/hwblk.h>
#include <asm/clock.h>

static DEFINE_SPINLOCK(hwblk_lock);

static void hwblk_area_inc(struct hwblk_info *info, int area)
{
	struct hwblk_area *hap = info->areas + area;

	hap->cnt++;
	if (hap->cnt == 1)
		if (hap->flags & HWBLK_AREA_FLAG_PARENT)
			hwblk_area_inc(info, hap->parent);
}

static void hwblk_area_dec(struct hwblk_info *info, int area)
{
	struct hwblk_area *hap = info->areas + area;

	if (hap->cnt == 1)
		if (hap->flags & HWBLK_AREA_FLAG_PARENT)
			hwblk_area_dec(info, hap->parent);
	hap->cnt--;
}

static void hwblk_enable(struct hwblk_info *info, int hwblk)
{
	struct hwblk *hp = info->hwblks + hwblk;
	unsigned long tmp;
	unsigned long flags;

	spin_lock_irqsave(&hwblk_lock, flags);

	hp->cnt++;
	if (hp->cnt == 1) {
		hwblk_area_inc(info, hp->area);

		tmp = __raw_readl(hp->mstp);
		tmp &= ~(1 << hp->bit);
		__raw_writel(tmp, hp->mstp);
	}

	spin_unlock_irqrestore(&hwblk_lock, flags);
}

static void hwblk_disable(struct hwblk_info *info, int hwblk)
{
	struct hwblk *hp = info->hwblks + hwblk;
	unsigned long tmp;
	unsigned long flags;

	spin_lock_irqsave(&hwblk_lock, flags);

	if (hp->cnt == 1) {
		hwblk_area_dec(info, hp->area);

		tmp = __raw_readl(hp->mstp);
		tmp |= 1 << hp->bit;
		__raw_writel(tmp, hp->mstp);
	}
	hp->cnt--;

	spin_unlock_irqrestore(&hwblk_lock, flags);
}

static struct hwblk_info *hwblk_info;

int __init hwblk_register(struct hwblk_info *info)
{
	hwblk_info = info;
	return 0;
}

int __init __weak arch_hwblk_init(void)
{
	return 0;
}

int __weak arch_hwblk_sleep_mode(void)
{
	return SUSP_SH_SLEEP;
}

int __init hwblk_init(void)
{
	return arch_hwblk_init();
}

/* allow clocks to enable and disable hardware blocks */
static int sh_hwblk_clk_enable(struct clk *clk)
{
	if (!hwblk_info)
		return -ENOENT;

	hwblk_enable(hwblk_info, clk->arch_flags);
	return 0;
}

static void sh_hwblk_clk_disable(struct clk *clk)
{
	if (hwblk_info)
		hwblk_disable(hwblk_info, clk->arch_flags);
}

static struct clk_ops sh_hwblk_clk_ops = {
	.enable		= sh_hwblk_clk_enable,
	.disable	= sh_hwblk_clk_disable,
	.recalc		= followparent_recalc,
};

int __init sh_hwblk_clk_register(struct clk *clks, int nr)
{
	struct clk *clkp;
	int ret = 0;
	int k;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;
		clkp->ops = &sh_hwblk_clk_ops;
		ret |= clk_register(clkp);
	}

	return ret;
}
