/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/
#ifndef __RTL8723A_XMIT_H__
#define __RTL8723A_XMIT_H__

#include <rtl8192c_xmit.h>

//
//defined for TX DESC Operation
//

#define MAX_TID (15)

//OFFSET 0
#define OFFSET_SZ	0
#define OFFSET_SHT	16
#define BMC		BIT(24)
#define LSG		BIT(26)
#define FSG		BIT(27)
#define OWN 		BIT(31)


//OFFSET 4
#define PKT_OFFSET_SZ	0
#define BK		BIT(6)
#define QSEL_SHT	8
#define Rate_ID_SHT	16
#define NAVUSEHDR	BIT(20)
#define PKT_OFFSET_SHT	26
#define HWPC		BIT(31)

//OFFSET 8
#define AGG_EN		BIT(29)

//OFFSET 12
#define SEQ_SHT		16

//OFFSET 16
#define QoS		BIT(6)
#define HW_SEQ_EN	BIT(7)
#define USERATE		BIT(8)
#define DISDATAFB	BIT(10)
#define DATA_SHORT	BIT(24)
#define DATA_BW		BIT(25)

//OFFSET 20
#define SGI		BIT(6)

void rtl8723a_update_txdesc(struct xmit_frame *pxmitframe, u8 *pmem);
void rtl8723a_fill_fake_txdesc(PADAPTER padapter, u8 *pDesc, u32 BufferLen, u8 IsPsPoll, u8 IsBTQosNull);

#ifdef CONFIG_SDIO_HCI
s32 rtl8723as_init_xmit_priv(PADAPTER padapter);
void rtl8723as_free_xmit_priv(PADAPTER padapter);
s32 rtl8723as_hal_xmit(PADAPTER padapter, struct xmit_frame *pxmitframe);
void rtl8723as_mgnt_xmit(PADAPTER padapter, struct xmit_frame *pmgntframe);
s32 rtl8723as_xmit_buf_handler(PADAPTER padapter);
#define hal_xmit_handler rtl8723as_xmit_buf_handler
#endif

#endif

