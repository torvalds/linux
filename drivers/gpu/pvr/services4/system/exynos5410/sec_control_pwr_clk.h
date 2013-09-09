/* /drivers/gpu/pvr/services4/system/exynos5410/sec_contorl_pwr_clk.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX power clock control driver
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
#ifndef __SEC_CONTROL_PWR_CLK_H__
#define __SEC_CONTROL_PWR_CLK_H__

typedef enum {
	GPU_PWR_CLK_STATE_OFF=0,
	GPU_PWR_CLK_STATE_ON,
} sec_gpu_state;

int  sec_gpu_pwr_clk_init(void);
int  sec_gpu_pwr_clk_deinit(void);
void sec_gpu_vol_clk_change(int sgx_clock, int sgx_voltage);
int  sec_gpu_pwr_clk_state_set(sec_gpu_state state);
int  sec_gpu_pwr_clk_margin_set(unsigned int margin_offset);

#endif /*__SEC_CONTROL_PWR_CLK_H__*/

