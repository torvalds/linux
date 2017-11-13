/******************************************************************************
 *
 * Copyright(c) 2007 - 2017 Realtek Corporation.
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
 *****************************************************************************/
#ifndef _RTW_RECV_H_
#define _RTW_RECV_H_

#ifdef PLATFORM_OS_XP
	#ifdef CONFIG_SDIO_HCI
		#define NR_RECVBUFF 1024/* 512 */ /* 128 */
	#else
		#define NR_RECVBUFF (16)
	#endif
#elif defined(PLATFORM_OS_CE)
	#ifdef CONFIG_SDIO_HCI
		#define NR_RECVBUFF (128)
	#else
		#define NR_RECVBUFF (4)
	#endif
#else /* PLATFORM_LINUX /PLATFORM_BSD */

	#ifdef CONFIG_SINGLE_RECV_BUF
		#define NR_RECVBUFF (1)
	#else
		#if defined(CONFIG_GSPI_HCI)
			#define NR_RECVBUFF (32)
		#elif defined(CONFIG_SDIO_HCI)
			#define NR_RECVBUFF (8)
		#else
			#define NR_RECVBUFF (8)
		#endif
	#endif /* CONFIG_SINGLE_RECV_BUF */
	#ifdef CONFIG_PREALLOC_RX_SKB_BUFFER
		#define NR_PREALLOC_RECV_SKB (rtw_rtkm_get_nr_recv_skb()>>1)
	#else /*!CONFIG_PREALLOC_RX_SKB_BUFFER */
		#define NR_PREALLOC_RECV_SKB 8
	#endif /* CONFIG_PREALLOC_RX_SKB_BUFFER */

	#ifdef CONFIG_RTW_NAPI
		#define RTL_NAPI_WEIGHT (32)
	#endif
#endif

#if defined(CONFIG_RTL8821C) && defined(CONFIG_SDIO_HCI) && defined(CONFIG_RECV_THREAD_MODE)
	#ifdef NR_RECVBUFF
	#undef NR_RECVBUFF
	#define NR_RECVBUFF (32)
	#endif
#endif

#define NR_RECVFRAME 256

#define RXFRAME_ALIGN	8
#define RXFRAME_ALIGN_SZ	(1<<RXFRAME_ALIGN)

#define DRVINFO_SZ	4 /* unit is 8bytes */

#define MAX_RXFRAME_CNT	512
#define MAX_RX_NUMBLKS		(32)
#define RECVFRAME_HDR_ALIGN 128
#define MAX_CONTINUAL_NORXPACKET_COUNT 4    /*  In MAX_CONTINUAL_NORXPACKET_COUNT*2 sec  , no rx traffict would issue DELBA*/

#define PHY_RSSI_SLID_WIN_MAX				100
#define PHY_LINKQUALITY_SLID_WIN_MAX		20


#define SNAP_SIZE sizeof(struct ieee80211_snap_hdr)

#define RX_MPDU_QUEUE				0
#define RX_CMD_QUEUE				1
#define RX_MAX_QUEUE				2

static u8 SNAP_ETH_TYPE_IPX[2] = {0x81, 0x37};

static u8 SNAP_ETH_TYPE_APPLETALK_AARP[2] = {0x80, 0xf3};
static u8 SNAP_ETH_TYPE_APPLETALK_DDP[2] = {0x80, 0x9b};
static u8 SNAP_ETH_TYPE_TDLS[2] = {0x89, 0x0d};
static u8 SNAP_HDR_APPLETALK_DDP[3] = {0x08, 0x00, 0x07}; /* Datagram Delivery Protocol */

static u8 oui_8021h[] = {0x00, 0x00, 0xf8};
static u8 oui_rfc1042[] = {0x00, 0x00, 0x00};

#define MAX_SUBFRAME_COUNT	64
static u8 rtw_rfc1042_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00 };
/* Bridge-Tunnel header (for EtherTypes ETH_P_AARP and ETH_P_IPX) */
static u8 rtw_bridge_tunnel_header[] = { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0xf8 };

/* for Rx reordering buffer control */
struct recv_reorder_ctrl {
	_adapter	*padapter;
	u8 tid;
	u8 enable;
	u16 indicate_seq;/* =wstart_b, init_value=0xffff */
	u16 wend_b;
	u8 wsize_b;
	u8 ampdu_size;
	_queue pending_recvframe_queue;
	_timer reordering_ctrl_timer;
	u8 bReorderWaiting;
};

