/*
 * copyright (c) 2012 samsung electronics co., ltd.
 *		http://www.samsung.com
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license version 2 as
 * published by the free software foundation.
*/

#ifndef _EXYNOS5_VOLT_INFO_H
#define _EXYNOS5_VOLT_INFO_H

#include <linux/regulator/consumer.h>

enum exynos5_volt_id {
	VDD_INT,
	VDD_MIF,
};

struct exynos5_volt_info {
	enum exynos5_volt_id idx;
	struct regulator *vdd_target;
	unsigned int set_volt;
	unsigned int target_volt;
	unsigned int cur_lv;
};

extern int exynos5_volt_ctrl(enum exynos5_volt_id target,
			unsigned int target_volt, unsigned int target_freq);

#endif /* _EXYNOS5_VOLT_INFO_H */
