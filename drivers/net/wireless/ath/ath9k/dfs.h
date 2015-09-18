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

#ifndef ATH9K_DFS_H
#define ATH9K_DFS_H
#include "../dfs_pattern_detector.h"

#if defined(CONFIG_ATH9K_DFS_CERTIFIED)
/**
 * ath9k_dfs_process_phyerr - process radar PHY error
 * @sc: ath_softc
 * @data: RX payload data
 * @rs: RX status after processing descriptor
 * @mactime: receive time
 *
 * This function is called whenever the HW DFS module detects a radar
 * pulse and reports it as a PHY error.
 *
 * The radar information provided as raw payload data is validated and
 * filtered for false pulses. Events passing all tests are forwarded to
 * the DFS detector for pattern detection.
 */
void ath9k_dfs_process_phyerr(struct ath_softc *sc, void *data,
			      struct ath_rx_status *rs, u64 mactime);
#else
static inline void
ath9k_dfs_process_phyerr(struct ath_softc *sc, void *data,
			 struct ath_rx_status *rs, u64 mactime) { }
#endif

#endif /* ATH9K_DFS_H */
