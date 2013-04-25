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
#ifndef _RTW_XMIT_H_
#define _RTW_XMIT_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#ifdef PLATFORM_FREEBSD
#include <if_ether.h>
#endif //PLATFORM_FREEBSD

#ifdef CONFIG_SDIO_HCI
//#define MAX_XMITBUF_SZ (30720)//	(2048)
#ifdef CONFIG_TX_AGGREGATION
#define MAX_XMITBUF_SZ	(20480)	// 20k
#else
#define MAX_XMITBUF_SZ (12288)  //12k 1536*8
#endif

#define NR_XMITBUFF	(16)

#elif defined (CONFIG_USB_HCI)

#ifdef CONFIG_USB_TX_AGGREGATION
#define MAX_XMITBUF_SZ	20480	// 20k
#else
#define MAX_XMITBUF_SZ	(2048)
#endif

#define NR_XMITBUFF	(4)

#elif defined (CONFIG_PCI_HCI)
#define MAX_XMITBUF_SZ	(1664)
#define NR_XMITBUFF	(128)
#endif

#ifdef PLATFORM_OS_CE
#define XMITBUF_ALIGN_SZ 4
#else
#ifdef CONFIG_PCI_HCI
#define XMITBUF_ALIGN_SZ 4
#else
#define XMITBUF_ALIGN_SZ 512
#endif
#endif

// xmit extension buff defination
#if defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTL8723A)
#define MAX_XMIT_EXTBUF_SZ	(20000)		// For Download BT FW array image.
#define NR_XMIT_EXTBUFF (1)
#else 
#define MAX_XMIT_EXTBUF_SZ	(1536)
#define NR_XMIT_EXTBUFF	(32)
#endif		//#if  defined(CONFIG_MP_INCLUDED) && defined(CONFIG_RTL8723A)

#define MAX_NUMBLKS		(1)

#define XMIT_VO_QUEUE (0)
#define XMIT_VI_QUEUE (1)
#define XMIT_BE_QUEUE (2)
#define XMIT_BK_QUEUE (3)

#ifdef CONFIG_PCI_HCI
//#define TXDESC_NUM						64
#define TXDESC_NUM						128
#define TXDESC_NUM_BE_QUEUE			128
#endif

#define WEP_IV(pattrib_iv, dot11txpn, keyidx)\
do{\
	pattrib_iv[0] = dot11txpn._byte_.TSC0;\
	pattrib_iv[1] = dot11txpn._byte_.TSC1;\
	pattrib_iv[2] = dot11txpn._byte_.TSC2;\
	pattrib_iv[3] = ((keyidx & 0x3)<<6);\
	dot11txpn.val = (dot11txpn.val == 0xffffff) ? 0: (dot11txpn.val+1);\
}while(0)


#define TKIP_IV(pattrib_iv, dot11txpn, keyidx)\
do{\
	pattrib_iv[0] = dot11txpn._byte_.TSC1;\
	pattrib_iv[1] = (dot11txpn._byte_.TSC1 | 0x20) & 0x7f;\
	pattrib_iv[2] = dot11txpn._byte_.TSC0;\
	pattrib_iv[3] = BIT(5) | ((keyidx & 0x3)<<6);\
	pattrib_iv[4] = dot11txpn._byte_.TSC2;\
	pattrib_iv[5] = dot11txpn._byte_.TSC3;\
	pattrib_iv[6] = dot11txpn._byte_.TSC4;\
	pattrib_iv[7] = dot11txpn._byte_.TSC5;\
	dot11txpn.val = dot11txpn.val == 0xffffffffffffULL ? 0: (dot11txpn.val+1);\
}while(0)

#define AES_IV(pattrib_iv, dot11txpn, keyidx)\
do{\
	pattrib_iv[0] = dot11txpn._byte_.TSC0;\
	pattrib_iv[1] = dot11txpn._byte_.TSC1;\
	pattrib_iv[2] = 0;\
	pattrib_iv[3] = BIT(5) | ((keyidx & 0x3)<<6);\
	pattrib_iv[4] = dot11txpn._byte_.TSC2;\
	pattrib_iv[5] = dot11txpn._byte_.TSC3;\
	pattrib_iv[6] = dot11txpn._byte_.TSC4;\
	pattrib_iv[7] = dot11txpn._byte_.TSC5;\
	dot11txpn.val = dot11txpn.val == 0xffffffffffffULL ? 0: (dot11txpn.val+1);\
}while(0)


