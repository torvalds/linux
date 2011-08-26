/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project, as well
 * as portions of the ieee80211 subsystem header files.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_helpers_h__
#define __iwl_helpers_h__

#include <linux/ctype.h>
#include <net/mac80211.h>

#include "iwl-io.h"

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))


static inline struct ieee80211_conf *ieee80211_get_hw_conf(
	struct ieee80211_hw *hw)
{
	return &hw->conf;
}

/**
 * iwl_queue_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_queue_inc_wrap(int index, int n_bd)
{
	return ++index & (n_bd - 1);
}

/**
 * iwl_queue_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_queue_dec_wrap(int index, int n_bd)
{
	return --index & (n_bd - 1);
}

static inline void iwl_enable_rfkill_int(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR(priv, "Enabling rfkill interrupt\n");
	iwl_write32(bus(priv), CSR_INT_MASK, CSR_INT_BIT_RF_KILL);
}

/**
 * iwl_beacon_time_mask_low - mask of lower 32 bit of beacon time
 * @priv -- pointer to iwl_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32 iwl_beacon_time_mask_low(struct iwl_priv *priv,
					   u16 tsf_bits)
{
	return (1 << tsf_bits) - 1;
}

/**
 * iwl_beacon_time_mask_high - mask of higher 32 bit of beacon time
 * @priv -- pointer to iwl_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32 iwl_beacon_time_mask_high(struct iwl_priv *priv,
					    u16 tsf_bits)
{
	return ((1 << (32 - tsf_bits)) - 1) << tsf_bits;
}

#endif				/* __iwl_helpers_h__ */
