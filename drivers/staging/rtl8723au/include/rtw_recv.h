/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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
#ifndef _RTW_RECV_H_
#define _RTW_RECV_H_

#include <osdep_service.h>
#include <drv_types.h>
#include <Hal8723APhyCfg.h>

#define NR_RECVFRAME		256

#define MAX_RXFRAME_CNT		512
#define MAX_RX_NUMBLKS		(32)
#define RECVFRAME_HDR_ALIGN	128

#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define MAX_SUBFRAME_COUNT	64

/* for Rx reordering buffer control */
struct recv_reorder_ctrl {
	struct rtw_adapter	*padapter;
	u8 enable;
	u16 indicate_seq;/* wstart_b, init_value=0xffff */
	u16 wend_b;
	u8 wsize_b;
	struct rtw_queue pending_recvframe_queue;
	struct timer_list reordering_ctrl_timer;
};

struct	stainfo_rxcache	{
	u16	tid_rxseq[16];
/*
	unsigned short	tid0_rxseq;
	unsigned short	tid1_rxseq;
	unsigned short	tid2_rxseq;
	unsigned short	tid3_rxseq;
	unsigned short	tid4_rxseq;
	unsigned short	tid5_rxseq;
	unsigned short	tid6_rxseq;
	unsigned short	tid7_rxseq;
	unsigned short	tid8_rxseq;
	unsigned short	tid9_rxseq;
	unsigned short	tid10_rxseq;
	unsigned short	tid11_rxseq;
	unsigned short	tid12_rxseq;
	unsigned short	tid13_rxseq;
	unsigned short	tid14_rxseq;
	unsigned short	tid15_rxseq;
*/
};

struct smooth_rssi_data {
	u32	elements[100];	/* array to store values */
	u32	index;			/* index to current array to store */
	u32	total_num;		/* num of valid elements */
	u32	total_val;		/* sum of valid elements */
};

struct signal_stat {
	u8	update_req;		/* used to indicate */
	u8	avg_val;		/* avg of valid elements */
	u32	total_num;		/* num of valid elements */
	u32	total_val;		/* sum of valid elements */
};

struct phy_info {
	u8		RxPWDBAll;
	u8		SignalQuality;	 /*  in 0-100 index. */
	u8		RxMIMOSignalQuality[RF_PATH_MAX]; /* EVM */
	u8		RxMIMOSignalStrength[RF_PATH_MAX];/* 0~100 */
	s8		RxPower; /*  in dBm Translate from PWdB */
	/* Real power in dBm for this packet, no beautification and aggregation.
	 * Keep this raw info to be used for the other procedures.
	 */
	s8		RecvSignalPower;
	u8		BTRxRSSIPercentage;
	u8		SignalStrength; /*  in 0-100 index. */
	u8		RxPwr[RF_PATH_MAX];/* per-path's pwdb */
	u8		RxSNR[RF_PATH_MAX];/* per-path's SNR */
};


struct rx_pkt_attrib	{
	u16	pkt_len;
	u8	physt;
	u8	drvinfo_sz;
	u8	shift_sz;
	u8	hdrlen; /* the WLAN Header Len */
	u8	amsdu;
	u8	qos;
	u8	priority;
	u8	pw_save;
	u8	mdata;
	u16	seq_num;
	u8	frag_num;
	u8	mfrag;
	u8	order;
	u8	privacy; /* in frame_ctrl field */
	u8	bdecrypted;
	/* when 0 indicate no encrypt. when non-zero, indicate the algorith */
	u32	encrypt;
	u8	iv_len;
	u8	icv_len;
	u8	crc_err;
	u8	icv_err;

	u16 eth_type;

	u8	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8	ra[ETH_ALEN];
	u8	bssid[ETH_ALEN];

	u8 ack_policy;

	u8	tcpchk_valid; /*  0: invalid, 1: valid */
	u8	ip_chkrpt; /* 0: incorrect, 1: correct */
	u8	tcp_chkrpt; /* 0: incorrect, 1: correct */
	u8	key_index;

	u8	mcs_rate;
	u8	rxht;
	u8	sgi;
	u8	pkt_rpt_type;
	u32	MacIDValidEntry[2];	/*  64 bits present 64 entry. */
	struct phy_info phy_info;
};

/* These definition is used for Rx packet reordering. */
#define SN_LESS(a, b)		(((a-b) & 0x800) != 0)
#define SN_EQUAL(a, b)		(a == b)
#define REORDER_WAIT_TIME	(50) /*  (ms) */

#define RECVBUFF_ALIGN_SZ 8

#define RXDESC_SIZE	24
#define RXDESC_OFFSET RXDESC_SIZE

struct recv_stat {
	__le32 rxdw0;
	__le32 rxdw1;
	__le32 rxdw2;
	__le32 rxdw3;
	__le32 rxdw4;
	__le32 rxdw5;
};

