#include <linux/module.h>
#include <linux/spinlock.h>
#include <mach/pmu.h>
#include <mach/sram.h>
#include <mach/cpu_axi.h>

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
static u32 lcdc0_qos[CPU_AXI_QOS_NUM_REGS];
static u32 lcdc1_qos[CPU_AXI_QOS_NUM_REGS];
static u32 cif0_qos[CPU_AXI_QOS_NUM_REGS];
static u32 cif1_qos[CPU_AXI_QOS_NUM_REGS];
static u32 ipp_qos[CPU_AXI_QOS_NUM_REGS];
static u32 rga_qos[CPU_AXI_QOS_NUM_REGS];
static u32 gpu_qos[CPU_AXI_QOS_NUM_REGS];
static u32 vpu_qos[CPU_AXI_QOS_NUM_REGS];

void pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
	unsigned long flags;

	spin_lock_irqsave(&pmu_pd_lock, flags);
	if (pmu_power_domain_is_on(pd) == on) {
		spin_unlock_irqrestore(&pmu_pd_lock, flags);
		return;
	}
	if (!on) {
		/* if power down, idle request to NIU first */
		if (pd == PD_VIO) {
			CPU_AXI_SAVE_QOS(lcdc0_qos, LCDC0);
			CPU_AXI_SAVE_QOS(lcdc1_qos, LCDC1);
			CPU_AXI_SAVE_QOS(cif0_qos, CIF0);
			CPU_AXI_SAVE_QOS(cif1_qos, CIF1);
			CPU_AXI_SAVE_QOS(ipp_qos, IPP);
			CPU_AXI_SAVE_QOS(rga_qos, RGA);
			pmu_set_idle_request(IDLE_REQ_VIO, true);
		} else if (pd == PD_VIDEO) {
			CPU_AXI_SAVE_QOS(vpu_qos, VPU);
			pmu_set_idle_request(IDLE_REQ_VIDEO, true);
		} else if (pd == PD_GPU) {
			CPU_AXI_SAVE_QOS(gpu_qos, GPU);
			pmu_set_idle_request(IDLE_REQ_GPU, true);
		}
	}
	do_pmu_set_power_domain(pd, on);
	if (on) {
		/* if power up, idle request release to NIU */
		if (pd == PD_VIO) {
			pmu_set_idle_request(IDLE_REQ_VIO, false);
			CPU_AXI_RESTORE_QOS(lcdc0_qos, LCDC0);
			CPU_AXI_RESTORE_QOS(lcdc1_qos, LCDC1);
			CPU_AXI_RESTORE_QOS(cif0_qos, CIF0);
			CPU_AXI_RESTORE_QOS(cif1_qos, CIF1);
			CPU_AXI_RESTORE_QOS(ipp_qos, IPP);
			CPU_AXI_RESTORE_QOS(rga_qos, RGA);
		} else if (pd == PD_VIDEO) {
			pmu_set_idle_request(IDLE_REQ_VIDEO, false);
			CPU_AXI_RESTORE_QOS(vpu_qos, VPU);
		} else if (pd == PD_GPU) {
			pmu_set_idle_request(IDLE_REQ_GPU, false);
			CPU_AXI_RESTORE_QOS(gpu_qos, GPU);
		}
	}
	spin_unlock_irqrestore(&pmu_pd_lock, flags);
}
EXPORT_SYMBOL(pmu_set_power_domain);

static DEFINE_SPINLOCK(pmu_misc_con1_lock);

void pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
	u32 idle_mask = 1 << (26 - req);
	u32 idle_target = idle << (26 - req);
	u32 ack_mask = 1 << (31 - req);
	u32 ack_target = idle << (31 - req);
	u32 mask = 1 << (req + 1);
	u32 val;
	unsigned long flags;

#if defined(CONFIG_ARCH_RK3188)
	if (req == IDLE_REQ_CORE) {
		idle_mask = 1 << 15;
		idle_target = idle << 15;
		ack_mask = 1 << 18;
		ack_target = idle << 18;
	} else if (req == IDLE_REQ_DMA) {
		idle_mask = 1 << 14;
		idle_target = idle << 14;
		ack_mask = 1 << 17;
		ack_target = idle << 17;
	}
#endif

	spin_lock_irqsave(&pmu_misc_con1_lock, flags);
	val = readl_relaxed(RK30_PMU_BASE + PMU_MISC_CON1);
	if (idle)
		val |=  mask;
	else
		val &= ~mask;
	writel_relaxed(val, RK30_PMU_BASE + PMU_MISC_CON1);
	dsb();

	while ((readl_relaxed(RK30_PMU_BASE + PMU_PWRDN_ST) & ack_mask) != ack_target)
		;
	while ((readl_relaxed(RK30_PMU_BASE + PMU_PWRDN_ST) & idle_mask) != idle_target)
		;
	spin_unlock_irqrestore(&pmu_misc_con1_lock, flags);
}
EXPORT_SYMBOL(pmu_set_idle_request);
