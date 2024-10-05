/* SPDX-License-Identifier: GPL-2.0 */
/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/
#ifndef __RTL8723B_RECV_H__
#define __RTL8723B_RECV_H__

#include <rtl8192c_recv.h>

struct rxreport_8723b {
	/* DWORD 0 */
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

	/* DWORD 1 */
	u32 macid:7;
	u32 rsvd0407:1;
	u32 tid:4;
	u32 macid_vld:1;
	u32 amsdu:1;
	u32 rxid_match:1;
	u32 paggr:1;
	u32 a1fit:4;
	u32 chkerr:1;  /* 20 */
	u32 rx_ipv:1;
	u32 rx_is_tcp_udp:1;
	u32 chk_vld:1;   /* 23 */
	u32 pam:1;
	u32 pwr:1;
	u32 md:1;
	u32 mf:1;
	u32 type:2;
	u32 mc:1;
	u32 bc:1;

	/* DWORD 2 */
	u32 seq:12;
	u32 frag:4;
	u32 rx_is_qos:1;
	u32 rsvd0817:1;
	u32 wlanhd_iv_len:6;
	u32 hwrsvd0824:4;
	u32 c2h_ind:1;
	u32 rsvd0829:2;
	u32 fcs_ok:1;

	/* DWORD 3 */
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

	/* DWORD 4 */
	u32 splcp:1;	/* Ofdm sgi or cck_splcp */
	u32 ldpc:1;
	u32 stbc:1;
	u32 not_sounding:1;
	u32 bw:2;
	u32 rsvd1606:26;

	/* DWORD 5 */
	u32 tsfl;
};

s32 rtl8723bs_init_recv_priv(struct adapter *padapter);
void rtl8723bs_free_recv_priv(struct adapter *padapter);

void rtl8723b_query_rx_phy_status(union recv_frame *prframe, struct phy_stat *pphy_stat);
void rtl8723b_process_phy_info(struct adapter *padapter, void *prframe);

void rtl8723b_read_chip_version(struct adapter *padapter);
void rtl8723bs_init_default_value(struct adapter *padapter);
void rtl8723bs_interface_configure(struct adapter *padapter);
void ReadAdapterInfo8723BS(struct adapter *padapter);

#endif
