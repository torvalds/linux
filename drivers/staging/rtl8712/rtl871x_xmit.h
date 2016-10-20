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
 * Modifications for inclusion into the Linux staging tree are
 * Copyright(c) 2010 Larry Finger. All rights reserved.
 *
 * Contact information:
 * WLAN FAE <wlanfae@realtek.com>
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 ******************************************************************************/
#ifndef _RTL871X_XMIT_H_
#define _RTL871X_XMIT_H_

#include "osdep_service.h"
#include "drv_types.h"
#include "xmit_osdep.h"

#ifdef CONFIG_R8712_TX_AGGR
#define MAX_XMITBUF_SZ  (16384)
#else
#define MAX_XMITBUF_SZ  (2048)
#endif

#define NR_XMITBUFF     (4)

#ifdef CONFIG_R8712_TX_AGGR
#define AGGR_NR_HIGH_BOUND      (4) /*(8) */
#define AGGR_NR_LOW_BOUND       (2)
#endif

#define XMITBUF_ALIGN_SZ 512
#define TX_GUARD_BAND		5
#define MAX_NUMBLKS		(1)

/* Fixed the Big Endian bug when using the software driver encryption.*/
#define WEP_IV(pattrib_iv, txpn, keyidx)\
do { \
	pattrib_iv[0] = txpn._byte_.TSC0;\
	pattrib_iv[1] = txpn._byte_.TSC1;\
	pattrib_iv[2] = txpn._byte_.TSC2;\
	pattrib_iv[3] = ((keyidx & 0x3)<<6);\
	txpn.val = (txpn.val == 0xffffff) ? 0 : (txpn.val+1);\
} while (0)

/* Fixed the Big Endian bug when doing the Tx.
 * The Linksys WRH54G will check this.
 */
#define TKIP_IV(pattrib_iv, txpn, keyidx)\
do { \
	pattrib_iv[0] = txpn._byte_.TSC1;\
	pattrib_iv[1] = (txpn._byte_.TSC1 | 0x20) & 0x7f;\
	pattrib_iv[2] = txpn._byte_.TSC0;\
	pattrib_iv[3] = BIT(5) | ((keyidx & 0x3)<<6);\
	pattrib_iv[4] = txpn._byte_.TSC2;\
	pattrib_iv[5] = txpn._byte_.TSC3;\
	pattrib_iv[6] = txpn._byte_.TSC4;\
	pattrib_iv[7] = txpn._byte_.TSC5;\
	txpn.val = txpn.val == 0xffffffffffffULL ? 0 : \
	(txpn.val+1);\
} while (0)

#define AES_IV(pattrib_iv, txpn, keyidx)\
do { \
	pattrib_iv[0] = txpn._byte_.TSC0;\
	pattrib_iv[1] = txpn._byte_.TSC1;\
	pattrib_iv[2] = 0;\
	pattrib_iv[3] = BIT(5) | ((keyidx & 0x3)<<6);\
	pattrib_iv[4] = txpn._byte_.TSC2;\
	pattrib_iv[5] = txpn._byte_.TSC3;\
	pattrib_iv[6] = txpn._byte_.TSC4;\
	pattrib_iv[7] = txpn._byte_.TSC5;\
	txpn.val = txpn.val == 0xffffffffffffULL ? 0 : \
	(txpn.val+1);\
} while (0)

struct hw_xmit {
	spinlock_t xmit_lock;
	struct list_head pending;
	struct  __queue *sta_queue;
	struct hw_txqueue *phwtxqueue;
	sint	txcmdcnt;
	int	accnt;
};

struct pkt_attrib {
	u8	type;
	u8	subtype;
	u8	bswenc;
	u8	dhcp_pkt;

	u16	seqnum;
	u16	ether_type;
	u16	pktlen;		/* the original 802.3 pkt raw_data len
				 * (not include ether_hdr data)
				 */
	u16	last_txcmdsz;

