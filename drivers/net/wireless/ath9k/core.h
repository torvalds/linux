/*
 * Copyright (c) 2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef CORE_H
#define CORE_H

#include <linux/version.h>
#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/in.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/list.h>
#include <asm/byteorder.h>
#include <linux/scatterlist.h>
#include <asm/page.h>
#include <net/mac80211.h>
#include <linux/leds.h>
#include <linux/rfkill.h>

#include "ath9k.h"
#include "rc.h"

struct ath_node;

/******************/
/* Utility macros */
/******************/

/* Macro to expand scalars to 64-bit objects */

#define	ito64(x) (sizeof(x) == 8) ?			\
	(((unsigned long long int)(x)) & (0xff)) :	\
	(sizeof(x) == 16) ?				\
	(((unsigned long long int)(x)) & 0xffff) :	\
	((sizeof(x) == 32) ?				\
	 (((unsigned long long int)(x)) & 0xffffffff) : \
	 (unsigned long long int)(x))

/* increment with wrap-around */
#define INCR(_l, _sz)   do {			\
		(_l)++;				\
		(_l) &= ((_sz) - 1);		\
	} while (0)

/* decrement with wrap-around */
#define DECR(_l,  _sz)  do {			\
		(_l)--;				\
		(_l) &= ((_sz) - 1);		\
	} while (0)

#define A_MAX(a, b) ((a) > (b) ? (a) : (b))

#define ASSERT(exp) do {			\
		if (unlikely(!(exp))) {		\
			BUG();			\
		}				\
	} while (0)

#define TSF_TO_TU(_h,_l) \
	((((u32)(_h)) << 22) | (((u32)(_l)) >> 10))

/* XXX: remove */
#define memzero(_buf, _len) memset(_buf, 0, _len)

#define ATH9K_BH_STATUS_INTACT		0
#define ATH9K_BH_STATUS_CHANGE		1

#define	ATH_TXQ_SETUP(sc, i)        ((sc)->sc_txqsetup & (1<<i))

static inline unsigned long get_timestamp(void)
{
	return ((jiffies / HZ) * 1000) + (jiffies % HZ) * (1000 / HZ);
}

static const u8 ath_bcast_mac[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

/*************/
/* Debugging */
/*************/

enum ATH_DEBUG {
	ATH_DBG_RESET		= 0x00000001,
	ATH_DBG_PHY_IO		= 0x00000002,
	ATH_DBG_REG_IO		= 0x00000004,
	ATH_DBG_QUEUE		= 0x00000008,
	ATH_DBG_EEPROM		= 0x00000010,
	ATH_DBG_NF_CAL		= 0x00000020,
	ATH_DBG_CALIBRATE	= 0x00000040,
	ATH_DBG_CHANNEL		= 0x00000080,
	ATH_DBG_INTERRUPT	= 0x00000100,
	ATH_DBG_REGULATORY	= 0x00000200,
	ATH_DBG_ANI		= 0x00000400,
	ATH_DBG_POWER_MGMT	= 0x00000800,
	ATH_DBG_XMIT		= 0x00001000,
	ATH_DBG_BEACON		= 0x00002000,
	ATH_DBG_RATE		= 0x00004000,
	ATH_DBG_CONFIG		= 0x00008000,
	ATH_DBG_KEYCACHE	= 0x00010000,
	ATH_DBG_AGGR		= 0x00020000,
	ATH_DBG_FATAL		= 0x00040000,
	ATH_DBG_ANY		= 0xffffffff
};

#define DBG_DEFAULT (ATH_DBG_FATAL)

#define	DPRINTF(sc, _m, _fmt, ...) do {			\
		if (sc->sc_debug & (_m))                \
			printk(_fmt , ##__VA_ARGS__);	\
	} while (0)

/***************************/
/* Load-time Configuration */
/***************************/

/* Per-instance load-time (note: NOT run-time) configurations
 * for Atheros Device */
struct ath_config {
	u32 ath_aggr_prot;
	u16 txpowlimit;
	u16 txpowlimit_override;
	u8 cabqReadytime; /* Cabq Readytime % */
	u8 swBeaconProcess; /* Process received beacons in SW (vs HW) */
};

/***********************/
/* Chainmask Selection */
/***********************/

#define ATH_CHAINMASK_SEL_TIMEOUT	   6000
/* Default - Number of last RSSI values that is used for
 * chainmask selection */
#define ATH_CHAINMASK_SEL_RSSI_CNT	   10
/* Means use 3x3 chainmask instead of configured chainmask */
#define ATH_CHAINMASK_SEL_3X3		   7
/* Default - Rssi threshold below which we have to switch to 3x3 */
#define ATH_CHAINMASK_SEL_UP_RSSI_THRES	   20
/* Default - Rssi threshold above which we have to switch to
 * user configured values */
#define ATH_CHAINMASK_SEL_DOWN_RSSI_THRES  35
/* Struct to store the chainmask select related info */
struct ath_chainmask_sel {
	struct timer_list timer;
	int cur_tx_mask; 	/* user configured or 3x3 */
	int cur_rx_mask; 	/* user configured or 3x3 */
	int tx_avgrssi;
	u8 switch_allowed:1, 	/* timer will set this */
	   cm_sel_enabled : 1;
};

int ath_chainmask_sel_logic(struct ath_softc *sc, struct ath_node *an);
void ath_update_chainmask(struct ath_softc *sc, int is_ht);

/*************************/
/* Descriptor Management */
/*************************/

#define ATH_TXBUF_RESET(_bf) do {				\
		(_bf)->bf_status = 0;				\
		(_bf)->bf_lastbf = NULL;			\
		(_bf)->bf_lastfrm = NULL;			\
		(_bf)->bf_next = NULL;				\
		memzero(&((_bf)->bf_state),			\
			    sizeof(struct ath_buf_state));	\
	} while (0)

