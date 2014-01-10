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

#include "hw.h"
#include "ar9003_mac.h"
#include "ar9003_2p2_initvals.h"
#include "ar9003_buffalo_initvals.h"
#include "ar9485_initvals.h"
#include "ar9340_initvals.h"
#include "ar9330_1p1_initvals.h"
#include "ar9330_1p2_initvals.h"
#include "ar955x_1p0_initvals.h"
#include "ar9580_1p0_initvals.h"
#include "ar9462_2p0_initvals.h"
#include "ar9462_2p1_initvals.h"
#include "ar9565_1p0_initvals.h"
#include "ar9565_1p1_initvals.h"
#include "ar953x_initvals.h"

/* General hardware code for the AR9003 hadware family */

/*
 * The AR9003 family uses a new INI format (pre, core, post
 * arrays per subsystem). This provides support for the
 * AR9003 2.2 chipsets.
 */
static void ar9003_hw_init_mode_regs(struct ath_hw *ah)
{
	if (AR_SREV_9330_11(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9331_1p1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9331_1p1_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9331_1p1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9331_1p1_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9331_1p1_radio_core);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9331_1p1_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9331_1p1_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p1);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9331_modes_lowest_ob_db_tx_gain_1p1);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9331_1p1_baseband_core_txfir_coeff_japan_2484);

		/* additional clock settings */
		if (ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p1_xtal_25M);
		else
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p1_xtal_40M);
	} else if (AR_SREV_9330_12(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9331_1p2_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9331_1p2_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9331_1p2_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9331_1p2_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9331_1p2_radio_core);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9331_1p2_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9331_1p2_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p2);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9331_modes_lowest_ob_db_tx_gain_1p2);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9331_1p2_baseband_core_txfir_coeff_japan_2484);

		/* additional clock settings */
		if (ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p2_xtal_25M);
		else
			INIT_INI_ARRAY(&ah->iniAdditional,
					ar9331_1p2_xtal_40M);
	} else if (AR_SREV_9340(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9340_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9340_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9340_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9340_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9340_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9340_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9340_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9340_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9340Common_wo_xlna_rx_gain_table_1p0);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9340Modes_high_ob_db_tx_gain_table_1p0);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       ar9340Modes_fast_clock_1p0);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9340_1p0_baseband_core_txfir_coeff_japan_2484);
		INIT_INI_ARRAY(&ah->ini_dfs,
			       ar9340_1p0_baseband_postamble_dfs_channel);

		if (!ah->is_clk_25mhz)
			INIT_INI_ARRAY(&ah->iniAdditional,
				       ar9340_1p0_radio_core_40M);
	} else if (AR_SREV_9485_11_OR_LATER(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9485_1_1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9485_1_1_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_PRE], ar9485_1_1);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9485_1_1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9485_1_1_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9485_1_1_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9485_1_1_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9485_1_1_soc_preamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9485Common_wo_xlna_rx_gain_1_1);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9485_modes_lowest_ob_db_tx_gain_1_1);

		/* Japan 2484 Mhz CCK */
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9485_1_1_baseband_core_txfir_coeff_japan_2484);

		if (ah->config.no_pll_pwrsave) {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
				       ar9485_1_1_pcie_phy_clkreq_disable_L1);
			INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				       ar9485_1_1_pcie_phy_clkreq_disable_L1);
		} else {
			INIT_INI_ARRAY(&ah->iniPcieSerdes,
				       ar9485_1_1_pll_on_cdr_on_clkreq_disable_L1);
			INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				       ar9485_1_1_pll_on_cdr_on_clkreq_disable_L1);
		}
	} else if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       ar9462_2p1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       ar9462_2p1_mac_postamble);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       ar9462_2p1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       ar9462_2p1_baseband_postamble);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       ar9462_2p1_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       ar9462_2p1_radio_postamble);
		INIT_INI_ARRAY(&ah->ini_radio_post_sys2ant,
			       ar9462_2p1_radio_postamble_sys2ant);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       ar9462_2p1_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       ar9462_2p1_soc_postamble);
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_rx_gain);
		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       ar9462_2p1_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9462_2p1_baseband_core_txfir_coeff_japan_2484);
		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9462_2p1_pciephy_clkreq_disable_L1);
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9462_2p1_pciephy_clkreq_disable_L1);
	} else if (AR_SREV_9462_20(ah)) {

		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE], ar9462_2p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9462_2p0_mac_postamble);

		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9462_2p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9462_2p0_baseband_postamble);

		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9462_2p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9462_2p0_radio_postamble);
		INIT_INI_ARRAY(&ah->ini_radio_post_sys2ant,
				ar9462_2p0_radio_postamble_sys2ant);

		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9462_2p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9462_2p0_soc_postamble);

		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_2p0_common_rx_gain);

		/* Awake -> Sleep Setting */
		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9462_2p0_pciephy_clkreq_disable_L1);
		/* Sleep -> Awake Setting */
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9462_2p0_pciephy_clkreq_disable_L1);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9462_2p0_modes_fast_clock);

		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9462_2p0_baseband_core_txfir_coeff_japan_2484);
	} else if (AR_SREV_9550(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar955x_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar955x_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar955x_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar955x_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar955x_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar955x_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar955x_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar955x_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar955x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			ar955x_1p0_common_wo_xlna_rx_gain_bounds);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar955x_1p0_modes_xpa_tx_gain_table);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar955x_1p0_modes_fast_clock);
	} else if (AR_SREV_9531(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       qca953x_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       qca953x_1p0_mac_postamble);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       qca953x_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       qca953x_1p0_baseband_postamble);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       qca953x_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       qca953x_1p0_radio_postamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       qca953x_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       qca953x_1p0_soc_postamble);
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       qca953x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			       qca953x_1p0_common_wo_xlna_rx_gain_bounds);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       qca953x_1p0_modes_no_xpa_tx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       qca953x_1p0_modes_fast_clock);
	} else if (AR_SREV_9580(ah)) {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9580_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9580_1p0_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9580_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9580_1p0_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9580_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9580_1p0_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9580_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9580_1p0_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9580_1p0_rx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9580_1p0_low_ob_db_tx_gain_table);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       ar9580_1p0_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9580_1p0_baseband_core_txfir_coeff_japan_2484);
		INIT_INI_ARRAY(&ah->ini_dfs,
			       ar9580_1p0_baseband_postamble_dfs_channel);
	} else if (AR_SREV_9565_11_OR_LATER(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       ar9565_1p1_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       ar9565_1p1_mac_postamble);

		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       ar9565_1p1_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       ar9565_1p1_baseband_postamble);

		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       ar9565_1p1_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       ar9565_1p1_radio_postamble);

		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       ar9565_1p1_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       ar9565_1p1_soc_postamble);

		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p1_Common_rx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p1_Modes_lowest_ob_db_tx_gain_table);

		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9565_1p1_pciephy_clkreq_disable_L1);
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9565_1p1_pciephy_clkreq_disable_L1);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9565_1p1_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9565_1p1_baseband_core_txfir_coeff_japan_2484);
	} else if (AR_SREV_9565(ah)) {
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
			       ar9565_1p0_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
			       ar9565_1p0_mac_postamble);

		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
			       ar9565_1p0_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
			       ar9565_1p0_baseband_postamble);

		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
			       ar9565_1p0_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
			       ar9565_1p0_radio_postamble);

		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
			       ar9565_1p0_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
			       ar9565_1p0_soc_postamble);

		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_Common_rx_gain_table);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_Modes_lowest_ob_db_tx_gain_table);

		INIT_INI_ARRAY(&ah->iniPcieSerdes,
			       ar9565_1p0_pciephy_clkreq_disable_L1);
		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
			       ar9565_1p0_pciephy_clkreq_disable_L1);

		INIT_INI_ARRAY(&ah->iniModesFastClock,
				ar9565_1p0_modes_fast_clock);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9565_1p0_baseband_core_txfir_coeff_japan_2484);
	} else {
		/* mac */
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_CORE],
				ar9300_2p2_mac_core);
		INIT_INI_ARRAY(&ah->iniMac[ATH_INI_POST],
				ar9300_2p2_mac_postamble);

		/* bb */
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_CORE],
				ar9300_2p2_baseband_core);
		INIT_INI_ARRAY(&ah->iniBB[ATH_INI_POST],
				ar9300_2p2_baseband_postamble);

		/* radio */
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_CORE],
				ar9300_2p2_radio_core);
		INIT_INI_ARRAY(&ah->iniRadio[ATH_INI_POST],
				ar9300_2p2_radio_postamble);

		/* soc */
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_PRE],
				ar9300_2p2_soc_preamble);
		INIT_INI_ARRAY(&ah->iniSOC[ATH_INI_POST],
				ar9300_2p2_soc_postamble);

		/* rx/tx gain */
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9300Common_rx_gain_table_2p2);
		INIT_INI_ARRAY(&ah->iniModesTxGain,
				ar9300Modes_lowest_ob_db_tx_gain_table_2p2);

		/* Load PCIE SERDES settings from INI */

		/* Awake Setting */

		INIT_INI_ARRAY(&ah->iniPcieSerdes,
				ar9300PciePhy_pll_on_clkreq_disable_L1_2p2);

		/* Sleep Setting */

		INIT_INI_ARRAY(&ah->iniPcieSerdesLowPower,
				ar9300PciePhy_pll_on_clkreq_disable_L1_2p2);

		/* Fast clock modal settings */
		INIT_INI_ARRAY(&ah->iniModesFastClock,
			       ar9300Modes_fast_clock_2p2);
		INIT_INI_ARRAY(&ah->iniCckfirJapan2484,
			       ar9300_2p2_baseband_core_txfir_coeff_japan_2484);
		INIT_INI_ARRAY(&ah->ini_dfs,
			       ar9300_2p2_baseband_postamble_dfs_channel);
	}
}

