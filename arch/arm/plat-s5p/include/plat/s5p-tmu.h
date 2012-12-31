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

#ifndef _S5P_THERMAL_H
#define _S5P_THERMAL_H

#define TMU_SAVE_NUM 10
#define TMU_DC_VALUE 25
#define EFUSE_MIN_VALUE 40
#define EFUSE_MAX_VALUE 100

struct temperature_params {
	unsigned int stop_throttle;
	unsigned int start_throttle;
	unsigned int stop_warning;
	unsigned int start_warning;
	unsigned int start_tripping; /* temp to do tripping */
};

struct cpufreq_params {
	unsigned int throttle_freq;
	unsigned int warning_freq;
};

struct tmu_data {
	struct temperature_params ts;
	struct cpufreq_params cpulimit;
	unsigned int efuse_value;
	unsigned int slope;
	int mode;
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

	struct delayed_work polling;
	struct delayed_work monitor;
	unsigned int reg_save[TMU_SAVE_NUM];
};

void s5p_tmu_set_platdata(struct tmu_data *pd);
struct tmu_info *s5p_tmu_get_platdata(void);
int s5p_tmu_get_irqno(int num);
extern struct platform_device exynos_device_tmu;
#endif /* _S5P_THERMAL_H */