enum buffer_type {
	BUF_DATA		= BIT(0),
	BUF_AGGR		= BIT(1),
	BUF_AMPDU		= BIT(2),
	BUF_HT			= BIT(3),
	BUF_RETRY		= BIT(4),
	BUF_XRETRY		= BIT(5),
	BUF_SHORT_PREAMBLE	= BIT(6),
	BUF_BAR			= BIT(7),
	BUF_PSPOLL		= BIT(8),
	BUF_AGGR_BURST		= BIT(9),
	BUF_CALC_AIRTIME	= BIT(10),
};

struct ath_buf_state {
	int bfs_nframes;			/* # frames in aggregate */
	u16 bfs_al;				/* length of aggregate */
	u16 bfs_frmlen;				/* length of frame */
	int bfs_seqno;				/* sequence number */
	int bfs_tidno;				/* tid of this frame */
	int bfs_retries;			/* current retries */
	struct ath_rc_series bfs_rcs[4];	/* rate series */
	u32 bf_type;				/* BUF_* (enum buffer_type) */
	/* key type use to encrypt this frame */
	enum ath9k_key_type bfs_keytype;
};

#define bf_nframes      	bf_state.bfs_nframes
#define bf_al           	bf_state.bfs_al
#define bf_frmlen       	bf_state.bfs_frmlen
#define bf_retries      	bf_state.bfs_retries
#define bf_seqno        	bf_state.bfs_seqno
#define bf_tidno        	bf_state.bfs_tidno
#define bf_rcs          	bf_state.bfs_rcs
#define bf_keytype      	bf_state.bfs_keytype
#define bf_isdata(bf)		(bf->bf_state.bf_type & BUF_DATA)
#define bf_isaggr(bf)		(bf->bf_state.bf_type & BUF_AGGR)
#define bf_isampdu(bf)		(bf->bf_state.bf_type & BUF_AMPDU)
#define bf_isht(bf)		(bf->bf_state.bf_type & BUF_HT)
#define bf_isretried(bf)	(bf->bf_state.bf_type & BUF_RETRY)
#define bf_isxretried(bf)	(bf->bf_state.bf_type & BUF_XRETRY)
#define bf_isshpreamble(bf)	(bf->bf_state.bf_type & BUF_SHORT_PREAMBLE)
#define bf_isbar(bf)		(bf->bf_state.bf_type & BUF_BAR)
#define bf_ispspoll(bf) 	(bf->bf_state.bf_type & BUF_PSPOLL)
#define bf_isaggrburst(bf)	(bf->bf_state.bf_type & BUF_AGGR_BURST)

/*
 * Abstraction of a contiguous buffer to transmit/receive.  There is only
 * a single hw descriptor encapsulated here.
 */
struct ath_buf {
	struct list_head list;
	struct list_head *last;
	struct ath_buf *bf_lastbf;	/* last buf of this unit (a frame or
					   an aggregate) */
	struct ath_buf *bf_lastfrm;	/* last buf of this frame */
	struct ath_buf *bf_next;	/* next subframe in the aggregate */
	struct ath_buf *bf_rifslast;	/* last buf for RIFS burst */
	void *bf_mpdu;			/* enclosing frame structure */
	void *bf_node;			/* pointer to the node */
	struct ath_desc *bf_desc;	/* virtual addr of desc */
	dma_addr_t bf_daddr;		/* physical addr of desc */
	dma_addr_t bf_buf_addr;		/* physical addr of data buffer */
	u32 bf_status;
	u16 bf_flags;			/* tx descriptor flags */
	struct ath_buf_state bf_state;	/* buffer state */
	dma_addr_t bf_dmacontext;
};

/*
 * reset the rx buffer.
 * any new fields added to the athbuf and require
 * reset need to be added to this macro.
 * currently bf_status is the only one requires that
 * requires reset.
 */
#define ATH_RXBUF_RESET(_bf)    ((_bf)->bf_status = 0)

/* hw processing complete, desc processed by hal */
#define ATH_BUFSTATUS_DONE      0x00000001
/* hw processing complete, desc hold for hw */
#define ATH_BUFSTATUS_STALE     0x00000002
/* Rx-only: OS is done with this packet and it's ok to queued it to hw */
#define ATH_BUFSTATUS_FREE      0x00000004

/* DMA state for tx/rx descriptors */

struct ath_descdma {
	const char *dd_name;
	struct ath_desc *dd_desc;	/* descriptors  */
	dma_addr_t dd_desc_paddr;	/* physical addr of dd_desc  */
	u32 dd_desc_len;		/* size of dd_desc  */
	struct ath_buf *dd_bufptr;	/* associated buffers */
	dma_addr_t dd_dmacontext;
};

/* Abstraction of a received RX MPDU/MMPDU, or a RX fragment */

struct ath_rx_context {
	struct ath_buf *ctx_rxbuf;	/* associated ath_buf for rx */
};
#define ATH_RX_CONTEXT(skb) ((struct ath_rx_context *)skb->cb)

