/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <wlc_cfg.h>
#include <typedefs.h>
#include <linuxver.h>
#include <bcmdefs.h>
#include <osl.h>
#include <bcmutils.h>
#include <siutils.h>
#include <bcmendian.h>
#include <wlioctl.h>
#include <sbhnddma.h>
#include <hnddma.h>
#include <d11.h>
#include <wlc_rate.h>
#include <wlc_pub.h>
#include <wlc_key.h>
#include <wlc_mac80211.h>
#include <wlc_phy_hal.h>
#include <wlc_antsel.h>
#include <wlc_scb.h>
#include <net/mac80211.h>
#include <wlc_ampdu.h>
#include <wl_export.h>

#ifdef WLC_HIGH_ONLY
#include <bcm_rpc_tp.h>
#include <wlc_rpctx.h>
#endif

#define AMPDU_MAX_MPDU		32	/* max number of mpdus in an ampdu */
#define AMPDU_NUM_MPDU_LEGACY	16	/* max number of mpdus in an ampdu to a legacy */
#define AMPDU_TX_BA_MAX_WSIZE	64	/* max Tx ba window size (in pdu) */
#define AMPDU_TX_BA_DEF_WSIZE	64	/* default Tx ba window size (in pdu) */
#define AMPDU_RX_BA_DEF_WSIZE   64	/* max Rx ba window size (in pdu) */
#define AMPDU_RX_BA_MAX_WSIZE   64	/* default Rx ba window size (in pdu) */
#define	AMPDU_MAX_DUR		5	/* max dur of tx ampdu (in msec) */
#define AMPDU_DEF_RETRY_LIMIT	5	/* default tx retry limit */
#define AMPDU_DEF_RR_RETRY_LIMIT	2	/* default tx retry limit at reg rate */
#define AMPDU_DEF_TXPKT_WEIGHT	2	/* default weight of ampdu in txfifo */
#define AMPDU_DEF_FFPLD_RSVD	2048	/* default ffpld reserved bytes */
#define AMPDU_INI_FREE		10	/* # of inis to be freed on detach */
#define	AMPDU_SCB_MAX_RELEASE	20	/* max # of mpdus released at a time */

#define NUM_FFPLD_FIFO 4	/* number of fifo concerned by pre-loading */
#define FFPLD_TX_MAX_UNFL   200	/* default value of the average number of ampdu
				 * without underflows
				 */
#define FFPLD_MPDU_SIZE 1800	/* estimate of maximum mpdu size */
#define FFPLD_MAX_MCS 23	/* we don't deal with mcs 32 */
#define FFPLD_PLD_INCR 1000	/* increments in bytes */
#define FFPLD_MAX_AMPDU_CNT 5000	/* maximum number of ampdu we
					 * accumulate between resets.
					 */

#define TX_SEQ_TO_INDEX(seq) ((seq) % AMPDU_TX_BA_MAX_WSIZE)

/* max possible overhead per mpdu in the ampdu; 3 is for roundup if needed */
#define AMPDU_MAX_MPDU_OVERHEAD (DOT11_FCS_LEN + DOT11_ICV_AES_LEN + AMPDU_DELIMITER_LEN + 3 \
	+ DOT11_A4_HDR_LEN + DOT11_QOS_LEN + DOT11_IV_MAX_LEN)

#ifdef BCMDBG
uint32 wl_ampdu_dbg =
    WL_AMPDU_UPDN_VAL |
    WL_AMPDU_ERR_VAL |
    WL_AMPDU_TX_VAL |
    WL_AMPDU_RX_VAL |
    WL_AMPDU_CTL_VAL |
    WL_AMPDU_HW_VAL | WL_AMPDU_HWTXS_VAL | WL_AMPDU_HWDBG_VAL;
#endif

/* structure to hold tx fifo information and pre-loading state
 * counters specific to tx underflows of ampdus
 * some counters might be redundant with the ones in wlc or ampdu structures.
 * This allows to maintain a specific state independantly of
 * how often and/or when the wlc counters are updated.
 */
typedef struct wlc_fifo_info {
	uint16 ampdu_pld_size;	/* number of bytes to be pre-loaded */
	u8 mcs2ampdu_table[FFPLD_MAX_MCS + 1];	/* per-mcs max # of mpdus in an ampdu */
	uint16 prev_txfunfl;	/* num of underflows last read from the HW macstats counter */
	uint32 accum_txfunfl;	/* num of underflows since we modified pld params */
	uint32 accum_txampdu;	/* num of tx ampdu since we modified pld params  */
	uint32 prev_txampdu;	/* previous reading of tx ampdu */
	uint32 dmaxferrate;	/* estimated dma avg xfer rate in kbits/sec */
} wlc_fifo_info_t;

/* AMPDU module specific state */
struct ampdu_info {
	wlc_info_t *wlc;	/* pointer to main wlc structure */
	int scb_handle;		/* scb cubby handle to retrieve data from scb */
	u8 ini_enable[AMPDU_MAX_SCB_TID];	/* per-tid initiator enable/disable of ampdu */
	u8 ba_tx_wsize;	/* Tx ba window size (in pdu) */
	u8 ba_rx_wsize;	/* Rx ba window size (in pdu) */
	u8 retry_limit;	/* mpdu transmit retry limit */
	u8 rr_retry_limit;	/* mpdu transmit retry limit at regular rate */
	u8 retry_limit_tid[AMPDU_MAX_SCB_TID];	/* per-tid mpdu transmit retry limit */
	/* per-tid mpdu transmit retry limit at regular rate */
	u8 rr_retry_limit_tid[AMPDU_MAX_SCB_TID];
	u8 mpdu_density;	/* min mpdu spacing (0-7) ==> 2^(x-1)/8 usec */
	int8 max_pdu;		/* max pdus allowed in ampdu */
	u8 dur;		/* max duration of an ampdu (in msec) */
	u8 txpkt_weight;	/* weight of ampdu in txfifo; reduces rate lag */
	u8 rx_factor;	/* maximum rx ampdu factor (0-3) ==> 2^(13+x) bytes */
	uint32 ffpld_rsvd;	/* number of bytes to reserve for preload */
	uint32 max_txlen[MCS_TABLE_SIZE][2][2];	/* max size of ampdu per mcs, bw and sgi */
	void *ini_free[AMPDU_INI_FREE];	/* array of ini's to be freed on detach */
	bool mfbr;		/* enable multiple fallback rate */
	uint32 tx_max_funl;	/* underflows should be kept such that
				 * (tx_max_funfl*underflows) < tx frames
				 */
	wlc_fifo_info_t fifo_tb[NUM_FFPLD_FIFO];	/* table of fifo infos  */

#ifdef WLC_HIGH_ONLY
	void *p;
	tx_status_t txs;
	bool waiting_status;	/* To help sanity checks */
#endif
};

#define AMPDU_CLEANUPFLAG_RX   (0x1)
#define AMPDU_CLEANUPFLAG_TX   (0x2)

#define SCB_AMPDU_CUBBY(ampdu, scb) (&(scb->scb_ampdu))
#define SCB_AMPDU_INI(scb_ampdu, tid) (&(scb_ampdu->ini[tid]))

static void wlc_ffpld_init(ampdu_info_t *ampdu);
static int wlc_ffpld_check_txfunfl(wlc_info_t *wlc, int f);
static void wlc_ffpld_calc_mcs2ampdu_table(ampdu_info_t *ampdu, int f);

static scb_ampdu_tid_ini_t *wlc_ampdu_init_tid_ini(ampdu_info_t *ampdu,
						   scb_ampdu_t *scb_ampdu,
						   u8 tid, bool override);
