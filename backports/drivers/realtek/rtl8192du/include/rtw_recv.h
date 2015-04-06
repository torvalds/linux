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
#ifndef _RTW_RECV_H_
#define _RTW_RECV_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>

#define NR_RECVFRAME			256

#define RXFRAME_ALIGN			8
#define RXFRAME_ALIGN_SZ	(1<<RXFRAME_ALIGN)

#define MAX_RXFRAME_CNT			512
#define MAX_RX_NUMBLKS			32
#define RECVFRAME_HDR_ALIGN		128

static u8 SNAP_ETH_TYPE_IPX[2] = {0x81, 0x37};

static u8 SNAP_ETH_TYPE_APPLETALK_AARP[2] = {0x80, 0xf3};
static u8 SNAP_ETH_TYPE_TDLS[2] = {0x89, 0x0d};
static u8 SNAP_HDR_APPLETALK_DDP[3] = {
	0x08, 0x00, 0x07
}; /*  Datagram Delivery Protocol */

static u8 oui_8021h[] = {0x00, 0x00, 0xf8};
static u8 oui_rfc1042[] = {0x00, 0x00, 0x00};

#define MAX_SUBFRAME_COUNT		64
static u8 rtw_rfc1042_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00
};
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static u8 rtw_bridge_tunnel_header[] = {
	0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8
};

/* for Rx reordering buffer control */
struct recv_reorder_ctrl {
	struct rtw_adapter	*padapter;
	u8 enable;
	u16 indicate_seq;/* wstart_b, init_value=0xffff */
	u16 wend_b;
	u8 wsize_b;
	struct __queue pending_recvframe_queue;
	struct timer_list reordering_ctrl_timer;
};

struct	stainfo_rxcache	{
	u16	tid_rxseq[16];
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

struct rx_pkt_attrib {
	u16	pkt_len;
	u8	physt;
	u8	drvinfo_sz;
	u8	shift_sz;
	u8	hdrlen; /* the WLAN Header Len */
	u8	to_fr_ds;
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
	u8	encrypt; /* when non-zero, indicate the encrypt algorith */
	u8	iv_len;
	u8	icv_len;
	u8	crc_err;
	u8	icv_err;
	u16	eth_type;
	u8	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8	ra[ETH_ALEN];
	u8	bssid[ETH_ALEN];
	u8	ack_policy;
	u8	tcpchk_valid; /*  0: invalid, 1: valid */
	u8	ip_chkrpt; /* 0: incorrect, 1: correct */
	u8	tcp_chkrpt; /* 0: incorrect, 1: correct */
	u8	key_index;
	u8	mcs_rate;
	u8	rxht;
	u8	sgi;
	u8	signal_qual;
	s8	rx_mimo_signal_qual[2];
	u8	signal_strength;
	u8	rx_rssi[2];  /* This value is percentage */
	u8	rx_snr[2];
	u32	rxpwdb_all;
	s32	recvsignalpower;
};

/* These definition is used for Rx packet reordering. */
#define SN_LESS(a, b)		(((a - b) & 0x800) != 0)
#define SN_EQUAL(a, b)		(a == b)
#define REORDER_WAIT_TIME	(50) /*  (ms) */

#define RECVBUFF_ALIGN_SZ	8

#define RXDESC_SIZE		24
#define RXDESC_OFFSET		RXDESC_SIZE

struct recv_stat {
	__le32 rxdw0;
	__le32 rxdw1;
	__le32 rxdw2;
	__le32 rxdw3;
	__le32 rxdw4;
	__le32 rxdw5;
};

#define EOR			BIT(30)

/* accesser of recv_priv: rtw_recv_entry(dispatch / passive level);
 * recv_thread(passive); returnpkt(dispatch); halt(passive);
 *
 * use enter_critical section to protect
 */
struct recv_priv {
	struct __queue free_recv_queue;
	struct __queue recv_pending_queue;
	struct __queue uc_swdec_pending_queue;
	struct rtw_adapter	*adapter;
	struct  semaphore allrxreturnevt;
	struct tasklet_struct irq_prepare_beacon_tasklet;
	struct tasklet_struct recv_tasklet;
	struct sk_buff_head free_recv_skb_queue;
	struct sk_buff_head rx_skb_queue;
	struct __queue free_recv_buf_queue;
	struct timer_list signal_stat_timer;
	struct signal_stat signal_qual_data;
	struct signal_stat signal_strength_data;