struct	stainfo_rxcache	{
	u16	tid_rxseq[16];
	u8 iv[16][8];
#if 0
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
#endif
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
	u32	total_val;		/* sum of valid elements	 */
};

struct rx_raw_rssi {
	u8 data_rate;
	u8 pwdball;
	s8 pwr_all;

	u8 mimo_signal_strength[4];/* in 0~100 index */
	u8 mimo_signal_quality[4];

	s8 ofdm_pwr[4];
	u8 ofdm_snr[4];
};


#include "cmn_info/rtw_sta_info.h"

struct rx_pkt_attrib	{
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
	u8	encrypt; /* when 0 indicate no encrypt. when non-zero, indicate the encrypt algorith */
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

	u8	key_index;

	u8	data_rate;
	u8 ch; /* RX channel */
	u8	bw;
	u8	stbc;
	u8	ldpc;
	u8	sgi;
	u8	pkt_rpt_type;
	u32 tsfl;
	u32	MacIDValidEntry[2];	/* 64 bits present 64 entry. */
	u8	ppdu_cnt;
	u32 	free_cnt;		/* free run counter */
	struct phydm_phyinfo_struct phy_info;
};


/* These definition is used for Rx packet reordering. */
#define SN_LESS(a, b)		(((a-b) & 0x800) != 0)
#define SN_EQUAL(a, b)	(a == b)
/* #define REORDER_WIN_SIZE	128 */
/* #define REORDER_ENTRY_NUM	128 */
#define REORDER_WAIT_TIME	(50) /* (ms) */

#define RECVBUFF_ALIGN_SZ 8

#ifdef CONFIG_TRX_BD_ARCH
	#define RX_WIFI_INFO_SIZE	24
#elif (defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B)) && defined(CONFIG_PCI_HCI)
	#define RXBD_SIZE	sizeof(struct recv_stat)
#endif

#define RXDESC_SIZE	24
#define RXDESC_OFFSET RXDESC_SIZE

#ifdef CONFIG_TRX_BD_ARCH
struct rx_buf_desc {
	/* RX has exactly one segment */
#ifdef CONFIG_64BIT_DMA
	unsigned int dword[4];
#else
	unsigned int dword[2];
#endif
};

struct recv_stat {
	unsigned int rxdw[8];
};
#else
struct recv_stat {
	unsigned int rxdw0;

	unsigned int rxdw1;

#if !((defined(CONFIG_RTL8192E) || defined(CONFIG_RTL8814A) || defined(CONFIG_RTL8822B) || defined(CONFIG_RTL8821C)) && defined(CONFIG_PCI_HCI))  /* exclude 8192ee, 8814ae, 8822be, 8821ce */
	unsigned int rxdw2;

	unsigned int rxdw3;
#endif

#ifndef BUF_DESC_ARCH
	unsigned int rxdw4;

	unsigned int rxdw5;

#ifdef CONFIG_PCI_HCI
	unsigned int rxdw6;

	unsigned int rxdw7;
#endif
#endif /* if BUF_DESC_ARCH is defined, rx_buf_desc occupy 4 double words */
};
#endif

#define EOR BIT(30)

#ifdef CONFIG_PCI_HCI
#define PCI_MAX_RX_QUEUE		1/* MSDU packet queue, Rx Command Queue */
#define PCI_MAX_RX_COUNT		128
#ifdef CONFIG_TRX_BD_ARCH
#define RX_BD_NUM				PCI_MAX_RX_COUNT	/* alias */
#endif

struct rtw_rx_ring {
#ifdef CONFIG_TRX_BD_ARCH
	struct rx_buf_desc	*buf_desc;
#else
	struct recv_stat	*desc;
#endif
	dma_addr_t		dma;
	unsigned int		idx;
	struct sk_buff	*rx_buf[PCI_MAX_RX_COUNT];
};
#endif



/*
accesser of recv_priv: rtw_recv_entry(dispatch / passive level); recv_thread(passive) ; returnpkt(dispatch)
; halt(passive) ;

using enter_critical section to protect
*/

#ifndef DBG_RX_BH_TRACKING
#define DBG_RX_BH_TRACKING 0
#endif

struct recv_priv {
	_lock	lock;

#ifdef CONFIG_RECV_THREAD_MODE
	_sema	recv_sema;

#endif

	/* _queue	blk_strms[MAX_RX_NUMBLKS];    */ /* keeping the block ack frame until return ack */
	_queue	free_recv_queue;
	_queue	recv_pending_queue;
	_queue	uc_swdec_pending_queue;