static void ampdu_cleanup_tid_ini(ampdu_info_t *ampdu, scb_ampdu_t *scb_ampdu,
				  u8 tid, bool force);
static void ampdu_update_max_txlen(ampdu_info_t *ampdu, u8 dur);
static void scb_ampdu_update_config(ampdu_info_t *ampdu, struct scb *scb);
static void scb_ampdu_update_config_all(ampdu_info_t *ampdu);

#define wlc_ampdu_txflowcontrol(a, b, c)	do {} while (0)

static void wlc_ampdu_dotxstatus_complete(ampdu_info_t *ampdu, struct scb *scb,
					  void *p, tx_status_t *txs,
					  uint32 frmtxstatus,
					  uint32 frmtxstatus2);

static inline uint16 pkt_txh_seqnum(wlc_info_t *wlc, void *p)
{
	d11txh_t *txh;
	struct dot11_header *h;
	txh = (d11txh_t *) PKTDATA(p);
	h = (struct dot11_header *)((u8 *) (txh + 1) + D11_PHY_HDR_LEN);
	return ltoh16(h->seq) >> SEQNUM_SHIFT;
}

ampdu_info_t *BCMATTACHFN(wlc_ampdu_attach) (wlc_info_t *wlc)
{
	ampdu_info_t *ampdu;
	int i;

	/* some code depends on packed structures */
	ASSERT(DOT11_MAXNUMFRAGS == NBITS(uint16));
	ASSERT(ISPOWEROF2(AMPDU_TX_BA_MAX_WSIZE));
	ASSERT(ISPOWEROF2(AMPDU_RX_BA_MAX_WSIZE));
	ASSERT(wlc->pub->tunables->ampdunummpdu <= AMPDU_MAX_MPDU);
	ASSERT(wlc->pub->tunables->ampdunummpdu > 0);

	ampdu = (ampdu_info_t *) MALLOC(wlc->osh, sizeof(ampdu_info_t));
	if (!ampdu) {
		WL_ERROR(("wl%d: wlc_ampdu_attach: out of mem, malloced %d bytes\n", wlc->pub->unit, MALLOCED(wlc->osh)));
		return NULL;
	}
	bzero((char *)ampdu, sizeof(ampdu_info_t));
	ampdu->wlc = wlc;

	for (i = 0; i < AMPDU_MAX_SCB_TID; i++)
		ampdu->ini_enable[i] = TRUE;
	/* Disable ampdu for VO by default */
	ampdu->ini_enable[PRIO_8021D_VO] = FALSE;
	ampdu->ini_enable[PRIO_8021D_NC] = FALSE;

	/* Disable ampdu for BK by default since not enough fifo space */
	ampdu->ini_enable[PRIO_8021D_NONE] = FALSE;
	ampdu->ini_enable[PRIO_8021D_BK] = FALSE;

	ampdu->ba_tx_wsize = AMPDU_TX_BA_DEF_WSIZE;
	ampdu->ba_rx_wsize = AMPDU_RX_BA_DEF_WSIZE;
	ampdu->mpdu_density = AMPDU_DEF_MPDU_DENSITY;
	ampdu->max_pdu = AUTO;
	ampdu->dur = AMPDU_MAX_DUR;
	ampdu->txpkt_weight = AMPDU_DEF_TXPKT_WEIGHT;

	ampdu->ffpld_rsvd = AMPDU_DEF_FFPLD_RSVD;
	/* bump max ampdu rcv size to 64k for all 11n devices except 4321A0 and 4321A1 */
	if (WLCISNPHY(wlc->band) && NREV_LT(wlc->band->phyrev, 2))
		ampdu->rx_factor = AMPDU_RX_FACTOR_32K;
	else
		ampdu->rx_factor = AMPDU_RX_FACTOR_64K;
#ifdef WLC_HIGH_ONLY
	/* Restrict to smaller rcv size for BMAC dongle */
	ampdu->rx_factor = AMPDU_RX_FACTOR_32K;
#endif
	ampdu->retry_limit = AMPDU_DEF_RETRY_LIMIT;
	ampdu->rr_retry_limit = AMPDU_DEF_RR_RETRY_LIMIT;

	for (i = 0; i < AMPDU_MAX_SCB_TID; i++) {
		ampdu->retry_limit_tid[i] = ampdu->retry_limit;
		ampdu->rr_retry_limit_tid[i] = ampdu->rr_retry_limit;
	}

	ampdu_update_max_txlen(ampdu, ampdu->dur);
	ampdu->mfbr = FALSE;
	/* try to set ampdu to the default value */
	wlc_ampdu_set(ampdu, wlc->pub->_ampdu);

	ampdu->tx_max_funl = FFPLD_TX_MAX_UNFL;
	wlc_ffpld_init(ampdu);

	return ampdu;
}

void BCMATTACHFN(wlc_ampdu_detach) (ampdu_info_t *ampdu)
{
	int i;

	if (!ampdu)
		return;

	/* free all ini's which were to be freed on callbacks which were never called */
	for (i = 0; i < AMPDU_INI_FREE; i++) {
		if (ampdu->ini_free[i]) {
			MFREE(ampdu->wlc->osh, ampdu->ini_free[i],
			      sizeof(scb_ampdu_tid_ini_t));
		}
	}

	wlc_module_unregister(ampdu->wlc->pub, "ampdu", ampdu);
	MFREE(ampdu->wlc->osh, ampdu, sizeof(ampdu_info_t));
}

void scb_ampdu_cleanup(ampdu_info_t *ampdu, struct scb *scb)
{
	scb_ampdu_t *scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	u8 tid;

	WL_AMPDU_UPDN(("scb_ampdu_cleanup: enter\n"));
	ASSERT(scb_ampdu);

	for (tid = 0; tid < AMPDU_MAX_SCB_TID; tid++) {
		ampdu_cleanup_tid_ini(ampdu, scb_ampdu, tid, FALSE);
	}
}

/* reset the ampdu state machine so that it can gracefully handle packets that were
 * freed from the dma and tx queues during reinit
 */
void wlc_ampdu_reset(ampdu_info_t *ampdu)
{
	WL_NONE(("%s: Entering\n", __func__));
}

static void scb_ampdu_update_config(ampdu_info_t *ampdu, struct scb *scb)
{
	scb_ampdu_t *scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	int i;

	scb_ampdu->max_pdu = (u8) ampdu->wlc->pub->tunables->ampdunummpdu;

	/* go back to legacy size if some preloading is occuring */
	for (i = 0; i < NUM_FFPLD_FIFO; i++) {
		if (ampdu->fifo_tb[i].ampdu_pld_size > FFPLD_PLD_INCR)
			scb_ampdu->max_pdu = AMPDU_NUM_MPDU_LEGACY;
	}

	/* apply user override */
	if (ampdu->max_pdu != AUTO)
		scb_ampdu->max_pdu = (u8) ampdu->max_pdu;

	scb_ampdu->release = MIN(scb_ampdu->max_pdu, AMPDU_SCB_MAX_RELEASE);

	if (scb_ampdu->max_rxlen)
		scb_ampdu->release =
		    MIN(scb_ampdu->release, scb_ampdu->max_rxlen / 1600);

	scb_ampdu->release = MIN(scb_ampdu->release,
				 ampdu->fifo_tb[TX_AC_BE_FIFO].
				 mcs2ampdu_table[FFPLD_MAX_MCS]);

	ASSERT(scb_ampdu->release);
}

void scb_ampdu_update_config_all(ampdu_info_t *ampdu)
{
	scb_ampdu_update_config(ampdu, ampdu->wlc->pub->global_scb);
}