	spinlock_t lock;

	u8 *pallocated_frame_buf;
	u8 *precv_frame_buf;

	uint free_recvframe_cnt;

	u32	bIsAnyNonBEPkts;
	u64	rx_bytes;
	u64	rx_pkts;
	u64	rx_drop;
	u64	last_rx_bytes;

	uint  rx_icv_err;
	uint  rx_largepacket_crcerr;
	uint  rx_smallpacket_crcerr;
	uint  rx_middlepacket_crcerr;

	uint	ff_hwaddr;
	u8	rx_pending_cnt;

	u8 *pallocated_recv_buf;
	u8 *precv_buf;    /*  4 alignment */
	u32	free_recv_buf_queue_cnt;

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
	int falsealmcnt_all;

	u32 signal_stat_sampling_interval;

	u32 recvbuf_skb_alloc_fail_cnt;
	u32 recvbuf_null_cnt;
	u32 read_port_complete_EINPROGRESS_cnt;
	u32 read_port_complete_other_urb_err_cnt;
};

#define rtw_set_signal_stat_timer(recvpriv)				\
	_set_timer(&(recvpriv)->signal_stat_timer,			\
		   (recvpriv)->signal_stat_sampling_interval)

struct sta_recv_priv {
	struct __queue defrag_q; /* keeping the fragment frame until defrag */
	struct	stainfo_rxcache rxcache;
	spinlock_t lock;
	int	option;
};

struct recv_buf {
	struct list_head list;
	struct rtw_adapter *adapter;
	struct urb *purb;
	struct sk_buff *pskb;
	dma_addr_t dma_transfer_addr;	/* (in) dma addr for transfer_buffer */

	spinlock_t recvbuf_lock;

	u32	ref_cnt;
	u32	len;
	u8	*pbuf;
	u8	*pallocated_buf;

	u8	*phead;
	u8	*pdata;
	u8	*ptail;
	u8	*pend;
	u32	alloc_sz;
	u8	irp_pending;
	int	transfer_len;
	u8	reuse;
};

/*	head  ----->
 *		data  ----->
 *			payload
 *		tail  ----->
 *	end   ----->
 *
 *	len = (unsigned int)(tail - data);
 */
struct recv_frame_hdr {
	struct list_head list;
	struct sk_buff	 *pkt;
	struct sk_buff	 *pkt_newalloc;
	struct rtw_adapter  *adapter;
	struct rx_pkt_attrib attrib;
	struct sta_info *psta;
	/* for A-MPDU Rx reordering buffer control */
	struct recv_reorder_ctrl *preorder_ctrl;
	void *precvbuf;

	u8 fragcnt;

	int frame_tag;

	uint  len;
	u8 *rx_head;
	u8 *rx_data;
	u8 *rx_tail;
	u8 *rx_end;
};

/* get a free recv_frame from pfree_recv_queue */
struct recv_frame_hdr *_rtw_alloc_recvframe(struct __queue *pfree_recv_queue);
/* get a free recv_frame from pfree_recv_queue */
struct recv_frame_hdr *rtw_alloc_recvframe(struct __queue *pfree_recv_queue);
void rtw_init_recvframe(struct recv_frame_hdr *precvframe,
			struct recv_priv *precvpriv);
int rtw_free_recvframe(struct recv_frame_hdr *precvframe,
		       struct __queue *pfree_recv_queue);

#define rtw_dequeue_recvframe(queue) rtw_alloc_recvframe(queue)
int _rtw_enqueue_recvframe(struct recv_frame_hdr *precvframe,
			   struct __queue *queue);
int rtw_enqueue_recvframe(struct recv_frame_hdr *precvframe,
			  struct __queue *queue);

void rtw_free_recvframe_queue(struct __queue *pframequeue,
			      struct __queue *pfree_recv_queue);
u32 rtw_free_uc_swdec_pending_queue(struct rtw_adapter *adapter);

int rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf,
				struct __queue *queue);
