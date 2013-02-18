/* linux/arch/arm/mach-exynos/include/mach/tmu.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for tmu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_THERMAL_H
#define _S5P_THERMAL_H

#define MUX_ADDR_VALUE 6
#define TMU_SAVE_NUM 10
#define TMU_DC_VALUE 25
#define EFUSE_MIN_VALUE 40
#define EFUSE_MAX_VALUE 100
#define UNUSED_THRESHOLD 0xFF

#define FREQ_IN_PLL       24000000  /* 24MHZ in Hz */
#define AUTO_REFRESH_PERIOD_TQ0    1950
#define AUTO_REFRESH_PERIOD_NORMAL 3900
#define TIMING_AREF_OFFSET 0x30

#if defined(CONFIG_CPU_EXYNOS4212) || defined(CONFIG_CPU_EXYNOS4412)
#define CONFIG_TC_VOLTAGE /* Temperature compensated voltage */
#endif

enum tmu_status_t {
	TMU_STATUS_INIT = 0,
	TMU_STATUS_NORMAL,
	TMU_STATUS_THROTTLED,
	TMU_STATUS_WARNING,
	TMU_STATUS_TRIPPED,
	TMU_STATUS_TC,
};

struct temperature_params {
	unsigned int stop_throttle;
	unsigned int start_throttle;
	unsigned int stop_warning;
	unsigned int start_warning;
	unsigned int start_tripping; /* temp to do tripping */
	unsigned int start_hw_tripping;
	unsigned int stop_mem_throttle;
	unsigned int start_mem_throttle;
#if defined(CONFIG_TC_VOLTAGE)
	int stop_tc;	/* temperature compensation for sram */
	int start_tc;
#endif
};

struct cpufreq_params {
	unsigned int throttle_freq;
	unsigned int warning_freq;
};

#if defined(CONFIG_TC_VOLTAGE)
struct temp_compensate_params {
	 unsigned int arm_volt; /* temperature compensated voltage for ARM */
	 unsigned int bus_volt; /* temperature compensated voltage for BUS */
	 unsigned int g3d_volt; /* temperature compensated voltage for G3D */
};
#endif

struct memory_params {
	unsigned int rclk;
	unsigned int period_bank_refresh;
};

struct tmu_data {
	struct temperature_params ts;
	struct cpufreq_params cpulimit;
	struct memory_params mp;
	unsigned int efuse_value;
	unsigned int slope;
	int mode;
#if defined(CONFIG_TC_VOLTAGE)
	struct temp_compensate_params temp_compensate;
#endif
};

struct tmu_info {
	int id;
	void __iomem	*tmu_base;
	struct device	*dev;
	struct resource *ioarea;
	int irq;

	unsigned int te1; /* triminfo_25 */
	unsigned int te2; /* triminfo_85 */
	int tmu_state;

	unsigned int throttle_freq;
	unsigned int warning_freq;
	/* memory refresh timing compensation */
	unsigned int auto_refresh_tq0;
	unsigned int auto_refresh_normal;
	/* monitoring rate */
	unsigned int sampling_rate;

	/* temperature compensation */
	unsigned int cpulevel_tc;
	unsigned int busfreq_tc;
	unsigned int g3dlevel_tc;

	struct delayed_work polling;
	struct delayed_work monitor;
	unsigned int reg_save[TMU_SAVE_NUM];
#if defined(CONFIG_BUSFREQ_OPP) && defined(CONFIG_TC_VOLTAGE)
	struct device *bus_dev;
#endif
};

void exynos_tmu_set_platdata(struct tmu_data *pd);
struct tmu_info *exynos_tmu_get_platdata(void);
int exynos_tmu_get_irqno(int num);
extern struct platform_device exynos_device_tmu;
extern int mali_dvfs_freq_lock(int level);
extern void mali_dvfs_freq_unlock(void);
#if defined(CONFIG_TC_VOLTAGE)
extern int mali_voltage_lock_init(void);
extern int mali_voltage_lock_push(int lock_vol);
extern int mali_voltage_lock_pop(void);
#endif
#endif /* _S5P_THERMAL_H */
