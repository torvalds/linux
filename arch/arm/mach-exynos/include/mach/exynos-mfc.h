/* linux/arch/arm/plat-s5p/include/plat/s5p-mfc.h
 *
 * Copyright 2011 Samsung Electronics Co., Ltd.
 *      http://www.samsung.com/
 *
 * Header file for s5p mfc support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _S5P_MFC_H
#define _S5P_MFC_H

#include <linux/platform_device.h>

#if defined(CONFIG_ARM_EXYNOS5410_BUS_DEVFREQ)
#define CONFIG_MFC_USE_BUS_DEVFREQ
#endif

#ifdef CONFIG_MFC_USE_BUS_DEVFREQ
/*
 * thrd_mb - threshold of total MB(macroblock) count
 * Total MB count can be calculated by
 *	(MB of width) * (MB of height) * fps
 */
struct s5p_mfc_qos {
	unsigned int thrd_mb;
	unsigned int freq_int;
	unsigned int freq_mif;
	unsigned int freq_cpu;
#ifndef CONFIG_ARM_EXYNOS_IKS_CPUFREQ
	unsigned int freq_kfc;
#endif
};
#endif

enum mfc_ip_version {
	IP_VER_MFC_4P_0,
	IP_VER_MFC_4P_1,
	IP_VER_MFC_4P_2,
	IP_VER_MFC_5G_0,
	IP_VER_MFC_5G_1,
	IP_VER_MFC_5A_0,
	IP_VER_MFC_5A_1,
};

struct s5p_mfc_platdata {
	int ip_ver;
	int clock_rate;
#ifdef CONFIG_MFC_USE_BUS_DEVFREQ
	int num_qos_steps;
	struct s5p_mfc_qos *qos_table;
#endif
};

void s5p_mfc_set_platdata(struct s5p_mfc_platdata *pd);
void s5p_mfc_setname(struct platform_device *pdev,char *name);

#endif /* _S5P_MFC_H */
