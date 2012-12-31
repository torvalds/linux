/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
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
#ifndef _RTL871X_XMIT_H_
#define _RTL871X_XMIT_H_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <xmit_osdep.h>

#ifdef CONFIG_SDIO_HCI
#define MAX_XMITBUF_SZ (30720)//	(2048)
#define NR_XMITBUFF	(16)
#else //USB

#ifdef CONFIG_USB_TX_AGGREGATION
#define MAX_XMITBUF_SZ	(16384)
#else
#define MAX_XMITBUF_SZ	(2048)
#endif //CONFIG_USB_TX_AGGREGATION

#define NR_XMITBUFF	(4)
#endif

#ifdef CONFIG_USB_TX_AGGREGATION
#define AGGR_NR_HIGH_BOUND	(4) //(8)
#define AGGR_NR_LOW_BOUND	(2)
#endif //CONFIG_USB_TX_AGGREGATION

#ifdef PLATFORM_OS_CE
#define XMITBUF_ALIGN_SZ 4
#else
#define XMITBUF_ALIGN_SZ 512
#endif

#define TX_GUARD_BAND		5
#define MAX_NUMBLKS		(1)

// Fixed the Big Endian bug when using the software driver encryption.
#define WEP_IV(pattrib_iv, dot11txpn, keyidx)\
do{\
	pattrib_iv[0] = dot11txpn._byte_.TSC0;\
	pattrib_iv[1] = dot11txpn._byte_.TSC1;\
	pattrib_iv[2] = dot11txpn._byte_.TSC2;\
	pattrib_iv[3] = ((keyidx & 0x3)<<6);\
	dot11txpn.val = (dot11txpn.val == 0xffffff) ? 0: (dot11txpn.val+1);\
}while(0)

// Fixed the Big Endian bug when doing the Tx.
// The Linksys WRH54G will check this.
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



struct	hw_xmit	{
	_lock xmit_lock;
	_list	pending;	
	_queue *sta_queue;
	struct hw_txqueue *phwtxqueue;
	sint	txcmdcnt;		
	int	accnt;		
};

struct pkt_attrib
{	
	u8	type;
	u8	subtype;
	u8	bswenc;
	u8	dhcp_pkt;

	u16	ether_type;	
	u16	pktlen;		//the original 802.3 pkt raw_data len (not include ether_hdr data)

	u16	seqnum;
	u16	last_txcmdsz;

	u8	pkt_hdrlen;	//the original 802.3 pkt header len
	u8	hdrlen;		//the WLAN Header Len
	u8	nr_frags;
	u8	ack_policy;

	u8	mac_id;
	u8	vcs_mode;	//virtual carrier sense method
	u8	pctrl;//per packet txdesc control enable
	u8	qsel;

	u8	priority;
	u8	encrypt;	//when 0 indicate no encrypt. when non-zero, indicate the encrypt algorith
	u8	iv_len;
	u8	icv_len;
	unsigned char iv[8];
	unsigned char icv[8];	
	
	u8 	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8 	ra[ETH_ALEN];

	struct sta_info *psta;
#ifdef CONFIG_RTL8712_TCP_CSUM_OFFLOAD_TX
	u8 hw_tcp_csum;
#endif
};


#define WLANHDR_OFFSET	64

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

	u8 *pallocated_buf;
	u8 *pbuf;

	void *priv_data;

#ifdef CONFIG_USB_HCI	
	//u8 *mem_addr;//removed
	//u32 sz[8];

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX)
	PURB	pxmit_urb[8];
#endif

#ifdef PLATFORM_OS_XP
	PIRP		pxmit_irp[8];
#endif
	//u8 bpending[8];
	//sint ac_tag[8];//removed
	//u8 last[8];//removed
	//uint irpcnt;//can be removed
	//uint fragcnt;//can be removed
#endif	
	   
#ifdef CONFIG_SDIO_HCI
	u32  len;	
	u8 *phead;
	u8 *pdata;
	u8 *ptail;
	u8 *pend;
	u32 ff_hwaddr;
#ifdef PLATFORM_OS_XP
	PMDL pxmitbuf_mdl;
	PIRP  pxmitbuf_irp; 
	PSDBUS_REQUEST_PACKET pxmitbuf_sdrp;
#endif	
#endif

	u32 aggr_nr;
};

struct xmit_frame
{
	_list list;

	struct pkt_attrib attrib;

	_pkt *pkt;

	int frame_tag;

	_adapter *padapter;

	 u8 *buf_addr;

	 struct xmit_buf *pxmitbuf;

#ifdef CONFIG_SDIO_HCI
	u8 pg_num;
#endif

#ifdef CONFIG_USB_HCI

	//insert urb, irp, and irpcnt info below...      
	//max frag_cnt = 8 

	u8 *mem_addr;
	u16 sz[8];

#if defined(PLATFORM_OS_XP)||defined(PLATFORM_LINUX)
	PURB	pxmit_urb[8];
#endif

#ifdef PLATFORM_WINDOWS
	PIRP	pxmit_irp[8];
#endif
	u8 bpending[8];
	u8 last[8];
	//sint ac_tag[8];
	//uint irpcnt;
	//uint fragcnt;
#endif
	
	//uint	mem[(MAX_XMITBUF_SZ >> 2)];
	//uint	mem[1];
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

	uint	sta_tx_bytes;
	u64	sta_tx_pkts;
	uint	sta_tx_fail;

};


struct	hw_txqueue {
	volatile sint	head;
	volatile sint	tail;
	volatile sint 	free_sz;	//in units of 64 bytes
	//volatile sint	budget_sz;
	volatile sint      free_cmdsz;
	volatile sint	 txsz[8];
	uint	ff_hwaddr;
	uint	cmd_hwaddr;
	sint	ac_tag;
};


