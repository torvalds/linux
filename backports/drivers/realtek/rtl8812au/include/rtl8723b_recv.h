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

#ifdef CONFIG_SDIO_HCI
#ifndef CONFIG_SDIO_RX_COPY
#undef MAX_RECVBUF_SZ
#define MAX_RECVBUF_SZ	(RX_DMA_SIZE_8723B - RX_DMA_RESERVED_SIZE_8723B)
#endif // !CONFIG_SDIO_RX_COPY
#endif // CONFIG_SDIO_HCI

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
	u32 rsvd0028:2;
	u32 eor:1;
	u32 rsvd0031:1;

	//DWORD 1
	u32 macid:7;
	u32 rsvd0407:1;
	u32 tid:4;
	u32 macid_vld:1;
	u32 amsdu:1;
	u32 rxid_match:1;
	u32 paggr:1;
	u32 a1fit:4;
	u32 chkerr:1;  //20
	u32 rx_ipv:1;
	u32 rx_is_tcp_udp:1;
	u32 chk_vld:1;   //23
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
	u32 rx_is_qos:1;
	u32 rsvd0817:1;
	u32 wlanhd_iv_len:6;
	u32 hwrsvd0824:4;
	u32 c2h_ind:1;
	u32 rsvd0829:2;
	u32 fcs_ok:1;

	//DWORD 3
	u32 rx_rate:7;
	u32 rsvd1207:3;
	u32 htc:1;
	u32 esop:1;
	u32 bssid_fit:2;
	u32 rsvd1214:2;
	u32 dma_agg_num:8;
	u32 rsvd1224:5;
	u32 patternmatch:1;
	u32 unicastwake:1;
	u32 magicwake:1;
	
	//DWORD 4
	u32 splcp:1;	//Ofdm sgi or cck_splcp
	u32 ldpc:1;
	u32 stbc:1;
	u32 not_sounding:1;
	u32 bw:2;
	u32 rsvd1606:26;

	//DWORD 5
	u32 tsfl;
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
int	rtl8723bu_init_recv_priv(_adapter *padapter);
void rtl8723bu_free_recv_priv (_adapter *padapter);
void rtl8723bu_init_recvbuf(_adapter *padapter, struct recv_buf *precvbuf);
#endif
#endif