	u8	pkt_hdrlen;	/*the original 802.3 pkt header len*/
	u8	hdrlen;		/*the WLAN Header Len*/
	u8	nr_frags;
	u8	ack_policy;
	u8	mac_id;
	u8	vcs_mode;	/*virtual carrier sense method*/
	u8	pctrl;/*per packet txdesc control enable*/
	u8	qsel;

	u8	priority;
	u8	encrypt;	/* when 0 indicate no encrypt. when non-zero,
				 * indicate the encrypt algorithm
				 */
	u8	iv_len;
	u8	icv_len;
	unsigned char iv[8];
	unsigned char icv[8];
	u8	dst[ETH_ALEN];
	u8	src[ETH_ALEN];
	u8	ta[ETH_ALEN];
	u8	ra[ETH_ALEN];
	struct sta_info *psta;
};

#define WLANHDR_OFFSET	64
#define DATA_FRAMETAG		0x01
#define L2_FRAMETAG		0x02
#define MGNT_FRAMETAG		0x03
#define AMSDU_FRAMETAG	0x04
#define EII_FRAMETAG		0x05
#define IEEE8023_FRAMETAG  0x06
#define MP_FRAMETAG		0x07
#define TXAGG_FRAMETAG	0x08

struct xmit_buf {
	struct list_head list;

	u8 *pallocated_buf;
	u8 *pbuf;
	void *priv_data;
	struct urb *pxmit_urb[8];
	u32 aggr_nr;
};

struct xmit_frame {
	struct list_head list;
	struct pkt_attrib attrib;
	_pkt *pkt;
	int frame_tag;
	struct _adapter *padapter;
	 u8 *buf_addr;
	 struct xmit_buf *pxmitbuf;
	u8 *mem_addr;
	u16 sz[8];
	struct urb *pxmit_urb[8];
	u8 bpending[8];
	u8 last[8];
};

struct tx_servq {
	struct list_head tx_pending;
	struct  __queue	sta_pending;
	int qcnt;
};

struct sta_xmit_priv {
	spinlock_t lock;
	sint	option;
	sint	apsd_setting;	/* When bit mask is on, the associated edca
				 * queue supports APSD.
				 */
	struct tx_servq	be_q;	/* priority == 0,3 */
	struct tx_servq	bk_q;	/* priority == 1,2*/
	struct tx_servq	vi_q;	/*priority == 4,5*/
	struct tx_servq	vo_q;	/*priority == 6,7*/
	struct list_head  legacy_dz;
	struct list_head apsd;
	u16 txseq_tid[16];
	uint	sta_tx_bytes;
	u64	sta_tx_pkts;
	uint	sta_tx_fail;
};

struct	hw_txqueue {
	/*volatile*/ sint	head;
	/*volatile*/ sint	tail;
	/*volatile*/ sint	free_sz;	/*in units of 64 bytes*/
	/*volatile*/ sint      free_cmdsz;
	/*volatile*/ sint	 txsz[8];
	uint	ff_hwaddr;
	uint	cmd_hwaddr;
	sint	ac_tag;
};

