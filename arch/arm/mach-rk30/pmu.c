#include <linux/spinlock.h>
#include <mach/pmu.h>
#include <mach/sram.h>

static void __sramfunc pmu_set_power_domain_sram(enum pmu_power_domain pd, bool on)
{
	u32 mask = 1 << pd;
	u32 val = readl_relaxed(RK30_PMU_BASE + PMU_PWRDN_CON);

	if (on)
		val &= ~mask;
	else
		val |=  mask;
	writel_relaxed(val, RK30_PMU_BASE + PMU_PWRDN_CON);
	dsb();

	while (pmu_power_domain_is_on(pd) != on)
		;
}

static noinline void do_pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	static unsigned long save_sp;

	DDR_SAVE_SP(save_sp);
	pmu_set_power_domain_sram(pd, on);
	DDR_RESTORE_SP(save_sp);
}

/*
 *  software should power down or power up power domain one by one. Power down or
 *  power up multiple power domains simultaneously will result in chip electric current
 *  change dramatically which will affect the chip function.
 */
static DEFINE_SPINLOCK(pmu_pd_lock);

void pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);
	do_pmu_set_power_domain(pd, on);
	spin_unlock_irqrestore(&pmu_pd_lock, flags);
}