int ath_descdma_setup(struct ath_softc *sc,
		      struct ath_descdma *dd,
		      struct list_head *head,
		      const char *name,
		      int nbuf,
		      int ndesc);
int ath_desc_alloc(struct ath_softc *sc);
void ath_desc_free(struct ath_softc *sc);
void ath_descdma_cleanup(struct ath_softc *sc,
			 struct ath_descdma *dd,
			 struct list_head *head);

/******/
/* RX */
/******/

#define ATH_MAX_ANTENNA          3
#define ATH_RXBUF                512
#define ATH_RX_TIMEOUT           40      /* 40 milliseconds */
#define WME_NUM_TID              16
#define IEEE80211_BAR_CTL_TID_M  0xF000  /* tid mask */
#define IEEE80211_BAR_CTL_TID_S  2       /* tid shift */

enum ATH_RX_TYPE {
	ATH_RX_NON_CONSUMED = 0,
	ATH_RX_CONSUMED
};

/* per frame rx status block */
struct ath_recv_status {
	u64 tsf;		/* mac tsf */
	int8_t rssi;		/* RSSI (noise floor ajusted) */
	int8_t rssictl[ATH_MAX_ANTENNA];	/* RSSI (noise floor ajusted) */
	int8_t rssiextn[ATH_MAX_ANTENNA];	/* RSSI (noise floor ajusted) */
	int8_t abs_rssi;	/* absolute RSSI */
	u8 rateieee;		/* data rate received (IEEE rate code) */
	u8 ratecode;		/* phy rate code */
	int rateKbps;		/* data rate received (Kbps) */
	int antenna;		/* rx antenna */
	int flags;		/* status of associated skb */
#define ATH_RX_FCS_ERROR        0x01
#define ATH_RX_MIC_ERROR        0x02
#define ATH_RX_DECRYPT_ERROR    0x04
#define ATH_RX_RSSI_VALID       0x08
/* if any of ctl,extn chainrssis are valid */
#define ATH_RX_CHAIN_RSSI_VALID 0x10
/* if extn chain rssis are valid */
#define ATH_RX_RSSI_EXTN_VALID  0x20
/* set if 40Mhz, clear if 20Mhz */
#define ATH_RX_40MHZ            0x40
/* set if short GI, clear if full GI */
#define ATH_RX_SHORT_GI         0x80
};

struct ath_rxbuf {
	struct sk_buff *rx_wbuf;
	unsigned long rx_time;			/* system time when received */
	struct ath_recv_status rx_status;	/* cached rx status */
};

/* Per-TID aggregate receiver state for a node */
struct ath_arx_tid {
	struct ath_node *an;
	struct ath_rxbuf *rxbuf;	/* re-ordering buffer */
	struct timer_list timer;
	spinlock_t tidlock;
	int baw_head;			/* seq_next at head */
	int baw_tail;			/* tail of block-ack window */
	int seq_reset;			/* need to reset start sequence */
	int addba_exchangecomplete;
	u16 seq_next;			/* next expected sequence */
	u16 baw_size;			/* block-ack window size */
};

/* Per-node receiver aggregate state */
struct ath_arx {
	struct ath_arx_tid tid[WME_NUM_TID];
};

int ath_startrecv(struct ath_softc *sc);
bool ath_stoprecv(struct ath_softc *sc);
void ath_flushrecv(struct ath_softc *sc);
u32 ath_calcrxfilter(struct ath_softc *sc);
void ath_rx_node_init(struct ath_softc *sc, struct ath_node *an);
void ath_rx_node_free(struct ath_softc *sc, struct ath_node *an);
void ath_rx_node_cleanup(struct ath_softc *sc, struct ath_node *an);
void ath_handle_rx_intr(struct ath_softc *sc);
int ath_rx_init(struct ath_softc *sc, int nbufs);
void ath_rx_cleanup(struct ath_softc *sc);
int ath_rx_tasklet(struct ath_softc *sc, int flush);
int ath_rx_input(struct ath_softc *sc,
		 struct ath_node *node,
		 int is_ampdu,
		 struct sk_buff *skb,
		 struct ath_recv_status *rx_status,
		 enum ATH_RX_TYPE *status);
int _ath_rx_indicate(struct ath_softc *sc,
		     struct sk_buff *skb,
		     struct ath_recv_status *status,
		     u16 keyix);
int ath_rx_subframe(struct ath_node *an, struct sk_buff *skb,
		    struct ath_recv_status *status);

/******/
/* TX */
/******/

#define ATH_TXBUF               512
/* max number of transmit attempts (tries) */
#define ATH_TXMAXTRY            13
/* max number of 11n transmit attempts (tries) */
#define ATH_11N_TXMAXTRY        10
/* max number of tries for management and control frames */
#define ATH_MGT_TXMAXTRY        4
#define WME_BA_BMP_SIZE         64
#define WME_MAX_BA              WME_BA_BMP_SIZE
#define ATH_TID_MAX_BUFS        (2 * WME_MAX_BA)
#define TID_TO_WME_AC(_tid)				\
	((((_tid) == 0) || ((_tid) == 3)) ? WME_AC_BE :	\
	 (((_tid) == 1) || ((_tid) == 2)) ? WME_AC_BK :	\
	 (((_tid) == 4) || ((_tid) == 5)) ? WME_AC_VI :	\
	 WME_AC_VO)


