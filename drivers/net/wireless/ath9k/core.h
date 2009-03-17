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

#include <linux/etherdevice.h>
#include <linux/pci.h>
#include <net/mac80211.h>
#include <linux/leds.h>
#include <linux/rfkill.h>

#include "ath9k.h"
#include "rc.h"

struct ath_node;

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

#define	ATH_TXQ_SETUP(sc, i)        ((sc)->tx.txqsetup & (1<<i))

static const u8 ath_bcast_mac[ETH_ALEN] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};

enum ATH_DEBUG {
	ATH_DBG_RESET		= 0x00000001,
	ATH_DBG_REG_IO		= 0x00000002,
	ATH_DBG_QUEUE		= 0x00000004,
	ATH_DBG_EEPROM		= 0x00000008,
	ATH_DBG_CALIBRATE	= 0x00000010,
	ATH_DBG_CHANNEL		= 0x00000020,
	ATH_DBG_INTERRUPT	= 0x00000040,
	ATH_DBG_REGULATORY	= 0x00000080,
	ATH_DBG_ANI		= 0x00000100,
	ATH_DBG_POWER_MGMT	= 0x00000200,
	ATH_DBG_XMIT		= 0x00000400,
	ATH_DBG_BEACON		= 0x00001000,
	ATH_DBG_CONFIG		= 0x00002000,
	ATH_DBG_KEYCACHE	= 0x00004000,
	ATH_DBG_FATAL		= 0x00008000,
	ATH_DBG_ANY		= 0xffffffff
};

#define DBG_DEFAULT (ATH_DBG_FATAL)

#ifdef CONFIG_ATH9K_DEBUG

/**
 * struct ath_interrupt_stats - Contains statistics about interrupts
 * @total: Total no. of interrupts generated so far
 * @rxok: RX with no errors
 * @rxeol: RX with no more RXDESC available
 * @rxorn: RX FIFO overrun
 * @txok: TX completed at the requested rate
 * @txurn: TX FIFO underrun
 * @mib: MIB regs reaching its threshold
 * @rxphyerr: RX with phy errors
 * @rx_keycache_miss: RX with key cache misses
 * @swba: Software Beacon Alert
 * @bmiss: Beacon Miss
 * @bnr: Beacon Not Ready
 * @cst: Carrier Sense TImeout
 * @gtt: Global TX Timeout
 * @tim: RX beacon TIM occurrence
 * @cabend: RX End of CAB traffic
 * @dtimsync: DTIM sync lossage
 * @dtim: RX Beacon with DTIM
 */
struct ath_interrupt_stats {
	u32 total;
	u32 rxok;
	u32 rxeol;
	u32 rxorn;
	u32 txok;
	u32 txeol;
	u32 txurn;
	u32 mib;
	u32 rxphyerr;
	u32 rx_keycache_miss;
	u32 swba;
	u32 bmiss;
	u32 bnr;
	u32 cst;
	u32 gtt;
	u32 tim;
	u32 cabend;
	u32 dtimsync;
	u32 dtim;
};

struct ath_stats {
	struct ath_interrupt_stats istats;
};

struct ath9k_debug {
	int debug_mask;
	struct dentry *debugfs_root;
	struct dentry *debugfs_phy;
	struct dentry *debugfs_dma;
	struct dentry *debugfs_interrupt;
	struct ath_stats stats;
};

void DPRINTF(struct ath_softc *sc, int dbg_mask, const char *fmt, ...);
int ath9k_init_debug(struct ath_softc *sc);
void ath9k_exit_debug(struct ath_softc *sc);
void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status);

#else

static inline void DPRINTF(struct ath_softc *sc, int dbg_mask,
			   const char *fmt, ...)
{
}

static inline int ath9k_init_debug(struct ath_softc *sc)
{
	return 0;
}

static inline void ath9k_exit_debug(struct ath_softc *sc)
{
}

static inline void ath_debug_stat_interrupt(struct ath_softc *sc,
					    enum ath9k_int status)
{
}

#endif /* CONFIG_ATH9K_DEBUG */

struct ath_config {
	u32 ath_aggr_prot;
	u16 txpowlimit;
	u16 txpowlimit_override;
	u8 cabqReadytime;
	u8 swBeaconProcess;
};