static void wlc_ffpld_init(ampdu_info_t *ampdu)
{
	int i, j;
	wlc_fifo_info_t *fifo;

	for (j = 0; j < NUM_FFPLD_FIFO; j++) {
		fifo = (ampdu->fifo_tb + j);
		fifo->ampdu_pld_size = 0;
		for (i = 0; i <= FFPLD_MAX_MCS; i++)
			fifo->mcs2ampdu_table[i] = 255;
		fifo->dmaxferrate = 0;
		fifo->accum_txampdu = 0;
		fifo->prev_txfunfl = 0;
		fifo->accum_txfunfl = 0;

	}
}

/* evaluate the dma transfer rate using the tx underflows as feedback.
 * If necessary, increase tx fifo preloading. If not enough,
 * decrease maximum ampdu size for each mcs till underflows stop
 * Return 1 if pre-loading not active, -1 if not an underflow event,
 * 0 if pre-loading module took care of the event.
 */
static int wlc_ffpld_check_txfunfl(wlc_info_t *wlc, int fid)
{
	ampdu_info_t *ampdu = wlc->ampdu;
	uint32 phy_rate = MCS_RATE(FFPLD_MAX_MCS, TRUE, FALSE);
	uint32 txunfl_ratio;
	u8 max_mpdu;
	uint32 current_ampdu_cnt = 0;
	uint16 max_pld_size;
	uint32 new_txunfl;
	wlc_fifo_info_t *fifo = (ampdu->fifo_tb + fid);
	uint xmtfifo_sz;
	uint16 cur_txunfl;

	/* return if we got here for a different reason than underflows */
	cur_txunfl =
	    wlc_read_shm(wlc,
			 M_UCODE_MACSTAT + OFFSETOF(macstat_t, txfunfl[fid]));
	new_txunfl = (uint16) (cur_txunfl - fifo->prev_txfunfl);
	if (new_txunfl == 0) {
		WL_FFPLD(("check_txunfl : TX status FRAG set but no tx underflows\n"));
		return -1;
	}
	fifo->prev_txfunfl = cur_txunfl;

	if (!ampdu->tx_max_funl)
		return 1;

	/* check if fifo is big enough */
	if (wlc_xmtfifo_sz_get(wlc, fid, &xmtfifo_sz)) {
		WL_FFPLD(("check_txunfl : get xmtfifo_sz failed.\n"));
		return -1;
	}

	if ((TXFIFO_SIZE_UNIT * (uint32) xmtfifo_sz) <= ampdu->ffpld_rsvd)
		return 1;

	max_pld_size = TXFIFO_SIZE_UNIT * xmtfifo_sz - ampdu->ffpld_rsvd;
	fifo->accum_txfunfl += new_txunfl;

	/* we need to wait for at least 10 underflows */
	if (fifo->accum_txfunfl < 10)
		return 0;

	WL_FFPLD(("ampdu_count %d  tx_underflows %d\n",
		  current_ampdu_cnt, fifo->accum_txfunfl));

	/*
	   compute the current ratio of tx unfl per ampdu.
	   When the current ampdu count becomes too
	   big while the ratio remains small, we reset
	   the current count in order to not
	   introduce too big of a latency in detecting a
	   large amount of tx underflows later.
	 */

	txunfl_ratio = current_ampdu_cnt / fifo->accum_txfunfl;

	if (txunfl_ratio > ampdu->tx_max_funl) {
		if (current_ampdu_cnt >= FFPLD_MAX_AMPDU_CNT) {
			fifo->accum_txfunfl = 0;
		}
		return 0;
	}
	max_mpdu =
	    MIN(fifo->mcs2ampdu_table[FFPLD_MAX_MCS], AMPDU_NUM_MPDU_LEGACY);

	/* In case max value max_pdu is already lower than
	   the fifo depth, there is nothing more we can do.
	 */

	if (fifo->ampdu_pld_size >= max_mpdu * FFPLD_MPDU_SIZE) {
		WL_FFPLD(("tx fifo pld : max ampdu fits in fifo\n)"));
		fifo->accum_txfunfl = 0;
		return 0;
	}

	if (fifo->ampdu_pld_size < max_pld_size) {

		/* increment by TX_FIFO_PLD_INC bytes */
		fifo->ampdu_pld_size += FFPLD_PLD_INCR;
		if (fifo->ampdu_pld_size > max_pld_size)
			fifo->ampdu_pld_size = max_pld_size;

		/* update scb release size */
		scb_ampdu_update_config_all(ampdu);

		/*
		   compute a new dma xfer rate for max_mpdu @ max mcs.
		   This is the minimum dma rate that
		   can acheive no unferflow condition for the current mpdu size.
		 */
		/* note : we divide/multiply by 100 to avoid integer overflows */
		fifo->dmaxferrate =
		    (((phy_rate / 100) *
		      (max_mpdu * FFPLD_MPDU_SIZE - fifo->ampdu_pld_size))
		     / (max_mpdu * FFPLD_MPDU_SIZE)) * 100;

		WL_FFPLD(("DMA estimated transfer rate %d; pre-load size %d\n",
			  fifo->dmaxferrate, fifo->ampdu_pld_size));
	} else {

		/* decrease ampdu size */
		if (fifo->mcs2ampdu_table[FFPLD_MAX_MCS] > 1) {
			if (fifo->mcs2ampdu_table[FFPLD_MAX_MCS] == 255)
				fifo->mcs2ampdu_table[FFPLD_MAX_MCS] =
				    AMPDU_NUM_MPDU_LEGACY - 1;
			else
				fifo->mcs2ampdu_table[FFPLD_MAX_MCS] -= 1;

			/* recompute the table */
			wlc_ffpld_calc_mcs2ampdu_table(ampdu, fid);

			/* update scb release size */
			scb_ampdu_update_config_all(ampdu);
		}
	}
	fifo->accum_txfunfl = 0;
	return 0;
}

static void wlc_ffpld_calc_mcs2ampdu_table(ampdu_info_t *ampdu, int f)
{
	int i;
	uint32 phy_rate, dma_rate, tmp;
	u8 max_mpdu;
	wlc_fifo_info_t *fifo = (ampdu->fifo_tb + f);

	/* recompute the dma rate */
	/* note : we divide/multiply by 100 to avoid integer overflows */
	max_mpdu =
	    MIN(fifo->mcs2ampdu_table[FFPLD_MAX_MCS], AMPDU_NUM_MPDU_LEGACY);
	phy_rate = MCS_RATE(FFPLD_MAX_MCS, TRUE, FALSE);
	dma_rate =
	    (((phy_rate / 100) *
	      (max_mpdu * FFPLD_MPDU_SIZE - fifo->ampdu_pld_size))
	     / (max_mpdu * FFPLD_MPDU_SIZE)) * 100;
	fifo->dmaxferrate = dma_rate;

	/* fill up the mcs2ampdu table; do not recalc the last mcs */
	dma_rate = dma_rate >> 7;
	for (i = 0; i < FFPLD_MAX_MCS; i++) {
		/* shifting to keep it within integer range */
		phy_rate = MCS_RATE(i, TRUE, FALSE) >> 7;
		if (phy_rate > dma_rate) {
			tmp = ((fifo->ampdu_pld_size * phy_rate) /
			       ((phy_rate - dma_rate) * FFPLD_MPDU_SIZE)) + 1;
			tmp = MIN(tmp, 255);
			fifo->mcs2ampdu_table[i] = (u8) tmp;
		}
	}
}