#define HWXMIT_ENTRY	4


#define TXDESC_SIZE 32

#ifdef CONFIG_TX_EARLY_MODE
#define EARLY_MODE_INFO_SIZE	8
#endif


#ifdef CONFIG_SDIO_HCI
#define TXDESC_OFFSET TXDESC_SIZE

#endif

#ifdef CONFIG_USB_HCI
#define PACKET_OFFSET_SZ (8)
#define TXDESC_OFFSET (TXDESC_SIZE + PACKET_OFFSET_SZ)
#endif

#ifdef CONFIG_PCI_HCI
#define TXDESC_OFFSET 0
#define TX_DESC_NEXT_DESC_OFFSET	40
#endif



struct tx_desc{

	//DWORD 0
	unsigned int txdw0;

	unsigned int txdw1;

	unsigned int txdw2;

	unsigned int txdw3;

	unsigned int txdw4;

	unsigned int txdw5;

	unsigned int txdw6;

	unsigned int txdw7;
#ifdef CONFIG_PCI_HCI
	unsigned int txdw8;

	unsigned int txdw9;

	unsigned int txdw10;

	unsigned int txdw11;

	// 2008/05/15 MH Because PCIE HW memory R/W 4K limit. And now,  our descriptor
	// size is 40 bytes. If you use more than 102 descriptor( 103*40>4096), HW will execute
	// memoryR/W CRC error. And then all DMA fetch will fail. We must decrease descriptor
	// number or enlarge descriptor size as 64 bytes.
	unsigned int txdw12;

	unsigned int txdw13;

	unsigned int txdw14;

	unsigned int txdw15;
#endif
};


union txdesc {
	struct tx_desc txdesc;
	unsigned int value[TXDESC_SIZE>>2];
};

#ifdef CONFIG_PCI_HCI
#define PCI_MAX_TX_QUEUE_COUNT	8

struct rtw_tx_ring {
	struct tx_desc	*desc;
	dma_addr_t		dma;
	unsigned int		idx;
	unsigned int		entries;
	_queue			queue;
	u32				qlen;
};
#endif

struct	hw_xmit	{
	//_lock xmit_lock;
	//_list	pending;
	_queue *sta_queue;
	//struct hw_txqueue *phwtxqueue;
	//sint	txcmdcnt;
	int	accnt;
};

#if 0
struct pkt_attrib
{
	u8	type;
	u8	subtype;
	u8	bswenc;
	u8	dhcp_pkt;
	u16	ether_type;
	int	pktlen;		//the original 802.3 pkt raw_data len (not include ether_hdr data)
	int	pkt_hdrlen;	//the original 802.3 pkt header len
	int	hdrlen;		//the WLAN Header Len
	int	nr_frags;
	int	last_txcmdsz;
	int	encrypt;	//when 0 indicate no encrypt. when non-zero, indicate the encrypt algorith
	u8	iv[8];
	int	iv_len;
	u8	icv[8];
	int	icv_len;
	int	priority;
	int	ack_policy;
	int	mac_id;
	int	vcs_mode;	//virtual carrier sense method

	u8 	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8 	ra[ETH_ALEN];

	u8	key_idx;

	u8	qos_en;
	u8	ht_en;
	u8	raid;//rate adpative id
	u8	bwmode;
	u8	ch_offset;//PRIME_CHNL_OFFSET
	u8	sgi;//short GI
	u8	ampdu_en;//tx ampdu enable
	u8	mdata;//more data bit
	u8	eosp;

	u8	pctrl;//per packet txdesc control enable
	u8	triggered;//for ap mode handling Power Saving sta

	u32	qsel;
	u16	seqnum;