	u8 *pallocated_frame_buf;
	u8 *precv_frame_buf;

	uint free_recvframe_cnt;

	#if DBG_RX_BH_TRACKING
	u32 rx_bh_stage;
	u32 rx_bh_buf_dq_cnt;
	void *rx_bh_lbuf;
	void *rx_bh_cbuf;
	void *rx_bh_cbuf_data;
	u32 rx_bh_cbuf_dlen;
	u32 rx_bh_cbuf_pos;
	void *rx_bh_cframe;
	#endif

	_adapter	*adapter;

#ifdef PLATFORM_WINDOWS
	_nic_hdl  RxPktPoolHdl;
	_nic_hdl  RxBufPoolHdl;

#ifdef PLATFORM_OS_XP
	PMDL	pbytecnt_mdl;
#endif
	uint	counter; /* record the number that up-layer will return to drv; only when counter==0 can we  release recv_priv */
	NDIS_EVENT	recv_resource_evt ;
#endif


	u32 is_any_non_be_pkts;

	u64	rx_bytes;
	u64	rx_pkts;
	u64	rx_drop;

	u64 dbg_rx_drop_count;
	u64 dbg_rx_ampdu_drop_count;
	u64 dbg_rx_ampdu_forced_indicate_count;
	u64 dbg_rx_ampdu_loss_count;
	u64 dbg_rx_dup_mgt_frame_drop_count;
	u64 dbg_rx_ampdu_window_shift_cnt;
	u64 dbg_rx_conflic_mac_addr_cnt;

	uint  rx_icv_err;
	uint  rx_largepacket_crcerr;
	uint  rx_smallpacket_crcerr;
	uint  rx_middlepacket_crcerr;

#ifdef CONFIG_USB_HCI
	/* u8 *pallocated_urb_buf; */
	_sema allrxreturnevt;
	uint	ff_hwaddr;
	ATOMIC_T	rx_pending_cnt;

#ifdef CONFIG_USB_INTERRUPT_IN_PIPE
#ifdef PLATFORM_LINUX
	PURB	int_in_urb;
#endif

	u8	*int_in_buf;
#endif /* CONFIG_USB_INTERRUPT_IN_PIPE */

#endif
#if defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD)
#ifdef PLATFORM_FREEBSD
	struct task irq_prepare_beacon_tasklet;
	struct task recv_tasklet;
#else /* PLATFORM_FREEBSD */
	struct tasklet_struct irq_prepare_beacon_tasklet;
	struct tasklet_struct recv_tasklet;
#endif /* PLATFORM_FREEBSD */
	struct sk_buff_head free_recv_skb_queue;
	struct sk_buff_head rx_skb_queue;
#ifdef CONFIG_RTW_NAPI
		struct sk_buff_head rx_napi_skb_queue;
#endif 
#ifdef CONFIG_RX_INDICATE_QUEUE
	struct task rx_indicate_tasklet;
	struct ifqueue rx_indicate_queue;
#endif /* CONFIG_RX_INDICATE_QUEUE */

#endif /* defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD) */

	u8 *pallocated_recv_buf;
	u8 *precv_buf;    /* 4 alignment */
	_queue	free_recv_buf_queue;
	u32	free_recv_buf_queue_cnt;

#if defined(CONFIG_SDIO_HCI) || defined(CONFIG_GSPI_HCI) || defined(CONFIG_USB_HCI)
	_queue	recv_buf_pending_queue;
#endif

#ifdef CONFIG_PCI_HCI
	/* Rx */
	struct rtw_rx_ring	rx_ring[PCI_MAX_RX_QUEUE];
	int rxringcount;	/* size should be PCI_MAX_RX_QUEUE */
	u16	rxbuffersize;
#endif

	/* For display the phy informatiom */
	u8 is_signal_dbg;	/* for debug */
	u8 signal_strength_dbg;	/* for debug */

	u8 signal_strength;
	u8 signal_qual;
	s8 rssi;	/* translate_percentage_to_dbm(ptarget_wlan->network.PhyInfo.SignalStrength); */
	struct rx_raw_rssi raw_rssi_info;
	/* s8 rxpwdb;	 */
	/* int RxSNRdB[2]; */
	/* s8 RxRssi[2]; */
	/* int FalseAlmCnt_all; */


#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
	_timer signal_stat_timer;
	u32 signal_stat_sampling_interval;
	/* u32 signal_stat_converging_constant; */
	struct signal_stat signal_qual_data;
	struct signal_stat signal_strength_data;
#else /* CONFIG_NEW_SIGNAL_STAT_PROCESS */
	struct smooth_rssi_data signal_qual_data;
	struct smooth_rssi_data signal_strength_data;
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */
	u16 sink_udpport, pre_rtp_rxseq, cur_rtp_rxseq;

