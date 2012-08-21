#ifndef __MACH_PMU_H
#define __MACH_PMU_H

enum pmu_power_domain {
	PD_A9_0 = 0,
	PD_A9_1,
	PD_ALIVE,
	PD_RTC,
	PD_SCU,
	PD_CPU,
	PD_PERI = 6,
	PD_VIO,
	PD_VIDEO,
	PD_VCODEC = PD_VIDEO,
	PD_GPU,
	PD_DBG,
};

static inline bool pmu_power_domain_is_on(enum pmu_power_domain pd)
{
	return true;
}

static inline void pmu_set_power_domain(enum pmu_power_domain pd, bool on)
{
}

enum pmu_idle_req {
	IDLE_REQ_CPU = 0,
	IDLE_REQ_PERI,
	IDLE_REQ_GPU,
	IDLE_REQ_VIDEO,
	IDLE_REQ_VIO,
};

static inline void pmu_set_idle_request(enum pmu_idle_req req, bool idle)
{
}

#endif
