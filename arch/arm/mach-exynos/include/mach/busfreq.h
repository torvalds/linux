/* linux/arch/arm/mach-exynos/include/mach/busfreq.h
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * EXYNOS4 - BUSFreq support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_BUSFREQ_H
#define __ASM_ARCH_BUSFREQ_H __FILE__

#include <linux/notifier.h>
#include <linux/earlysuspend.h>

#include <mach/ppmu.h>

#define MAX_LOAD		100
#define LOAD_HISTORY_SIZE	5
#define DIVIDING_FACTOR		10000

#define TIMINGROW_OFFSET	0x34

struct opp;
struct device;
struct busfreq_table;

struct busfreq_data {
	bool use;
	struct device *dev;
	struct delayed_work worker;
	struct opp *curr_opp;
	struct opp *max_opp;
	struct opp *min_opp;
	struct regulator *vdd_int;
	struct regulator *vdd_mif;
	unsigned int sampling_rate;
	struct kobject *busfreq_kobject;
	int table_size;
	struct busfreq_table *table;
	unsigned long long *time_in_state;
	unsigned long long last_time;
	unsigned int load_history[PPMU_END][LOAD_HISTORY_SIZE];
	int index;

	struct notifier_block exynos_buspm_notifier;
	struct notifier_block exynos_reboot_notifier;
	struct notifier_block exynos_request_notifier;
	struct early_suspend busfreq_early_suspend_handler;
	struct attribute_group busfreq_attr_group;
	int (*init)	(struct device *dev, struct busfreq_data *data);
	struct opp *(*monitor)(struct busfreq_data *data);
	void (*target)	(int mif_index, int int_index);
	unsigned int (*get_int_volt) (unsigned long freq);
	unsigned int (*get_table_index) (struct opp *opp);
	void (*busfreq_prepare) (unsigned int index);
	void (*busfreq_post) (unsigned int index);
	void (*set_qos) (unsigned int index);
	void (*busfreq_suspend) (void);
	void (*busfreq_resume) (void);
};

struct busfreq_table {
	unsigned int idx;
	unsigned int mem_clk;
	unsigned int volt;
	unsigned int clk_topdiv;
	unsigned int clk_dmc0div;
	unsigned int clk_dmc1div;
};

void exynos_request_apply(unsigned long freq, struct device *dev);
struct opp *step_down(struct busfreq_data *data, int step);

#if defined(CONFIG_ARCH_EXYNOS5)
int exynos5250_init(struct device *dev, struct busfreq_data *data);
void exynos5250_target(int mif_index, int int_index);
unsigned int exynos5250_get_int_volt(unsigned long freq);
unsigned int exynos5250_get_table_index(struct opp *opp);
struct opp *exynos5250_monitor(struct busfreq_data *data);
void exynos5250_prepare(unsigned int index);
void exynos5250_post(unsigned int index);
void exynos5250_suspend(void);
void exynos5250_resume(void);

#define exynos4x12_init NULL
#define exynos4x12_target NULL
#define exynos4x12_get_int_volt NULL
#define exynos4x12_get_table_index NULL
#define exynos4x12_monitor NULL
#define exynos4x12_prepare NULL
#define exynos4x12_post NULL
#define exynos4x12_suspend NULL
#define exynos4x12_resume NULL
#elif defined(CONFIG_ARCH_EXYNOS4)
#define exynos5250_init NULL
#define exynos5250_target NULL
#define exynos5250_get_int_volt NULL
#define exynos5250_get_table_index NULL
#define exynos5250_monitor NULL
#define exynos5250_prepare NULL
#define exynos5250_post NULL
#define exynos5250_suspend NULL
#define exynos5250_resume NULL

int exynos4x12_init(struct device *dev, struct busfreq_data *data);
void exynos4x12_target(int mif_index, int int_index);
unsigned int exynos4x12_get_int_volt(unsigned long freq);
unsigned int exynos4x12_get_table_index(struct opp *opp);
struct opp *exynos4x12_monitor(struct busfreq_data *data);
void exynos4x12_prepare(unsigned int index);
void exynos4x12_post(unsigned int index);
void exynos4x12_set_qos(unsigned int index);
void exynos4x12_suspend(void);
void exynos4x12_resume(void);
#endif
#endif /* __ASM_ARCH_BUSFREQ_H */
