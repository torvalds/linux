#ifndef __MACH_PMU_H
#define __MACH_PMU_H

#include <linux/io.h>

#define PMU_WAKEUP_CFG0		0x00
#define PMU_WAKEUP_CFG1		0x04
#define PMU_PWRDN_CON		0x08
#define PMU_PWRDN_ST		0x0c
#define PMU_INT_CON		0x10
#define PMU_INT_ST		0x14
#define PMU_MISC_CON		0x18
#define PMU_OSC_CNT		0x1c
#define PMU_PLL_CNT		0x20
#define PMU_PMU_CNT		0x24
#define PMU_DDRIO_PWRON_CNT	0x28
#define PMU_WAKEUP_RST_CLR_CNT	0x2c
#define PMU_SCU_PWRDWN_CNT	0x30
#define PMU_SCU_PWRUP_CNT	0x34
#define PMU_MISC_CON1		0x38
#define PMU_GPIO0_CON		0x3c
#define PMU_SYS_REG0		0x40
#define PMU_SYS_REG1		0x44
#define PMU_SYS_REG2		0x48
#define PMU_SYS_REG3		0x4c
#define PMU_STOP_INT_DLY	0x60
#define PMU_GPIO0A_PULL		0x64
#define PMU_GPIO0B_PULL		0x68

enum pmu_power_domain {
	PD_A9_0 = 0,
	PD_A9_1,
	PD_A9_2,
	PD_A9_3,
	PD_SCU,
	PD_CPU,
	PD_PERI,
	PD_VIO,
	PD_VIDEO,
	PD_GPU,
	PD_CS,
	PD_DBG = PD_CS,
};

static inline bool pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	return !(readl_relaxed(RK30_PMU_BASE + PMU_PWRDN_ST) & (1 << pd));
}

void pmu_set_power_domain(enum pmu_power_domain pd, bool on);

enum pmu_idle_req {
	IDLE_REQ_CPU = 0,
	IDLE_REQ_PERI,
	IDLE_REQ_GPU,
	IDLE_REQ_VIDEO,
	IDLE_REQ_VIO,
	IDLE_REQ_CORE = 13,
	IDLE_REQ_DMA = 15,
};

void pmu_set_idle_request(enum pmu_idle_req req, bool idle);

#endif