static void BCMFASTPATH
wlc_ampdu_agg(ampdu_info_t *ampdu, struct scb *scb, void *p, uint prec)
{
	scb_ampdu_t *scb_ampdu;
	scb_ampdu_tid_ini_t *ini;
	u8 tid = (u8) PKTPRIO(p);

	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);

	/* initialize initiator on first packet; sends addba req */
	ini = SCB_AMPDU_INI(scb_ampdu, tid);
	if (ini->magic != INI_MAGIC) {
		ini = wlc_ampdu_init_tid_ini(ampdu, scb_ampdu, tid, FALSE);
	}
	return;
}

int BCMFASTPATH
wlc_sendampdu(ampdu_info_t *ampdu, wlc_txq_info_t *qi, void **pdu, int prec)
{
	wlc_info_t *wlc;
	osl_t *osh;
	void *p, *pkt[AMPDU_MAX_MPDU];
	u8 tid, ndelim;
	int err = 0;
	u8 preamble_type = WLC_GF_PREAMBLE;
	u8 fbr_preamble_type = WLC_GF_PREAMBLE;
	u8 rts_preamble_type = WLC_LONG_PREAMBLE;
	u8 rts_fbr_preamble_type = WLC_LONG_PREAMBLE;

	bool rr = TRUE, fbr = FALSE;
	uint i, count = 0, fifo, seg_cnt = 0;
	uint16 plen, len, seq = 0, mcl, mch, index, frameid, dma_len = 0;
	uint32 ampdu_len, maxlen = 0;
	d11txh_t *txh = NULL;
	u8 *plcp;
	struct dot11_header *h;
	struct scb *scb;
	scb_ampdu_t *scb_ampdu;
	scb_ampdu_tid_ini_t *ini;
	u8 mcs = 0;
	bool use_rts = FALSE, use_cts = FALSE;
	ratespec_t rspec = 0, rspec_fallback = 0;
	ratespec_t rts_rspec = 0, rts_rspec_fallback = 0;
	uint16 mimo_ctlchbw = PHY_TXC1_BW_20MHZ;
	struct dot11_rts_frame *rts;
	u8 rr_retry_limit;
	wlc_fifo_info_t *f;
	bool fbr_iscck;
	struct ieee80211_tx_info *tx_info;
	uint16 qlen;

	wlc = ampdu->wlc;
	osh = wlc->osh;
	p = *pdu;

	ASSERT(p);

	tid = (u8) PKTPRIO(p);
	ASSERT(tid < AMPDU_MAX_SCB_TID);

	f = ampdu->fifo_tb + prio2fifo[tid];

	scb = wlc->pub->global_scb;
	ASSERT(scb->magic == SCB_MAGIC);

	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	ASSERT(scb_ampdu);
	ini = &scb_ampdu->ini[tid];

	/* Let pressure continue to build ... */
	qlen = pktq_plen(&qi->q, prec);
	if (ini->tx_in_transit > 0 && qlen < scb_ampdu->max_pdu) {
		return BCME_BUSY;
	}

	wlc_ampdu_agg(ampdu, scb, p, tid);

	if (wlc->block_datafifo) {
		WL_ERROR(("%s: Fifo blocked\n", __func__));
		return BCME_BUSY;
	}
	rr_retry_limit = ampdu->rr_retry_limit_tid[tid];
	ampdu_len = 0;
	dma_len = 0;
	while (p) {
		struct ieee80211_tx_rate *txrate;

		tx_info = IEEE80211_SKB_CB(p);
		txrate = tx_info->status.rates;

		if (tx_info->flags & IEEE80211_TX_CTL_AMPDU) {
			err = wlc_prep_pdu(wlc, p, &fifo);
		} else {
			WL_ERROR(("%s: AMPDU flag is off!\n", __func__));
			*pdu = NULL;
			err = 0;
			break;
		}

		if (err) {
			if (err == BCME_BUSY) {
				WL_ERROR(("wl%d: wlc_sendampdu: prep_xdu retry; seq 0x%x\n", wlc->pub->unit, seq));
				WLCNTINCR(ampdu->cnt->sduretry);
				*pdu = p;
				break;
			}

			/* error in the packet; reject it */
			WL_AMPDU_ERR(("wl%d: wlc_sendampdu: prep_xdu rejected; seq 0x%x\n", wlc->pub->unit, seq));
			WLCNTINCR(ampdu->cnt->sdurejected);

			*pdu = NULL;
			break;
		}

		/* pkt is good to be aggregated */
		ASSERT(tx_info->flags & IEEE80211_TX_CTL_AMPDU);
		txh = (d11txh_t *) PKTDATA(p);
		plcp = (u8 *) (txh + 1);
		h = (struct dot11_header *)(plcp + D11_PHY_HDR_LEN);
		seq = ltoh16(h->seq) >> SEQNUM_SHIFT;
		index = TX_SEQ_TO_INDEX(seq);

		/* check mcl fields and test whether it can be agg'd */
		mcl = ltoh16(txh->MacTxControlLow);
		mcl &= ~TXC_AMPDU_MASK;
		fbr_iscck = !(ltoh16(txh->XtraFrameTypes) & 0x3);
		ASSERT(!fbr_iscck);
		txh->PreloadSize = 0;	/* always default to 0 */

		/*  Handle retry limits */
		if (txrate[0].count <= rr_retry_limit) {
			txrate[0].count++;
			rr = TRUE;
			fbr = FALSE;
			ASSERT(!fbr);
		} else {
			fbr = TRUE;
			rr = FALSE;
			txrate[1].count++;
		}

		/* extract the length info */
		len = fbr_iscck ? WLC_GET_CCK_PLCP_LEN(txh->FragPLCPFallback)
		    : WLC_GET_MIMO_PLCP_LEN(txh->FragPLCPFallback);

		/* retrieve null delimiter count */
		ndelim = txh->RTSPLCPFallback[AMPDU_FBR_NULL_DELIM];
		seg_cnt += 1;

		WL_AMPDU_TX(("wl%d: wlc_sendampdu: mpdu %d plcp_len %d\n",
			     wlc->pub->unit, count, len));

		/*
		 * aggregateable mpdu. For ucode/hw agg,
		 * test whether need to break or change the epoch
		 */
		if (count == 0) {
			uint16 fc;
			mcl |= (TXC_AMPDU_FIRST << TXC_AMPDU_SHIFT);
			/* refill the bits since might be a retx mpdu */
			mcl |= TXC_STARTMSDU;
			rts = (struct dot11_rts_frame *)&txh->rts_frame;
			fc = ltoh16(rts->fc);
			if ((fc & FC_KIND_MASK) == FC_RTS) {
				mcl |= TXC_SENDRTS;
				use_rts = TRUE;
			}
			if ((fc & FC_KIND_MASK) == FC_CTS) {
				mcl |= TXC_SENDCTS;
				use_cts = TRUE;
			}
		} else {
			mcl |= (TXC_AMPDU_MIDDLE << TXC_AMPDU_SHIFT);
			mcl &= ~(TXC_STARTMSDU | TXC_SENDRTS | TXC_SENDCTS);
		}

		len = ROUNDUP(len, 4);
		ampdu_len += (len + (ndelim + 1) * AMPDU_DELIMITER_LEN);

		dma_len += (uint16) pkttotlen(osh, p);

		WL_AMPDU_TX(("wl%d: wlc_sendampdu: ampdu_len %d seg_cnt %d null delim %d\n", wlc->pub->unit, ampdu_len, seg_cnt, ndelim));

		txh->MacTxControlLow = htol16(mcl);

		/* this packet is added */
		pkt[count++] = p;

		/* patch the first MPDU */
		if (count == 1) {
			u8 plcp0, plcp3, is40, sgi;
			struct ieee80211_sta *sta;

			sta = tx_info->control.sta;

			if (rr) {
				plcp0 = plcp[0];
				plcp3 = plcp[3];
			} else {
				plcp0 = txh->FragPLCPFallback[0];
				plcp3 = txh->FragPLCPFallback[3];

			}
			is40 = (plcp0 & MIMO_PLCP_40MHZ) ? 1 : 0;
			sgi = PLCP3_ISSGI(plcp3) ? 1 : 0;
			mcs = plcp0 & ~MIMO_PLCP_40MHZ;
			ASSERT(mcs < MCS_TABLE_SIZE);
			maxlen =
			    MIN(scb_ampdu->max_rxlen,
				ampdu->max_txlen[mcs][is40][sgi]);

			WL_NONE(("sendampdu: sgi %d, is40 %d, mcs %d\n", sgi,
				 is40, mcs));

			maxlen = 64 * 1024;	/* XXX Fix me to honor real max_rxlen */

			if (is40)
				mimo_ctlchbw =
				    CHSPEC_SB_UPPER(WLC_BAND_PI_RADIO_CHANSPEC)
				    ? PHY_TXC1_BW_20MHZ_UP : PHY_TXC1_BW_20MHZ;

			/* rebuild the rspec and rspec_fallback */
			rspec = RSPEC_MIMORATE;
			rspec |= plcp[0] & ~MIMO_PLCP_40MHZ;
			if (plcp[0] & MIMO_PLCP_40MHZ)
				rspec |= (PHY_TXC1_BW_40MHZ << RSPEC_BW_SHIFT);

			if (fbr_iscck)	/* CCK */
				rspec_fallback =
				    CCK_RSPEC(CCK_PHY2MAC_RATE
					      (txh->FragPLCPFallback[0]));
			else {	/* MIMO */
				rspec_fallback = RSPEC_MIMORATE;
				rspec_fallback |=
				    txh->FragPLCPFallback[0] & ~MIMO_PLCP_40MHZ;
				if (txh->FragPLCPFallback[0] & MIMO_PLCP_40MHZ)
					rspec_fallback |=
					    (PHY_TXC1_BW_40MHZ <<
					     RSPEC_BW_SHIFT);
			}

			if (use_rts || use_cts) {
				rts_rspec =
				    wlc_rspec_to_rts_rspec(wlc, rspec, FALSE,
							   mimo_ctlchbw);
				rts_rspec_fallback =
				    wlc_rspec_to_rts_rspec(wlc, rspec_fallback,
							   FALSE, mimo_ctlchbw);
			}
		}

		/* if (first mpdu for host agg) */
		/* test whether to add more */
		if ((MCS_RATE(mcs, TRUE, FALSE) >= f->dmaxferrate) &&
		    (count == f->mcs2ampdu_table[mcs])) {
			WL_AMPDU_ERR(("wl%d: PR 37644: stopping ampdu at %d for mcs %d", wlc->pub->unit, count, mcs));
			break;
		}

		if (count == scb_ampdu->max_pdu) {
			WL_NONE(("Stop taking from q, reached %d deep\n",
				 scb_ampdu->max_pdu));
			break;
		}

		/* check to see if the next pkt is a candidate for aggregation */
		p = pktq_ppeek(&qi->q, prec);
		tx_info = IEEE80211_SKB_CB(p);	/* tx_info must be checked with current p */

		if (p) {
			if ((tx_info->flags & IEEE80211_TX_CTL_AMPDU) &&
			    ((u8) PKTPRIO(p) == tid)) {

				plen =
				    pkttotlen(osh, p) + AMPDU_MAX_MPDU_OVERHEAD;
				plen = MAX(scb_ampdu->min_len, plen);

				if ((plen + ampdu_len) > maxlen) {
					p = NULL;
					WL_ERROR(("%s: Bogus plen #1\n",
						  __func__));
					ASSERT(3 == 4);
					continue;
				}

				/* check if there are enough descriptors available */
				if (TXAVAIL(wlc, fifo) <= (seg_cnt + 1)) {
					WL_ERROR(("%s: No fifo space   !!!!!!\n", __func__));
					p = NULL;
					continue;
				}
				p = pktq_pdeq(&qi->q, prec);
				ASSERT(p);
			} else {
				p = NULL;
			}
		}
	}			/* end while(p) */

	ini->tx_in_transit += count;

	if (count) {
		WLCNTADD(ampdu->cnt->txmpdu, count);

		/* patch up the last txh */
		txh = (d11txh_t *) PKTDATA(pkt[count - 1]);
		mcl = ltoh16(txh->MacTxControlLow);
		mcl &= ~TXC_AMPDU_MASK;
		mcl |= (TXC_AMPDU_LAST << TXC_AMPDU_SHIFT);
		txh->MacTxControlLow = htol16(mcl);

		/* remove the null delimiter after last mpdu */
		ndelim = txh->RTSPLCPFallback[AMPDU_FBR_NULL_DELIM];
		txh->RTSPLCPFallback[AMPDU_FBR_NULL_DELIM] = 0;
		ampdu_len -= ndelim * AMPDU_DELIMITER_LEN;

		/* remove the pad len from last mpdu */
		fbr_iscck = ((ltoh16(txh->XtraFrameTypes) & 0x3) == 0);
		len = fbr_iscck ? WLC_GET_CCK_PLCP_LEN(txh->FragPLCPFallback)
		    : WLC_GET_MIMO_PLCP_LEN(txh->FragPLCPFallback);
		ampdu_len -= ROUNDUP(len, 4) - len;

		/* patch up the first txh & plcp */
		txh = (d11txh_t *) PKTDATA(pkt[0]);
		plcp = (u8 *) (txh + 1);

		WLC_SET_MIMO_PLCP_LEN(plcp, ampdu_len);
		/* mark plcp to indicate ampdu */
		WLC_SET_MIMO_PLCP_AMPDU(plcp);

		/* reset the mixed mode header durations */
		if (txh->MModeLen) {
			uint16 mmodelen =
			    wlc_calc_lsig_len(wlc, rspec, ampdu_len);
			txh->MModeLen = htol16(mmodelen);
			preamble_type = WLC_MM_PREAMBLE;
		}
		if (txh->MModeFbrLen) {
			uint16 mmfbrlen =
			    wlc_calc_lsig_len(wlc, rspec_fallback, ampdu_len);
			txh->MModeFbrLen = htol16(mmfbrlen);
			fbr_preamble_type = WLC_MM_PREAMBLE;
		}

		/* set the preload length */
		if (MCS_RATE(mcs, TRUE, FALSE) >= f->dmaxferrate) {
			dma_len = MIN(dma_len, f->ampdu_pld_size);
			txh->PreloadSize = htol16(dma_len);
		} else
			txh->PreloadSize = 0;

		mch = ltoh16(txh->MacTxControlHigh);

		/* update RTS dur fields */
		if (use_rts || use_cts) {
			uint16 durid;
			rts = (struct dot11_rts_frame *)&txh->rts_frame;
			if ((mch & TXC_PREAMBLE_RTS_MAIN_SHORT) ==
			    TXC_PREAMBLE_RTS_MAIN_SHORT)
				rts_preamble_type = WLC_SHORT_PREAMBLE;

			if ((mch & TXC_PREAMBLE_RTS_FB_SHORT) ==
			    TXC_PREAMBLE_RTS_FB_SHORT)
				rts_fbr_preamble_type = WLC_SHORT_PREAMBLE;

			durid =
			    wlc_compute_rtscts_dur(wlc, use_cts, rts_rspec,
						   rspec, rts_preamble_type,
						   preamble_type, ampdu_len,
						   TRUE);
			rts->durid = htol16(durid);
			durid = wlc_compute_rtscts_dur(wlc, use_cts,
						       rts_rspec_fallback,
						       rspec_fallback,
						       rts_fbr_preamble_type,
						       fbr_preamble_type,
						       ampdu_len, TRUE);
			txh->RTSDurFallback = htol16(durid);
			/* set TxFesTimeNormal */
			txh->TxFesTimeNormal = rts->durid;
			/* set fallback rate version of TxFesTimeNormal */
			txh->TxFesTimeFallback = txh->RTSDurFallback;
		}

		/* set flag and plcp for fallback rate */
		if (fbr) {
			WLCNTADD(ampdu->cnt->txfbr_mpdu, count);
			WLCNTINCR(ampdu->cnt->txfbr_ampdu);
			mch |= TXC_AMPDU_FBR;
			txh->MacTxControlHigh = htol16(mch);
			WLC_SET_MIMO_PLCP_AMPDU(plcp);
			WLC_SET_MIMO_PLCP_AMPDU(txh->FragPLCPFallback);
		}

		WL_AMPDU_TX(("wl%d: wlc_sendampdu: count %d ampdu_len %d\n",
			     wlc->pub->unit, count, ampdu_len));

		/* inform rate_sel if it this is a rate probe pkt */
		frameid = ltoh16(txh->TxFrameID);
		if (frameid & TXFID_RATE_PROBE_MASK) {
			WL_ERROR(("%s: XXX what to do with TXFID_RATE_PROBE_MASK!?\n", __func__));
		}
#ifdef WLC_HIGH_ONLY
		if (wlc->rpc_agg & BCM_RPC_TP_HOST_AGG_AMPDU)
			bcm_rpc_tp_agg_set(bcm_rpc_tp_get(wlc->rpc),
					   BCM_RPC_TP_HOST_AGG_AMPDU, TRUE);
#endif
		for (i = 0; i < count; i++)
			wlc_txfifo(wlc, fifo, pkt[i], i == (count - 1),
				   ampdu->txpkt_weight);
#ifdef WLC_HIGH_ONLY
		if (wlc->rpc_agg & BCM_RPC_TP_HOST_AGG_AMPDU)
			bcm_rpc_tp_agg_set(bcm_rpc_tp_get(wlc->rpc),
					   BCM_RPC_TP_HOST_AGG_AMPDU, FALSE);
#endif

	}
	/* endif (count) */
	return err;
}

