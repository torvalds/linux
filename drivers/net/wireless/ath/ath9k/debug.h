/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
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

#ifndef DEBUG_H
#define DEBUG_H

#include "hw.h"
#include "rc.h"
#include "dfs_debug.h"

struct ath_txq;
struct ath_buf;

#ifdef CONFIG_ATH9K_DEBUGFS
#define TX_STAT_INC(q, c) sc->debug.stats.txstats[q].c++
#define RESET_STAT_INC(sc, type) sc->debug.stats.reset[type]++
#else
#define TX_STAT_INC(q, c) do { } while (0)
#define RESET_STAT_INC(sc, type) do { } while (0)
#endif

#ifdef CONFIG_ATH9K_DEBUGFS

/**
 * struct ath_interrupt_stats - Contains statistics about interrupts
 * @total: Total no. of interrupts generated so far
 * @rxok: RX with no errors
 * @rxlp: RX with low priority RX
 * @rxhp: RX with high priority, uapsd only
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
 * @bb_watchdog: Baseband watchdog
 * @tsfoor: TSF out of range, indicates that the corrected TSF received
 * from a beacon differs from the PCU's internal TSF by more than a
 * (programmable) threshold
 * @local_timeout: Internal bus timeout.
 */
struct ath_interrupt_stats {
	u32 total;
	u32 rxok;
	u32 rxlp;
	u32 rxhp;
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
	u32 bb_watchdog;
	u32 tsfoor;

	/* Sync-cause stats */
	u32 sync_cause_all;
	u32 sync_rtc_irq;
	u32 sync_mac_irq;
	u32 eeprom_illegal_access;
	u32 apb_timeout;
	u32 pci_mode_conflict;
	u32 host1_fatal;
	u32 host1_perr;
	u32 trcv_fifo_perr;
	u32 radm_cpl_ep;
	u32 radm_cpl_dllp_abort;
	u32 radm_cpl_tlp_abort;
	u32 radm_cpl_ecrc_err;
	u32 radm_cpl_timeout;
	u32 local_timeout;
	u32 pm_access;
	u32 mac_awake;
	u32 mac_asleep;
	u32 mac_sleep_access;
};


/**
 * struct ath_tx_stats - Statistics about TX
 * @tx_pkts_all:  No. of total frames transmitted, including ones that
	may have had errors.
 * @tx_bytes_all:  No. of total bytes transmitted, including ones that
	may have had errors.
 * @queued: Total MPDUs (non-aggr) queued
 * @completed: Total MPDUs (non-aggr) completed
 * @a_aggr: Total no. of aggregates queued
 * @a_queued_hw: Total AMPDUs queued to hardware
 * @a_queued_sw: Total AMPDUs queued to software queues
 * @a_completed: Total AMPDUs completed
 * @a_retries: No. of AMPDUs retried (SW)
 * @a_xretries: No. of AMPDUs dropped due to xretries
 * @fifo_underrun: FIFO underrun occurrences
	Valid only for:
		- non-aggregate condition.
		- first packet of aggregate.
 * @xtxop: No. of frames filtered because of TXOP limit
 * @timer_exp: Transmit timer expiry
 * @desc_cfg_err: Descriptor configuration errors
 * @data_urn: TX data underrun errors
 * @delim_urn: TX delimiter underrun errors
 * @puttxbuf: Number of times hardware was given txbuf to write.
 * @txstart:  Number of times hardware was told to start tx.
 * @txprocdesc:  Number of times tx descriptor was processed
 * @txfailed:  Out-of-memory or other errors in xmit path.
 */
struct ath_tx_stats {
	u32 tx_pkts_all;
	u32 tx_bytes_all;
	u32 queued;
	u32 completed;
	u32 xretries;
	u32 a_aggr;
	u32 a_queued_hw;
	u32 a_queued_sw;
	u32 a_completed;
	u32 a_retries;
	u32 a_xretries;
	u32 fifo_underrun;
	u32 xtxop;
	u32 timer_exp;
	u32 desc_cfg_err;
	u32 data_underrun;
	u32 delim_underrun;
	u32 puttxbuf;
	u32 txstart;
	u32 txprocdesc;
	u32 txfailed;
};

#define RX_STAT_INC(c) (sc->debug.stats.rxstats.c++)

/**
 * struct ath_rx_stats - RX Statistics
 * @rx_pkts_all:  No. of total frames received, including ones that
	may have had errors.
 * @rx_bytes_all:  No. of total bytes received, including ones that
	may have had errors.
 * @crc_err: No. of frames with incorrect CRC value
 * @decrypt_crc_err: No. of frames whose CRC check failed after
	decryption process completed
 * @phy_err: No. of frames whose reception failed because the PHY
	encountered an error
 * @mic_err: No. of frames with incorrect TKIP MIC verification failure
 * @pre_delim_crc_err: Pre-Frame delimiter CRC error detections
 * @post_delim_crc_err: Post-Frame delimiter CRC error detections
 * @decrypt_busy_err: Decryption interruptions counter
 * @phy_err_stats: Individual PHY error statistics
 * @rx_len_err:  No. of frames discarded due to bad length.
 * @rx_oom_err:  No. of frames dropped due to OOM issues.
 * @rx_rate_err:  No. of frames dropped due to rate errors.
 * @rx_too_many_frags_err:  Frames dropped due to too-many-frags received.
 * @rx_drop_rxflush: No. of frames dropped due to RX-FLUSH.
 * @rx_beacons:  No. of beacons received.
 * @rx_frags:  No. of rx-fragements received.
 */
