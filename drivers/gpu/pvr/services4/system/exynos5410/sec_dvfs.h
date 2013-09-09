/* /drivers/gpu/pvr/services4/system/exynos5410/sec_dvfs.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX DVFS driver
 *
 * This software is proprietary of Samsung Electronics.
 * No part of this software, either material or conceptual may be copied or distributed, transmitted,
 * transcribed, stored in a retrieval system or translated into any human or computer language in any form by any means,
 * electronic, mechanical, manual or otherwise, or disclosed
 * to third parties without the express written permission of Samsung Electronics.
 *
 * Alternatively, this program is free software in case of Linux Kernel;
 * you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SEC_DVFS_H__
#define __SEC_DVFS_H__
typedef struct _GPU_DVFS_DATA_TAG_ {
	int level;
	int clock;
	int voltage;
	int clock_source;
	int min_threadhold;
	int max_threadhold;
	int quick_down_threadhold;
	int quick_up_threadhold;
	int stay_total_count;
	int mask;
	int etc;
} GPU_DVFS_DATA, *pGPU_DVFS_DATA;

void sec_gpu_dvfs_init(void);
int sec_clock_change_up(int level, int step);
int sec_clock_change_down(int value, int step);
int sec_gpu_dvfs_level_from_clk_get(int clock);
void sec_gpu_dvfs_down_requirement_reset(void);
int sec_custom_threshold_set(void);
void sec_gpu_dvfs_handler(int utilization_value);
#endif /*__SEC_DVFS_H__*/
