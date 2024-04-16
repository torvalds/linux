/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 */

#ifndef __T7XX_MHCCIF_H__
#define __T7XX_MHCCIF_H__

#include <linux/types.h>

#include "t7xx_pci.h"
#include "t7xx_reg.h"

#define D2H_SW_INT_MASK (D2H_INT_EXCEPTION_INIT |		\
			 D2H_INT_EXCEPTION_INIT_DONE |		\
			 D2H_INT_EXCEPTION_CLEARQ_DONE |	\
			 D2H_INT_EXCEPTION_ALLQ_RESET |		\
			 D2H_INT_PORT_ENUM |			\
			 D2H_INT_ASYNC_MD_HK)

void t7xx_mhccif_mask_set(struct t7xx_pci_dev *t7xx_dev, u32 val);
void t7xx_mhccif_mask_clr(struct t7xx_pci_dev *t7xx_dev, u32 val);
u32 t7xx_mhccif_mask_get(struct t7xx_pci_dev *t7xx_dev);
void t7xx_mhccif_init(struct t7xx_pci_dev *t7xx_dev);
u32 t7xx_mhccif_read_sw_int_sts(struct t7xx_pci_dev *t7xx_dev);
void t7xx_mhccif_h2d_swint_trigger(struct t7xx_pci_dev *t7xx_dev, u32 channel);

#endif /*__T7XX_MHCCIF_H__ */