/*************************/
/* Descriptor Management */
/*************************/

#define ATH_TXBUF_RESET(_bf) do {				\
		(_bf)->bf_status = 0;				\
		(_bf)->bf_lastbf = NULL;			\
		(_bf)->bf_lastfrm = NULL;			\
		(_bf)->bf_next = NULL;				\
		memset(&((_bf)->bf_state), 0,			\
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
	int bfs_nframes;		/* # frames in aggregate */
	u16 bfs_al;			/* length of aggregate */
	u16 bfs_frmlen;			/* length of frame */
	int bfs_seqno;			/* sequence number */
	int bfs_tidno;			/* tid of this frame */
	int bfs_retries;		/* current retries */
	u32 bf_type;			/* BUF_* (enum buffer_type) */
	u32 bfs_keyix;
	enum ath9k_key_type bfs_keytype;
};

#define bf_nframes      	bf_state.bfs_nframes
#define bf_al           	bf_state.bfs_al
#define bf_frmlen       	bf_state.bfs_frmlen
#define bf_retries      	bf_state.bfs_retries
#define bf_seqno        	bf_state.bfs_seqno
#define bf_tidno        	bf_state.bfs_tidno
#define bf_rcs          	bf_state.bfs_rcs
#define bf_keyix                bf_state.bfs_keyix
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
	void *bf_mpdu;			/* enclosing frame structure */
	struct ath_desc *bf_desc;	/* virtual addr of desc */
	dma_addr_t bf_daddr;		/* physical addr of desc */
	dma_addr_t bf_buf_addr;		/* physical addr of data buffer */
	u32 bf_status;
	u16 bf_flags;			/* tx descriptor flags */
	struct ath_buf_state bf_state;	/* buffer state */
	dma_addr_t bf_dmacontext;
};

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

int ath_descdma_setup(struct ath_softc *sc, struct ath_descdma *dd,
		      struct list_head *head, const char *name,
		      int nbuf, int ndesc);
void ath_descdma_cleanup(struct ath_softc *sc, struct ath_descdma *dd,
			 struct list_head *head);

/***********/
/* RX / TX */
/***********/

#define ATH_MAX_ANTENNA         3
#define ATH_RXBUF               512
#define WME_NUM_TID             16
#define ATH_TXBUF               512
#define ATH_TXMAXTRY            13
#define ATH_11N_TXMAXTRY        10
#define ATH_MGT_TXMAXTRY        4
#define WME_BA_BMP_SIZE         64
#define WME_MAX_BA              WME_BA_BMP_SIZE
#define ATH_TID_MAX_BUFS        (2 * WME_MAX_BA)

#define TID_TO_WME_AC(_tid)				\
	((((_tid) == 0) || ((_tid) == 3)) ? WME_AC_BE :	\
	 (((_tid) == 1) || ((_tid) == 2)) ? WME_AC_BK :	\
	 (((_tid) == 4) || ((_tid) == 5)) ? WME_AC_VI :	\
	 WME_AC_VO)

#define WME_AC_BE   0
#define WME_AC_BK   1
#define WME_AC_VI   2
#define WME_AC_VO   3
#define WME_NUM_AC  4

#define ADDBA_EXCHANGE_ATTEMPTS    10
#define ATH_AGGR_DELIM_SZ          4
#define ATH_AGGR_MINPLEN           256 /* in bytes, minimum packet length */
/* number of delimiters for encryption padding */
#define ATH_AGGR_ENCRYPTDELIM      10
/* minimum h/w qdepth to be sustained to maximize aggregation */
#define ATH_AGGR_MIN_QDEPTH        2
#define ATH_AMPDU_SUBFRAME_DEFAULT 32
#define IEEE80211_SEQ_SEQ_SHIFT    4
#define IEEE80211_SEQ_MAX          4096
#define IEEE80211_MIN_AMPDU_BUF    0x8
#define IEEE80211_HTCAP_MAXRXAMPDU_FACTOR 13

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

#define ATH_DS_BA_SEQ(_ds)         ((_ds)->ds_us.tx.ts_seqnum)
#define ATH_DS_BA_BITMAP(_ds)      (&(_ds)->ds_us.tx.ba_low)
#define ATH_DS_TX_BA(_ds)          ((_ds)->ds_us.tx.ts_flags & ATH9K_TX_BA)
#define ATH_AN_2_TID(_an, _tidno)  (&(_an)->tid[(_tidno)])