void BCMFASTPATH
wlc_ampdu_dotxstatus(ampdu_info_t *ampdu, struct scb *scb, void *p,
		     tx_status_t *txs)
{
	scb_ampdu_t *scb_ampdu;
	wlc_info_t *wlc = ampdu->wlc;
	scb_ampdu_tid_ini_t *ini;
	uint32 s1 = 0, s2 = 0;
	struct ieee80211_tx_info *tx_info;

	tx_info = IEEE80211_SKB_CB(p);
	ASSERT(tx_info->flags & IEEE80211_TX_CTL_AMPDU);
	ASSERT(scb);
	ASSERT(scb->magic == SCB_MAGIC);
	ASSERT(txs->status & TX_STATUS_AMPDU);
	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	ASSERT(scb_ampdu);
	ini = SCB_AMPDU_INI(scb_ampdu, PKTPRIO(p));
	ASSERT(ini->scb == scb);

	/* BMAC_NOTE: For the split driver, second level txstatus comes later
	 * So if the ACK was received then wait for the second level else just
	 * call the first one
	 */
	if (txs->status & TX_STATUS_ACK_RCV) {
#ifdef WLC_LOW
		u8 status_delay = 0;

		/* wait till the next 8 bytes of txstatus is available */
		while (((s1 =
			 R_REG(wlc->osh,
			       &wlc->regs->frmtxstatus)) & TXS_V) == 0) {
			OSL_DELAY(1);
			status_delay++;
			if (status_delay > 10) {
				ASSERT(status_delay <= 10);
				return;
			}
		}

		ASSERT(!(s1 & TX_STATUS_INTERMEDIATE));
		ASSERT(s1 & TX_STATUS_AMPDU);
		s2 = R_REG(wlc->osh, &wlc->regs->frmtxstatus2);
#else				/* WLC_LOW */

		/* Store the relevant information in ampdu structure */
		WL_AMPDU_TX(("wl%d: wlc_ampdu_dotxstatus: High Recvd\n",
			     wlc->pub->unit));

		ASSERT(!ampdu->p);
		ampdu->p = p;
		bcopy(txs, &ampdu->txs, sizeof(tx_status_t));
		ampdu->waiting_status = TRUE;
		return;
#endif				/* WLC_LOW */
	}

	wlc_ampdu_dotxstatus_complete(ampdu, scb, p, txs, s1, s2);
	wlc_ampdu_txflowcontrol(wlc, scb_ampdu, ini);
}