/* Wireless Multimedia Extension Defines */
#define WME_AC_BE               0 /* best effort */
#define WME_AC_BK               1 /* background */
#define WME_AC_VI               2 /* video */
#define WME_AC_VO               3 /* voice */
#define WME_NUM_AC              4

enum ATH_SM_PWRSAV{
	ATH_SM_ENABLE,
	ATH_SM_PWRSAV_STATIC,
	ATH_SM_PWRSAV_DYNAMIC,
};

/*
 * Data transmit queue state.  One of these exists for each
 * hardware transmit queue.  Packets sent to us from above
 * are assigned to queues based on their priority.  Not all
 * devices support a complete set of hardware transmit queues.
 * For those devices the array sc_ac2q will map multiple
 * priorities to fewer hardware queues (typically all to one
 * hardware queue).
 */
struct ath_txq {
	u32 axq_qnum;			/* hardware q number */
	u32 *axq_link;			/* link ptr in last TX desc */
	struct list_head axq_q;		/* transmit queue */
	spinlock_t axq_lock;
	unsigned long axq_lockflags;	/* intr state when must cli */
	u32 axq_depth;			/* queue depth */
	u8 axq_aggr_depth;		/* aggregates queued */
	u32 axq_totalqueued;		/* total ever queued */

	/* count to determine if descriptor should generate int on this txq. */
	u32 axq_intrcnt;

	bool stopped;			/* Is mac80211 queue stopped ? */
	struct ath_buf *axq_linkbuf;	/* virtual addr of last buffer*/

	/* first desc of the last descriptor that contains CTS */
	struct ath_desc *axq_lastdsWithCTS;

	/* final desc of the gating desc that determines whether
	   lastdsWithCTS has been DMA'ed or not */
	struct ath_desc *axq_gatingds;

	struct list_head axq_acq;
};

/* per TID aggregate tx state for a destination */
struct ath_atx_tid {
	struct list_head list;		/* round-robin tid entry */
	struct list_head buf_q;		/* pending buffers */
	struct ath_node *an;
	struct ath_atx_ac *ac;
	struct ath_buf *tx_buf[ATH_TID_MAX_BUFS]; /* active tx frames */
	u16 seq_start;
	u16 seq_next;
	u16 baw_size;
	int tidno;
	int baw_head;			/* first un-acked tx buffer */
	int baw_tail;			/* next unused tx buffer slot */
	int sched;
	int paused;
	int cleanup_inprogress;
	u32 addba_exchangecomplete:1;
	int32_t addba_exchangeinprogress;
	int addba_exchangeattempts;
};

/* per access-category aggregate tx state for a destination */
struct ath_atx_ac {
	int sched;			/* dest-ac is scheduled */
	int qnum;			/* H/W queue number associated
					   with this AC */
	struct list_head list;		/* round-robin txq entry */
	struct list_head tid_q;		/* queue of TIDs with buffers */
};

/* per dest tx state */
struct ath_atx {
	struct ath_atx_tid tid[WME_NUM_TID];
	struct ath_atx_ac ac[WME_NUM_AC];
};

/* per-frame tx control block */
struct ath_tx_control {
	struct ath_node *an;
	int if_id;
	int qnum;
	u32 ht:1;
	u32 ps:1;
	u32 use_minrate:1;
	enum ath9k_pkt_type atype;
	enum ath9k_key_type keytype;
	u32 flags;
	u16 seqno;
	u16 tidno;
	u16 txpower;
	u16 frmlen;
	u32 keyix;
	int min_rate;
	int mcast_rate;
	struct ath_softc *dev;
	dma_addr_t dmacontext;
};

/* per frame tx status block */
struct ath_xmit_status {
	int retries;	/* number of retries to successufully
			   transmit this frame */
	int flags;	/* status of transmit */
#define ATH_TX_ERROR        0x01
#define ATH_TX_XRETRY       0x02
#define ATH_TX_BAR          0x04
};

struct ath_tx_stat {
	int rssi;		/* RSSI (noise floor ajusted) */
	int rssictl[ATH_MAX_ANTENNA];	/* RSSI (noise floor ajusted) */
	int rssiextn[ATH_MAX_ANTENNA];	/* RSSI (noise floor ajusted) */
	int rateieee;		/* data rate xmitted (IEEE rate code) */
	int rateKbps;		/* data rate xmitted (Kbps) */
	int ratecode;		/* phy rate code */
	int flags;		/* validity flags */
/* if any of ctl,extn chain rssis are valid */
#define ATH_TX_CHAIN_RSSI_VALID 0x01
/* if extn chain rssis are valid */
#define ATH_TX_RSSI_EXTN_VALID  0x02
	u32 airtime;	/* time on air per final tx rate */
};

struct ath_txq *ath_txq_setup(struct ath_softc *sc, int qtype, int subtype);
void ath_tx_cleanupq(struct ath_softc *sc, struct ath_txq *txq);
int ath_tx_setup(struct ath_softc *sc, int haltype);
void ath_draintxq(struct ath_softc *sc, bool retry_tx);
void ath_tx_draintxq(struct ath_softc *sc,
		     struct ath_txq *txq, bool retry_tx);
void ath_tx_node_init(struct ath_softc *sc, struct ath_node *an);
void ath_tx_node_cleanup(struct ath_softc *sc,
			 struct ath_node *an, bool bh_flag);
