/* linux/arch/arm/mach-exynos/sysreg.c
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 * MyungJoo Ham <myungjoo.ham@samsung.com>
 *
 * EXYNOS - System Register PM Support Driver
 *
 * Currently support Exynos4210, 4212, 4412.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/syscore_ops.h>

#include <plat/cpu.h>
#include <plat/map-base.h>
#include <plat/pm.h>

#ifdef CONFIG_PM_SLEEP
static struct sleep_save exynos4210_sysreg_save[] = {
	SAVE_ITEM(S3C_VA_SYS + 0x210),
	SAVE_ITEM(S3C_VA_SYS + 0x214),
	SAVE_ITEM(S3C_VA_SYS + 0x218),
	SAVE_ITEM(S3C_VA_SYS + 0x220),
	SAVE_ITEM(S3C_VA_SYS + 0x230),
};

static struct sleep_save exynos4x12_sysreg_save[] = {
	SAVE_ITEM(S3C_VA_SYS + 0x10C),
	SAVE_ITEM(S3C_VA_SYS + 0x110),
	SAVE_ITEM(S3C_VA_SYS + 0x114),
	SAVE_ITEM(S3C_VA_SYS + 0x20C),
	SAVE_ITEM(S3C_VA_SYS + 0x210),
	SAVE_ITEM(S3C_VA_SYS + 0x214),
	SAVE_ITEM(S3C_VA_SYS + 0x218),
	SAVE_ITEM(S3C_VA_SYS + 0x21C),
	SAVE_ITEM(S3C_VA_SYS + 0x320),
	SAVE_ITEM(S3C_VA_SYS + 0x330),
};

static int exynos4_sysreg_suspend(void)
{
	if (soc_is_exynos4210()) {
		s3c_pm_do_save(exynos4210_sysreg_save,
			       ARRAY_SIZE(exynos4210_sysreg_save));
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		s3c_pm_do_save(exynos4x12_sysreg_save,
			       ARRAY_SIZE(exynos4x12_sysreg_save));
	}
	return 0;
}

static void exynos4_sysreg_resume(void)
{
	if (soc_is_exynos4210()) {
		s3c_pm_do_restore_core(exynos4210_sysreg_save,
				       ARRAY_SIZE(exynos4210_sysreg_save));
	} else if (soc_is_exynos4212() || soc_is_exynos4412()) {
		s3c_pm_do_restore_core(exynos4x12_sysreg_save,
				       ARRAY_SIZE(exynos4x12_sysreg_save));
	}
}

static struct syscore_ops exynos4_syscore_ops = {
	.suspend	= exynos4_sysreg_suspend,
	.resume		= exynos4_sysreg_resume,
};

static int __init exynos4_register_sysreg_pm(void)
{
	register_syscore_ops(&exynos4_syscore_ops);
	return 0;
}
arch_initcall(exynos4_register_sysreg_pm);
#endif