struct	xmit_priv {
	spinlock_t lock;
	struct  __queue	be_pending;
	struct  __queue	bk_pending;
	struct  __queue	vi_pending;
	struct  __queue	vo_pending;
	struct  __queue	bm_pending;
	struct  __queue	legacy_dz_queue;
	struct  __queue	apsd_queue;
	u8 *pallocated_frame_buf;
	u8 *pxmit_frame_buf;
	uint free_xmitframe_cnt;
	uint mapping_addr;
	uint pkt_sz;
	struct  __queue	free_xmit_queue;
	struct	hw_txqueue	be_txqueue;
	struct	hw_txqueue	bk_txqueue;
	struct	hw_txqueue	vi_txqueue;
	struct	hw_txqueue	vo_txqueue;
	struct	hw_txqueue	bmc_txqueue;
	uint	frag_len;
	struct _adapter	*adapter;
	u8   vcs_setting;
	u8	vcs;
	u8	vcs_type;
	u16  rts_thresh;
	uint	tx_bytes;
	u64	tx_pkts;
	uint	tx_drop;
	struct hw_xmit *hwxmits;
	u8	hwxmit_entry;
	u8	txirp_cnt;
	struct tasklet_struct xmit_tasklet;
	struct work_struct xmit_pipe4_reset_wi;
	struct work_struct xmit_pipe6_reset_wi;
	struct work_struct xmit_piped_reset_wi;
	/*per AC pending irp*/
	int beq_cnt;
	int bkq_cnt;
	int viq_cnt;
	int voq_cnt;
	struct  __queue	free_amsdu_xmit_queue;
	u8 *pallocated_amsdu_frame_buf;
	u8 *pxmit_amsdu_frame_buf;
	uint free_amsdu_xmitframe_cnt;
	struct  __queue free_txagg_xmit_queue;
	u8 *pallocated_txagg_frame_buf;
	u8 *pxmit_txagg_frame_buf;
	uint free_txagg_xmitframe_cnt;
	int cmdseq;
	struct  __queue free_xmitbuf_queue;
	struct  __queue pending_xmitbuf_queue;
	u8 *pallocated_xmitbuf;
	u8 *pxmitbuf;
	uint free_xmitbuf_cnt;
};

static inline struct  __queue *get_free_xmit_queue(
				struct xmit_priv *pxmitpriv)
{
	return &(pxmitpriv->free_xmit_queue);
}

int r8712_free_xmitbuf(struct xmit_priv *pxmitpriv,
		       struct xmit_buf *pxmitbuf);
struct xmit_buf *r8712_alloc_xmitbuf(struct xmit_priv *pxmitpriv);
void r8712_update_protection(struct _adapter *padapter, u8 *ie, uint ie_len);
struct xmit_frame *r8712_alloc_xmitframe(struct xmit_priv *pxmitpriv);
void r8712_free_xmitframe(struct xmit_priv *pxmitpriv,
			  struct xmit_frame *pxmitframe);
void r8712_free_xmitframe_queue(struct xmit_priv *pxmitpriv,
				struct  __queue *pframequeue);
sint r8712_xmit_classifier(struct _adapter *padapter,
			    struct xmit_frame *pxmitframe);
sint r8712_xmitframe_coalesce(struct _adapter *padapter, _pkt *pkt,
			      struct xmit_frame *pxmitframe);
sint _r8712_init_hw_txqueue(struct hw_txqueue *phw_txqueue, u8 ac_tag);
void _r8712_init_sta_xmit_priv(struct sta_xmit_priv *psta_xmitpriv);
sint r8712_update_attrib(struct _adapter *padapter, _pkt *pkt,
			 struct pkt_attrib *pattrib);
int r8712_txframes_sta_ac_pending(struct _adapter *padapter,
				  struct pkt_attrib *pattrib);
sint _r8712_init_xmit_priv(struct xmit_priv *pxmitpriv,
			   struct _adapter *padapter);
void _free_xmit_priv(struct xmit_priv *pxmitpriv);
void r8712_free_xmitframe_ex(struct xmit_priv *pxmitpriv,
			     struct xmit_frame *pxmitframe);
int r8712_pre_xmit(struct _adapter *padapter, struct xmit_frame *pxmitframe);
int r8712_xmit_enqueue(struct _adapter *padapter,
		       struct xmit_frame *pxmitframe);
int r8712_xmit_direct(struct _adapter *padapter, struct xmit_frame *pxmitframe);
void r8712_xmit_bh(void *priv);

void xmitframe_xmitbuf_attach(struct xmit_frame *pxmitframe,
			struct xmit_buf *pxmitbuf);

#include "rtl8712_xmit.h"

#endif	/*_RTL871X_XMIT_H_*/

