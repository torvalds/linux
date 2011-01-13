/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
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

struct ath_txq;
struct ath_buf;

#ifdef CONFIG_ATH9K_DEBUGFS
#define TX_STAT_INC(q, c) sc->debug.stats.txstats[q].c++
#else
#define TX_STAT_INC(q, c) do { } while (0)
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
 * @a_queued: Total AMPDUs queued
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
 */
struct ath_tx_stats {
	u32 tx_pkts_all;
	u32 tx_bytes_all;
	u32 queued;
	u32 completed;
	u32 a_aggr;
	u32 a_queued;
	u32 a_completed;
	u32 a_retries;
	u32 a_xretries;
	u32 fifo_underrun;
	u32 xtxop;
	u32 timer_exp;
	u32 desc_cfg_err;
	u32 data_underrun;
	u32 delim_underrun;
};

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
};

struct ath_stats {
	struct ath_interrupt_stats istats;
	struct ath_tx_stats txstats[ATH9K_NUM_TX_QUEUES];
	struct ath_rx_stats rxstats;
};

struct ath9k_debug {
	struct dentry *debugfs_phy;
	u32 regidx;
	struct ath_stats stats;
};

int ath9k_init_debug(struct ath_hw *ah);

void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status);
void ath_debug_stat_tx(struct ath_softc *sc, struct ath_buf *bf,
		       struct ath_tx_status *ts);
void ath_debug_stat_rx(struct ath_softc *sc, struct ath_rx_status *rs);

#else

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
				     struct ath_tx_status *ts)
{
}

static inline void ath_debug_stat_rx(struct ath_softc *sc,
				     struct ath_rx_status *rs)
{
}

#endif /* CONFIG_ATH9K_DEBUGFS */

#endif /* DEBUG_H */
