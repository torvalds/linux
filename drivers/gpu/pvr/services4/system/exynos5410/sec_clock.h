/* gpu/drivers/gpu/pvr/services4/system/exynos5410/sec_clock.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX clock driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software FoundatIon.
 */

#ifndef __SEC_CLOCK_H__
#define __SEC_CLOCK_H__
int gpu_clks_get(void);
void gpu_clks_put(void);
void gpu_clock_set_parent(void);
void gpu_clock_enable(void);
void gpu_clock_disable(void);
void gpu_clock_set(int sgx_clk);
int gpu_clock_get(void);

#endif /*__SEC_CLOCK_H__*/