void ath_tx_node_free(struct ath_softc *sc, struct ath_node *an);
void ath_txq_schedule(struct ath_softc *sc, struct ath_txq *txq);
int ath_tx_init(struct ath_softc *sc, int nbufs);
int ath_tx_cleanup(struct ath_softc *sc);
int ath_tx_get_qnum(struct ath_softc *sc, int qtype, int haltype);
int ath_txq_update(struct ath_softc *sc, int qnum,
		   struct ath9k_tx_queue_info *q);
int ath_tx_start(struct ath_softc *sc, struct sk_buff *skb);
void ath_tx_tasklet(struct ath_softc *sc);
u32 ath_txq_depth(struct ath_softc *sc, int qnum);
u32 ath_txq_aggr_depth(struct ath_softc *sc, int qnum);
void ath_notify_txq_status(struct ath_softc *sc, u16 queue_depth);
void ath_tx_complete(struct ath_softc *sc, struct sk_buff *skb,
		     struct ath_xmit_status *tx_status, struct ath_node *an);
void ath_tx_cabq(struct ath_softc *sc, struct sk_buff *skb);

/**********************/
/* Node / Aggregation */
/**********************/

/* indicates the node is clened up */
#define ATH_NODE_CLEAN          0x1
/* indicates the node is 80211 power save */
#define ATH_NODE_PWRSAVE        0x2

#define ADDBA_EXCHANGE_ATTEMPTS    10
#define ATH_AGGR_DELIM_SZ          4   /* delimiter size   */
#define ATH_AGGR_MINPLEN           256 /* in bytes, minimum packet length */
/* number of delimiters for encryption padding */
#define ATH_AGGR_ENCRYPTDELIM      10
/* minimum h/w qdepth to be sustained to maximize aggregation */
#define ATH_AGGR_MIN_QDEPTH        2
#define ATH_AMPDU_SUBFRAME_DEFAULT 32
#define IEEE80211_SEQ_SEQ_SHIFT    4
#define IEEE80211_SEQ_MAX          4096
#define IEEE80211_MIN_AMPDU_BUF    0x8

/* return whether a bit at index _n in bitmap _bm is set
 * _sz is the size of the bitmap  */
#define ATH_BA_ISSET(_bm, _n)  (((_n) < (WME_BA_BMP_SIZE)) &&		\
				((_bm)[(_n) >> 5] & (1 << ((_n) & 31))))

/* return block-ack bitmap index given sequence and starting sequence */
#define ATH_BA_INDEX(_st, _seq) (((_seq) - (_st)) & (IEEE80211_SEQ_MAX - 1))

/* returns delimiter padding required given the packet length */
#define ATH_AGGR_GET_NDELIM(_len)					\
	(((((_len) + ATH_AGGR_DELIM_SZ) < ATH_AGGR_MINPLEN) ?           \
	  (ATH_AGGR_MINPLEN - (_len) - ATH_AGGR_DELIM_SZ) : 0) >> 2)

#define BAW_WITHIN(_start, _bawsz, _seqno) \
	((((_seqno) - (_start)) & 4095) < (_bawsz))

#define ATH_DS_BA_SEQ(_ds)               ((_ds)->ds_us.tx.ts_seqnum)
#define ATH_DS_BA_BITMAP(_ds)            (&(_ds)->ds_us.tx.ba_low)
#define ATH_DS_TX_BA(_ds)	((_ds)->ds_us.tx.ts_flags & ATH9K_TX_BA)
#define ATH_AN_2_TID(_an, _tidno)        (&(_an)->an_aggr.tx.tid[(_tidno)])

enum ATH_AGGR_STATUS {
	ATH_AGGR_DONE,
	ATH_AGGR_BAW_CLOSED,
	ATH_AGGR_LIMITED,
	ATH_AGGR_SHORTPKT,
	ATH_AGGR_8K_LIMITED,
};

enum ATH_AGGR_CHECK {
	AGGR_NOT_REQUIRED,
	AGGR_REQUIRED,
	AGGR_CLEANUP_PROGRESS,
	AGGR_EXCHANGE_PROGRESS,
	AGGR_EXCHANGE_DONE
};

struct aggr_rifs_param {
	int param_max_frames;
	int param_max_len;
	int param_rl;
	int param_al;
	struct ath_rc_series *param_rcs;
};

/* Per-node aggregation state */
struct ath_node_aggr {
	struct ath_atx tx;	/* node transmit state */
	struct ath_arx rx;	/* node receive state */
};

/* driver-specific node state */
struct ath_node {
	struct list_head list;
	struct ath_softc *an_sc;
	atomic_t an_refcnt;
	struct ath_chainmask_sel an_chainmask_sel;
	struct ath_node_aggr an_aggr;
	u8 an_smmode; /* SM Power save mode */
	u8 an_flags;
	u8 an_addr[ETH_ALEN];
};

void ath_tx_resume_tid(struct ath_softc *sc,
	struct ath_atx_tid *tid);
enum ATH_AGGR_CHECK ath_tx_aggr_check(struct ath_softc *sc,
	struct ath_node *an, u8 tidno);
void ath_tx_aggr_teardown(struct ath_softc *sc,
	struct ath_node *an, u8 tidno);
void ath_rx_aggr_teardown(struct ath_softc *sc,
	struct ath_node *an, u8 tidno);
int ath_rx_aggr_start(struct ath_softc *sc,
		      const u8 *addr,
		      u16 tid,
		      u16 *ssn);
