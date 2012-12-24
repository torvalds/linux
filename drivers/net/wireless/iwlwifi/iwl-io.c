/******************************************************************************
 *
 * Copyright(c) 2003 - 2012 Intel Corporation. All rights reserved.
 *
 * Portions of this file are derived from the ipw3945 project.
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
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/export.h>

#include "iwl-io.h"
#include "iwl-csr.h"
#include "iwl-debug.h"

#define IWL_POLL_INTERVAL 10	/* microseconds */

void __iwl_set_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	iwl_write32(trans, reg, iwl_read32(trans, reg) | mask);
}

void __iwl_clear_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	iwl_write32(trans, reg, iwl_read32(trans, reg) & ~mask);
}

void iwl_set_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	__iwl_set_bit(trans, reg, mask);
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bit);

void iwl_clear_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	__iwl_clear_bit(trans, reg, mask);
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_clear_bit);

void iwl_set_bits_mask(struct iwl_trans *trans, u32 reg, u32 mask, u32 value)
{
	unsigned long flags;
	u32 v;

#ifdef CONFIG_IWLWIFI_DEBUG
	WARN_ON_ONCE(value & ~mask);
#endif

	spin_lock_irqsave(&trans->reg_lock, flags);
	v = iwl_read32(trans, reg);
	v &= ~mask;
	v |= value;
	iwl_write32(trans, reg, v);
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bits_mask);

int iwl_poll_bit(struct iwl_trans *trans, u32 addr,
		 u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read32(trans, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(iwl_poll_bit);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	iwl_trans_grab_nic_access(trans, false);
	value = iwl_read32(trans, reg);
	iwl_trans_release_nic_access(trans);
	spin_unlock_irqrestore(&trans->reg_lock, flags);

	return value;
}
EXPORT_SYMBOL_GPL(iwl_read_direct32);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		iwl_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_write_direct32);

int iwl_poll_direct_bit(struct iwl_trans *trans, u32 addr, u32 mask,
			int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_direct32(trans, addr) & mask) == mask)
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_GPL(iwl_poll_direct_bit);

static inline u32 __iwl_read_prph(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read_prph(trans, ofs);
	trace_iwlwifi_dev_ioread_prph32(trans->dev, ofs, val);
	return val;
}

static inline void __iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite_prph32(trans->dev, ofs, val);
	iwl_trans_write_prph(trans, ofs, val);
}

u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&trans->reg_lock, flags);
	iwl_trans_grab_nic_access(trans, false);
	val = __iwl_read_prph(trans, ofs);
	iwl_trans_release_nic_access(trans);
	spin_unlock_irqrestore(&trans->reg_lock, flags);
	return val;
}
EXPORT_SYMBOL_GPL(iwl_read_prph);

void iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		__iwl_write_prph(trans, ofs, val);
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_write_prph);

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		__iwl_write_prph(trans, ofs,
				 __iwl_read_prph(trans, ofs) | mask);
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		__iwl_write_prph(trans, ofs,
				 (__iwl_read_prph(trans, ofs) & mask) | bits);
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		val = __iwl_read_prph(trans, ofs);
		__iwl_write_prph(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_clear_bits_prph);

void _iwl_read_targ_mem_dwords(struct iwl_trans *trans, u32 addr,
			       void *buf, int dwords)
{
	unsigned long flags;
	int offs;
	u32 *vals = buf;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		iwl_write32(trans, HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		iwl_trans_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(_iwl_read_targ_mem_dwords);

u32 iwl_read_targ_mem(struct iwl_trans *trans, u32 addr)
{
	u32 value;

	_iwl_read_targ_mem_dwords(trans, addr, &value, 1);

	return value;
}
EXPORT_SYMBOL_GPL(iwl_read_targ_mem);

int _iwl_write_targ_mem_dwords(struct iwl_trans *trans, u32 addr,
			       const void *buf, int dwords)
{
	unsigned long flags;
	int offs, result = 0;
	const u32 *vals = buf;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_trans_grab_nic_access(trans, false))) {
		iwl_write32(trans, HBUS_TARG_MEM_WADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			iwl_write32(trans, HBUS_TARG_MEM_WDAT, vals[offs]);
		iwl_trans_release_nic_access(trans);
	} else {
		result = -EBUSY;
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);

	return result;
}
EXPORT_SYMBOL_GPL(_iwl_write_targ_mem_dwords);

int iwl_write_targ_mem(struct iwl_trans *trans, u32 addr, u32 val)
{
	return _iwl_write_targ_mem_dwords(trans, addr, &val, 1);
}
EXPORT_SYMBOL_GPL(iwl_write_targ_mem);
