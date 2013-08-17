/* gpu/drivers/gpu/pvr/services4/system/exynos5410/sec_power.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX power driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

#ifndef __SEC_POWER_H__
#define __SEC_POWER_H__

void gpu_voltage_set(int sgx_vol);
int gpu_regulator_enable(void);
int gpu_regulator_disable(void);
void gpu_power_init(void);
int gpu_power_enable(void);
int gpu_power_disable(void);
int gpu_voltage_get(void);

#endif /*__SEC_POWER_H__*/