int rtw_enqueue_recvbuf(struct recv_buf *precvbuf, struct __queue *queue);
struct recv_buf *rtw_dequeue_recvbuf(struct __queue *queue);

void rtw_reordering_ctrl_timeout_handler(void *pcontext);

static inline u8 *get_rxmem(struct recv_frame_hdr *precvframe)
{
	/* always return rx_head... */
	if (precvframe == NULL)
		return NULL;

	return precvframe->rx_head;
}

static inline u8 *get_rx_status(struct recv_frame_hdr *precvframe)
{
	return get_rxmem(precvframe);
}

static inline u8 *get_recvframe_data(struct recv_frame_hdr *precvframe)
{
	/* alwasy return rx_data */
	if (precvframe == NULL)
		return NULL;

	return precvframe->rx_data;
}

static inline u8 *recvframe_push(struct recv_frame_hdr *precvframe, int sz)
{
/*  append data before rx_data
 * add data to the start of recv_frame
 *
 *      This function extends the used data area of the recv_frame at the buffer
 *      start. rx_data must be still larger than rx_head, after pushing.
 */
	if (precvframe == NULL)
		return NULL;
	precvframe->rx_data -= sz;
	if (precvframe->rx_data < precvframe->rx_head) {
		precvframe->rx_data += sz;
		return NULL;
	}
	precvframe->len += sz;
	return precvframe->rx_data;
}

static inline u8 *recvframe_pull(struct recv_frame_hdr *precvframe, int sz)
{
/*  rx_data += sz; move rx_data sz bytes  hereafter
 * used for extract sz bytes from rx_data, update rx_data and return the
 * updated rx_data to the caller
 */
	if (precvframe == NULL)
		return NULL;


	precvframe->rx_data += sz;

	if (precvframe->rx_data > precvframe->rx_tail) {
		precvframe->rx_data -= sz;
		return NULL;
	}

	precvframe->len -= sz;

	return precvframe->rx_data;
}

static inline u8 *recvframe_put(struct recv_frame_hdr *precvframe, int sz)
{
/*  rx_tai += sz; move rx_tail sz bytes  hereafter
 * used for append sz bytes from ptr to rx_tail, update rx_tail and return
 * the updated rx_tail to the caller
 * after putting, rx_tail must be still larger than rx_end.
 */
	if (precvframe == NULL)
		return NULL;

	precvframe->rx_tail += sz;

	if (precvframe->rx_tail > precvframe->rx_end) {
		precvframe->rx_tail -= sz;
		return NULL;
	}

	precvframe->len += sz;

	return precvframe->rx_tail;
}

static inline u8 *recvframe_pull_tail(struct recv_frame_hdr *precvframe, int sz)
{
/*  rmv data from rx_tail
 * used for extract sz bytes from rx_end, update rx_end and return the
 * updated rx_end to the caller
 * after pulling, rx_end must be still larger than rx_data.
 */
	if (precvframe == NULL)
		return NULL;

	precvframe->rx_tail -= sz;

	if (precvframe->rx_tail < precvframe->rx_data) {
		precvframe->rx_tail += sz;
		return NULL;
	}

	precvframe->len -= sz;

	return precvframe->rx_tail;
}

static inline unsigned char *get_rxbuf_desc(struct recv_frame_hdr *precvframe)
{
	unsigned char *buf_desc;

	if (precvframe == NULL)
		return NULL;
	return buf_desc;
}

static inline u8 query_rx_pwr_percentage(s8 antpower)
{
	if ((antpower <= -100) || (antpower >= 20))
		return	0;
	else if (antpower >= 0)
		return	100;
	else
		return	100+antpower;
}

static inline s32 translate_percentage_to_dbm(u32 index)
{
	s32	power; /*  in dBm. */

	/*  Translate to dBm (x=0.5y-95). */
	power = (s32)((index + 1) >> 1);
	power -= 95;

	return power;
}

struct sta_info;

void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv);
void  mgt_dispatcher(struct rtw_adapter *padapter,
		     struct recv_frame_hdr *precv_frame);

#endif
