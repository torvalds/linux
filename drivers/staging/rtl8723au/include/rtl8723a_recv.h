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
 ******************************************************************************/
#ifndef __RTL8723A_RECV_H__
#define __RTL8723A_RECV_H__

#include <osdep_service.h>
#include <drv_types.h>

#define NR_RECVBUFF			4

#define NR_PREALLOC_RECV_SKB		8

#define RECV_BLK_SZ			512
#define RECV_BLK_CNT			16
#define RECV_BLK_TH			RECV_BLK_CNT

#define MAX_RECVBUF_SZ			15360 /*  15k < 16k */

#define PHY_RSSI_SLID_WIN_MAX		100
#define PHY_LINKQUALITY_SLID_WIN_MAX	20


struct phy_stat {
	unsigned int phydw0;
	unsigned int phydw1;
	unsigned int phydw2;
	unsigned int phydw3;
	unsigned int phydw4;
	unsigned int phydw5;
	unsigned int phydw6;
	unsigned int phydw7;
};

/*  Rx smooth factor */
#define	Rx_Smooth_Factor		20

struct interrupt_msg_format {
	unsigned int C2H_MSG0;
	unsigned int C2H_MSG1;
	unsigned int C2H_MSG2;
	unsigned int C2H_MSG3;
	unsigned int HISR; /*  from HISR Reg0x124, read to clear */
	unsigned int HISRE;/*  from HISRE Reg0x12c, read to clear */
	unsigned int  MSG_EX;
};

int rtl8723au_init_recv_priv(struct rtw_adapter * padapter);
void rtl8723au_free_recv_priv(struct rtw_adapter * padapter);
void rtl8723a_process_phy_info(struct rtw_adapter *padapter, void *prframe);
void update_recvframe_attrib(struct recv_frame *precvframe, struct recv_stat *prxstat);
void update_recvframe_phyinfo(struct recv_frame *precvframe, struct phy_stat *pphy_info);

#endif
