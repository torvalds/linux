/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2016 - 2019 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 ******************************************************************************/

#ifndef _HALMAC_88XX_CFG_H_
#define _HALMAC_88XX_CFG_H_

#include "../halmac_api.h"

#if HALMAC_88XX_SUPPORT

#define TX_PAGE_SIZE_88XX		128
#define TX_PAGE_SIZE_SHIFT_88XX		7 /* 128 = 2^7 */
#define TX_ALIGN_SIZE_88XX		8
#define SDIO_TX_MAX_SIZE_88XX		31744
#define RX_BUF_FW_88XX			12288

#define TX_DESC_SIZE_88XX		48
#define RX_DESC_SIZE_88XX		24

#define H2C_PKT_SIZE_88XX		32 /* Only support 32 byte packet now */
#define H2C_PKT_HDR_SIZE_88XX		8
#define C2H_DATA_OFFSET_88XX		10
#define C2H_PKT_BUF_88XX		256

/* HW memory address */
#define OCPBASE_TXBUF_88XX		0x18780000
#define OCPBASE_DMEM_88XX		0x00200000
#define OCPBASE_EMEM_88XX		0x00100000

#endif /* HALMAC_88XX_SUPPORT */

#endif