int ath_rx_aggr_stop(struct ath_softc *sc,
		     const u8 *addr,
		     u16 tid);
int ath_tx_aggr_start(struct ath_softc *sc,
		      const u8 *addr,
		      u16 tid,
		      u16 *ssn);
int ath_tx_aggr_stop(struct ath_softc *sc,
		     const u8 *addr,
		     u16 tid);
void ath_newassoc(struct ath_softc *sc,
	struct ath_node *node, int isnew, int isuapsd);
struct ath_node *ath_node_attach(struct ath_softc *sc,
	u8 addr[ETH_ALEN], int if_id);
void ath_node_detach(struct ath_softc *sc, struct ath_node *an, bool bh_flag);
struct ath_node *ath_node_get(struct ath_softc *sc, u8 addr[ETH_ALEN]);
void ath_node_put(struct ath_softc *sc, struct ath_node *an, bool bh_flag);
struct ath_node *ath_node_find(struct ath_softc *sc, u8 *addr);

/*******************/
/* Beacon Handling */
/*******************/

/*
 * Regardless of the number of beacons we stagger, (i.e. regardless of the
 * number of BSSIDs) if a given beacon does not go out even after waiting this
 * number of beacon intervals, the game's up.
 */
#define BSTUCK_THRESH           	(9 * ATH_BCBUF)
#define	ATH_BCBUF               	4   /* number of beacon buffers */
#define ATH_DEFAULT_BINTVAL     	100 /* default beacon interval in TU */
#define ATH_DEFAULT_BMISS_LIMIT 	10
#define IEEE80211_MS_TO_TU(x)           (((x) * 1000) / 1024)

/* beacon configuration */
struct ath_beacon_config {
	u16 beacon_interval;
	u16 listen_interval;
	u16 dtim_period;
	u16 bmiss_timeout;
	u8 dtim_count;
	u8 tim_offset;
	union {
		u64 last_tsf;
		u8 last_tstamp[8];
	} u; /* last received beacon/probe response timestamp of this BSS. */
};

void ath9k_beacon_tasklet(unsigned long data);
void ath_beacon_config(struct ath_softc *sc, int if_id);
int ath_beaconq_setup(struct ath_hal *ah);
int ath_beacon_alloc(struct ath_softc *sc, int if_id);
void ath_bstuck_process(struct ath_softc *sc);
void ath_beacon_return(struct ath_softc *sc, struct ath_vap *avp);
void ath_beacon_sync(struct ath_softc *sc, int if_id);
void ath_get_beaconconfig(struct ath_softc *sc,
			  int if_id,
			  struct ath_beacon_config *conf);
/********/
/* VAPs */
/********/

/*
 * Define the scheme that we select MAC address for multiple
 * BSS on the same radio. The very first VAP will just use the MAC
 * address from the EEPROM. For the next 3 VAPs, we set the
 * U/L bit (bit 1) in MAC address, and use the next two bits as the
 * index of the VAP.
 */

#define ATH_SET_VAP_BSSID_MASK(bssid_mask) \
	((bssid_mask)[0] &= ~(((ATH_BCBUF-1)<<2)|0x02))

/* VAP configuration (from protocol layer) */
struct ath_vap_config {
	u32 av_fixed_rateset;
	u32 av_fixed_retryset;
};

/* driver-specific vap state */
struct ath_vap {
	struct ieee80211_vif *av_if_data;
	enum ath9k_opmode av_opmode;	/* VAP operational mode */
	struct ath_buf *av_bcbuf;	/* beacon buffer */
	struct ath_tx_control av_btxctl;  /* txctl information for beacon */
	int av_bslot;			/* beacon slot index */
	struct ath_vap_config av_config;/* vap configuration parameters*/
	struct ath_rate_node *rc_node;
};

int ath_vap_attach(struct ath_softc *sc,
		   int if_id,
		   struct ieee80211_vif *if_data,
		   enum ath9k_opmode opmode);
int ath_vap_detach(struct ath_softc *sc, int if_id);
int ath_vap_config(struct ath_softc *sc,
		   int if_id, struct ath_vap_config *if_config);

/*********************/
/* Antenna diversity */
/*********************/

#define ATH_ANT_DIV_MAX_CFG      2
#define ATH_ANT_DIV_MIN_IDLE_US  1000000  /* us */
#define ATH_ANT_DIV_MIN_SCAN_US  50000	  /* us */

enum ATH_ANT_DIV_STATE{
	ATH_ANT_DIV_IDLE,
	ATH_ANT_DIV_SCAN,	/* evaluating antenna */
};

struct ath_antdiv {
	struct ath_softc *antdiv_sc;
	u8 antdiv_start;
	enum ATH_ANT_DIV_STATE antdiv_state;
	u8 antdiv_num_antcfg;
	u8 antdiv_curcfg;
	u8 antdiv_bestcfg;
	int32_t antdivf_rssitrig;
	int32_t antdiv_lastbrssi[ATH_ANT_DIV_MAX_CFG];
	u64 antdiv_lastbtsf[ATH_ANT_DIV_MAX_CFG];
	u64 antdiv_laststatetsf;
	u8 antdiv_bssid[ETH_ALEN];
};

void ath_slow_ant_div_init(struct ath_antdiv *antdiv,
	struct ath_softc *sc, int32_t rssitrig);
