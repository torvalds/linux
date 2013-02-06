/* linux/arch/arm/plat-s5p/include/plat/s5p-tmu.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for s5p tmu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_TMU_H
#define _S5P_TMU_H

#define TMU_SAVE_NUM   10

/*
 * struct temperature_params have values to manange throttling, tripping
 * and other software safety control
 */
struct temperature_params {
	unsigned int stop_1st_throttle;
	unsigned int start_1st_throttle;
	unsigned int stop_2nd_throttle;
	unsigned int start_2nd_throttle;
	unsigned int start_tripping; /* temp to do tripping */
	unsigned int start_emergency; /* To protect chip,forcely kernel panic */
	unsigned int stop_mem_throttle;
	unsigned int start_mem_throttle;
	unsigned int stop_tc; /* temperature compensationfor sram */
	unsigned int start_tc;
};

struct cpufreq_params {
	unsigned int limit_1st_throttle;
	unsigned int limit_2nd_throttle;
};

struct temp_compensate_params {
	unsigned int arm_volt; /* temperature compensated voltage */
	unsigned int bus_volt; /* temperature compensated voltage */
	unsigned int g3d_volt; /* temperature compensated voltage */
};

struct memory_params {
	unsigned int rclk;
	unsigned int period_bank_refresh;
};

struct tmu_config {
	unsigned char mode;
	unsigned char slope;
	unsigned int sampling_rate;
	unsigned int monitoring_rate;
};

struct s5p_platform_tmu {
	struct temperature_params ts;
	struct cpufreq_params cpufreq;
	struct temp_compensate_params temp_compensate;
	struct memory_params mp;
	struct tmu_config cfg;
};

struct s5p_tmu_info {
	struct device   *dev;
#ifdef CONFIG_BUSFREQ_OPP
	struct device   *bus_dev;
#endif

	int     id;
	char *s5p_name;

	void __iomem    *tmu_base;
	struct resource *ioarea;
	int irq;

	int mode;
	unsigned char te1; /* triminfo_25 */
	unsigned char te2; /* triminfo_85 */
	int slope;

	int	tmu_state;
	unsigned int last_temperature;

	unsigned int cpufreq_level_1st_throttle;
	unsigned int cpufreq_level_2nd_throttle;
	unsigned int auto_refresh_tq0;
	unsigned int auto_refresh_normal;
	/* temperature compensation */
	unsigned int cpulevel_tc;
	unsigned int busfreq_tc;
	unsigned int g3dlevel_tc;

	struct delayed_work monitor;
	struct delayed_work polling;

	unsigned int monitor_period;
	unsigned int sampling_rate;
	unsigned int reg_save[TMU_SAVE_NUM];
};

void __init s5p_tmu_set_platdata(struct s5p_platform_tmu *pd);
struct s5p_tmu *s5p_tmu_get_platdata(void);
int s5p_tmu_get_irqno(int num);
extern struct platform_device s5p_device_tmu;
#endif /* _S5P_TMU_H */
