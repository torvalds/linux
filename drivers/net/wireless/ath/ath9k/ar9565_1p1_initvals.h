/*
 * Copyright (c) 2010-2011 Atheros Communications Inc.
 * Copyright (c) 2011-2012 Qualcomm Atheros Inc.
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

#ifndef INITVALS_9565_1P1_H
#define INITVALS_9565_1P1_H

/* AR9565 1.1 */

#define ar9565_1p1_mac_core ar9565_1p0_mac_core

#define ar9565_1p1_mac_postamble ar9565_1p0_mac_postamble

#define ar9565_1p1_baseband_core ar9565_1p0_baseband_core

#define ar9565_1p1_baseband_postamble ar9565_1p0_baseband_postamble

#define ar9565_1p1_radio_core ar9565_1p0_radio_core

#define ar9565_1p1_soc_preamble ar9565_1p0_soc_preamble

#define ar9565_1p1_soc_postamble ar9565_1p0_soc_postamble

#define ar9565_1p1_Common_rx_gain_table ar9565_1p0_Common_rx_gain_table

#define ar9565_1p1_Modes_lowest_ob_db_tx_gain_table ar9565_1p0_Modes_lowest_ob_db_tx_gain_table

#define ar9565_1p1_pciephy_clkreq_disable_L1 ar9565_1p0_pciephy_clkreq_disable_L1

#define ar9565_1p1_modes_fast_clock ar9565_1p0_modes_fast_clock

#define ar9565_1p1_common_wo_xlna_rx_gain_table ar9565_1p0_common_wo_xlna_rx_gain_table

#define ar9565_1p1_modes_low_ob_db_tx_gain_table ar9565_1p0_modes_low_ob_db_tx_gain_table

#define ar9565_1p1_modes_high_ob_db_tx_gain_table ar9565_1p0_modes_high_ob_db_tx_gain_table

#define ar9565_1p1_modes_high_power_tx_gain_table ar9565_1p0_modes_high_power_tx_gain_table

#define ar9565_1p1_baseband_core_txfir_coeff_japan_2484 ar9565_1p0_baseband_core_txfir_coeff_japan_2484

static const u32 ar9565_1p1_radio_postamble[][5] = {
	/* Addr      5G_HT20     5G_HT40     2G_HT40     2G_HT20   */
	{0x0001609c, 0x0b8ee524, 0x0b8ee524, 0x0b8ee524, 0x0b8ee524},
	{0x000160ac, 0xa4646c08, 0xa4646c08, 0x24645808, 0x24645808},
	{0x000160b0, 0x01d67f70, 0x01d67f70, 0x01d67f70, 0x01d67f70},
	{0x0001610c, 0x40000000, 0x40000000, 0x40000000, 0x40000000},
	{0x00016140, 0x10804008, 0x10804008, 0x50804008, 0x50804008},
};

#endif /* INITVALS_9565_1P1_H */