	struct sta_info * psta;
#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	u8	hw_tcp_csum;
#endif
};
#else
//reduce size
struct pkt_attrib
{
	u8	type;
	u8	subtype;
	u8	bswenc;
	u8	dhcp_pkt;
	u16	ether_type;
	u16	seqnum;
	u16	pkt_hdrlen;	//the original 802.3 pkt header len
	u16	hdrlen;		//the WLAN Header Len
	u32	pktlen;		//the original 802.3 pkt raw_data len (not include ether_hdr data)
	u32	last_txcmdsz;
	u8	nr_frags;
	u8	encrypt;	//when 0 indicate no encrypt. when non-zero, indicate the encrypt algorith
	u8	iv_len;
	u8	icv_len;
	u8	iv[8];
	u8	icv[8];
	u8	priority;
	u8	ack_policy;
	u8	mac_id;
	u8	vcs_mode;	//virtual carrier sense method
	u8 	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8 	ra[ETH_ALEN];
	u8	key_idx;
	u8	qos_en;
	u8	ht_en;
	u8	raid;//rate adpative id
	u8	bwmode;
	u8	ch_offset;//PRIME_CHNL_OFFSET
	u8	sgi;//short GI
	u8	ampdu_en;//tx ampdu enable
	u8	mdata;//more data bit
	u8	pctrl;//per packet txdesc control enable
	u8	triggered;//for ap mode handling Power Saving sta
	u8	qsel;
	u8	eosp;
	u8	rate;
	u8	intel_proxim;
	u8 	retry_ctrl;
	struct sta_info * psta;
#ifdef CONFIG_TCP_CSUM_OFFLOAD_TX
	u8	hw_tcp_csum;
#endif
};
#endif

#ifdef PLATFORM_FREEBSD
#define ETH_ALEN	6		/* Octets in one ethernet addr	 */
#define ETH_HLEN	14		/* Total octets in header.	 */
#define ETH_P_IP	0x0800		/* Internet Protocol packet	*/

/*struct rtw_ieee80211_hdr {
	uint16_t frame_control;
	uint16_t duration_id;
	u8 addr1[6];
	u8 addr2[6];
	u8 addr3[6];
	uint16_t seq_ctrl;
	u8 addr4[6];
} ;*/
#endif //PLATFORM_FREEBSD

#define WLANHDR_OFFSET	64

#define NULL_FRAMETAG		(0x0)
#define DATA_FRAMETAG		0x01
#define L2_FRAMETAG		0x02
#define MGNT_FRAMETAG		0x03
#define AMSDU_FRAMETAG	0x04

#define EII_FRAMETAG		0x05
#define IEEE8023_FRAMETAG  0x06

#define MP_FRAMETAG		0x07

#define TXAGG_FRAMETAG 	0x08


struct xmit_buf
{
	_list	list;

	_adapter *padapter;

	u8 *pallocated_buf;

	u8 *pbuf;

	void *priv_data;

	u16 ext_tag; // 0: Normal xmitbuf, 1: extension xmitbuf.
	u16 flags;
	u32 alloc_sz;

	u32  len;

#ifdef CONFIG_USB_HCI

	//u32 sz[8];
	u32	ff_hwaddr;

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX) || defined(PLATFORM_FREEBSD)
	PURB	pxmit_urb[8];
	dma_addr_t dma_transfer_addr;	/* (in) dma addr for transfer_buffer */
#endif

#ifdef PLATFORM_OS_XP
	PIRP		pxmit_irp[8];
#endif

#ifdef PLATFORM_OS_CE
	USB_TRANSFER	usb_transfer_write_port;
#endif

#ifdef PLATFORM_LINUX
	u8 isSync; //is this synchronous?
	int status; // keeping urb status for synchronous call to access
	struct completion done; // for wirte_port synchronously
#endif

	u8 bpending[8];

	sint last[8];

#endif

#ifdef CONFIG_SDIO_HCI
	u8 *phead;
	u8 *pdata;
	u8 *ptail;
	u8 *pend;
	u32 ff_hwaddr;
	u8	pg_num;	
	u8	agg_num;
#ifdef PLATFORM_OS_XP
	PMDL pxmitbuf_mdl;
	PIRP  pxmitbuf_irp;
	PSDBUS_REQUEST_PACKET pxmitbuf_sdrp;
#endif
#endif

#if defined(DBG_XMIT_BUF )|| defined(DBG_XMIT_BUF_EXT)
	u8 no;
#endif

};


struct xmit_frame
{
	_list	list;

	struct pkt_attrib attrib;

	_pkt *pkt;

	int	frame_tag;

	_adapter *padapter;

	u8	*buf_addr;

	struct xmit_buf *pxmitbuf;

#ifdef CONFIG_SDIO_HCI
	u8	pg_num;
	u8	agg_num;
#endif

#ifdef CONFIG_USB_HCI
#ifdef CONFIG_USB_TX_AGGREGATION
	u8	agg_num;
#endif
	s8	pkt_offset;
#ifdef CONFIG_RTL8192D
	u8	EMPktNum;
	u16	EMPktLen[5];//The max value by HW
#endif
#endif

};

struct tx_servq {
	_list	tx_pending;
	_queue	sta_pending;
	int qcnt;
};


