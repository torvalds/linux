/*
 * Copyright (c) 2009-2011 Atheros Communications Inc.
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

#include <linux/export.h>
#include "hw.h"

enum ath_bt_mode {
	ATH_BT_COEX_MODE_LEGACY,        /* legacy rx_clear mode */
	ATH_BT_COEX_MODE_UNSLOTTED,     /* untimed/unslotted mode */
	ATH_BT_COEX_MODE_SLOTTED,       /* slotted mode */
	ATH_BT_COEX_MODE_DISABLED,      /* coexistence disabled */
};

struct ath_btcoex_config {
	u8 bt_time_extend;
	bool bt_txstate_extend;
	bool bt_txframe_extend;
	enum ath_bt_mode bt_mode; /* coexistence mode */
	bool bt_quiet_collision;
	bool bt_rxclear_polarity; /* invert rx_clear as WLAN_ACTIVE*/
	u8 bt_priority_time;
	u8 bt_first_slot_time;
	bool bt_hold_rx_clear;
};

static const u32 ar9003_wlan_weights[ATH_BTCOEX_STOMP_MAX]
				    [AR9300_NUM_WLAN_WEIGHTS] = {
	{ 0xfffffff0, 0xfffffff0, 0xfffffff0, 0xfffffff0 }, /* STOMP_ALL */
	{ 0x88888880, 0x88888880, 0x88888880, 0x88888880 }, /* STOMP_LOW */
	{ 0x00000000, 0x00000000, 0x00000000, 0x00000000 }, /* STOMP_NONE */
};

static const u32 mci_wlan_weights[ATH_BTCOEX_STOMP_MAX]
				 [AR9300_NUM_WLAN_WEIGHTS] = {
	{ 0x01017d01, 0x41414101, 0x41414101, 0x41414141 }, /* STOMP_ALL */
	{ 0x01017d01, 0x3b3b3b01, 0x3b3b3b01, 0x3b3b3b3b }, /* STOMP_LOW */
	{ 0x01017d01, 0x01010101, 0x01010101, 0x01010101 }, /* STOMP_NONE */
	{ 0x01017d01, 0x013b0101, 0x3b3b0101, 0x3b3b013b }, /* STOMP_LOW_FTP */
	{ 0xffffff01, 0xffffffff, 0xffffff01, 0xffffffff }, /* STOMP_AUDIO */
};

void ath9k_hw_init_btcoex_hw(struct ath_hw *ah, int qnum)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;
	const struct ath_btcoex_config ath_bt_config = {
		.bt_time_extend = 0,
		.bt_txstate_extend = true,
		.bt_txframe_extend = true,
		.bt_mode = ATH_BT_COEX_MODE_SLOTTED,
		.bt_quiet_collision = true,
		.bt_rxclear_polarity = true,
		.bt_priority_time = 2,
		.bt_first_slot_time = 5,
		.bt_hold_rx_clear = true,
	};
	u32 i, idx;
	bool rxclear_polarity = ath_bt_config.bt_rxclear_polarity;

	if (AR_SREV_9300_20_OR_LATER(ah))
		rxclear_polarity = !ath_bt_config.bt_rxclear_polarity;

	btcoex_hw->bt_coex_mode =
		(btcoex_hw->bt_coex_mode & AR_BT_QCU_THRESH) |
		SM(ath_bt_config.bt_time_extend, AR_BT_TIME_EXTEND) |
		SM(ath_bt_config.bt_txstate_extend, AR_BT_TXSTATE_EXTEND) |
		SM(ath_bt_config.bt_txframe_extend, AR_BT_TX_FRAME_EXTEND) |
		SM(ath_bt_config.bt_mode, AR_BT_MODE) |
		SM(ath_bt_config.bt_quiet_collision, AR_BT_QUIET) |
		SM(rxclear_polarity, AR_BT_RX_CLEAR_POLARITY) |
		SM(ath_bt_config.bt_priority_time, AR_BT_PRIORITY_TIME) |
		SM(ath_bt_config.bt_first_slot_time, AR_BT_FIRST_SLOT_TIME) |
		SM(qnum, AR_BT_QCU_THRESH);

	btcoex_hw->bt_coex_mode2 =
		SM(ath_bt_config.bt_hold_rx_clear, AR_BT_HOLD_RX_CLEAR) |
		SM(ATH_BTCOEX_BMISS_THRESH, AR_BT_BCN_MISS_THRESH) |
		AR_BT_DISABLE_BT_ANT;

	for (i = 0; i < 32; i++) {
		idx = (debruijn32 << i) >> 27;
		ah->hw_gen_timers.gen_timer_index[idx] = i;
	}
}
EXPORT_SYMBOL(ath9k_hw_init_btcoex_hw);