void ath_slow_ant_div_start(struct ath_antdiv *antdiv,
			    u8 num_antcfg,
			    const u8 *bssid);
void ath_slow_ant_div_stop(struct ath_antdiv *antdiv);
void ath_slow_ant_div(struct ath_antdiv *antdiv,
		      struct ieee80211_hdr *wh,
		      struct ath_rx_status *rx_stats);
void ath_setdefantenna(void *sc, u32 antenna);

/********************/
/*   LED Control    */
/********************/

#define ATH_LED_PIN	1

enum ath_led_type {
	ATH_LED_RADIO,
	ATH_LED_ASSOC,
	ATH_LED_TX,
	ATH_LED_RX
};

struct ath_led {
	struct ath_softc *sc;
	struct led_classdev led_cdev;
	enum ath_led_type led_type;
	char name[32];
	bool registered;
};

/* Rfkill */
#define ATH_RFKILL_POLL_INTERVAL	2000 /* msecs */

struct ath_rfkill {
	struct rfkill *rfkill;
	struct delayed_work rfkill_poll;
	char rfkill_name[32];
};

/********************/
/* Main driver core */
/********************/

/*
 * Default cache line size, in bytes.
 * Used when PCI device not fully initialized by bootrom/BIOS
*/
#define DEFAULT_CACHELINE       32
#define	ATH_DEFAULT_NOISE_FLOOR -95
#define ATH_REGCLASSIDS_MAX     10
#define ATH_CABQ_READY_TIME     80  /* % of beacon interval */
#define ATH_MAX_SW_RETRIES      10
#define ATH_CHAN_MAX            255
#define IEEE80211_WEP_NKID      4       /* number of key ids */
#define IEEE80211_RATE_VAL      0x7f
/*
 * The key cache is used for h/w cipher state and also for
 * tracking station state such as the current tx antenna.
 * We also setup a mapping table between key cache slot indices
 * and station state to short-circuit node lookups on rx.
 * Different parts have different size key caches.  We handle
 * up to ATH_KEYMAX entries (could dynamically allocate state).
 */
#define	ATH_KEYMAX	        128        /* max key cache size we handle */

#define ATH_IF_ID_ANY   	0xff
#define ATH_TXPOWER_MAX         100     /* .5 dBm units */

#define RSSI_LPF_THRESHOLD         -20
#define ATH_RSSI_EP_MULTIPLIER     (1<<7)  /* pow2 to optimize out * and / */
#define ATH_RATE_DUMMY_MARKER      0
#define ATH_RSSI_LPF_LEN           10
#define ATH_RSSI_DUMMY_MARKER      0x127

#define ATH_EP_MUL(x, mul)         ((x) * (mul))
#define ATH_EP_RND(x, mul)						\
	((((x)%(mul)) >= ((mul)/2)) ? ((x) + ((mul) - 1)) / (mul) : (x)/(mul))
#define ATH_RSSI_OUT(x)							\
	(((x) != ATH_RSSI_DUMMY_MARKER) ?				\
	 (ATH_EP_RND((x), ATH_RSSI_EP_MULTIPLIER)) : ATH_RSSI_DUMMY_MARKER)
#define ATH_RSSI_IN(x)					\
	(ATH_EP_MUL((x), ATH_RSSI_EP_MULTIPLIER))
#define ATH_LPF_RSSI(x, y, len)						\
	((x != ATH_RSSI_DUMMY_MARKER) ? \
		(((x) * ((len) - 1) + (y)) / (len)) : (y))
#define ATH_RSSI_LPF(x, y) do {						\
		if ((y) >= RSSI_LPF_THRESHOLD)				\
			x = ATH_LPF_RSSI((x), \
				ATH_RSSI_IN((y)), ATH_RSSI_LPF_LEN); \
	} while (0)


enum PROT_MODE {
	PROT_M_NONE = 0,
	PROT_M_RTSCTS,
	PROT_M_CTSONLY
};

enum RATE_TYPE {
	NORMAL_RATE = 0,
	HALF_RATE,
	QUARTER_RATE
};

struct ath_ht_info {
	enum ath9k_ht_macmode tx_chan_width;
	u16 maxampdu;
	u8 mpdudensity;
	u8 ext_chan_offset;
};

#define SC_OP_INVALID		BIT(0)
#define SC_OP_BEACONS		BIT(1)
#define SC_OP_RXAGGR		BIT(2)
#define SC_OP_TXAGGR		BIT(3)
#define SC_OP_CHAINMASK_UPDATE	BIT(4)
#define SC_OP_FULL_RESET	BIT(5)
#define SC_OP_NO_RESET		BIT(6)
#define SC_OP_PREAMBLE_SHORT	BIT(7)
#define SC_OP_PROTECT_ENABLE	BIT(8)
#define SC_OP_RXFLUSH		BIT(9)
#define SC_OP_LED_ASSOCIATED	BIT(10)
#define SC_OP_RFKILL_REGISTERED	BIT(11)
#define SC_OP_RFKILL_SW_BLOCKED	BIT(12)
#define SC_OP_RFKILL_HW_BLOCKED	BIT(13)

struct ath_softc {
	struct ieee80211_hw *hw;
	struct pci_dev *pdev;
	struct tasklet_struct intr_tq;
	struct tasklet_struct bcon_tasklet;
	struct ath_config sc_config;
	struct ath_hal *sc_ah;
	struct ath_rate_softc *sc_rc;
	void __iomem *mem;