struct sta_xmit_priv
{
	_lock	lock;
	sint	option;
	sint	apsd_setting;	//When bit mask is on, the associated edca queue supports APSD.


	//struct tx_servq blk_q[MAX_NUMBLKS];
	struct tx_servq	be_q;			//priority == 0,3
	struct tx_servq	bk_q;			//priority == 1,2
	struct tx_servq	vi_q;			//priority == 4,5
	struct tx_servq	vo_q;			//priority == 6,7
	_list 	legacy_dz;
	_list  apsd;

	u16 txseq_tid[16];

	//uint	sta_tx_bytes;
	//u64	sta_tx_pkts;
	//uint	sta_tx_fail;


};


struct	hw_txqueue	{
	volatile sint	head;
	volatile sint	tail;
	volatile sint 	free_sz;	//in units of 64 bytes
	volatile sint      free_cmdsz;
	volatile sint	 txsz[8];
	uint	ff_hwaddr;
	uint	cmd_hwaddr;
	sint	ac_tag;
};

struct agg_pkt_info{
	u16 offset;
	u16 pkt_len;
};

struct	xmit_priv	{

	_lock	lock;

	_sema	xmit_sema;
	_sema	terminate_xmitthread_sema;

	//_queue	blk_strms[MAX_NUMBLKS];
	_queue	be_pending;
	_queue	bk_pending;
	_queue	vi_pending;
	_queue	vo_pending;
	_queue	bm_pending;

	//_queue	legacy_dz_queue;
	//_queue	apsd_queue;

	u8 *pallocated_frame_buf;
	u8 *pxmit_frame_buf;
	uint free_xmitframe_cnt;

	//uint mapping_addr;
	//uint pkt_sz;

	_queue	free_xmit_queue;

	//struct	hw_txqueue	be_txqueue;
	//struct	hw_txqueue	bk_txqueue;
	//struct	hw_txqueue	vi_txqueue;
	//struct	hw_txqueue	vo_txqueue;
	//struct	hw_txqueue	bmc_txqueue;

	uint	frag_len;

	_adapter	*adapter;

	u8   vcs_setting;
	u8	vcs;
	u8	vcs_type;
	//u16  rts_thresh;

	u64	tx_bytes;
	u64	tx_pkts;
	u64	tx_drop;
	u64	last_tx_bytes;
	u64	last_tx_pkts;

	struct hw_xmit *hwxmits;
	u8	hwxmit_entry;

#ifdef CONFIG_USB_HCI
	_sema	tx_retevt;//all tx return event;
	u8		txirp_cnt;//

#ifdef PLATFORM_OS_CE
	USB_TRANSFER	usb_transfer_write_port;
//	USB_TRANSFER	usb_transfer_write_mem;
#endif
#ifdef PLATFORM_LINUX
	struct tasklet_struct xmit_tasklet;
#endif
#ifdef PLATFORM_FREEBSD
	struct task xmit_tasklet;
#endif
	//per AC pending irp
	int beq_cnt;
	int bkq_cnt;
	int viq_cnt;
	int voq_cnt;

#endif

#ifdef CONFIG_PCI_HCI
	// Tx
	struct rtw_tx_ring	tx_ring[PCI_MAX_TX_QUEUE_COUNT];
	int	txringcount[PCI_MAX_TX_QUEUE_COUNT];
	u8 	beaconDMAing;		//flag of indicating beacon is transmiting to HW by DMA
#ifdef PLATFORM_LINUX
	struct tasklet_struct xmit_tasklet;
#endif
#endif

#ifdef CONFIG_SDIO_HCI
#ifdef CONFIG_SDIO_TX_TASKLET
#ifdef PLATFORM_LINUX
	struct tasklet_struct xmit_tasklet;
#endif
#endif
#endif

	_queue free_xmitbuf_queue;
	_queue pending_xmitbuf_queue;
	u8 *pallocated_xmitbuf;
	u8 *pxmitbuf;
	uint free_xmitbuf_cnt;

	_queue free_xmit_extbuf_queue;
	u8 *pallocated_xmit_extbuf;
	u8 *pxmit_extbuf;
	uint free_xmit_extbuf_cnt;

	u16	nqos_ssn;
	#ifdef CONFIG_TX_EARLY_MODE

	#ifdef CONFIG_SDIO_HCI
	#define MAX_AGG_PKT_NUM 20	
	#else
	#define MAX_AGG_PKT_NUM 256 //Max tx ampdu coounts		
	#endif
	
