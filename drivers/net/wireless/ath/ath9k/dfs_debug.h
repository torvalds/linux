/*
 * Copyright (c) 2008-2011 Atheros Communications Inc.
 * Copyright (c) 2011 Neratec Solutions AG
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


#ifndef ATH9K_DFS_DEBUG_H
#define ATH9K_DFS_DEBUG_H

#include "hw.h"

/**
 * struct ath_dfs_stats - DFS Statistics per wiphy
 * @pulses_total:     pulses reported by HW
 * @pulses_no_dfs:    pulses wrongly reported as DFS
 * @pulses_detected:  pulses detected so far
 * @datalen_discards: pulses discarded due to invalid datalen
 * @rssi_discards:    pulses discarded due to invalid RSSI
 * @bwinfo_discards:  pulses discarded due to invalid BW info
 * @pri_phy_errors:   pulses reported for primary channel
 * @ext_phy_errors:   pulses reported for extension channel
 * @dc_phy_errors:    pulses reported for primary + extension channel
 * @pulses_processed: pulses forwarded to detector
 * @radar_detected:   radars detected
 */
struct ath_dfs_stats {
	/* pulse stats */
	u32 pulses_total;
	u32 pulses_no_dfs;
	u32 pulses_detected;
	u32 datalen_discards;
	u32 rssi_discards;
	u32 bwinfo_discards;
	u32 pri_phy_errors;
	u32 ext_phy_errors;
	u32 dc_phy_errors;
	/* pattern detection stats */
	u32 pulses_processed;
	u32 radar_detected;
};

/**
 * struct ath_dfs_pool_stats - DFS Statistics for global pools
 */
struct ath_dfs_pool_stats {
	u32 pool_reference;
	u32 pulse_allocated;
	u32 pulse_alloc_error;
	u32 pulse_used;
	u32 pseq_allocated;
	u32 pseq_alloc_error;
	u32 pseq_used;
};
#if defined(CONFIG_ATH9K_DFS_DEBUGFS)

#define DFS_STAT_INC(sc, c) (sc->debug.stats.dfs_stats.c++)
void ath9k_dfs_init_debug(struct ath_softc *sc);

#define DFS_POOL_STAT_INC(c) (global_dfs_pool_stats.c++)
#define DFS_POOL_STAT_DEC(c) (global_dfs_pool_stats.c--)
extern struct ath_dfs_pool_stats global_dfs_pool_stats;

#else

#define DFS_STAT_INC(sc, c) do { } while (0)
static inline void ath9k_dfs_init_debug(struct ath_softc *sc) { }

#define DFS_POOL_STAT_INC(c) do { } while (0)
#define DFS_POOL_STAT_DEC(c) do { } while (0)
#endif /* CONFIG_ATH9K_DFS_DEBUGFS */

#endif /* ATH9K_DFS_DEBUG_H */
