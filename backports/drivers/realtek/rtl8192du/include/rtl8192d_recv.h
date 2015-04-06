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
 *
 ******************************************************************************/
#ifndef _RTL8192D_RECV_H_
#define _RTL8192D_RECV_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>


#ifdef CONFIG_SINGLE_RECV_BUF
	#define NR_RECVBUFF (1)
#else
	#define NR_RECVBUFF (4)
#endif /* CONFIG_SINGLE_RECV_BUF */
	#define NR_PREALLOC_RECV_SKB (8)

#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT

#define MAX_RECVBUF_SZ (8192+1024) /*  8K+1k */

#define RECV_BULK_IN_ADDR		0x80
#define RECV_INT_IN_ADDR		0x81

#define PHY_RSSI_SLID_WIN_MAX				100
#define PHY_LINKQUALITY_SLID_WIN_MAX		20

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

struct phy_ofdm_rx_status_report_8192cd {
	unsigned char	trsw_gain_X[4];
	unsigned char	pwdb_all;
	unsigned char	cfosho_X[4];
	unsigned char	cfotail_X[4];
	unsigned char	rxevm_X[2];
	unsigned char	rxsnr_X[4];
	unsigned char	pdsnr_X[2];
	unsigned char	csi_current_X[2];
	unsigned char	csi_target_X[2];
	unsigned char	sigevm;
	unsigned char	max_ex_pwr;
#ifdef __LITTLE_ENDIAN
	unsigned char ex_intf_flg:1;
	unsigned char sgi_en:1;
	unsigned char rxsc:2;
	unsigned char idle_long:1;
	unsigned char r_ant_train_en:1;
	unsigned char ANTSELB:1;
	unsigned char ANTSEL:1;
#else	/*  __BIG_ENDIAN */
	unsigned char ANTSEL:1;
	unsigned char ANTSELB:1;
	unsigned char r_ant_train_en:1;
	unsigned char idle_long:1;
	unsigned char rxsc:2;
	unsigned char sgi_en:1;
	unsigned char ex_intf_flg:1;
#endif
} __packed;

struct phy_cck_rx_status_report_8192cd {
	/* For CCK rate descriptor. This is a signed 8:1 variable. LSB bit presend
		0.5. And MSB 7 bts presend a signed value. Range from -64~+63.5. */
	u8	adc_pwdb_X[4];
	u8	SQ_rpt;
	u8	cck_agc_rpt;
};

/*  Rx smooth factor */
#define	Rx_Smooth_Factor (20)

void rtl8192du_init_recvbuf(struct rtw_adapter *padapter, struct recv_buf *precvbuf);
int	rtl8192du_init_recv_priv(struct rtw_adapter * padapter);
void	rtl8192du_free_recv_priv(struct rtw_adapter * padapter);

void rtl8192d_translate_rx_signal_stuff(struct recv_frame_hdr *precvframe, struct phy_stat *pphy_info);
void rtl8192d_query_rx_desc_status(struct recv_frame_hdr *precvframe, struct recv_stat *pdesc);

#endif