enum ATH_AGGR_STATUS {
	ATH_AGGR_DONE,
	ATH_AGGR_BAW_CLOSED,
	ATH_AGGR_LIMITED,
	ATH_AGGR_SHORTPKT,
	ATH_AGGR_8K_LIMITED,
};

struct ath_txq {
	u32 axq_qnum;			/* hardware q number */
	u32 *axq_link;			/* link ptr in last TX desc */
	struct list_head axq_q;		/* transmit queue */
	spinlock_t axq_lock;
	unsigned long axq_lockflags;	/* intr state when must cli */
	u32 axq_depth;			/* queue depth */
	u8 axq_aggr_depth;		/* aggregates queued */
	u32 axq_totalqueued;		/* total ever queued */
	bool stopped;			/* Is mac80211 queue stopped ? */
	struct ath_buf *axq_linkbuf;	/* virtual addr of last buffer*/

	/* first desc of the last descriptor that contains CTS */
	struct ath_desc *axq_lastdsWithCTS;

	/* final desc of the gating desc that determines whether
	   lastdsWithCTS has been DMA'ed or not */
	struct ath_desc *axq_gatingds;

	struct list_head axq_acq;
};

#define AGGR_CLEANUP         BIT(1)
#define AGGR_ADDBA_COMPLETE  BIT(2)
#define AGGR_ADDBA_PROGRESS  BIT(3)

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
	u8 state;
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

/* per-frame tx control block */
struct ath_tx_control {
	struct ath_txq *txq;
	int if_id;
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

/* All RSSI values are noise floor adjusted */
struct ath_tx_stat {
	int rssi;
	int rssictl[ATH_MAX_ANTENNA];
	int rssiextn[ATH_MAX_ANTENNA];
	int rateieee;
	int rateKbps;
	int ratecode;
	int flags;
	u32 airtime;	/* time on air per final tx rate */
};

struct aggr_rifs_param {
	int param_max_frames;
	int param_max_len;
	int param_rl;
	int param_al;
	struct ath_rc_series *param_rcs;
};

struct ath_node {
	struct ath_softc *an_sc;
	struct ath_atx_tid tid[WME_NUM_TID];
	struct ath_atx_ac ac[WME_NUM_AC];
	u16 maxampdu;
	u8 mpdudensity;
};

struct ath_tx {
	u16 seq_no;
	u32 txqsetup;
	int hwq_map[ATH9K_WME_AC_VO+1];
	spinlock_t txbuflock;
	struct list_head txbuf;
	struct ath_txq txq[ATH9K_NUM_TX_QUEUES];
	struct ath_descdma txdma;
};

struct ath_rx {
	u8 defant;
	u8 rxotherant;
	u32 *rxlink;
	int bufsize;
	unsigned int rxfilter;
	spinlock_t rxflushlock;
	spinlock_t rxbuflock;
	struct list_head rxbuf;
	struct ath_descdma rxdma;
};

int ath_startrecv(struct ath_softc *sc);
bool ath_stoprecv(struct ath_softc *sc);
void ath_flushrecv(struct ath_softc *sc);
u32 ath_calcrxfilter(struct ath_softc *sc);
int ath_rx_init(struct ath_softc *sc, int nbufs);
void ath_rx_cleanup(struct ath_softc *sc);
int ath_rx_tasklet(struct ath_softc *sc, int flush);
struct ath_txq *ath_txq_setup(struct ath_softc *sc, int qtype, int subtype);
void ath_tx_cleanupq(struct ath_softc *sc, struct ath_txq *txq);
int ath_tx_setup(struct ath_softc *sc, int haltype);
void ath_draintxq(struct ath_softc *sc, bool retry_tx);
void ath_tx_draintxq(struct ath_softc *sc,
		     struct ath_txq *txq, bool retry_tx);
void ath_tx_node_init(struct ath_softc *sc, struct ath_node *an);
void ath_tx_node_cleanup(struct ath_softc *sc, struct ath_node *an);
void ath_tx_node_free(struct ath_softc *sc, struct ath_node *an);
void ath_txq_schedule(struct ath_softc *sc, struct ath_txq *txq);
int ath_tx_init(struct ath_softc *sc, int nbufs);
int ath_tx_cleanup(struct ath_softc *sc);
int ath_tx_get_qnum(struct ath_softc *sc, int qtype, int haltype);
struct ath_txq *ath_test_get_txq(struct ath_softc *sc, struct sk_buff *skb);
int ath_txq_update(struct ath_softc *sc, int qnum,
		   struct ath9k_tx_queue_info *q);
int ath_tx_start(struct ath_softc *sc, struct sk_buff *skb,
		 struct ath_tx_control *txctl);
void ath_tx_tasklet(struct ath_softc *sc);
u32 ath_txq_depth(struct ath_softc *sc, int qnum);
u32 ath_txq_aggr_depth(struct ath_softc *sc, int qnum);
void ath_tx_cabq(struct ath_softc *sc, struct sk_buff *skb);
void ath_tx_resume_tid(struct ath_softc *sc, struct ath_atx_tid *tid);
bool ath_tx_aggr_check(struct ath_softc *sc, struct ath_node *an, u8 tidno);
void ath_tx_aggr_teardown(struct ath_softc *sc,	struct ath_node *an, u8 tidno);
int ath_tx_aggr_start(struct ath_softc *sc, struct ieee80211_sta *sta,
		      u16 tid, u16 *ssn);
int ath_tx_aggr_stop(struct ath_softc *sc, struct ieee80211_sta *sta, u16 tid);
void ath_tx_aggr_resume(struct ath_softc *sc, struct ieee80211_sta *sta, u16 tid);

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

struct ath_vap {
	int av_bslot;
	enum nl80211_iftype av_opmode;
	struct ath_buf *av_bcbuf;
	struct ath_tx_control av_btxctl;
};

/*******************/
/* Beacon Handling */
/*******************/

/*
 * Regardless of the number of beacons we stagger, (i.e. regardless of the
 * number of BSSIDs) if a given beacon does not go out even after waiting this
 * number of beacon intervals, the game's up.
 */
#define BSTUCK_THRESH           	(9 * ATH_BCBUF)
#define	ATH_BCBUF               	1
#define ATH_DEFAULT_BINTVAL     	100 /* TU */
#define ATH_DEFAULT_BMISS_LIMIT 	10
#define IEEE80211_MS_TO_TU(x)           (((x) * 1000) / 1024)

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

struct ath_beacon {
	enum {
		OK,		/* no change needed */
		UPDATE,		/* update pending */
		COMMIT		/* beacon sent, commit change */
	} updateslot;		/* slot time update fsm */

