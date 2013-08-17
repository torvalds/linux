/* linux/arch/arm/mach-exynos/include/mach/asv.h
 *
 * Copyright (c) 2012 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS4 - Adoptive Support Voltage Header file
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#ifndef __ASM_ARCH_ASV_H
#define __ASM_ARCH_ASV_H __FILE__

#include <mach/regs-pmu.h>

#include <plat/cpu.h>

#define JUDGE_TABLE_END			NULL

#define LOOP_CNT			10

#define ABB_MODE_060V				0
#define ABB_MODE_065V				1
#define ABB_MODE_070V				2
#define ABB_MODE_075V				3
#define ABB_MODE_080V				4
#define ABB_MODE_085V				5
#define ABB_MODE_090V				6
#define ABB_MODE_095V				7
#define ABB_MODE_100V				8
#define ABB_MODE_105V				9
#define ABB_MODE_110V				10
#define ABB_MODE_115V				11
#define ABB_MODE_120V				12
#define ABB_MODE_125V				13
#define ABB_MODE_130V				14
#define ABB_MODE_135V				15
#define ABB_MODE_140V				16
#define ABB_MODE_145V				17
#define ABB_MODE_150V				18
#define ABB_MODE_155V				19
#define ABB_MODE_160V				20
#define ABB_MODE_BYPASS				255

#define ABB_INIT				(0x80000080)
#define ABB_INIT_BYPASS				(0x80000000)

#define MIF_LOCK_FLAG		0
#define INT_LOCK_FLAG		1
#define G3D_LOCK_FLAG		2
#define ARM_LOCK_FLAG		3

extern unsigned int exynos_result_of_asv;
extern unsigned int exynos_result_mif_asv;
extern unsigned int exynos_special_flag;
extern bool exynos_lot_id;
extern bool exynos_lot_is_nzvpu;
extern bool exynos_dynamic_ema;

static inline unsigned int is_special_flag(void)
{
	return exynos_special_flag;
}

enum exynos4x12_abb_member {
	ABB_INT,
	ABB_MIF,
	ABB_G3D,
	ABB_ARM,
};

static inline void exynos4x12_set_abb_member(enum exynos4x12_abb_member abb_target,
					     unsigned int abb_mode_value)
{
	unsigned int tmp;

	if (abb_mode_value != ABB_MODE_BYPASS) {
		tmp = ABB_INIT;
		tmp |= abb_mode_value;
	} else
		tmp = ABB_INIT_BYPASS;

	if (!soc_is_exynos5250())
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(abb_target));
	else if (abb_target == ABB_INT)
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_INT));
	else if (abb_target == ABB_MIF)
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_MIF));
	else if (abb_target == ABB_G3D)
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_G3D));
	else if (abb_target == ABB_ARM)
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_ARM));
}

static inline void exynos4x12_set_abb(unsigned int abb_mode_value)
{
	unsigned int tmp;

	if (abb_mode_value != ABB_MODE_BYPASS)
		tmp = ABB_INIT;
	else
		tmp = ABB_INIT_BYPASS;

	tmp |= abb_mode_value;

	if (!soc_is_exynos5250()) {
		__raw_writel(tmp, EXYNOS4_ABB_INT);
		__raw_writel(tmp, EXYNOS4_ABB_MIF);
		__raw_writel(tmp, EXYNOS4_ABB_G3D);
		__raw_writel(tmp, EXYNOS4_ABB_ARM);
	} else {
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_INT));
		if (exynos_lot_id)
			__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_MIF));
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_G3D));
		__raw_writel(tmp, EXYNOS4_ABB_MEMBER(ABB_ARM));
	}
}

static inline int exynos4x12_get_abb_member(enum exynos4x12_abb_member abb_target)
{
	return (__raw_readl(EXYNOS4_ABB_MEMBER(abb_target)) & 0x1f);
}

struct asv_judge_table {
	unsigned int hpm_limit; /* HPM value to decide group of target */
	unsigned int ids_limit; /* IDS value to decide group of target */
};

struct samsung_asv {
	unsigned int pkg_id;			/* fused value for pakage */
	unsigned int ids_offset;		/* ids_offset of chip */
	unsigned int ids_mask;			/* ids_mask of chip */
	unsigned int hpm_result;		/* hpm value of chip */
	unsigned int ids_result;		/* ids value of chip */
	int (*check_vdd_arm)(void);		/* check vdd_arm value, this function is selectable */
	int (*pre_clock_init)(void);		/* clock init function to get hpm */
	int (*pre_clock_setup)(void);		/* clock setup function to get hpm */
	/* specific get ids function */
	int (*get_ids)(struct samsung_asv *asv_info);
	/* specific get hpm function */
	int (*get_hpm)(struct samsung_asv *asv_info);
	/* store into some repository to send result of asv */
	int (*store_result)(struct samsung_asv *asv_info);
};

extern int exynos4210_asv_init(struct samsung_asv *asv_info);
extern int exynos4x12_asv_init(struct samsung_asv *asv_info);

#endif /* __ASM_ARCH_ASV_H */
