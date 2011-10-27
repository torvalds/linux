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

#ifndef __iwl_legacy_helpers_h__
#define __iwl_legacy_helpers_h__

#include <linux/ctype.h>
#include <net/mac80211.h>

#include "iwl-io.h"

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))


static inline struct ieee80211_conf *iwl_legacy_ieee80211_get_hw_conf(
	struct ieee80211_hw *hw)
{
	return &hw->conf;
}

/**
 * iwl_legacy_queue_inc_wrap - increment queue index, wrap back to beginning
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_legacy_queue_inc_wrap(int index, int n_bd)
{
	return ++index & (n_bd - 1);
}

/**
 * iwl_legacy_queue_dec_wrap - decrement queue index, wrap back to end
 * @index -- current index
 * @n_bd -- total number of entries in queue (must be power of 2)
 */
static inline int iwl_legacy_queue_dec_wrap(int index, int n_bd)
{
	return --index & (n_bd - 1);
}

/* TODO: Move fw_desc functions to iwl-pci.ko */
static inline void iwl_legacy_free_fw_desc(struct pci_dev *pci_dev,
				    struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(&pci_dev->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static inline int iwl_legacy_alloc_fw_desc(struct pci_dev *pci_dev,
				    struct fw_desc *desc)
{
	if (!desc->len) {
		desc->v_addr = NULL;
		return -EINVAL;
	}

	desc->v_addr = dma_alloc_coherent(&pci_dev->dev, desc->len,
					  &desc->p_addr, GFP_KERNEL);
	return (desc->v_addr != NULL) ? 0 : -ENOMEM;
}

/*
 * we have 8 bits used like this:
 *
 * 7 6 5 4 3 2 1 0
 * | | | | | | | |
 * | | | | | | +-+-------- AC queue (0-3)
 * | | | | | |
 * | +-+-+-+-+------------ HW queue ID
 * |
 * +---------------------- unused
 */
static inline void
iwl_legacy_set_swq_id(struct iwl_tx_queue *txq, u8 ac, u8 hwq)
{
	BUG_ON(ac > 3);   /* only have 2 bits */
	BUG_ON(hwq > 31); /* only use 5 bits */

	txq->swq_id = (hwq << 2) | ac;
}

static inline void iwl_legacy_wake_queue(struct iwl_priv *priv,
				  struct iwl_tx_queue *txq)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;

	if (test_and_clear_bit(hwq, priv->queue_stopped))
		if (atomic_dec_return(&priv->queue_stop_count[ac]) <= 0)
			ieee80211_wake_queue(priv->hw, ac);
}

static inline void iwl_legacy_stop_queue(struct iwl_priv *priv,
				  struct iwl_tx_queue *txq)
{
	u8 queue = txq->swq_id;
	u8 ac = queue & 3;
	u8 hwq = (queue >> 2) & 0x1f;

	if (!test_and_set_bit(hwq, priv->queue_stopped))
		if (atomic_inc_return(&priv->queue_stop_count[ac]) > 0)
			ieee80211_stop_queue(priv->hw, ac);
}

#ifdef ieee80211_stop_queue
#undef ieee80211_stop_queue
#endif

#define ieee80211_stop_queue DO_NOT_USE_ieee80211_stop_queue

#ifdef ieee80211_wake_queue
#undef ieee80211_wake_queue
#endif

#define ieee80211_wake_queue DO_NOT_USE_ieee80211_wake_queue

static inline void iwl_legacy_disable_interrupts(struct iwl_priv *priv)
{
	clear_bit(STATUS_INT_ENABLED, &priv->status);

	/* disable interrupts from uCode/NIC to host */
	iwl_write32(priv, CSR_INT_MASK, 0x00000000);

	/* acknowledge/clear/reset any interrupts still pending
	 * from uCode or flow handler (Rx/Tx DMA) */
	iwl_write32(priv, CSR_INT, 0xffffffff);
	iwl_write32(priv, CSR_FH_INT_STATUS, 0xffffffff);
	IWL_DEBUG_ISR(priv, "Disabled interrupts\n");
}

static inline void iwl_legacy_enable_rfkill_int(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR(priv, "Enabling rfkill interrupt\n");
	iwl_write32(priv, CSR_INT_MASK, CSR_INT_BIT_RF_KILL);
}

static inline void iwl_legacy_enable_interrupts(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR(priv, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &priv->status);
	iwl_write32(priv, CSR_INT_MASK, priv->inta_mask);
}

/**
 * iwl_legacy_beacon_time_mask_low - mask of lower 32 bit of beacon time
 * @priv -- pointer to iwl_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32 iwl_legacy_beacon_time_mask_low(struct iwl_priv *priv,
					   u16 tsf_bits)
{
	return (1 << tsf_bits) - 1;
}

/**
 * iwl_legacy_beacon_time_mask_high - mask of higher 32 bit of beacon time
 * @priv -- pointer to iwl_priv data structure
 * @tsf_bits -- number of bits need to shift for masking)
 */
static inline u32 iwl_legacy_beacon_time_mask_high(struct iwl_priv *priv,
					    u16 tsf_bits)
{
	return ((1 << (32 - tsf_bits)) - 1) << tsf_bits;
}

#endif				/* __iwl_legacy_helpers_h__ */