	struct agg_pkt_info agg_pkt[MAX_AGG_PKT_NUM];
	#endif
};

extern struct xmit_buf *rtw_alloc_xmitbuf_ext(struct xmit_priv *pxmitpriv);
extern s32 rtw_free_xmitbuf_ext(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);

extern struct xmit_buf *rtw_alloc_xmitbuf(struct xmit_priv *pxmitpriv);
extern s32 rtw_free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);

void rtw_count_tx_stats(_adapter *padapter, struct xmit_frame *pxmitframe, int sz);
extern void rtw_update_protection(_adapter *padapter, u8 *ie, uint ie_len);
extern s32 rtw_make_wlanhdr(_adapter *padapter, u8 *hdr, struct pkt_attrib *pattrib);
extern s32 rtw_put_snap(u8 *data, u16 h_proto);

extern struct xmit_frame *rtw_alloc_xmitframe(struct xmit_priv *pxmitpriv);
extern s32 rtw_free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe);
extern void rtw_free_xmitframe_queue(struct xmit_priv *pxmitpriv, _queue *pframequeue);
struct tx_servq *rtw_get_sta_pending(_adapter *padapter, struct sta_info *psta, sint up, u8 *ac);
extern s32 rtw_xmitframe_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
extern struct xmit_frame* rtw_dequeue_xframe(struct xmit_priv *pxmitpriv, struct hw_xmit *phwxmit_i, sint entry);

extern s32 rtw_xmit_classifier(_adapter *padapter, struct xmit_frame *pxmitframe);
extern u32 rtw_calculate_wlan_pkt_size_by_attribue(struct pkt_attrib *pattrib);
#define rtw_wlan_pkt_size(f) rtw_calculate_wlan_pkt_size_by_attribue(&f->attrib)
extern s32 rtw_xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe);
#ifdef CONFIG_TDLS
extern void rtw_tdls_dis_rsp_fr(_adapter * padapter, struct xmit_frame * pxmitframe, u8 *pframe, u8 dialog);
extern s32 rtw_xmit_tdls_coalesce(_adapter *padapter, struct xmit_frame *pxmitframe, u8 action);
void rtw_dump_xframe(_adapter *padapter, struct xmit_frame *pxmitframe);
#endif
#ifdef CONFIG_IOL
void rtw_dump_xframe_sync(_adapter *padapter, struct xmit_frame *pxmitframe);
#endif
s32 _rtw_init_hw_txqueue(struct hw_txqueue* phw_txqueue, u8 ac_tag);
void _rtw_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv);


s32 rtw_txframes_pending(_adapter *padapter);
s32 rtw_txframes_sta_ac_pending(_adapter *padapter, struct pkt_attrib *pattrib);
void rtw_init_hwxmits(struct hw_xmit *phwxmit, sint entry);


s32 _rtw_init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter);
void _rtw_free_xmit_priv (struct xmit_priv *pxmitpriv);


void rtw_alloc_hwxmits(_adapter *padapter);
void rtw_free_hwxmits(_adapter *padapter);

s32 rtw_free_xmitframe_ex(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe);

s32 rtw_xmit(_adapter *padapter, _pkt **pkt);

#ifdef CONFIG_TDLS
sint xmitframe_enqueue_for_tdls_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe);
#endif

#ifdef CONFIG_AP_MODE
sint xmitframe_enqueue_for_sleeping_sta(_adapter *padapter, struct xmit_frame *pxmitframe);
void stop_sta_xmit(_adapter *padapter, struct sta_info *psta);
void wakeup_sta_to_xmit(_adapter *padapter, struct sta_info *psta);
void xmit_delivery_enabled_frames(_adapter *padapter, struct sta_info *psta);
#endif

u8	qos_acm(u8 acm_mask, u8 priority);

#ifdef CONFIG_XMIT_THREAD_MODE
void	enqueue_pending_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
struct xmit_buf*	dequeue_pending_xmitbuf(struct xmit_priv *pxmitpriv);
struct xmit_buf*	dequeue_pending_xmitbuf_under_survey(struct xmit_priv *pxmitpriv);
sint	check_pending_xmitbuf(struct xmit_priv *pxmitpriv);
thread_return	rtw_xmit_thread(thread_context context);
#endif


//include after declaring struct xmit_buf, in order to avoid warning
#include <xmit_osdep.h>

#endif	//_RTL871X_XMIT_H_