struct	xmit_priv {
	
	_lock	lock;

	_sema	xmit_sema;
	_sema	terminate_xmitthread_sema;
	
	//_queue	blk_strms[MAX_NUMBLKS];
	_queue	be_pending;
	_queue	bk_pending;
	_queue	vi_pending;
	_queue	vo_pending;
	_queue	bm_pending;
	
	_queue	legacy_dz_queue;
	_queue	apsd_queue;
	
	u8 *pallocated_frame_buf;
	u8 *pxmit_frame_buf;
	uint free_xmitframe_cnt;

	uint mapping_addr;
	uint pkt_sz;	
	
	_queue	free_xmit_queue;
	

	struct	hw_txqueue	be_txqueue;
	struct	hw_txqueue	bk_txqueue;
	struct	hw_txqueue	vi_txqueue;
	struct	hw_txqueue	vo_txqueue;
	struct	hw_txqueue	bmc_txqueue;

	uint	frag_len;

	_adapter	*adapter;
	
	u8   vcs_setting;
	u8	vcs;
	u8	vcs_type;
	u16  rts_thresh;
	
	uint	tx_bytes;
	u64	tx_pkts;
	uint	tx_drop;
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
	_workitem xmit_pipe4_reset_wi;
	_workitem xmit_pipe6_reset_wi;
	_workitem xmit_piped_reset_wi;

#endif
	//per AC pending irp
	int beq_cnt;
	int bkq_cnt;
	int viq_cnt;
	int voq_cnt;
	
#endif

#ifdef CONFIG_RTL8712
	_queue	free_amsdu_xmit_queue;
	u8 *pallocated_amsdu_frame_buf;
	u8 *pxmit_amsdu_frame_buf;
	uint free_amsdu_xmitframe_cnt;

	_queue free_txagg_xmit_queue;
	u8 *pallocated_txagg_frame_buf;
	u8 *pxmit_txagg_frame_buf;
	uint free_txagg_xmitframe_cnt;	

	int cmdseq;
#endif
#ifdef CONFIG_SDIO_HCI
	u8 free_pg[8];
	u8	public_pgsz;
	u8	required_pgsz;
	u8	used_pgsz;
	u8	init_pgsz;
#ifdef PLATFORM_OS_XP
	PMDL prd_freesz_mdl[2];
	u8 brd_freesz_pending[2];
	PIRP  prd_freesz_irp[2]; 
	PSDBUS_REQUEST_PACKET prd_freesz_sdrp[2];
	u8 rd_freesz_irp_idx;
#endif

#endif


	_queue free_xmitbuf_queue;
	_queue pending_xmitbuf_queue;
	u8 *pallocated_xmitbuf;
	u8 *pxmitbuf;
	uint free_xmitbuf_cnt;	


};


static __inline _queue *get_free_xmit_queue(struct	xmit_priv	*pxmitpriv)
{
	return &(pxmitpriv->free_xmit_queue);
}


extern int free_xmitbuf(struct xmit_priv *pxmitpriv, struct xmit_buf *pxmitbuf);
extern struct xmit_buf *alloc_xmitbuf(struct xmit_priv *pxmitpriv);

extern void update_protection(_adapter *padapter, u8 *ie, uint ie_len);
extern struct xmit_frame *alloc_xmitframe(struct xmit_priv *pxmitpriv);
extern sint make_wlanhdr (_adapter *padapter, unsigned char *hdr, struct pkt_attrib *pattrib);
extern sint rtl8711_put_snap(u8 *data, u16 h_proto);
extern sint free_xmitframe(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe);
extern void free_xmitframe_queue(struct xmit_priv *pxmitpriv, _queue *pframequeue );
extern sint xmit_classifier(_adapter *padapter, struct xmit_frame *pxmitframe);
extern thread_return xmit_thread(thread_context context);
extern sint xmitframe_coalesce(_adapter *padapter, _pkt *pkt, struct xmit_frame *pxmitframe);

sint _init_hw_txqueue(struct hw_txqueue* phw_txqueue, u8 ac_tag);
void	_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv);
sint update_attrib(_adapter *padapter, _pkt *pkt, struct pkt_attrib *pattrib);


sint txframes_pending(_adapter *padapter);
int txframes_sta_ac_pending(_adapter *padapter, struct pkt_attrib *pattrib);
void init_hwxmits(struct hw_xmit *phwxmit, sint entry);


sint _init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter);
void _free_xmit_priv (struct xmit_priv *pxmitpriv);



//new added for 871x
int init_xmit_priv(struct xmit_priv *pxmitpriv, _adapter *padapter);
void free_xmit_priv (struct xmit_priv *pxmitpriv);

void alloc_hwxmits(_adapter *padapter);
void free_hwxmits(_adapter *padapter);

struct xmit_frame *alloc_xmitframe_ex(struct xmit_priv *pxmitpriv, int tag);
int free_xmitframe_ex(struct xmit_priv *pxmitpriv, struct xmit_frame *pxmitframe);

int pre_xmit(_adapter *padapter, struct xmit_frame *pxmitframe);
void check_xmit(_adapter *padapter);
int check_xmit_resource(_adapter *padapter, struct xmit_frame *pxmitframe);
int xmit_enqueue(_adapter *padapter, struct xmit_frame *pxmitframe);
int xmit_direct(_adapter *padapter, struct xmit_frame *pxmitframe);

#if defined (CONFIG_USB_HCI) && defined(PLATFORM_LINUX)
extern void xmit_bh(void *priv);
#endif

void xmitframe_xmitbuf_attach(struct xmit_frame *pxmitframe, struct xmit_buf *pxmitbuf);


#ifdef CONFIG_RTL8712
#include "rtl8712_xmit.h"
#endif

#endif	//_RTL871X_XMIT_H_