/* accesser of recv_priv: rtw_recv_entry23a(dispatch / passive level);	\
 * recv_thread(passive) ; returnpkt(dispatch) ; halt(passive) ;
 *
 * using enter_critical section to protect
 */
struct recv_priv {
	spinlock_t	lock;

	struct rtw_queue	free_recv_queue;
	struct rtw_queue	recv_pending_queue;
	struct rtw_queue	uc_swdec_pending_queue;

	int free_recvframe_cnt;

	struct rtw_adapter	*adapter;

	u32	bIsAnyNonBEPkts;
	u64	rx_bytes;
	u64	rx_pkts;
	u64	rx_drop;
	u64	last_rx_bytes;

	uint  rx_icv_err;
	uint  rx_largepacket_crcerr;
	uint  rx_smallpacket_crcerr;
	uint  rx_middlepacket_crcerr;

	/* u8 *pallocated_urb_buf; */
	u8	rx_pending_cnt;

	struct urb *int_in_urb;

	u8	*int_in_buf;

	struct tasklet_struct irq_prepare_beacon_tasklet;
	struct tasklet_struct recv_tasklet;
	struct sk_buff_head free_recv_skb_queue;
	struct sk_buff_head rx_skb_queue;
	u8 *precv_buf;

	/* For display the phy informatiom */
	u8 is_signal_dbg;	/*  for debug */
	u8 signal_strength_dbg;	/*  for debug */
	s8 rssi;
	s8 rxpwdb;
	u8 signal_strength;
	u8 signal_qual;
	u8 noise;
	int RxSNRdB[2];
	s8 RxRssi[2];
	int FalseAlmCnt_all;

	struct timer_list signal_stat_timer;
	u32 signal_stat_sampling_interval;
	/* u32 signal_stat_converging_constant; */
	struct signal_stat signal_qual_data;
	struct signal_stat signal_strength_data;
};

#define rtw_set_signal_stat_timer(recvpriv)			\
	 mod_timer(&(recvpriv)->signal_stat_timer, jiffies +	\
		   msecs_to_jiffies((recvpriv)->signal_stat_sampling_interval))

struct sta_recv_priv {
	spinlock_t	lock;
	int	option;

	/* struct rtw_queue	blk_strms[MAX_RX_NUMBLKS]; */
	struct rtw_queue defrag_q;	 /* keeping the fragment frame until defrag */

	struct	stainfo_rxcache rxcache;

	/* uint	sta_rx_bytes; */
	/* uint	sta_rx_pkts; */
	/* uint	sta_rx_fail; */

};


struct recv_buf {
	struct list_head list;

	struct rtw_adapter *adapter;

	struct urb *purb;
	struct sk_buff *pskb;
};

/*	head  ----->
 *
 *		data  ----->
 *
 *			payload
 *
 *		tail  ----->
 *
 *	end   ----->
 *
 *	len = (unsigned int )(tail - data);
 *
 */
struct recv_frame {
	struct list_head	list;
	struct sk_buff *pkt;

	struct rtw_adapter  *adapter;

	struct rx_pkt_attrib attrib;

	struct sta_info *psta;

	/* for A-MPDU Rx reordering buffer control */
	struct recv_reorder_ctrl *preorder_ctrl;
};

/* get a free recv_frame from pfree_recv_queue */
struct recv_frame *rtw_alloc_recvframe23a(struct rtw_queue *pfree_recv_queue);
int rtw_free_recvframe23a(struct recv_frame *precvframe);

int rtw_enqueue_recvframe23a(struct recv_frame *precvframe, struct rtw_queue *queue);

u32 rtw_free_uc_swdec_pending_queue23a(struct rtw_adapter *adapter);

int rtw_enqueue_recvbuf23a_to_head(struct recv_buf *precvbuf, struct rtw_queue *queue);
int rtw_enqueue_recvbuf23a(struct recv_buf *precvbuf, struct rtw_queue *queue);
struct recv_buf *rtw_dequeue_recvbuf23a(struct rtw_queue *queue);

void rtw_reordering_ctrl_timeout_handler23a(unsigned long pcontext);

static inline s32 translate_percentage_to_dbm(u32 SignalStrengthIndex)
{
	s32	SignalPower; /*  in dBm. */

	/*  Translate to dBm (x=0.5y-95). */
	SignalPower = (s32)((SignalStrengthIndex + 1) >> 1);
	SignalPower -= 95;

	return SignalPower;
}


struct sta_info;

void _rtw_init_sta_recv_priv23a(struct sta_recv_priv *psta_recvpriv);

void mgt_dispatcher23a(struct rtw_adapter *padapter,
		    struct recv_frame *precv_frame);

#endif
