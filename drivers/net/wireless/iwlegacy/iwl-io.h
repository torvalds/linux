/******************************************************************************
 *
 * Copyright(c) 2003 - 2011 Intel Corporation. All rights reserved.
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

#ifndef __il_io_h__
#define __il_io_h__

#include <linux/io.h>

#include "iwl-dev.h"
#include "iwl-debug.h"

static inline void _il_write8(struct il_priv *il, u32 ofs, u8 val)
{
	iowrite8(val, il->hw_base + ofs);
}
#define il_write8(il, ofs, val) _il_write8(il, ofs, val)

static inline void _il_write32(struct il_priv *il, u32 ofs, u32 val)
{
	iowrite32(val, il->hw_base + ofs);
}
#define il_write32(il, ofs, val) _il_write32(il, ofs, val)

static inline u32 _il_read32(struct il_priv *il, u32 ofs)
{
	u32 val = ioread32(il->hw_base + ofs);
	return val;
}
#define il_read32(p, o) _il_read32(p, o)

#define IL_POLL_INTERVAL 10	/* microseconds */
static inline int
_il_poll_bit(struct il_priv *il, u32 addr,
				u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((_il_read32(il, addr) & mask) == (bits & mask))
			return t;
		udelay(IL_POLL_INTERVAL);
		t += IL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
#define il_poll_bit(p, a, b, m, t) _il_poll_bit(p, a, b, m, t)

static inline void _il_set_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_write32(il, reg, _il_read32(il, reg) | mask);
}

static inline void il_set_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_set_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}

static inline void
_il_clear_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_write32(il, reg, _il_read32(il, reg) & ~mask);
}

static inline void il_clear_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_clear_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}

static inline int _il_grab_nic_access(struct il_priv *il)
{
	int ret;
	u32 val;

	/* this bit wakes up the NIC */
	_il_set_bit(il, CSR_GP_CNTRL,
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
	 */
	ret = _il_poll_bit(il, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (ret < 0) {
		val = _il_read32(il, CSR_GP_CNTRL);
		IL_ERR(il,
			"MAC is in deep sleep!.  CSR_GP_CNTRL = 0x%08X\n", val);
		_il_write32(il, CSR_RESET,
				CSR_RESET_REG_FLAG_FORCE_NMI);
		return -EIO;
	}

	return 0;
}
#define il_grab_nic_access(il) _il_grab_nic_access(il)

static inline void _il_release_nic_access(struct il_priv *il)
{
	_il_clear_bit(il, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#define il_release_nic_access(il) _il_release_nic_access(il)

static inline u32 _il_read_direct32(struct il_priv *il, u32 reg)
{
	return _il_read32(il, reg);
}

static inline u32 il_read_direct32(struct il_priv *il, u32 reg)
{
	u32 value;
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	value = _il_read_direct32(il, reg);
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return value;

}

static inline void _il_write_direct32(struct il_priv *il,
					 u32 reg, u32 value)
{
	_il_write32(il, reg, value);
}
static inline void
il_write_direct32(struct il_priv *il, u32 reg, u32 value)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (!il_grab_nic_access(il)) {
		_il_write_direct32(il, reg, value);
		il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline void il_write_reg_buf(struct il_priv *il,
					       u32 reg, u32 len, u32 *values)
{
	u32 count = sizeof(u32);

	if ((il != NULL) && (values != NULL)) {
		for (; 0 < len; len -= count, reg += count, values++)
			il_write_direct32(il, reg, *values);
	}
}

static inline int _il_poll_direct_bit(struct il_priv *il, u32 addr,
				       u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((il_read_direct32(il, addr) & mask) == mask)
			return t;
		udelay(IL_POLL_INTERVAL);
		t += IL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
#define il_poll_direct_bit _il_poll_direct_bit

static inline u32 _il_read_prph(struct il_priv *il, u32 reg)
{
	_il_write_direct32(il, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	rmb();
	return _il_read_direct32(il, HBUS_TARG_PRPH_RDAT);
}
static inline u32 il_read_prph(struct il_priv *il, u32 reg)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	val = _il_read_prph(il, reg);
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return val;
}

static inline void _il_write_prph(struct il_priv *il,
					     u32 addr, u32 val)
{
	_il_write_direct32(il, HBUS_TARG_PRPH_WADDR,
			      ((addr & 0x0000FFFF) | (3 << 24)));
	wmb();
	_il_write_direct32(il, HBUS_TARG_PRPH_WDAT, val);
}

static inline void
il_write_prph(struct il_priv *il, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (!il_grab_nic_access(il)) {
		_il_write_prph(il, addr, val);
		il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

#define _il_set_bits_prph(il, reg, mask) \
_il_write_prph(il, reg, (_il_read_prph(il, reg) | mask))

static inline void
il_set_bits_prph(struct il_priv *il, u32 reg, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	_il_set_bits_prph(il, reg, mask);
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

#define _il_set_bits_mask_prph(il, reg, bits, mask) \
_il_write_prph(il, reg,				\
		 ((_il_read_prph(il, reg) & mask) | bits))

static inline void il_set_bits_mask_prph(struct il_priv *il, u32 reg,
				u32 bits, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	_il_set_bits_mask_prph(il, reg, bits, mask);
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline void il_clear_bits_prph(struct il_priv
						 *il, u32 reg, u32 mask)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	val = _il_read_prph(il, reg);
	_il_write_prph(il, reg, (val & ~mask));
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline u32 il_read_targ_mem(struct il_priv *il, u32 addr)
{
	unsigned long reg_flags;
	u32 value;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);

	_il_write_direct32(il, HBUS_TARG_MEM_RADDR, addr);
	rmb();
	value = _il_read_direct32(il, HBUS_TARG_MEM_RDAT);

	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return value;
}

static inline void
il_write_targ_mem(struct il_priv *il, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (!il_grab_nic_access(il)) {
		_il_write_direct32(il, HBUS_TARG_MEM_WADDR, addr);
		wmb();
		_il_write_direct32(il, HBUS_TARG_MEM_WDAT, val);
		il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}

static inline void
il_write_targ_mem_buf(struct il_priv *il, u32 addr,
					  u32 len, u32 *values)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	if (!il_grab_nic_access(il)) {
		_il_write_direct32(il, HBUS_TARG_MEM_WADDR, addr);
		wmb();
		for (; 0 < len; len -= sizeof(u32), values++)
			_il_write_direct32(il,
					HBUS_TARG_MEM_WDAT, *values);

		il_release_nic_access(il);
	}
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
}
#endif
