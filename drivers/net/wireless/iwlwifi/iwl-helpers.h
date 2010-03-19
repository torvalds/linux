/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
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

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))


static inline struct ieee80211_conf *ieee80211_get_hw_conf(
	struct ieee80211_hw *hw)
{
	return &hw->conf;
}

static inline int iwl_check_bits(unsigned long field, unsigned long mask)
{
	return ((field & mask) == mask) ? 1 : 0;
}

static inline unsigned long elapsed_jiffies(unsigned long start,
					    unsigned long end)
{
	if (end >= start)
		return end - start;

	return end + (MAX_JIFFY_OFFSET - start) + 1;
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

/* TODO: Move fw_desc functions to iwl-pci.ko */
static inline void iwl_free_fw_desc(struct pci_dev *pci_dev,
				    struct fw_desc *desc)
{
	if (desc->v_addr)
		dma_free_coherent(&pci_dev->dev, desc->len,
				  desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static inline int iwl_alloc_fw_desc(struct pci_dev *pci_dev,
				    struct fw_desc *desc)
{
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
 * | +-+-+-+-+------------ HW A-MPDU queue
 * |
 * +---------------------- indicates agg queue
 */
static inline u8 iwl_virtual_agg_queue_num(u8 ac, u8 hwq)
{
	BUG_ON(ac > 3);   /* only have 2 bits */
	BUG_ON(hwq > 31); /* only have 5 bits */

	return 0x80 | (hwq << 2) | ac;
}

static inline void iwl_wake_queue(struct iwl_priv *priv, u8 queue)
{
	u8 ac = queue;
	u8 hwq = queue;

	if (queue & 0x80) {
		ac = queue & 3;
		hwq = (queue >> 2) & 0x1f;
	}

	if (test_and_clear_bit(hwq, priv->queue_stopped))
		if (atomic_dec_return(&priv->queue_stop_count[ac]) <= 0)
			ieee80211_wake_queue(priv->hw, ac);
}

static inline void iwl_stop_queue(struct iwl_priv *priv, u8 queue)
{
	u8 ac = queue;
	u8 hwq = queue;

	if (queue & 0x80) {
		ac = queue & 3;
		hwq = (queue >> 2) & 0x1f;
	}

	if (!test_and_set_bit(hwq, priv->queue_stopped))
		if (atomic_inc_return(&priv->queue_stop_count[ac]) > 0)
			ieee80211_stop_queue(priv->hw, ac);
}

#define ieee80211_stop_queue DO_NOT_USE_ieee80211_stop_queue
#define ieee80211_wake_queue DO_NOT_USE_ieee80211_wake_queue

static inline void iwl_disable_interrupts(struct iwl_priv *priv)
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

static inline void iwl_enable_interrupts(struct iwl_priv *priv)
{
	IWL_DEBUG_ISR(priv, "Enabling interrupts\n");
	set_bit(STATUS_INT_ENABLED, &priv->status);
	iwl_write32(priv, CSR_INT_MASK, priv->inta_mask);
}

#endif				/* __iwl_helpers_h__ */