static void ar9003_tx_gain_table_mode0(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_lowest_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_lowest_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_lowest_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485_modes_lowest_ob_db_tx_gain_1_1);
	else if (AR_SREV_9550(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar955x_1p0_modes_xpa_tx_gain_table);
	else if (AR_SREV_9531(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			qca953x_1p0_modes_xpa_tx_gain_table);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_lowest_ob_db_tx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p1_modes_low_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p0_modes_low_ob_db_tx_gain);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p1_modes_low_ob_db_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_low_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_lowest_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode1(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_high_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_high_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_high_ob_db_tx_gain_table);
	else if (AR_SREV_9550(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar955x_1p0_modes_no_xpa_tx_gain_table);
	else if (AR_SREV_9531(ah)) {
		if (AR_SREV_9531_11(ah))
			INIT_INI_ARRAY(&ah->iniModesTxGain,
				       qca953x_1p1_modes_no_xpa_tx_gain_table);
		else
			INIT_INI_ARRAY(&ah->iniModesTxGain,
				       qca953x_1p0_modes_no_xpa_tx_gain_table);
	} else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p1_modes_high_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9462_2p0_modes_high_ob_db_tx_gain);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p1_modes_high_ob_db_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_high_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_high_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode2(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_low_ob_db_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_low_ob_db_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_low_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_low_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_low_ob_db_tx_gain_table);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p1_modes_low_ob_db_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_low_ob_db_tx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_low_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode3(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_power_tx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9331_modes_high_power_tx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_high_power_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_high_power_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_high_power_tx_gain_table);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p1_modes_high_power_tx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9565_1p0_modes_high_power_tx_gain_table);
	else {
		if (ah->config.tx_gain_buffalo)
			INIT_INI_ARRAY(&ah->iniModesTxGain,
				       ar9300Modes_high_power_tx_gain_table_buffalo);
		else
			INIT_INI_ARRAY(&ah->iniModesTxGain,
				       ar9300Modes_high_power_tx_gain_table_2p2);
	}
}

