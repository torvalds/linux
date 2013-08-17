/* /drivers/gpu/pvr/services4/system/exynos5410/secutils.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX DVFS driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

#ifndef __SYSUTILS_H__
#define __SYSUTILS_H__

u64 _time_get_ns(void);
unsigned int inline _clz(unsigned int input);
void sgx_hw_start(void);
void sgx_hw_end(void);
void utilization_init(void);

void sec_gpu_utilization_init(void);
void sec_gpu_utilization_pause(void);
void sec_gpu_utilization_resume(void);

#endif /*__SYSUTILS_H__*/
