/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTL8188E_RECV_H__
#define __RTL8188E_RECV_H__

#define TX_RPT1_PKT_LEN 8

#define RECV_BLK_SZ 512
#define RECV_BLK_CNT 16
#define RECV_BLK_TH RECV_BLK_CNT
#define RECV_BULK_IN_ADDR		0x80
#define RECV_INT_IN_ADDR		0x81

#define NR_PREALLOC_RECV_SKB (8)

#define NR_RECVBUFF (4)

#define MAX_RECVBUF_SZ (15360) /*  15k < 16k */

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
#define	Rx_Smooth_Factor (20)

enum rx_packet_type {
	NORMAL_RX,/* Normal rx packet */
	TX_REPORT1,/* CCX */
	TX_REPORT2,/* TX RPT */
	HIS_REPORT,/*  USB HISR RPT */
};

#define INTERRUPT_MSG_FORMAT_LEN 60
void rtl8188eu_init_recvbuf(struct recv_buf *buf);
s32 rtl8188eu_init_recv_priv(struct adapter *padapter);
void rtl8188eu_free_recv_priv(struct adapter * padapter);
void rtl8188eu_recv_hdl(struct adapter * padapter, struct recv_buf *precvbuf);
void rtl8188eu_recv_tasklet(unsigned long priv);
void rtl8188e_query_rx_phy_status(struct recv_frame *fr, struct phy_stat *phy);
void rtl8188e_process_phy_info(struct adapter * padapter, void *prframe);
void update_recvframe_phyinfo_88e(struct recv_frame *fra, struct phy_stat *phy);
void update_recvframe_attrib_88e(struct recv_frame *fra, struct recv_stat *stat);

#endif
