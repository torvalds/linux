/* /drivers/gpu/pvr/services4/system/exynos5410/secutils.h
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

#ifndef __SYSUTILS_H__
#define __SYSUTILS_H__

u64 get_time_ns(void);
void sgx_hw_start(void);
void sgx_hw_end(void);
void utilization_init(void);

void sec_gpu_utilization_init(void);
void sec_gpu_utilization_pause(void);
void sec_gpu_utilization_resume(void);

#endif /*__SYSUTILS_H__*/