#ifdef WLC_HIGH_ONLY
void wlc_ampdu_txstatus_complete(ampdu_info_t *ampdu, uint32 s1, uint32 s2)
{
	WL_AMPDU_TX(("wl%d: wlc_ampdu_txstatus_complete: High Recvd 0x%x 0x%x p:%p\n", ampdu->wlc->pub->unit, s1, s2, ampdu->p));

	ASSERT(ampdu->waiting_status);

	/* The packet may have been freed if the SCB went away, if so, then still free the
	 * DMA chain
	 */
	if (ampdu->p) {
		struct ieee80211_tx_info *tx_info;
		struct scb *scb;

		tx_info = IEEE80211_SKB_CB(ampdu->p);
		scb = (struct scb *)tx_info->control.sta->drv_priv;

		wlc_ampdu_dotxstatus_complete(ampdu, scb, ampdu->p, &ampdu->txs,
					      s1, s2);
		ampdu->p = NULL;
	}

	ampdu->waiting_status = FALSE;
}
#endif				/* WLC_HIGH_ONLY */
void rate_status(wlc_info_t *wlc, struct ieee80211_tx_info *tx_info,
		 tx_status_t *txs, u8 mcs);

void
rate_status(wlc_info_t *wlc, struct ieee80211_tx_info *tx_info,
	    tx_status_t *txs, u8 mcs)
{
	struct ieee80211_tx_rate *txrate = tx_info->status.rates;
	int i;

	/* clear the rest of the rates */
	for (i = 2; i < IEEE80211_TX_MAX_RATES; i++) {
		txrate[i].idx = -1;
		txrate[i].count = 0;
	}
}

extern void wlc_txq_enq(wlc_info_t *wlc, struct scb *scb, void *sdu,
			uint prec);

#define SHORTNAME "AMPDU status"