	u32 beaconq;
	u32 bmisscnt;
	u32 ast_be_xmit;
	u64 bc_tstamp;
	int bslot[ATH_BCBUF];
	int slottime;
	int slotupdate;
	struct ath9k_tx_queue_info beacon_qi;
	struct ath_descdma bdma;
	struct ath_txq *cabq;
	struct list_head bbuf;
};

void ath9k_beacon_tasklet(unsigned long data);
void ath_beacon_config(struct ath_softc *sc, int if_id);
int ath_beaconq_setup(struct ath_hal *ah);
int ath_beacon_alloc(struct ath_softc *sc, int if_id);
void ath_beacon_return(struct ath_softc *sc, struct ath_vap *avp);
void ath_beacon_sync(struct ath_softc *sc, int if_id);

/*******/
/* ANI */
/*******/

/* ANI values for STA only.
   FIXME: Add appropriate values for AP later */

#define ATH_ANI_POLLINTERVAL    100     /* 100 milliseconds between ANI poll */
#define ATH_SHORT_CALINTERVAL   1000    /* 1 second between calibrations */
#define ATH_LONG_CALINTERVAL    30000   /* 30 seconds between calibrations */
#define ATH_RESTART_CALINTERVAL 1200000 /* 20 minutes between calibrations */

struct ath_ani {
	bool sc_caldone;
	int16_t sc_noise_floor;
	unsigned int sc_longcal_timer;
	unsigned int sc_shortcal_timer;
	unsigned int sc_resetcal_timer;
	unsigned int sc_checkani_timer;
	struct timer_list timer;
};

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
#define ATH_CABQ_READY_TIME     80      /* % of beacon interval */
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
#define	ATH_KEYMAX	        128     /* max key cache size we handle */

#define ATH_IF_ID_ANY   	0xff
#define ATH_TXPOWER_MAX         100     /* .5 dBm units */
#define ATH_RSSI_DUMMY_MARKER   0x127
#define ATH_RATE_DUMMY_MARKER   0

enum PROT_MODE {
	PROT_M_NONE = 0,
	PROT_M_RTSCTS,
	PROT_M_CTSONLY
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
	struct ath_hal *sc_ah;
	void __iomem *mem;
	spinlock_t sc_resetlock;
	spinlock_t sc_serial_rw;
	struct mutex mutex;

