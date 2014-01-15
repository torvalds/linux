#ifndef __MACH_ROCKCHIP_PMU_H
#define __MACH_ROCKCHIP_PMU_H

#define RK3188_PMU_WAKEUP_CFG0          0x00
#define RK3188_PMU_WAKEUP_CFG1          0x04
#define RK3188_PMU_PWRDN_CON            0x08
#define RK3188_PMU_PWRDN_ST             0x0c
#define RK3188_PMU_INT_CON              0x10
#define RK3188_PMU_INT_ST               0x14
#define RK3188_PMU_MISC_CON             0x18
#define RK3188_PMU_OSC_CNT              0x1c
#define RK3188_PMU_PLL_CNT              0x20
#define RK3188_PMU_PMU_CNT              0x24
#define RK3188_PMU_DDRIO_PWRON_CNT      0x28
#define RK3188_PMU_WAKEUP_RST_CLR_CNT   0x2c
#define RK3188_PMU_SCU_PWRDWN_CNT       0x30
#define RK3188_PMU_SCU_PWRUP_CNT        0x34
#define RK3188_PMU_MISC_CON1            0x38
#define RK3188_PMU_GPIO0_CON            0x3c
#define RK3188_PMU_SYS_REG0             0x40
#define RK3188_PMU_SYS_REG1             0x44
#define RK3188_PMU_SYS_REG2             0x48
#define RK3188_PMU_SYS_REG3             0x4c
#define RK3188_PMU_STOP_INT_DLY         0x60
#define RK3188_PMU_GPIO0A_PULL          0x64
#define RK3188_PMU_GPIO0B_PULL          0x68

enum pmu_power_domain {
	PD_BCPU,
	PD_BDSP,
	PD_BUS,
	PD_CPU_0,
	PD_CPU_1,
	PD_CPU_2,
	PD_CPU_3,
	PD_CS,
	PD_GPU,
	PD_PERI,
	PD_SCU,
	PD_VIDEO,
	PD_VIO,
};

enum pmu_idle_req {
	IDLE_REQ_ALIVE,
	IDLE_REQ_AP2BP,
	IDLE_REQ_BP2AP,
	IDLE_REQ_BUS,
	IDLE_REQ_CORE,
	IDLE_REQ_DMA,
	IDLE_REQ_GPU,
	IDLE_REQ_PERI,
	IDLE_REQ_VIDEO,
	IDLE_REQ_VIO,
};

struct rockchip_pmu_operations {
	int (*set_power_domain)(enum pmu_power_domain pd, bool on);
	bool (*power_domain_is_on)(enum pmu_power_domain pd);
	int (*set_idle_request)(enum pmu_idle_req req, bool idle);
};

extern struct rockchip_pmu_operations rockchip_pmu_ops;

#endif