void ath9k_hw_btcoex_init_scheme(struct ath_hw *ah)
{
	struct ath_common *common = ath9k_hw_common(ah);
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	/*
	 * Check if BTCOEX is globally disabled.
	 */
	if (!common->btcoex_enabled) {
		btcoex_hw->scheme = ATH_BTCOEX_CFG_NONE;
		return;
	}

	if (AR_SREV_9300_20_OR_LATER(ah)) {
		btcoex_hw->scheme = ATH_BTCOEX_CFG_3WIRE;
		btcoex_hw->btactive_gpio = ATH_BTACTIVE_GPIO_9300;
		btcoex_hw->wlanactive_gpio = ATH_WLANACTIVE_GPIO_9300;
		btcoex_hw->btpriority_gpio = ATH_BTPRIORITY_GPIO_9300;
	} else if (AR_SREV_9280_20_OR_LATER(ah)) {
		btcoex_hw->btactive_gpio = ATH_BTACTIVE_GPIO_9280;
		btcoex_hw->wlanactive_gpio = ATH_WLANACTIVE_GPIO_9280;

		if (AR_SREV_9285(ah)) {
			btcoex_hw->scheme = ATH_BTCOEX_CFG_3WIRE;
			btcoex_hw->btpriority_gpio = ATH_BTPRIORITY_GPIO_9285;
		} else {
			btcoex_hw->scheme = ATH_BTCOEX_CFG_2WIRE;
		}
	}
}
EXPORT_SYMBOL(ath9k_hw_btcoex_init_scheme);

void ath9k_hw_btcoex_init_2wire(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	/* connect bt_active to baseband */
	REG_CLR_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    (AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF |
		     AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF));

	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
		    AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB);

	/* Set input mux for bt_active to gpio pin */
	REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
		      AR_GPIO_INPUT_MUX1_BT_ACTIVE,
		      btcoex_hw->btactive_gpio);

	/* Configure the desired gpio port for input */
	ath9k_hw_cfg_gpio_input(ah, btcoex_hw->btactive_gpio);
}
EXPORT_SYMBOL(ath9k_hw_btcoex_init_2wire);

void ath9k_hw_btcoex_init_3wire(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	/* btcoex 3-wire */
	REG_SET_BIT(ah, AR_GPIO_INPUT_EN_VAL,
			(AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB |
			 AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB));

	/* Set input mux for bt_prority_async and
	 *                  bt_active_async to GPIO pins */
	REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
			AR_GPIO_INPUT_MUX1_BT_ACTIVE,
			btcoex_hw->btactive_gpio);

	REG_RMW_FIELD(ah, AR_GPIO_INPUT_MUX1,
			AR_GPIO_INPUT_MUX1_BT_PRIORITY,
			btcoex_hw->btpriority_gpio);

	/* Configure the desired GPIO ports for input */

	ath9k_hw_cfg_gpio_input(ah, btcoex_hw->btactive_gpio);
	ath9k_hw_cfg_gpio_input(ah, btcoex_hw->btpriority_gpio);
}
EXPORT_SYMBOL(ath9k_hw_btcoex_init_3wire);

void ath9k_hw_btcoex_init_mci(struct ath_hw *ah)
{
	ah->btcoex_hw.mci.ready = false;
	ah->btcoex_hw.mci.bt_state = 0;
	ah->btcoex_hw.mci.bt_ver_major = 3;
	ah->btcoex_hw.mci.bt_ver_minor = 0;
	ah->btcoex_hw.mci.bt_version_known = false;
	ah->btcoex_hw.mci.update_2g5g = true;
	ah->btcoex_hw.mci.is_2g = true;
	ah->btcoex_hw.mci.wlan_channels_update = false;
	ah->btcoex_hw.mci.wlan_channels[0] = 0x00000000;
	ah->btcoex_hw.mci.wlan_channels[1] = 0xffffffff;
	ah->btcoex_hw.mci.wlan_channels[2] = 0xffffffff;
	ah->btcoex_hw.mci.wlan_channels[3] = 0x7fffffff;
	ah->btcoex_hw.mci.query_bt = true;
	ah->btcoex_hw.mci.unhalt_bt_gpm = true;
	ah->btcoex_hw.mci.halted_bt_gpm = false;
	ah->btcoex_hw.mci.need_flush_btinfo = false;
	ah->btcoex_hw.mci.wlan_cal_seq = 0;
	ah->btcoex_hw.mci.wlan_cal_done = 0;
	ah->btcoex_hw.mci.config = (AR_SREV_9462(ah)) ? 0x2201 : 0xa4c1;
}
EXPORT_SYMBOL(ath9k_hw_btcoex_init_mci);

