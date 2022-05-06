/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_REG_H__
#define __T7XX_REG_H__

enum t7xx_int {
	DPMAIF_INT,
	CLDMA0_INT,
	CLDMA1_INT,
	CLDMA2_INT,
	MHCCIF_INT,
	DPMAIF2_INT,
	SAP_RGU_INT,
	CLDMA3_INT,
};

#endif /* __T7XX_REG_H__ */