	u8 sc_curbssid[ETH_ALEN];
	u8 sc_myaddr[ETH_ALEN];
	u8 sc_bssidmask[ETH_ALEN];
	u32 sc_intrstatus;
	u32 sc_flags; /* SC_OP_* */
	u16 sc_curtxpow;
	u16 sc_curaid;
	u16 sc_cachelsz;
	u8 sc_nbcnvaps;
	u16 sc_nvaps;
	u8 sc_tx_chainmask;
	u8 sc_rx_chainmask;
	u32 sc_keymax;
	DECLARE_BITMAP(sc_keymap, ATH_KEYMAX);
	u8 sc_splitmic;
	u8 sc_protrix;
	enum ath9k_int sc_imask;
	enum PROT_MODE sc_protmode;
	enum ath9k_ht_extprotspacing sc_ht_extprotspacing;
	enum ath9k_ht_macmode tx_chan_width;

	struct ath_config sc_config;
	struct ath_rx rx;
	struct ath_tx tx;
	struct ath_beacon beacon;
	struct ieee80211_vif *sc_vaps[ATH_BCBUF];
	struct ieee80211_rate rates[IEEE80211_NUM_BANDS][ATH_RATE_MAX];
	struct ath_rate_table *hw_rate_table[ATH9K_MODE_MAX];
	struct ath_rate_table *cur_rate_table;
	struct ieee80211_channel channels[IEEE80211_NUM_BANDS][ATH_CHAN_MAX];
	struct ieee80211_supported_band sbands[IEEE80211_NUM_BANDS];
	struct ath_led radio_led;
	struct ath_led assoc_led;
	struct ath_led tx_led;
	struct ath_led rx_led;
	struct ath_rfkill rf_kill;
	struct ath_ani sc_ani;
	struct ath9k_node_stats sc_halstats;
#ifdef CONFIG_ATH9K_DEBUG
	struct ath9k_debug sc_debug;
#endif
};

int ath_reset(struct ath_softc *sc, bool retry_tx);
int ath_get_hal_qnum(u16 queue, struct ath_softc *sc);
int ath_get_mac80211_qnum(u32 queue, struct ath_softc *sc);
int ath_cabq_update(struct ath_softc *);

/*
 * Read and write, they both share the same lock. We do this to serialize
 * reads and writes on Atheros 802.11n PCI devices only. This is required
 * as the FIFO on these devices can only accept sanely 2 requests. After
 * that the device goes bananas. Serializing the reads/writes prevents this
 * from happening.
 */

static inline void ath9k_iowrite32(struct ath_hal *ah, u32 reg_offset, u32 val)
{
	if (ah->ah_config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&ah->ah_sc->sc_serial_rw, flags);
		iowrite32(val, ah->ah_sc->mem + reg_offset);
		spin_unlock_irqrestore(&ah->ah_sc->sc_serial_rw, flags);
	} else
		iowrite32(val, ah->ah_sc->mem + reg_offset);
}

static inline unsigned int ath9k_ioread32(struct ath_hal *ah, u32 reg_offset)
{
	u32 val;
	if (ah->ah_config.serialize_regmode == SER_REG_MODE_ON) {
		unsigned long flags;
		spin_lock_irqsave(&ah->ah_sc->sc_serial_rw, flags);
		val = ioread32(ah->ah_sc->mem + reg_offset);
		spin_unlock_irqrestore(&ah->ah_sc->sc_serial_rw, flags);
	} else
		val = ioread32(ah->ah_sc->mem + reg_offset);
	return val;
}

#endif /* CORE_H */