struct ath_rx_stats {
	u32 rx_pkts_all;
	u32 rx_bytes_all;
	u32 crc_err;
	u32 decrypt_crc_err;
	u32 phy_err;
	u32 mic_err;
	u32 pre_delim_crc_err;
	u32 post_delim_crc_err;
	u32 decrypt_busy_err;
	u32 phy_err_stats[ATH9K_PHYERR_MAX];
	u32 rx_len_err;
	u32 rx_oom_err;
	u32 rx_rate_err;
	u32 rx_too_many_frags_err;
	u32 rx_drop_rxflush;
	u32 rx_beacons;
	u32 rx_frags;
};

enum ath_reset_type {
	RESET_TYPE_BB_HANG,
	RESET_TYPE_BB_WATCHDOG,
	RESET_TYPE_FATAL_INT,
	RESET_TYPE_TX_ERROR,
	RESET_TYPE_TX_HANG,
	RESET_TYPE_PLL_HANG,
	RESET_TYPE_MAC_HANG,
	__RESET_TYPE_MAX
};

struct ath_stats {
	struct ath_interrupt_stats istats;
	struct ath_tx_stats txstats[ATH9K_NUM_TX_QUEUES];
	struct ath_rx_stats rxstats;
	struct ath_dfs_stats dfs_stats;
	u32 reset[__RESET_TYPE_MAX];
};

#define ATH_DBG_MAX_SAMPLES	10
struct ath_dbg_bb_mac_samp {
	u32 dma_dbg_reg_vals[ATH9K_NUM_DMA_DEBUG_REGS];
	u32 pcu_obs, pcu_cr, noise;
	struct {
		u64 jiffies;
		int8_t rssi_ctl0;
		int8_t rssi_ctl1;
		int8_t rssi_ctl2;
		int8_t rssi_ext0;
		int8_t rssi_ext1;
		int8_t rssi_ext2;
		int8_t rssi;
		bool isok;
		u8 rts_fail_cnt;
		u8 data_fail_cnt;
		u8 rateindex;
		u8 qid;
		u8 tid;
		u32 ba_low;
		u32 ba_high;
	} ts[ATH_DBG_MAX_SAMPLES];
	struct {
		u64 jiffies;
		int8_t rssi_ctl0;
		int8_t rssi_ctl1;
		int8_t rssi_ctl2;
		int8_t rssi_ext0;
		int8_t rssi_ext1;
		int8_t rssi_ext2;
		int8_t rssi;
		bool is_mybeacon;
		u8 antenna;
		u8 rate;
	} rs[ATH_DBG_MAX_SAMPLES];
	struct ath_cycle_counters cc;
	struct ath9k_nfcal_hist nfCalHist[NUM_NF_READINGS];
};

struct ath9k_debug {
	struct dentry *debugfs_phy;
	u32 regidx;
	struct ath_stats stats;
#ifdef CONFIG_ATH9K_MAC_DEBUG
	spinlock_t samp_lock;
	struct ath_dbg_bb_mac_samp bb_mac_samp[ATH_DBG_MAX_SAMPLES];
	u8 sampidx;
	u8 tsidx;
	u8 rsidx;
#endif
};

int ath9k_init_debug(struct ath_hw *ah);

void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status);
void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts, struct ath_txq *txq,
		       unsigned int flags);
void ath_debug_stat_rx(struct ath_softc *sc, struct ath_rx_status *rs);

#else

#define RX_STAT_INC(c) /* NOP */

static inline int ath9k_init_debug(struct ath_hw *ah)
{
	return 0;
}

static inline void ath_debug_stat_interrupt(struct ath_softc *sc,
					    enum ath9k_int status)
{
}

static inline void ath_debug_stat_tx(struct ath_softc *sc,
				     struct ath_buf *bf,
				     struct ath_tx_status *ts,
				     struct ath_txq *txq,
				     unsigned int flags)
{
}

static inline void ath_debug_stat_rx(struct ath_softc *sc,
				     struct ath_rx_status *rs)
{
}

#endif /* CONFIG_ATH9K_DEBUGFS */

#ifdef CONFIG_ATH9K_MAC_DEBUG

void ath9k_debug_samp_bb_mac(struct ath_softc *sc);

#else

static inline void ath9k_debug_samp_bb_mac(struct ath_softc *sc)
{
}

#endif


#endif /* DEBUG_H */
