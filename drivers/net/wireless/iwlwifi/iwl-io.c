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
#include"iwl-csr.h"
#include "iwl-debug.h"

#define IWL_POLL_INTERVAL 10	/* microseconds */

static inline void __iwl_set_bit(struct iwl_trans *trans, u32 reg, u32 mask)
{
	iwl_write32(trans, reg, iwl_read32(trans, reg) | mask);
}

static inline void __iwl_clear_bit(struct iwl_trans *trans, u32 reg, u32 mask)
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

int iwl_grab_nic_access_silent(struct iwl_trans *trans)
{
	int ret;

	lockdep_assert_held(&trans->reg_lock);

	/* this bit wakes up the NIC */
	__iwl_set_bit(trans, CSR_GP_CNTRL,
		      CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);

	/*
	 * These bits say the device is running, and should keep running for
	 * at least a short while (at least as long as MAC_ACCESS_REQ stays 1),
	 * but they do not indicate that embedded SRAM is restored yet;
	 * 3945 and 4965 have volatile SRAM, and must save/restore contents
	 * to/from host DRAM when sleeping/waking for power-saving.
	 * Each direction takes approximately 1/4 millisecond; with this
	 * overhead, it's a good idea to grab and hold MAC_ACCESS_REQUEST if a
	 * series of register accesses are expected (e.g. reading Event Log),
	 * to keep device from sleeping.
	 *
	 * CSR_UCODE_DRV_GP1 register bit MAC_SLEEP == 0 indicates that
	 * SRAM is okay/restored.  We don't check that here because this call
	 * is just for hardware register access; but GP1 MAC_SLEEP check is a
	 * good idea before accessing 3945/4965 SRAM (e.g. reading Event Log).
	 *
	 * 5000 series and later (including 1000 series) have non-volatile SRAM,
	 * and do not save/restore SRAM when power cycling.
	 */
	ret = iwl_poll_bit(trans, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (ret < 0) {
		iwl_write32(trans, CSR_RESET, CSR_RESET_REG_FLAG_FORCE_NMI);
		return -EIO;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(iwl_grab_nic_access_silent);

bool iwl_grab_nic_access(struct iwl_trans *trans)
{
	int ret = iwl_grab_nic_access_silent(trans);
	if (unlikely(ret)) {
		u32 val = iwl_read32(trans, CSR_GP_CNTRL);
		WARN_ONCE(1, "Timeout waiting for hardware access "
			     "(CSR_GP_CNTRL 0x%08x)\n", val);
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(iwl_grab_nic_access);

void iwl_release_nic_access(struct iwl_trans *trans)
{
	lockdep_assert_held(&trans->reg_lock);
	__iwl_clear_bit(trans, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	/*
	 * Above we read the CSR_GP_CNTRL register, which will flush
	 * any previous writes, but we need the write that clears the
	 * MAC_ACCESS_REQ bit to be performed before any other writes
	 * scheduled on different CPUs (after we drop reg_lock).
	 */
	mmiowb();
}
EXPORT_SYMBOL_GPL(iwl_release_nic_access);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	u32 value;
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	iwl_grab_nic_access(trans);
	value = iwl_read32(trans, reg);
	iwl_release_nic_access(trans);
	spin_unlock_irqrestore(&trans->reg_lock, flags);

	return value;
}
EXPORT_SYMBOL_GPL(iwl_read_direct32);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		iwl_write32(trans, reg, value);
		iwl_release_nic_access(trans);
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

static inline u32 __iwl_read_prph(struct iwl_trans *trans, u32 reg)
{
	iwl_write32(trans, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	return iwl_read32(trans, HBUS_TARG_PRPH_RDAT);
}

static inline void __iwl_write_prph(struct iwl_trans *trans, u32 addr, u32 val)
{
	iwl_write32(trans, HBUS_TARG_PRPH_WADDR,
		    ((addr & 0x0000FFFF) | (3 << 24)));
	iwl_write32(trans, HBUS_TARG_PRPH_WDAT, val);
}

u32 iwl_read_prph(struct iwl_trans *trans, u32 reg)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&trans->reg_lock, flags);
	iwl_grab_nic_access(trans);
	val = __iwl_read_prph(trans, reg);
	iwl_release_nic_access(trans);
	spin_unlock_irqrestore(&trans->reg_lock, flags);
	return val;
}
EXPORT_SYMBOL_GPL(iwl_read_prph);

void iwl_write_prph(struct iwl_trans *trans, u32 addr, u32 val)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		__iwl_write_prph(trans, addr, val);
		iwl_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_write_prph);

void iwl_set_bits_prph(struct iwl_trans *trans, u32 reg, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		__iwl_write_prph(trans, reg,
				 __iwl_read_prph(trans, reg) | mask);
		iwl_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 reg,
			    u32 bits, u32 mask)
{
	unsigned long flags;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		__iwl_write_prph(trans, reg,
				 (__iwl_read_prph(trans, reg) & mask) | bits);
		iwl_release_nic_access(trans);
	}
	spin_unlock_irqrestore(&trans->reg_lock, flags);
}
EXPORT_SYMBOL_GPL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 reg, u32 mask)
{
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		val = __iwl_read_prph(trans, reg);
		__iwl_write_prph(trans, reg, (val & ~mask));
		iwl_release_nic_access(trans);
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
	if (likely(iwl_grab_nic_access(trans))) {
		iwl_write32(trans, HBUS_TARG_MEM_RADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			vals[offs] = iwl_read32(trans, HBUS_TARG_MEM_RDAT);
		iwl_release_nic_access(trans);
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
			       void *buf, int dwords)
{
	unsigned long flags;
	int offs, result = 0;
	u32 *vals = buf;

	spin_lock_irqsave(&trans->reg_lock, flags);
	if (likely(iwl_grab_nic_access(trans))) {
		iwl_write32(trans, HBUS_TARG_MEM_WADDR, addr);
		for (offs = 0; offs < dwords; offs++)
			iwl_write32(trans, HBUS_TARG_MEM_WDAT, vals[offs]);
		iwl_release_nic_access(trans);
	} else
		result = -EBUSY;
	spin_unlock_irqrestore(&trans->reg_lock, flags);

	return result;
}
EXPORT_SYMBOL_GPL(_iwl_write_targ_mem_dwords);

int iwl_write_targ_mem(struct iwl_trans *trans, u32 addr, u32 val)
{
	return _iwl_write_targ_mem_dwords(trans, addr, &val, 1);
}
EXPORT_SYMBOL_GPL(iwl_write_targ_mem);
