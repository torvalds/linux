/*
 * TC956X ethernet driver.
 *
 * tc956x_pf_rsc_mng.h
 *
 * Copyright (C) 2021 Toshiba Electronic Devices & Storage Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

/*! History:
 *  10 July 2020 : Initial Version
 *  VERSION       : 00-01
 *
 *  30 Nov 2021  : Base lined for SRIOV
 *  VERSION      : 01-02
 */
#ifndef __TC956X_PF_RSC_MNG_H__
#define __TC956X_PF_RSC_MNG_H__

#include "common.h"
#include <linux/netdevice.h>

#define MAX_FUNCTIONS_PER_PF 4 /*Max functions per pf including pf, vfs*/
#define RSC_MNG_OFFSET 0x2000
#define RSCMNG_ID_REG ((RSC_MNG_OFFSET) + 0x00000000)
#define RSCMNG_RSC_CTRL_REG ((RSC_MNG_OFFSET) + 0x00000004)
#define RSCMNG_RSC_ST_REG ((RSC_MNG_OFFSET) + 0x00000008)
#define RSCMNG_INT_CTRL_REG ((RSC_MNG_OFFSET) + 0x0000000c)
#define RSCMNG_INT_ST_REG ((RSC_MNG_OFFSET) + 0x00000010)

#define RSC_MNG_FN_TYPE BIT(16)
#define RSC_MNG_VF_FN_NUM GENMASK(11, 8)
#define RSC_MNG_PF_FN_NUM GENMASK(3, 0)

#define RSC_MNG_DMA_CH7_MASK 0x80
#define RSC_MNG_DMA_CH6_MASK 0x40
#define RSC_MNG_DMA_CH5_MASK 0x20
#define RSC_MNG_DMA_CH4_MASK 0x10
#define RSC_MNG_DMA_CH3_MASK 0x08
#define RSC_MNG_DMA_CH2_MASK 0x04
#define RSC_MNG_DMA_CH1_MASK 0x02
#define RSC_MNG_DMA_CH0_MASK 0x01

#define RSC_MNG_DMA_CH7_BIT_POS 28
#define RSC_MNG_DMA_CH6_BIT_POS 24
#define RSC_MNG_DMA_CH5_BIT_POS 20
#define RSC_MNG_DMA_CH4_BIT_POS 16
#define RSC_MNG_DMA_CH3_BIT_POS 12
#define RSC_MNG_DMA_CH2_BIT_POS 8
#define RSC_MNG_DMA_CH1_BIT_POS 4
#define RSC_MNG_DMA_CH0_BIT_POS 0

#define RSC_MNG_RSC_STATUS_MASK 0xff

#define RSC_MNG_INT_STATUS_MASK 0x1f
#define RSC_MNG_INT_MCU_MASK 0x10
#define RSC_MNG_INT_VF2_MASK 0x08
#define RSC_MNG_INT_VF1_MASK 0x04
#define RSC_MNG_INT_VF0_MASK 0x02
#define RSC_MNG_INT_OTHR_PF_MASK 0x01

#define MBX_MCU_INTERRUPT BIT(4)
#define MBX_VF3_INTERRUPT BIT(3)
#define MBX_VF2_INTERRUPT BIT(2)
#define MBX_VF1_INTERRUPT BIT(1)
#define MBX_PF_INTERRUPT BIT(0)

#define RSC_MNG_ACK_MASK 0x01
#define RSC_MNG_NACK_MASK 0x02

#define MBX_TIMEOUT 1000

#endif /* __TC956X_PF_RSC_MNG_H__ */