static void ath9k_hw_btcoex_enable_2wire(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	/* Configure the desired GPIO port for TX_FRAME output */
	ath9k_hw_cfg_output(ah, btcoex_hw->wlanactive_gpio,
			    AR_GPIO_OUTPUT_MUX_AS_TX_FRAME);
}

/*
 * For AR9002, bt_weight/wlan_weight are used.
 * For AR9003 and above, stomp_type is used.
 */
void ath9k_hw_btcoex_set_weight(struct ath_hw *ah,
				u32 bt_weight,
				u32 wlan_weight,
				enum ath_stomp_type stomp_type)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;
	struct ath9k_hw_mci *mci_hw = &ah->btcoex_hw.mci;
	u8 txprio_shift[] = { 24, 16, 16, 0 }; /* tx priority weight */
	bool concur_tx = (mci_hw->concur_tx && btcoex_hw->tx_prio[stomp_type]);
	const u32 *weight = ar9003_wlan_weights[stomp_type];
	int i;

	if (!AR_SREV_9300_20_OR_LATER(ah)) {
		btcoex_hw->bt_coex_weights =
			SM(bt_weight, AR_BTCOEX_BT_WGHT) |
			SM(wlan_weight, AR_BTCOEX_WL_WGHT);
		return;
	}

	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		enum ath_stomp_type stype =
			((stomp_type == ATH_BTCOEX_STOMP_LOW) &&
			 btcoex_hw->mci.stomp_ftp) ?
			ATH_BTCOEX_STOMP_LOW_FTP : stomp_type;
		weight = mci_wlan_weights[stype];
	}

	for (i = 0; i < AR9300_NUM_WLAN_WEIGHTS; i++) {
		btcoex_hw->bt_weight[i] = AR9300_BT_WGHT;
		btcoex_hw->wlan_weight[i] = weight[i];
		if (concur_tx && i) {
			btcoex_hw->wlan_weight[i] &=
				~(0xff << txprio_shift[i-1]);
			btcoex_hw->wlan_weight[i] |=
				(btcoex_hw->tx_prio[stomp_type] <<
				 txprio_shift[i-1]);
		}
	}
	/* Last WLAN weight has to be adjusted wrt tx priority */
	if (concur_tx) {
		btcoex_hw->wlan_weight[i-1] &= ~(0xff << txprio_shift[i-1]);
		btcoex_hw->wlan_weight[i-1] |= (btcoex_hw->tx_prio[stomp_type]
						      << txprio_shift[i-1]);
	}

}
EXPORT_SYMBOL(ath9k_hw_btcoex_set_weight);


static void ath9k_hw_btcoex_enable_3wire(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex = &ah->btcoex_hw;
	u32  val;
	int i;

	/*
	 * Program coex mode and weight registers to
	 * enable coex 3-wire
	 */
	REG_WRITE(ah, AR_BT_COEX_MODE, btcoex->bt_coex_mode);
	REG_WRITE(ah, AR_BT_COEX_MODE2, btcoex->bt_coex_mode2);


	if (AR_SREV_9300_20_OR_LATER(ah)) {
		REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS0, btcoex->wlan_weight[0]);
		REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS1, btcoex->wlan_weight[1]);
		for (i = 0; i < AR9300_NUM_BT_WEIGHTS; i++)
			REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS(i),
				  btcoex->bt_weight[i]);
	} else
		REG_WRITE(ah, AR_BT_COEX_WEIGHT, btcoex->bt_coex_weights);



	if (AR_SREV_9271(ah)) {
		val = REG_READ(ah, 0x50040);
		val &= 0xFFFFFEFF;
		REG_WRITE(ah, 0x50040, val);
	}

	REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
	REG_RMW_FIELD(ah, AR_PCU_MISC, AR_PCU_BT_ANT_PREVENT_RX, 0);

	ath9k_hw_cfg_output(ah, btcoex->wlanactive_gpio,
			    AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL);
}

static void ath9k_hw_btcoex_enable_mci(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex = &ah->btcoex_hw;
	int i;

	for (i = 0; i < AR9300_NUM_BT_WEIGHTS; i++)
		REG_WRITE(ah, AR_MCI_COEX_WL_WEIGHTS(i),
			  btcoex->wlan_weight[i]);

	REG_RMW_FIELD(ah, AR_QUIET1, AR_QUIET1_QUIET_ACK_CTS_ENABLE, 1);
	btcoex->enabled = true;
}

