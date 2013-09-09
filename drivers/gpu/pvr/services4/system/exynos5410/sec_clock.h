/* gpu/drivers/gpu/pvr/services4/system/exynos5410/sec_clock.h
 *
 * Copyright 2011 by S.LSI. Samsung Electronics Inc.
 * San#24, Nongseo-Dong, Giheung-Gu, Yongin, Korea
 *
 * Samsung SoC SGX clock driver
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

#ifndef __SEC_CLOCK_H__
#define __SEC_CLOCK_H__
int gpu_clks_get(void);
void gpu_clks_put(void);
int gpu_clock_set_parent(void);
int gpu_clock_enable(void);
void gpu_clock_disable(void);
void gpu_clock_set(int sgx_clk);
int gpu_clock_get(void);

#endif /*__SEC_CLOCK_H__*/