	BOOLEAN store_law_data_flag;
};

#define RX_BH_STG_UNKNOWN		0
#define RX_BH_STG_HDL_ENTER		1
#define RX_BH_STG_HDL_EXIT		2
#define RX_BH_STG_NEW_BUF		3
#define RX_BH_STG_NEW_FRAME		4
#define RX_BH_STG_NORMAL_RX		5
#define RX_BH_STG_NORMAL_RX_END	6
#define RX_BH_STG_C2H			7
#define RX_BH_STG_C2H_END		8

#if DBG_RX_BH_TRACKING
void rx_bh_tk_set_stage(struct recv_priv *recv, u32 s);
void rx_bh_tk_set_buf(struct recv_priv *recv, void *buf, void *data, u32 dlen);
void rx_bh_tk_set_buf_pos(struct recv_priv *recv, void *pos);
void rx_bh_tk_set_frame(struct recv_priv *recv, void *frame);
void dump_rx_bh_tk(void *sel, struct recv_priv *recv);
#else
#define rx_bh_tk_set_stage(recv, s) do {} while (0)
#define rx_bh_tk_set_buf(recv, buf, data, dlen) do {} while (0)
#define rx_bh_tk_set_buf_pos(recv, pos) do {} while (0)
#define rx_bh_tk_set_frame(recv, frame) do {} while (0)
#define dump_rx_bh_tk(sel, recv) do {} while (0)
#endif

#ifdef CONFIG_NEW_SIGNAL_STAT_PROCESS
#define rtw_set_signal_stat_timer(recvpriv) _set_timer(&(recvpriv)->signal_stat_timer, (recvpriv)->signal_stat_sampling_interval)
#endif /* CONFIG_NEW_SIGNAL_STAT_PROCESS */

struct sta_recv_priv {

	_lock	lock;
	sint	option;

	/* _queue	blk_strms[MAX_RX_NUMBLKS]; */
	_queue defrag_q;	 /* keeping the fragment frame until defrag */

	struct	stainfo_rxcache rxcache;

	/* uint	sta_rx_bytes; */
	/* uint	sta_rx_pkts; */
	/* uint	sta_rx_fail; */

};


struct recv_buf {
	_list list;

	_lock recvbuf_lock;

	u32	ref_cnt;

	PADAPTER adapter;

	u8	*pbuf;
	u8	*pallocated_buf;

	u32	len;
	u8	*phead;
	u8	*pdata;
	u8	*ptail;
	u8	*pend;

#ifdef CONFIG_USB_HCI

#if defined(PLATFORM_OS_XP) || defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD)
	PURB	purb;
	dma_addr_t dma_transfer_addr;	/* (in) dma addr for transfer_buffer */
	u32 alloc_sz;
#endif

#ifdef PLATFORM_OS_XP
	PIRP		pirp;
#endif

#ifdef PLATFORM_OS_CE
	USB_TRANSFER	usb_transfer_read_port;
#endif

	u8  irp_pending;
	int  transfer_len;

#endif

#if defined(PLATFORM_LINUX)
	_pkt *pskb;
#elif defined(PLATFORM_FREEBSD) /* skb solution */
	struct sk_buff *pskb;
#endif
};


/*
	head  ----->

		data  ----->

			payload

		tail  ----->


	end   ----->

	len = (unsigned int )(tail - data);

*/
struct recv_frame_hdr {
	_list	list;
	_pkt *pkt;

	_adapter  *adapter;

	u8 fragcnt;

	int frame_tag;

	struct rx_pkt_attrib attrib;

	uint  len;
	u8 *rx_head;
	u8 *rx_data;
	u8 *rx_tail;
	u8 *rx_end;

	void *precvbuf;


	/*  */
	struct sta_info *psta;

	/* for A-MPDU Rx reordering buffer control */
	struct recv_reorder_ctrl *preorder_ctrl;

#ifdef CONFIG_WAPI_SUPPORT
	u8 UserPriority;
	u8 WapiTempPN[16];
	u8 WapiSrcAddr[6];
	u8 bWapiCheckPNInDecrypt;
	u8 bIsWaiPacket;
#endif

};


