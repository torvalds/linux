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
	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO)
			pmu_set_idle_request(IDLE_REQ_VIO, true);
		else if (pd == PD_VIDEO)
			pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		else if (pd == PD_GPU)
			pmu_set_idle_request(IDLE_REQ_GPU, true);
	}
	do_pmu_set_power_domain(pd, on);
	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO)
			pmu_set_idle_request(IDLE_REQ_VIO, false);
		else if (pd == PD_VIDEO)
			pmu_set_idle_request(IDLE_REQ_VIDEO, false);
		else if (pd == PD_GPU)
			pmu_set_idle_request(IDLE_REQ_GPU, false);
	}
	spin_unlock_irqrestore(&pmu_pd_lock, flags);
}

static DEFINE_SPINLOCK(pmu_misc_con1_lock);

void pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 idle_mask = 1 << (26 - req);
	u32 idle_target = idle << (26 - req);
	u32 mask = 1 << (req + 1);
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&pmu_misc_con1_lock, flags);
	val = readl_relaxed(RK30_PMU_BASE + PMU_MISC_CON1);
	if (idle)
		val |=  mask;
	else
		val &= ~mask;
	writel_relaxed(val, RK30_PMU_BASE + PMU_MISC_CON1);
	dsb();

	while ((readl_relaxed(RK30_PMU_BASE + PMU_PWRDN_ST) & idle_mask) != idle_target)
		;
	spin_unlock_irqrestore(&pmu_misc_con1_lock, flags);
}