static void BCMFASTPATH
wlc_ampdu_dotxstatus_complete(ampdu_info_t *ampdu, struct scb *scb, void *p,
			      tx_status_t *txs, uint32 s1, uint32 s2)
{
	scb_ampdu_t *scb_ampdu;
	wlc_info_t *wlc = ampdu->wlc;
	scb_ampdu_tid_ini_t *ini;
	u8 bitmap[8], queue, tid;
	d11txh_t *txh;
	u8 *plcp;
	struct dot11_header *h;
	uint16 seq, start_seq = 0, bindex, index, mcl;
	u8 mcs = 0;
	bool ba_recd = FALSE, ack_recd = FALSE;
	u8 suc_mpdu = 0, tot_mpdu = 0;
	uint supr_status;
	bool update_rate = TRUE, retry = TRUE, tx_error = FALSE;
	uint16 mimoantsel = 0;
	u8 antselid = 0;
	u8 retry_limit, rr_retry_limit;
	struct ieee80211_tx_info *tx_info = IEEE80211_SKB_CB(p);

#ifdef BCMDBG
	u8 hole[AMPDU_MAX_MPDU];
	bzero(hole, sizeof(hole));
#endif

	ASSERT(tx_info->flags & IEEE80211_TX_CTL_AMPDU);
	ASSERT(txs->status & TX_STATUS_AMPDU);

	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	ASSERT(scb_ampdu);

	tid = (u8) PKTPRIO(p);

	ini = SCB_AMPDU_INI(scb_ampdu, tid);
	retry_limit = ampdu->retry_limit_tid[tid];
	rr_retry_limit = ampdu->rr_retry_limit_tid[tid];

	ASSERT(ini->scb == scb);

	bzero(bitmap, sizeof(bitmap));
	queue = txs->frameid & TXFID_QUEUE_MASK;
	ASSERT(queue < AC_COUNT);

	supr_status = txs->status & TX_STATUS_SUPR_MASK;

	if (txs->status & TX_STATUS_ACK_RCV) {
		if (TX_STATUS_SUPR_UF == supr_status) {
			update_rate = FALSE;
		}

		ASSERT(txs->status & TX_STATUS_INTERMEDIATE);
		start_seq = txs->sequence >> SEQNUM_SHIFT;
		bitmap[0] = (txs->status & TX_STATUS_BA_BMAP03_MASK) >>
		    TX_STATUS_BA_BMAP03_SHIFT;

		ASSERT(!(s1 & TX_STATUS_INTERMEDIATE));
		ASSERT(s1 & TX_STATUS_AMPDU);

		bitmap[0] |=
		    (s1 & TX_STATUS_BA_BMAP47_MASK) <<
		    TX_STATUS_BA_BMAP47_SHIFT;
		bitmap[1] = (s1 >> 8) & 0xff;
		bitmap[2] = (s1 >> 16) & 0xff;
		bitmap[3] = (s1 >> 24) & 0xff;

		bitmap[4] = s2 & 0xff;
		bitmap[5] = (s2 >> 8) & 0xff;
		bitmap[6] = (s2 >> 16) & 0xff;
		bitmap[7] = (s2 >> 24) & 0xff;

		ba_recd = TRUE;
	} else {
		WLCNTINCR(ampdu->cnt->noba);
		if (supr_status) {
			update_rate = FALSE;
			if (supr_status == TX_STATUS_SUPR_BADCH) {
				WL_ERROR(("%s: Pkt tx suppressed, illegal channel possibly %d\n", __func__, CHSPEC_CHANNEL(wlc->default_bss->chanspec)));
			} else {
				if (supr_status == TX_STATUS_SUPR_FRAG)
					WL_NONE(("%s: AMPDU frag err\n",
						 __func__));
				else
					WL_ERROR(("%s: wlc_ampdu_dotxstatus: supr_status 0x%x\n", __func__, supr_status));
			}
			/* no need to retry for badch; will fail again */
			if (supr_status == TX_STATUS_SUPR_BADCH ||
			    supr_status == TX_STATUS_SUPR_EXPTIME) {
				retry = FALSE;
				WLCNTINCR(wlc->pub->_cnt->txchanrej);
			} else if (supr_status == TX_STATUS_SUPR_EXPTIME) {

				WLCNTINCR(wlc->pub->_cnt->txexptime);

				/* TX underflow : try tuning pre-loading or ampdu size */
			} else if (supr_status == TX_STATUS_SUPR_FRAG) {
				/* if there were underflows, but pre-loading is not active,
				   notify rate adaptation.
				 */
				if (wlc_ffpld_check_txfunfl(wlc, prio2fifo[tid])
				    > 0) {
					tx_error = TRUE;
#ifdef WLC_HIGH_ONLY
					/* With BMAC, TX Underflows should not happen */
					WL_ERROR(("wl%d: BMAC TX Underflow?",
						  wlc->pub->unit));
#endif
				}
			}
		} else if (txs->phyerr) {
			update_rate = FALSE;
			WLCNTINCR(wlc->pub->_cnt->txphyerr);
			WL_ERROR(("wl%d: wlc_ampdu_dotxstatus: tx phy error (0x%x)\n", wlc->pub->unit, txs->phyerr));

#ifdef BCMDBG
			if (WL_ERROR_ON()) {
				prpkt("txpkt (AMPDU)", wlc->osh, p);
				wlc_print_txdesc((d11txh_t *) PKTDATA(p));
				wlc_print_txstatus(txs);
			}
#endif				/* BCMDBG */
		}
	}

	/* loop through all pkts and retry if not acked */
	while (p) {
		tx_info = IEEE80211_SKB_CB(p);
		ASSERT(tx_info->flags & IEEE80211_TX_CTL_AMPDU);
		txh = (d11txh_t *) PKTDATA(p);
		mcl = ltoh16(txh->MacTxControlLow);
		plcp = (u8 *) (txh + 1);
		h = (struct dot11_header *)(plcp + D11_PHY_HDR_LEN);
		seq = ltoh16(h->seq) >> SEQNUM_SHIFT;

		if (tot_mpdu == 0) {
			mcs = plcp[0] & MIMO_PLCP_MCS_MASK;
			mimoantsel = ltoh16(txh->ABI_MimoAntSel);
		}

		index = TX_SEQ_TO_INDEX(seq);
		ack_recd = FALSE;
		if (ba_recd) {
			bindex = MODSUB_POW2(seq, start_seq, SEQNUM_MAX);

			WL_AMPDU_TX(("%s: tid %d seq is %d, start_seq is %d, "
				     "bindex is %d set %d, index %d\n",
				     __func__, tid, seq, start_seq, bindex,
				     isset(bitmap, bindex), index));

			/* if acked then clear bit and free packet */
			if ((bindex < AMPDU_TX_BA_MAX_WSIZE)
			    && isset(bitmap, bindex)) {
				ini->tx_in_transit--;
				ini->txretry[index] = 0;

				/* ampdu_ack_len: number of acked aggregated frames */
				/* ampdu_ack_map: block ack bit map for the aggregation */
				/* ampdu_len: number of aggregated frames */
				rate_status(wlc, tx_info, txs, mcs);
				tx_info->flags |= IEEE80211_TX_STAT_ACK;
				tx_info->flags |= IEEE80211_TX_STAT_AMPDU;

				/* XXX TODO: Make these accurate. */
				tx_info->status.ampdu_ack_len =
				    (txs->
				     status & TX_STATUS_FRM_RTX_MASK) >>
				    TX_STATUS_FRM_RTX_SHIFT;
				tx_info->status.ampdu_len =
				    (txs->
				     status & TX_STATUS_FRM_RTX_MASK) >>
				    TX_STATUS_FRM_RTX_SHIFT;

				PKTPULL(p, D11_PHY_HDR_LEN);
				PKTPULL(p, D11_TXH_LEN);

				ieee80211_tx_status_irqsafe(wlc->pub->ieee_hw,
							    p);
				ack_recd = TRUE;
				suc_mpdu++;
			}
		}
		/* either retransmit or send bar if ack not recd */
		if (!ack_recd) {
			struct ieee80211_tx_rate *txrate =
			    tx_info->status.rates;
			if (retry && (txrate[0].count < (int)retry_limit)) {
				ini->txretry[index]++;
				ini->tx_in_transit--;
				/* Use high prededence for retransmit to give some punch */
				/* wlc_txq_enq(wlc, scb, p, WLC_PRIO_TO_PREC(tid)); */
				wlc_txq_enq(wlc, scb, p,
					    WLC_PRIO_TO_HI_PREC(tid));
			} else {
				/* Retry timeout */
				ini->tx_in_transit--;
				ieee80211_tx_info_clear_status(tx_info);
				tx_info->flags |=
				    IEEE80211_TX_STAT_AMPDU_NO_BACK;
				PKTPULL(p, D11_PHY_HDR_LEN);
				PKTPULL(p, D11_TXH_LEN);
				WL_ERROR(("%s: BA Timeout, seq %d, in_transit %d\n", SHORTNAME, seq, ini->tx_in_transit));
				ieee80211_tx_status_irqsafe(wlc->pub->ieee_hw,
							    p);
			}
		}
		tot_mpdu++;

		/* break out if last packet of ampdu */
		if (((mcl & TXC_AMPDU_MASK) >> TXC_AMPDU_SHIFT) ==
		    TXC_AMPDU_LAST)
			break;

		p = GETNEXTTXP(wlc, queue);
		if (p == NULL) {
			ASSERT(p);
			break;
		}
	}
	wlc_send_q(wlc, wlc->active_queue);

	/* update rate state */
	if (WLANTSEL_ENAB(wlc))
		antselid = wlc_antsel_antsel2id(wlc->asi, mimoantsel);

	wlc_txfifo_complete(wlc, queue, ampdu->txpkt_weight);
}