static void ar9003_tx_gain_table_mode4(struct ath_hw *ah)
{
	if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_mixed_ob_db_tx_gain_table_1p0);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_mixed_ob_db_tx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		       ar9462_2p1_modes_mix_ob_db_tx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
		       ar9462_2p0_modes_mix_ob_db_tx_gain);
	else
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_mixed_ob_db_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode5(struct ath_hw *ah)
{
	if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_green_ob_db_tx_gain_1_1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_ub124_tx_gain_table_1p0);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_type5_tx_gain_table);
	else if (AR_SREV_9300_22(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9300Modes_type5_tx_gain_table_2p2);
}

static void ar9003_tx_gain_table_mode6(struct ath_hw *ah)
{
	if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9340Modes_low_ob_db_and_spur_tx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9485Modes_green_spur_ob_db_tx_gain_1_1);
	else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			ar9580_1p0_type6_tx_gain_table);
}

static void ar9003_tx_gain_table_mode7(struct ath_hw *ah)
{
	if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesTxGain,
			       ar9340_cus227_tx_gain_table_1p0);
}

typedef void (*ath_txgain_tab)(struct ath_hw *ah);

static void ar9003_tx_gain_table_apply(struct ath_hw *ah)
{
	static const ath_txgain_tab modes[] = {
		ar9003_tx_gain_table_mode0,
		ar9003_tx_gain_table_mode1,
		ar9003_tx_gain_table_mode2,
		ar9003_tx_gain_table_mode3,
		ar9003_tx_gain_table_mode4,
		ar9003_tx_gain_table_mode5,
		ar9003_tx_gain_table_mode6,
		ar9003_tx_gain_table_mode7,
	};
	int idx = ar9003_hw_get_tx_gain_idx(ah);

	if (idx >= ARRAY_SIZE(modes))
		idx = 0;

	modes[idx](ah);
}

