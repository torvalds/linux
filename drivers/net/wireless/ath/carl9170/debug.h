/*
 * Atheros CARL9170 driver
 *
 * debug header
 *
 * Copyright 2010, Christian Lamparter <chunkeey@googlemail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, see
 * http://www.gnu.org/licenses/.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *    Copyright (c) 2007-2008 Atheros Communications, Inc.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef __DEBUG_H
#define __DEBUG_H

#include "eeprom.h"
#include "wlan.h"
#include "hw.h"
#include "fwdesc.h"
#include "fwcmd.h"
#include "../regd.h"

struct hw_stat_reg_entry {
	u32 reg;
	char nreg[32];
};

#define	STAT_MAC_REG(reg)	\
	{ (AR9170_MAC_REG_##reg), #reg }

#define	STAT_PTA_REG(reg)	\
	{ (AR9170_PTA_REG_##reg), #reg }

#define	STAT_USB_REG(reg)	\
	{ (AR9170_USB_REG_##reg), #reg }

static const struct hw_stat_reg_entry hw_rx_tally_regs[] = {
	STAT_MAC_REG(RX_CRC32),		STAT_MAC_REG(RX_CRC16),
	STAT_MAC_REG(RX_TIMEOUT_COUNT),	STAT_MAC_REG(RX_ERR_DECRYPTION_UNI),
	STAT_MAC_REG(RX_ERR_DECRYPTION_MUL), STAT_MAC_REG(RX_MPDU),
	STAT_MAC_REG(RX_DROPPED_MPDU),	STAT_MAC_REG(RX_DEL_MPDU),
};

static const struct hw_stat_reg_entry hw_phy_errors_regs[] = {
	STAT_MAC_REG(RX_PHY_MISC_ERROR), STAT_MAC_REG(RX_PHY_XR_ERROR),
	STAT_MAC_REG(RX_PHY_OFDM_ERROR), STAT_MAC_REG(RX_PHY_CCK_ERROR),
	STAT_MAC_REG(RX_PHY_HT_ERROR), STAT_MAC_REG(RX_PHY_TOTAL),
};

static const struct hw_stat_reg_entry hw_tx_tally_regs[] = {
	STAT_MAC_REG(TX_TOTAL),		STAT_MAC_REG(TX_UNDERRUN),
	STAT_MAC_REG(TX_RETRY),
};

static const struct hw_stat_reg_entry hw_wlan_queue_regs[] = {
	STAT_MAC_REG(DMA_STATUS),	STAT_MAC_REG(DMA_TRIGGER),
	STAT_MAC_REG(DMA_TXQ0_ADDR),	STAT_MAC_REG(DMA_TXQ0_CURR_ADDR),
	STAT_MAC_REG(DMA_TXQ1_ADDR),	STAT_MAC_REG(DMA_TXQ1_CURR_ADDR),
	STAT_MAC_REG(DMA_TXQ2_ADDR),	STAT_MAC_REG(DMA_TXQ2_CURR_ADDR),
	STAT_MAC_REG(DMA_TXQ3_ADDR),	STAT_MAC_REG(DMA_TXQ3_CURR_ADDR),
	STAT_MAC_REG(DMA_RXQ_ADDR),	STAT_MAC_REG(DMA_RXQ_CURR_ADDR),
};

static const struct hw_stat_reg_entry hw_ampdu_info_regs[] = {
	STAT_MAC_REG(AMPDU_DENSITY),	STAT_MAC_REG(AMPDU_FACTOR),
};

static const struct hw_stat_reg_entry hw_pta_queue_regs[] = {
	STAT_PTA_REG(DN_CURR_ADDRH),	STAT_PTA_REG(DN_CURR_ADDRL),
	STAT_PTA_REG(UP_CURR_ADDRH),	STAT_PTA_REG(UP_CURR_ADDRL),
	STAT_PTA_REG(DMA_STATUS),	STAT_PTA_REG(DMA_MODE_CTRL),
};

#define	DEFINE_TALLY(name)					\
	u32 name##_sum[ARRAY_SIZE(name##_regs)],		\
	    name##_counter[ARRAY_SIZE(name##_regs)]		\

#define	DEFINE_STAT(name)					\
	u32 name##_counter[ARRAY_SIZE(name##_regs)]		\

struct ath_stats {
	DEFINE_TALLY(hw_tx_tally);
	DEFINE_TALLY(hw_rx_tally);
	DEFINE_TALLY(hw_phy_errors);
	DEFINE_STAT(hw_wlan_queue);
	DEFINE_STAT(hw_pta_queue);
	DEFINE_STAT(hw_ampdu_info);
};

struct carl9170_debug_mem_rbe {
	u32 reg;
	u32 value;
};

#define	CARL9170_DEBUG_RING_SIZE			64

struct carl9170_debug {
	struct ath_stats stats;
	struct carl9170_debug_mem_rbe ring[CARL9170_DEBUG_RING_SIZE];
	struct mutex ring_lock;
	unsigned int ring_head, ring_tail;
	struct delayed_work update_tally;
};

struct ar9170;

void carl9170_debugfs_register(struct ar9170 *ar);
void carl9170_debugfs_unregister(struct ar9170 *ar);
#endif /* __DEBUG_H */