union recv_frame {

	union {
		_list list;
		struct recv_frame_hdr hdr;
		uint mem[RECVFRAME_HDR_ALIGN >> 2];
	} u;

	/* uint mem[MAX_RXSZ>>2]; */

};

bool rtw_rframe_del_wfd_ie(union recv_frame *rframe, u8 ies_offset);

typedef enum _RX_PACKET_TYPE {
	NORMAL_RX,/* Normal rx packet */
	TX_REPORT1,/* CCX */
	TX_REPORT2,/* TX RPT */
	HIS_REPORT,/* USB HISR RPT */
	C2H_PACKET
} RX_PACKET_TYPE, *PRX_PACKET_TYPE;

extern union recv_frame *_rtw_alloc_recvframe(_queue *pfree_recv_queue);   /* get a free recv_frame from pfree_recv_queue */
extern union recv_frame *rtw_alloc_recvframe(_queue *pfree_recv_queue);   /* get a free recv_frame from pfree_recv_queue */
extern void rtw_init_recvframe(union recv_frame *precvframe , struct recv_priv *precvpriv);
extern int	 rtw_free_recvframe(union recv_frame *precvframe, _queue *pfree_recv_queue);

#define rtw_dequeue_recvframe(queue) rtw_alloc_recvframe(queue)
extern int _rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue);
extern int rtw_enqueue_recvframe(union recv_frame *precvframe, _queue *queue);

extern void rtw_free_recvframe_queue(_queue *pframequeue,  _queue *pfree_recv_queue);
u32 rtw_free_uc_swdec_pending_queue(_adapter *adapter);

sint rtw_enqueue_recvbuf_to_head(struct recv_buf *precvbuf, _queue *queue);
sint rtw_enqueue_recvbuf(struct recv_buf *precvbuf, _queue *queue);
struct recv_buf *rtw_dequeue_recvbuf(_queue *queue);

#if defined(CONFIG_80211N_HT) && defined(CONFIG_RECV_REORDERING_CTRL)
void rtw_reordering_ctrl_timeout_handler(void *pcontext);
#endif

void rx_query_phy_status(union recv_frame *rframe, u8 *phy_stat);
int rtw_inc_and_chk_continual_no_rx_packet(struct sta_info *sta, int tid_index);
void rtw_reset_continual_no_rx_packet(struct sta_info *sta, int tid_index);

#ifdef CONFIG_RECV_THREAD_MODE
thread_return rtw_recv_thread(thread_context context);
#endif

__inline static u8 *get_rxmem(union recv_frame *precvframe)
{
	/* always return rx_head... */
	if (precvframe == NULL)
		return NULL;

	return precvframe->u.hdr.rx_head;
}

__inline static u8 *get_rx_status(union recv_frame *precvframe)
{

	return get_rxmem(precvframe);

}

__inline static u8 *get_recvframe_data(union recv_frame *precvframe)
{

	/* alwasy return rx_data */
	if (precvframe == NULL)
		return NULL;

	return precvframe->u.hdr.rx_data;

}

__inline static u8 *recvframe_push(union recv_frame *precvframe, sint sz)
{
	/* append data before rx_data */

	/* add data to the start of recv_frame
	*
	*      This function extends the used data area of the recv_frame at the buffer
	*      start. rx_data must be still larger than rx_head, after pushing.
	*/

	if (precvframe == NULL)
		return NULL;


	precvframe->u.hdr.rx_data -= sz ;
	if (precvframe->u.hdr.rx_data < precvframe->u.hdr.rx_head) {
		precvframe->u.hdr.rx_data += sz ;
		return NULL;
	}

	precvframe->u.hdr.len += sz;

	return precvframe->u.hdr.rx_data;

}


__inline static u8 *recvframe_pull(union recv_frame *precvframe, sint sz)
{
	/* rx_data += sz; move rx_data sz bytes  hereafter */

	/* used for extract sz bytes from rx_data, update rx_data and return the updated rx_data to the caller */


	if (precvframe == NULL)
		return NULL;


	precvframe->u.hdr.rx_data += sz;

	if (precvframe->u.hdr.rx_data > precvframe->u.hdr.rx_tail) {
		precvframe->u.hdr.rx_data -= sz;
		return NULL;
	}

	precvframe->u.hdr.len -= sz;

	return precvframe->u.hdr.rx_data;

}