void ath9k_hw_btcoex_enable(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;

	switch (ath9k_hw_get_btcoex_scheme(ah)) {
	case ATH_BTCOEX_CFG_NONE:
		return;
	case ATH_BTCOEX_CFG_2WIRE:
		ath9k_hw_btcoex_enable_2wire(ah);
		break;
	case ATH_BTCOEX_CFG_3WIRE:
		if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
			ath9k_hw_btcoex_enable_mci(ah);
			return;
		}
		ath9k_hw_btcoex_enable_3wire(ah);
		break;
	}

	REG_RMW(ah, AR_GPIO_PDPU,
		(0x2 << (btcoex_hw->btactive_gpio * 2)),
		(0x3 << (btcoex_hw->btactive_gpio * 2)));

	ah->btcoex_hw.enabled = true;
}
EXPORT_SYMBOL(ath9k_hw_btcoex_enable);

void ath9k_hw_btcoex_disable(struct ath_hw *ah)
{
	struct ath_btcoex_hw *btcoex_hw = &ah->btcoex_hw;
	int i;

	btcoex_hw->enabled = false;
	if (AR_SREV_9462(ah) || AR_SREV_9565(ah)) {
		ath9k_hw_btcoex_bt_stomp(ah, ATH_BTCOEX_STOMP_NONE);
		for (i = 0; i < AR9300_NUM_BT_WEIGHTS; i++)
			REG_WRITE(ah, AR_MCI_COEX_WL_WEIGHTS(i),
				  btcoex_hw->wlan_weight[i]);
		return;
	}
	ath9k_hw_set_gpio(ah, btcoex_hw->wlanactive_gpio, 0);

	ath9k_hw_cfg_output(ah, btcoex_hw->wlanactive_gpio,
			AR_GPIO_OUTPUT_MUX_AS_OUTPUT);

	if (btcoex_hw->scheme == ATH_BTCOEX_CFG_3WIRE) {
		REG_WRITE(ah, AR_BT_COEX_MODE, AR_BT_QUIET | AR_BT_MODE);
		REG_WRITE(ah, AR_BT_COEX_MODE2, 0);

		if (AR_SREV_9300_20_OR_LATER(ah)) {
			REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS0, 0);
			REG_WRITE(ah, AR_BT_COEX_WL_WEIGHTS1, 0);
			for (i = 0; i < AR9300_NUM_BT_WEIGHTS; i++)
				REG_WRITE(ah, AR_BT_COEX_BT_WEIGHTS(i), 0);
		} else
			REG_WRITE(ah, AR_BT_COEX_WEIGHT, 0);

	}
}
EXPORT_SYMBOL(ath9k_hw_btcoex_disable);

/*
 * Configures appropriate weight based on stomp type.
 */
void ath9k_hw_btcoex_bt_stomp(struct ath_hw *ah,
			      enum ath_stomp_type stomp_type)
{
	if (AR_SREV_9300_20_OR_LATER(ah)) {
		ath9k_hw_btcoex_set_weight(ah, 0, 0, stomp_type);
		return;
	}

	switch (stomp_type) {
	case ATH_BTCOEX_STOMP_ALL:
		ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
					   AR_STOMP_ALL_WLAN_WGHT, 0);
		break;
	case ATH_BTCOEX_STOMP_LOW:
		ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
					   AR_STOMP_LOW_WLAN_WGHT, 0);
		break;
	case ATH_BTCOEX_STOMP_NONE:
		ath9k_hw_btcoex_set_weight(ah, AR_BT_COEX_WGHT,
					   AR_STOMP_NONE_WLAN_WGHT, 0);
		break;
	default:
		ath_dbg(ath9k_hw_common(ah), BTCOEX, "Invalid Stomptype\n");
		break;
	}
}
EXPORT_SYMBOL(ath9k_hw_btcoex_bt_stomp);

void ath9k_hw_btcoex_set_concur_txprio(struct ath_hw *ah, u8 *stomp_txprio)
{
	struct ath_btcoex_hw *btcoex = &ah->btcoex_hw;
	int i;

	for (i = 0; i < ATH_BTCOEX_STOMP_MAX; i++)
		btcoex->tx_prio[i] = stomp_txprio[i];
}
EXPORT_SYMBOL(ath9k_hw_btcoex_set_concur_txprio);