static void ar9003_rx_gain_table_mode0(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9331_common_rx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9340Common_rx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9485_common_rx_gain_1_1);
	else if (AR_SREV_9550(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar955x_1p0_common_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
				ar955x_1p0_common_rx_gain_bounds);
	} else if (AR_SREV_9531(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       qca953x_1p0_common_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			       qca953x_1p0_common_rx_gain_bounds);
	} else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9580_1p0_rx_gain_table);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_2p1_common_rx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9462_2p0_common_rx_gain);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p1_Common_rx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_Common_rx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesRxGain,
				ar9300Common_rx_gain_table_2p2);
}

static void ar9003_rx_gain_table_mode1(struct ath_hw *ah)
{
	if (AR_SREV_9330_12(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9331_common_wo_xlna_rx_gain_1p2);
	else if (AR_SREV_9330_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9331_common_wo_xlna_rx_gain_1p1);
	else if (AR_SREV_9340(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9340Common_wo_xlna_rx_gain_table_1p0);
	else if (AR_SREV_9485_11_OR_LATER(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9485Common_wo_xlna_rx_gain_1_1);
	else if (AR_SREV_9462_21(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9462_2p1_common_wo_xlna_rx_gain);
	else if (AR_SREV_9462_20(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9462_2p0_common_wo_xlna_rx_gain);
	else if (AR_SREV_9550(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar955x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			ar955x_1p0_common_wo_xlna_rx_gain_bounds);
	} else if (AR_SREV_9531(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       qca953x_1p0_common_wo_xlna_rx_gain_table);
		INIT_INI_ARRAY(&ah->ini_modes_rx_gain_bounds,
			       qca953x_1p0_common_wo_xlna_rx_gain_bounds);
	} else if (AR_SREV_9580(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9580_1p0_wo_xlna_rx_gain_table);
	else if (AR_SREV_9565_11(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p1_common_wo_xlna_rx_gain_table);
	else if (AR_SREV_9565(ah))
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9565_1p0_common_wo_xlna_rx_gain_table);
	else
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			ar9300Common_wo_xlna_rx_gain_table_2p2);
}

static void ar9003_rx_gain_table_mode2(struct ath_hw *ah)
{
	if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_mixed_rx_gain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_core,
			       ar9462_2p1_baseband_core_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
			       ar9462_2p1_baseband_postamble_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p1_baseband_postamble_5g_xlna);
	} else if (AR_SREV_9462_20(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p0_common_mixed_rx_gain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_core,
			       ar9462_2p0_baseband_core_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_bb_postamble,
			       ar9462_2p0_baseband_postamble_mix_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p0_baseband_postamble_5g_xlna);
	}
}

static void ar9003_rx_gain_table_mode3(struct ath_hw *ah)
{
	if (AR_SREV_9462_21(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p1_common_5g_xlna_only_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p1_baseband_postamble_5g_xlna);
	} else if (AR_SREV_9462_20(ah)) {
		INIT_INI_ARRAY(&ah->iniModesRxGain,
			       ar9462_2p0_common_5g_xlna_only_rxgain);
		INIT_INI_ARRAY(&ah->ini_modes_rxgain_5g_xlna,
			       ar9462_2p0_baseband_postamble_5g_xlna);
	}
}

static void ar9003_rx_gain_table_apply(struct ath_hw *ah)
{
	switch (ar9003_hw_get_rx_gain_idx(ah)) {
	case 0:
	default:
		ar9003_rx_gain_table_mode0(ah);
		break;
	case 1:
		ar9003_rx_gain_table_mode1(ah);
		break;
	case 2:
		ar9003_rx_gain_table_mode2(ah);
		break;
	case 3:
		ar9003_rx_gain_table_mode3(ah);
		break;
	}
}

/* set gain table pointers according to values read from the eeprom */
static void ar9003_hw_init_mode_gain_regs(struct ath_hw *ah)
{
	ar9003_tx_gain_table_apply(ah);
	ar9003_rx_gain_table_apply(ah);
}

/*
 * Helper for ASPM support.
 *
 * Disable PLL when in L0s as well as receiver clock when in L1.
 * This power saving option must be enabled through the SerDes.
 *
 * Programming the SerDes must go through the same 288 bit serial shift
 * register as the other analog registers.  Hence the 9 writes.
 */
static void ar9003_hw_configpcipowersave(struct ath_hw *ah,
					 bool power_off)
{
	unsigned int i;
	struct ar5416IniArray *array;

	/*
	 * Increase L1 Entry Latency. Some WB222 boards don't have
	 * this change in eeprom/OTP.
	 *
	 */
	if (AR_SREV_9462(ah)) {
		u32 val = ah->config.aspm_l1_fix;
		if ((val & 0xff000000) == 0x17000000) {
			val &= 0x00ffffff;
			val |= 0x27000000;
			REG_WRITE(ah, 0x570c, val);
		}
	}

	/* Nothing to do on restore for 11N */
	if (!power_off /* !restore */) {
		/* set bit 19 to allow forcing of pcie core into L1 state */
		REG_SET_BIT(ah, AR_PCIE_PM_CTRL, AR_PCIE_PM_CTRL_ENA);
		REG_WRITE(ah, AR_WA, ah->WARegVal);
	}

	/*
	 * Configire PCIE after Ini init. SERDES values now come from ini file
	 * This enables PCIe low power mode.
	 */
	array = power_off ? &ah->iniPcieSerdes :
		&ah->iniPcieSerdesLowPower;

	for (i = 0; i < array->ia_rows; i++) {
		REG_WRITE(ah,
			  INI_RA(array, i, 0),
			  INI_RA(array, i, 1));
	}
}

static void ar9003_hw_init_hang_checks(struct ath_hw *ah)
{
	/*
	 * All chips support detection of BB/MAC hangs.
	 */
	ah->config.hw_hang_checks |= HW_BB_WATCHDOG;
	ah->config.hw_hang_checks |= HW_MAC_HANG;

	/*
	 * This is not required for AR9580 1.0
	 */
	if (AR_SREV_9300_22(ah))
		ah->config.hw_hang_checks |= HW_PHYRESTART_CLC_WAR;

	if (AR_SREV_9330(ah))
		ah->bb_watchdog_timeout_ms = 85;
	else
		ah->bb_watchdog_timeout_ms = 25;
}

/*
 * MAC HW hang check
 * =================
 *
 * Signature: dcu_chain_state is 0x6 and dcu_complete_state is 0x1.
 *
 * The state of each DCU chain (mapped to TX queues) is available from these
 * DMA debug registers:
 *
 * Chain 0 state : Bits 4:0   of AR_DMADBG_4
 * Chain 1 state : Bits 9:5   of AR_DMADBG_4
 * Chain 2 state : Bits 14:10 of AR_DMADBG_4
 * Chain 3 state : Bits 19:15 of AR_DMADBG_4
 * Chain 4 state : Bits 24:20 of AR_DMADBG_4
 * Chain 5 state : Bits 29:25 of AR_DMADBG_4
 * Chain 6 state : Bits 4:0   of AR_DMADBG_5
 * Chain 7 state : Bits 9:5   of AR_DMADBG_5
 * Chain 8 state : Bits 14:10 of AR_DMADBG_5
 * Chain 9 state : Bits 19:15 of AR_DMADBG_5
 *
 * The DCU chain state "0x6" means "WAIT_FRDONE" - wait for TX frame to be done.
 */

#define NUM_STATUS_READS 50

static bool ath9k_hw_verify_hang(struct ath_hw *ah, unsigned int queue)
{
	u32 dma_dbg_chain, dma_dbg_complete;
	u8 dcu_chain_state, dcu_complete_state;
	int i;

	for (i = 0; i < NUM_STATUS_READS; i++) {
		if (queue < 6)
			dma_dbg_chain = REG_READ(ah, AR_DMADBG_4);
		else
			dma_dbg_chain = REG_READ(ah, AR_DMADBG_5);

		dma_dbg_complete = REG_READ(ah, AR_DMADBG_6);

		dcu_chain_state = (dma_dbg_chain >> (5 * queue)) & 0x1f;
		dcu_complete_state = dma_dbg_complete & 0x3;

		if ((dcu_chain_state != 0x6) || (dcu_complete_state != 0x1))
			return false;
	}

	ath_dbg(ath9k_hw_common(ah), RESET,
		"MAC Hang signature found for queue: %d\n", queue);

	return true;
}

static bool ar9003_hw_detect_mac_hang(struct ath_hw *ah)
{
	u32 dma_dbg_4, dma_dbg_5, dma_dbg_6, chk_dbg;
	u8 dcu_chain_state, dcu_complete_state;
	bool dcu_wait_frdone = false;
	unsigned long chk_dcu = 0;
	unsigned int i = 0;

	dma_dbg_4 = REG_READ(ah, AR_DMADBG_4);
	dma_dbg_5 = REG_READ(ah, AR_DMADBG_5);
	dma_dbg_6 = REG_READ(ah, AR_DMADBG_6);

	dcu_complete_state = dma_dbg_6 & 0x3;
	if (dcu_complete_state != 0x1)
		goto exit;

	for (i = 0; i < ATH9K_NUM_TX_QUEUES; i++) {
		if (i < 6)
			chk_dbg = dma_dbg_4;
		else
			chk_dbg = dma_dbg_5;

		dcu_chain_state = (chk_dbg >> (5 * i)) & 0x1f;
		if (dcu_chain_state == 0x6) {
			dcu_wait_frdone = true;
			chk_dcu |= BIT(i);
		}
	}

	if ((dcu_complete_state == 0x1) && dcu_wait_frdone) {
		for_each_set_bit(i, &chk_dcu, ATH9K_NUM_TX_QUEUES) {
			if (ath9k_hw_verify_hang(ah, i))
				return true;
		}
	}
exit:
	return false;
}

/* Sets up the AR9003 hardware familiy callbacks */
void ar9003_hw_attach_ops(struct ath_hw *ah)
{
	struct ath_hw_private_ops *priv_ops = ath9k_hw_private_ops(ah);
	struct ath_hw_ops *ops = ath9k_hw_ops(ah);

	ar9003_hw_init_mode_regs(ah);
	priv_ops->init_mode_gain_regs = ar9003_hw_init_mode_gain_regs;
	priv_ops->init_hang_checks = ar9003_hw_init_hang_checks;
	priv_ops->detect_mac_hang = ar9003_hw_detect_mac_hang;

	ops->config_pci_powersave = ar9003_hw_configpcipowersave;

	ar9003_hw_attach_phy_ops(ah);
	ar9003_hw_attach_calib_ops(ah);
	ar9003_hw_attach_mac_ops(ah);
}