__inline static u8 *recvframe_put(union recv_frame *precvframe, sint sz)
{
	/* rx_tai += sz; move rx_tail sz bytes  hereafter */

	/* used for append sz bytes from ptr to rx_tail, update rx_tail and return the updated rx_tail to the caller */
	/* after putting, rx_tail must be still larger than rx_end. */
	unsigned char *prev_rx_tail;

	/* RTW_INFO("recvframe_put: len=%d\n", sz); */

	if (precvframe == NULL)
		return NULL;

	prev_rx_tail = precvframe->u.hdr.rx_tail;

	precvframe->u.hdr.rx_tail += sz;

	if (precvframe->u.hdr.rx_tail > precvframe->u.hdr.rx_end) {
		precvframe->u.hdr.rx_tail -= sz;
		return NULL;
	}

	precvframe->u.hdr.len += sz;

	return precvframe->u.hdr.rx_tail;

}



__inline static u8 *recvframe_pull_tail(union recv_frame *precvframe, sint sz)
{
	/* rmv data from rx_tail (by yitsen) */

	/* used for extract sz bytes from rx_end, update rx_end and return the updated rx_end to the caller */
	/* after pulling, rx_end must be still larger than rx_data. */

	if (precvframe == NULL)
		return NULL;

	precvframe->u.hdr.rx_tail -= sz;

	if (precvframe->u.hdr.rx_tail < precvframe->u.hdr.rx_data) {
		precvframe->u.hdr.rx_tail += sz;
		return NULL;
	}

	precvframe->u.hdr.len -= sz;

	return precvframe->u.hdr.rx_tail;

}



__inline static _buffer *get_rxbuf_desc(union recv_frame *precvframe)
{
	_buffer *buf_desc;

	if (precvframe == NULL)
		return NULL;
#ifdef PLATFORM_WINDOWS
	NdisQueryPacket(precvframe->u.hdr.pkt, NULL, NULL, &buf_desc, NULL);
#endif

	return buf_desc;
}


__inline static union recv_frame *rxmem_to_recvframe(u8 *rxmem)
{
	/* due to the design of 2048 bytes alignment of recv_frame, we can reference the union recv_frame */
	/* from any given member of recv_frame. */
	/* rxmem indicates the any member/address in recv_frame */

	return (union recv_frame *)(((SIZE_PTR)rxmem >> RXFRAME_ALIGN) << RXFRAME_ALIGN);

}

__inline static union recv_frame *pkt_to_recvframe(_pkt *pkt)
{

	u8 *buf_star;
	union recv_frame *precv_frame;
#ifdef PLATFORM_WINDOWS
	_buffer *buf_desc;
	uint len;

	NdisQueryPacket(pkt, NULL, NULL, &buf_desc, &len);
	NdisQueryBufferSafe(buf_desc, &buf_star, &len, HighPagePriority);
#endif
	precv_frame = rxmem_to_recvframe((unsigned char *)buf_star);

	return precv_frame;
}

__inline static u8 *pkt_to_recvmem(_pkt *pkt)
{
	/* return the rx_head */

	union recv_frame *precv_frame = pkt_to_recvframe(pkt);

	return	precv_frame->u.hdr.rx_head;

}

__inline static u8 *pkt_to_recvdata(_pkt *pkt)
{
	/* return the rx_data */

	union recv_frame *precv_frame = pkt_to_recvframe(pkt);

	return	precv_frame->u.hdr.rx_data;

}


__inline static sint get_recvframe_len(union recv_frame *precvframe)
{
	return precvframe->u.hdr.len;
}


__inline static s32 translate_percentage_to_dbm(u32 SignalStrengthIndex)
{
	s32	SignalPower; /* in dBm. */

#ifdef CONFIG_SIGNAL_SCALE_MAPPING
	/* Translate to dBm (x=0.5y-95). */
	SignalPower = (s32)((SignalStrengthIndex + 1) >> 1);
	SignalPower -= 95;
#else
	/* Translate to dBm (x=y-100) */
	SignalPower = SignalStrengthIndex - 100;
#endif

	return SignalPower;
}

struct sta_info;

extern void _rtw_init_sta_recv_priv(struct sta_recv_priv *psta_recvpriv);

extern void  mgt_dispatcher(_adapter *padapter, union recv_frame *precv_frame);

u8 adapter_allow_bmc_data_rx(_adapter *adapter);
s32 pre_recv_entry(union recv_frame *precvframe, u8 *pphy_status);

#endif
