/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
		pci_free_consistent(pci_dev, desc->len,
				    desc->v_addr, desc->p_addr);
	desc->v_addr = NULL;
	desc->len = 0;
}

static inline int iwl_alloc_fw_desc(struct pci_dev *pci_dev,
				    struct fw_desc *desc)
{
	desc->v_addr = pci_alloc_consistent(pci_dev, desc->len, &desc->p_addr);
	return (desc->v_addr != NULL) ? 0 : -ENOMEM;
}

#endif				/* __iwl_helpers_h__ */
