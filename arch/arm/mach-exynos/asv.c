/* linux/arch/arm/mach-exynos/asv.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - ASV(Adaptive Supply Voltage) driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <plat/cpu.h>

#include <mach/map.h>
#include <mach/regs-iem.h>
#include <mach/asv.h>

static struct samsung_asv *exynos_asv;
unsigned int exynos_result_of_asv;
unsigned int exynos_special_flag;
bool exynos_dynamic_ema;

static int __init exynos4_asv_init(void)
{
	int ret = -EINVAL;

	exynos_asv = kzalloc(sizeof(struct samsung_asv), GFP_KERNEL);
	if (!exynos_asv) {
		ret = -ENOMEM;
		goto out1;
	}

	if (soc_is_exynos4210())
		ret = exynos4210_asv_init(exynos_asv);
	else if (soc_is_exynos4412() || soc_is_exynos4212()) {
		ret = exynos4x12_asv_init(exynos_asv);

		/*
		 * If return value is not zero,
		 * There is already value for asv group.
		 * So, It is not necessary to execute for getting asv group.
		 */
		if (ret) {
			kfree(exynos_asv);
			return 0;
		}
	} else if (soc_is_exynos5250())  {
		ret = exynos5250_asv_init(exynos_asv);
                if (ret)
                        return 0;
	} else {
		pr_info("EXYNOS: There is no type for ASV\n");
		goto out2;
	}

	if (exynos_asv->check_vdd_arm) {
		if (exynos_asv->check_vdd_arm()) {
			pr_info("EXYNOS: It is wrong vdd_arm\n");
			goto out2;
		}
	}

	/* Get HPM Delay value */
	if (exynos_asv->get_hpm) {
		if (exynos_asv->get_hpm(exynos_asv)) {
			pr_info("EXYNOS: Fail to get HPM Value\n");
			goto out2;
		}
	} else {
		pr_info("EXYNOS: Fail to get HPM Value\n");
		goto out2;
	}

	/* Get IDS ARM Value */
	if (exynos_asv->get_ids) {
		if (exynos_asv->get_ids(exynos_asv)) {
			pr_info("EXYNOS: Fail to get IDS Value\n");
			goto out2;
		}
	} else {
		pr_info("EXYNOS: Fail to get IDS Value\n");
		goto out2;
	}

	if (exynos_asv->store_result) {
		if (exynos_asv->store_result(exynos_asv)) {
			pr_info("EXYNOS: Can not success to store result\n");
			goto out2;
		}
	} else {
		pr_info("EXYNOS: No store_result function\n");
		goto out2;
	}

	return 0;
out2:
	kfree(exynos_asv);
out1:
	return ret;
}
device_initcall_sync(exynos4_asv_init);