static void
ampdu_cleanup_tid_ini(ampdu_info_t *ampdu, scb_ampdu_t *scb_ampdu, u8 tid,
		      bool force)
{
	scb_ampdu_tid_ini_t *ini;
	ini = SCB_AMPDU_INI(scb_ampdu, tid);
	if (!ini)
		return;

	WL_AMPDU_CTL(("wl%d: ampdu_cleanup_tid_ini: tid %d\n",
		      ampdu->wlc->pub->unit, tid));

	if (ini->tx_in_transit && !force)
		return;

	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, ini->scb);
	ASSERT(ini == &scb_ampdu->ini[ini->tid]);

	/* free all buffered tx packets */
	pktq_pflush(ampdu->wlc->osh, &scb_ampdu->txq, ini->tid, TRUE, NULL, 0);
}

/* initialize the initiator code for tid */
static scb_ampdu_tid_ini_t *wlc_ampdu_init_tid_ini(ampdu_info_t *ampdu,
						   scb_ampdu_t *scb_ampdu,
						   u8 tid, bool override)
{
	scb_ampdu_tid_ini_t *ini;

	ASSERT(scb_ampdu);
	ASSERT(scb_ampdu->scb);
	ASSERT(SCB_AMPDU(scb_ampdu->scb));
	ASSERT(tid < AMPDU_MAX_SCB_TID);

	/* check for per-tid control of ampdu */
	if (!ampdu->ini_enable[tid]) {
		WL_ERROR(("%s: Rejecting tid %d\n", __func__, tid));
		return NULL;
	}

	ini = SCB_AMPDU_INI(scb_ampdu, tid);
	ini->tid = tid;
	ini->scb = scb_ampdu->scb;
	ini->magic = INI_MAGIC;
	WLCNTINCR(ampdu->cnt->txaddbareq);

	return ini;
}

int wlc_ampdu_set(ampdu_info_t *ampdu, bool on)
{
	wlc_info_t *wlc = ampdu->wlc;

	wlc->pub->_ampdu = FALSE;

	if (on) {
		if (!N_ENAB(wlc->pub)) {
			WL_AMPDU_ERR(("wl%d: driver not nmode enabled\n",
				      wlc->pub->unit));
			return BCME_UNSUPPORTED;
		}
		if (!wlc_ampdu_cap(ampdu)) {
			WL_AMPDU_ERR(("wl%d: device not ampdu capable\n",
				      wlc->pub->unit));
			return BCME_UNSUPPORTED;
		}
		wlc->pub->_ampdu = on;
	}

	return 0;
}

bool wlc_ampdu_cap(ampdu_info_t *ampdu)
{
	if (WLC_PHY_11N_CAP(ampdu->wlc->band))
		return TRUE;
	else
		return FALSE;
}

static void ampdu_update_max_txlen(ampdu_info_t *ampdu, u8 dur)
{
	uint32 rate, mcs;

	for (mcs = 0; mcs < MCS_TABLE_SIZE; mcs++) {
		/* rate is in Kbps; dur is in msec ==> len = (rate * dur) / 8 */
		/* 20MHz, No SGI */
		rate = MCS_RATE(mcs, FALSE, FALSE);
		ampdu->max_txlen[mcs][0][0] = (rate * dur) >> 3;
		/* 40 MHz, No SGI */
		rate = MCS_RATE(mcs, TRUE, FALSE);
		ampdu->max_txlen[mcs][1][0] = (rate * dur) >> 3;
		/* 20MHz, SGI */
		rate = MCS_RATE(mcs, FALSE, TRUE);
		ampdu->max_txlen[mcs][0][1] = (rate * dur) >> 3;
		/* 40 MHz, SGI */
		rate = MCS_RATE(mcs, TRUE, TRUE);
		ampdu->max_txlen[mcs][1][1] = (rate * dur) >> 3;
	}
}

u8 BCMFASTPATH
wlc_ampdu_null_delim_cnt(ampdu_info_t *ampdu, struct scb *scb,
			 ratespec_t rspec, int phylen)
{
	scb_ampdu_t *scb_ampdu;
	int bytes, cnt, tmp;
	u8 tx_density;

	ASSERT(scb);
	ASSERT(SCB_AMPDU(scb));

	scb_ampdu = SCB_AMPDU_CUBBY(ampdu, scb);
	ASSERT(scb_ampdu);

	if (scb_ampdu->mpdu_density == 0)
		return 0;

	/* RSPEC2RATE is in kbps units ==> ~RSPEC2RATE/2^13 is in bytes/usec
	   density x is in 2^(x-4) usec
	   ==> # of bytes needed for req density = rate/2^(17-x)
	   ==> # of null delimiters = ceil(ceil(rate/2^(17-x)) - phylen)/4)
	 */

	tx_density = scb_ampdu->mpdu_density;

	ASSERT(tx_density <= AMPDU_MAX_MPDU_DENSITY);
	tmp = 1 << (17 - tx_density);
	bytes = CEIL(RSPEC2RATE(rspec), tmp);

	if (bytes > phylen) {
		cnt = CEIL(bytes - phylen, AMPDU_DELIMITER_LEN);
		ASSERT(cnt <= 255);
		return (u8) cnt;
	} else
		return 0;
}

void wlc_ampdu_macaddr_upd(wlc_info_t *wlc)
{
	char template[T_RAM_ACCESS_SZ * 2];

	/* driver needs to write the ta in the template; ta is at offset 16 */
	bzero(template, sizeof(template));
	bcopy((char *)wlc->pub->cur_etheraddr.octet, template, ETHER_ADDR_LEN);
	wlc_write_template_ram(wlc, (T_BA_TPL_BASE + 16), (T_RAM_ACCESS_SZ * 2),
			       template);
}

bool wlc_aggregatable(wlc_info_t *wlc, u8 tid)
{
	return wlc->ampdu->ini_enable[tid];
}

void wlc_ampdu_shm_upd(ampdu_info_t *ampdu)
{
	wlc_info_t *wlc = ampdu->wlc;

	/* Extend ucode internal watchdog timer to match larger received frames */
	if ((ampdu->rx_factor & HT_PARAMS_RX_FACTOR_MASK) ==
	    AMPDU_RX_FACTOR_64K) {
		wlc_write_shm(wlc, M_MIMO_MAXSYM, MIMO_MAXSYM_MAX);
		wlc_write_shm(wlc, M_WATCHDOG_8TU, WATCHDOG_8TU_MAX);
	} else {
		wlc_write_shm(wlc, M_MIMO_MAXSYM, MIMO_MAXSYM_DEF);
		wlc_write_shm(wlc, M_WATCHDOG_8TU, WATCHDOG_8TU_DEF);
	}
}
