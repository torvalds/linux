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
void rtl8188eu_init_recvbuf(struct adapter *padapter, struct recv_buf *buf);
s32 rtl8188eu_init_recv_priv(struct adapter *padapter);
void rtl8188eu_free_recv_priv(struct adapter *padapter);
void rtl8188eu_recv_hdl(struct adapter *padapter, struct recv_buf *precvbuf);
void rtl8188eu_recv_tasklet(void *priv);
void rtl8188e_query_rx_phy_status(struct recv_frame *fr, struct phy_stat *phy);
void rtl8188e_process_phy_info(struct adapter *padapter, void *prframe);
void update_recvframe_phyinfo_88e(struct recv_frame *fra, struct phy_stat *phy);
void update_recvframe_attrib_88e(struct recv_frame *fra,
				 struct recv_stat *stat);

#endif
