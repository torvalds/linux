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
#ifndef __RTL8723B_RECV_H__
#define __RTL8723B_RECV_H__

#include <rtl8192c_recv.h>

typedef struct rxreport_8723b
{
	//DWORD 0
	u32 pktlen:14;
	u32 crc32:1;
	u32 icverr:1;
	u32 drvinfosize:4;
	u32 security:3;
	u32 qos:1;
	u32 shift:2;
	u32 physt:1;
	u32 swdec:1;
	u32 ls:1;
	u32 fs:1;
	u32 eor:1;
	u32 own:1;

	//DWORD 1
	u32 macid:7;
	u32 hwrsvd10:1;
	u32 tid:4;
	u32 hwrsvd11:1;
	u32 amsdu:1;
	u32 rxidmatch:1;
	u32 paggr:1;
	u32 a1fit:4;
	u32 chkerr:1;  //20
	u32 ipver:1;
	u32 istcpudp:1;
	u32 chkvld:1;   //23
	u32 pam:1;
	u32 pwr:1;
	u32 md:1;
	u32 mf:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	//DWORD 2
	u32 seq:12;
	u32 frag:4;
	u32 rxisqos:1;
	u32 hwrsvd20:1;
	u32 ivlen:6;
	u32 hwrsvd21:4;
	u32 rptsel:1;
	u32 hwrsvd22:3;

	//DWORD 3
	u32 rxrate:7;
	u32 hwrsvd30:3;
	u32 htc:1;
	u32 esop:1;
	u32 bssidfit:2;
	u32 hwrsvd31:2;
	u32 usbaggpktnum:8;
	u32 hwrsvd32:5;
	u32 eosp:1;
	u32 patternwake:1;
	u32 unicastwake:1;
	u32 magicwake:1;
	
	//DWORD 4
	u32 splcp:1;
	u32 ldpc:1;
	u32 stbc:1;
	u32 hwrsvd40:1;
	u32 bw:2;
	u32 hwrsvd41:26;

	//DWORD 5
	u32 tsfl;

	//DWORD 6
	u32 bufaddr;

	//DWORD 7
	u32 bufaddr64;
} RXREPORT, *PRXREPORT;

typedef struct phystatus_8723b
{
	u32 rxgain_a:7;
	u32 trsw_a:1;
	u32 rxgain_b:7;
	u32 trsw_b:1;
	u32 chcorr_l:16;

	u32 sigqualcck:8;
	u32 cfo_a:8;
	u32 cfo_b:8;
	u32 chcorr_h:8;

	u32 noisepwrdb_h:8;
	u32 cfo_tail_a:8;
	u32 cfo_tail_b:8;
	u32 rsvd0824:8;

	u32 rsvd1200:8;
	u32 rxevm_a:8;
	u32 rxevm_b:8;
	u32 rxsnr_a:8;

	u32 rxsnr_b:8;
	u32 noisepwrdb_l:8;
	u32 rsvd1616:8;
	u32 postsnr_a:8;

	u32 postsnr_b:8;
	u32 csi_a:8;
	u32 csi_b:8;
	u32 targetcsi_a:8;

	u32 targetcsi_b:8;
	u32 sigevm:8;
	u32 maxexpwr:8;
	u32 exintflag:1;
	u32 sgien:1;
	u32 rxsc:2;
	u32 idlelong:1;
	u32 anttrainen:1;
	u32 antselb:1;
	u32 antsel:1;
} PHYSTATUS, *PPHYSTATUS;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI)
s32 rtl8723bs_init_recv_priv(PADAPTER padapter);
void rtl8723bs_free_recv_priv(PADAPTER padapter);
#endif

void rtl8723b_query_rx_phy_status(union recv_frame *prframe, struct phy_stat *pphy_stat);
void rtl8723b_process_phy_info(PADAPTER padapter, void *prframe);
#ifdef CONFIG_USB_HCI
void update_recvframe_attrib(PADAPTER padapter, union recv_frame *precvframe, struct recv_stat *prxstat);
void update_recvframe_phyinfo(union recv_frame *precvframe, struct phy_stat *pphy_info);
#endif
#endif

