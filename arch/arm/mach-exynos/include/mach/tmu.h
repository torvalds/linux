/*
 * Copyright 2012 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for tmu support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_ARCH_TMU_H
#define __ASM_ARCH_TMU_H

#define MUX_ADDR_VALUE 6
#define TMU_SAVE_NUM 10
#define TMU_DC_VALUE 25
#define EFUSE_MIN_VALUE 40
#define EFUSE_MAX_VALUE 100
#define UNUSED_THRESHOLD 0xFF

enum tmu_status_t {
	TMU_STATUS_INIT = 0,
	TMU_STATUS_NORMAL,
	TMU_STATUS_THROTTLED,
	TMU_STATUS_TRIPPED,
};

enum tmu_noti_state_t {
	TMU_COLD,
	TMU_NORMAL,
	TMU_HOT,
	TMU_CRITICAL,
};

struct temperature_params {
	unsigned int stop_throttle;
	unsigned int start_throttle;
	unsigned int start_tripping; /* temp to do tripping */
	unsigned int start_emergency;
	unsigned int stop_mem_throttle;
	unsigned int start_mem_throttle;
};

struct tmu_data {
	struct temperature_params ts;
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

	bool mem_throttled;
	unsigned int auto_refresh_mem_throttle;
	unsigned int auto_refresh_normal;

	/* monitoring rate */
	unsigned int sampling_rate;

	struct delayed_work polling;
	struct delayed_work monitor;
	unsigned int reg_save[TMU_SAVE_NUM];
};

extern void exynos_tmu_set_platdata(struct tmu_data *pd);
extern struct tmu_info *exynos_tmu_get_platdata(void);
extern int exynos_tmu_get_irqno(int num);
extern struct platform_device exynos_device_tmu;
#ifdef CONFIG_EXYNOS_THERMAL
extern int exynos_tmu_add_notifier(struct notifier_block *n);
#else
static inline int exynos_tmu_add_notifier(struct notifier_block *n)
{
	return 0;
}
#endif
#endif /* __ASM_ARCH_TMU_H */
