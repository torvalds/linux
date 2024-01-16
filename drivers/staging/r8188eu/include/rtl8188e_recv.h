/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/* Copyright(c) 2007 - 2011 Realtek Corporation. */

#ifndef __RTL8188E_RECV_H__
#define __RTL8188E_RECV_H__

#define TX_RPT1_PKT_LEN 8

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

void rtl8188eu_recv_tasklet(unsigned long priv);
void update_recvframe_phyinfo_88e(struct recv_frame *fra, struct phy_stat *phy);
void update_recvframe_attrib_88e(struct recv_frame *fra, struct recv_stat *stat);

#endif