	u8 sc_curbssid[ETH_ALEN];
	u8 sc_myaddr[ETH_ALEN];
	u8 sc_bssidmask[ETH_ALEN];

	int sc_debug;
	u32 sc_intrstatus;
	u32 sc_flags; /* SC_OP_* */
	unsigned int rx_filter;
	u16 sc_curtxpow;
	u16 sc_curaid;
	u16 sc_cachelsz;
	int sc_slotupdate;		/* slot to next advance fsm */
	int sc_slottime;
	int sc_bslot[ATH_BCBUF];
	u8 sc_tx_chainmask;
	u8 sc_rx_chainmask;
	enum ath9k_int sc_imask;
	enum wireless_mode sc_curmode;	/* current phy mode */
	enum PROT_MODE sc_protmode;

	u8 sc_nbcnvaps;			/* # of vaps sending beacons */
	u16 sc_nvaps;			/* # of active virtual ap's */
	struct ath_vap *sc_vaps[ATH_BCBUF];

	u8 sc_mcastantenna;
	u8 sc_defant;			/* current default antenna */
	u8 sc_rxotherant;		/* rx's on non-default antenna */

	struct ath9k_node_stats sc_halstats; /* station-mode rssi stats */
	struct list_head node_list;
	struct ath_ht_info sc_ht_info;
	enum ath9k_ht_extprotspacing sc_ht_extprotspacing;

#ifdef CONFIG_SLOW_ANT_DIV
	struct ath_antdiv sc_antdiv;
#endif
	enum {
		OK,		/* no change needed */
		UPDATE,		/* update pending */
		COMMIT		/* beacon sent, commit change */
	} sc_updateslot;	/* slot time update fsm */

	/* Crypto */
	u32 sc_keymax;		/* size of key cache */
	DECLARE_BITMAP(sc_keymap, ATH_KEYMAX);	/* key use bit map */
	u8 sc_splitmic;		/* split TKIP MIC keys */

	/* RX */
	struct list_head sc_rxbuf;
	struct ath_descdma sc_rxdma;
	int sc_rxbufsize;	/* rx size based on mtu */
	u32 *sc_rxlink;		/* link ptr in last RX desc */

	/* TX */
	struct list_head sc_txbuf;
	struct ath_txq sc_txq[ATH9K_NUM_TX_QUEUES];
	struct ath_descdma sc_txdma;
	u32 sc_txqsetup;
	u32 sc_txintrperiod;	/* tx interrupt batching */
	int sc_haltype2q[ATH9K_WME_AC_VO+1]; /* HAL WME	AC -> h/w qnum */
	u16 seq_no; /* TX sequence number */

	/* Beacon */
	struct ath9k_tx_queue_info sc_beacon_qi;
	struct ath_descdma sc_bdma;
	struct ath_txq *sc_cabq;
	struct list_head sc_bbuf;
	u32 sc_bhalq;
	u32 sc_bmisscount;
	u32 ast_be_xmit;	/* beacons transmitted */
	u64 bc_tstamp;

	/* Rate */
	struct ieee80211_rate rates[IEEE80211_NUM_BANDS][ATH_RATE_MAX];
	const struct ath9k_rate_table *sc_currates;
	u8 sc_rixmap[256];	/* IEEE to h/w rate table ix */
	u8 sc_protrix;		/* protection rate index */
	struct {
		u32 rateKbps;	/* transfer rate in kbs */
		u8 ieeerate;	/* IEEE rate */
	} sc_hwmap[256];	/* h/w rate ix mappings */

	/* Channel, Band */
	struct ieee80211_channel channels[IEEE80211_NUM_BANDS][ATH_CHAN_MAX];
	struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];

	/* Locks */
	spinlock_t sc_rxflushlock;
	spinlock_t sc_rxbuflock;
	spinlock_t sc_txbuflock;
	spinlock_t sc_resetlock;
	spinlock_t node_lock;

	/* LEDs */
	struct ath_led radio_led;
	struct ath_led assoc_led;
	struct ath_led tx_led;
	struct ath_led rx_led;

	/* Rfkill */
	struct ath_rfkill rf_kill;
};

int ath_init(u16 devid, struct ath_softc *sc);
void ath_deinit(struct ath_softc *sc);
int ath_open(struct ath_softc *sc, struct ath9k_channel *initial_chan);
int ath_suspend(struct ath_softc *sc);
irqreturn_t ath_isr(int irq, void *dev);
int ath_reset(struct ath_softc *sc, bool retry_tx);
int ath_set_channel(struct ath_softc *sc, struct ath9k_channel *hchan);

/*********************/
/* Utility Functions */
/*********************/

void ath_key_reset(struct ath_softc *sc, u16 keyix, int freeslot);
int ath_keyset(struct ath_softc *sc,
	       u16 keyix,
	       struct ath9k_keyval *hk,
	       const u8 mac[ETH_ALEN]);
int ath_get_hal_qnum(u16 queue, struct ath_softc *sc);
int ath_get_mac80211_qnum(u32 queue, struct ath_softc *sc);
void ath_setslottime(struct ath_softc *sc);
void ath_update_txpow(struct ath_softc *sc);
int ath_cabq_update(struct ath_softc *);
void ath_get_currentCountry(struct ath_softc *sc,
	struct ath9k_country_entry *ctry);
u64 ath_extend_tsf(struct ath_softc *sc, u32 rstamp);

#endif /* CORE_H */
