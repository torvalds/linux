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

/*
 * IO, register, and NIC memory access functions
 *
 * NOTE on naming convention and macro usage for these
 *
 * A single _ prefix before a an access function means that no state
 * check or debug information is printed when that function is called.
 *
 * A double __ prefix before an access function means that state is checked
 * and the current line number and caller function name are printed in addition
 * to any other debug output.
 *
 * The non-prefixed name is the #define that maps the caller into a
 * #define that provides the caller's name and __LINE__ to the double
 * prefix version.
 *
 * If you wish to call the function without any debug or state checking,
 * you should use the single _ prefix version (as is used by dependent IO
 * routines, for example _il_read_direct32 calls the non-check version of
 * _il_read32.)
 *
 * These declarations are *extremely* useful in quickly isolating code deltas
 * which result in misconfiguration of the hardware I/O.  In combination with
 * git-bisect and the IO debug level you can quickly determine the specific
 * commit which breaks the IO sequence to the hardware.
 *
 */

static inline void _il_write8(struct il_priv *il, u32 ofs, u8 val)
{
	iowrite8(val, il->hw_base + ofs);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_write8(const char *f, u32 l, struct il_priv *il,
				 u32 ofs, u8 val)
{
	IL_DEBUG_IO(il, "write8(0x%08X, 0x%02X) - %s %d\n", ofs, val, f, l);
	_il_write8(il, ofs, val);
}
#define il_write8(il, ofs, val) \
	__il_write8(__FILE__, __LINE__, il, ofs, val)
#else
#define il_write8(il, ofs, val) _il_write8(il, ofs, val)
#endif


static inline void _il_write32(struct il_priv *il, u32 ofs, u32 val)
{
	iowrite32(val, il->hw_base + ofs);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_write32(const char *f, u32 l, struct il_priv *il,
				 u32 ofs, u32 val)
{
	IL_DEBUG_IO(il, "write32(0x%08X, 0x%08X) - %s %d\n", ofs, val, f, l);
	_il_write32(il, ofs, val);
}
#define il_write32(il, ofs, val) \
	__il_write32(__FILE__, __LINE__, il, ofs, val)
#else
#define il_write32(il, ofs, val) _il_write32(il, ofs, val)
#endif

static inline u32 _il_read32(struct il_priv *il, u32 ofs)
{
	u32 val = ioread32(il->hw_base + ofs);
	return val;
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline u32
__il_read32(char *f, u32 l, struct il_priv *il, u32 ofs)
{
	IL_DEBUG_IO(il, "read_direct32(0x%08X) - %s %d\n", ofs, f, l);
	return _il_read32(il, ofs);
}
#define il_read32(il, ofs) __il_read32(__FILE__, __LINE__, il, ofs)
#else
#define il_read32(p, o) _il_read32(p, o)
#endif

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
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_poll_bit(const char *f, u32 l,
				 struct il_priv *il, u32 addr,
				 u32 bits, u32 mask, int timeout)
{
	int ret = _il_poll_bit(il, addr, bits, mask, timeout);
	IL_DEBUG_IO(il, "poll_bit(0x%08X, 0x%08X, 0x%08X) - %s- %s %d\n",
		     addr, bits, mask,
		     unlikely(ret  == -ETIMEDOUT) ? "timeout" : "", f, l);
	return ret;
}
#define il_poll_bit(il, addr, bits, mask, timeout) \
	__il_poll_bit(__FILE__, __LINE__, il, addr, \
	bits, mask, timeout)
#else
#define il_poll_bit(p, a, b, m, t) _il_poll_bit(p, a, b, m, t)
#endif

static inline void _il_set_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_write32(il, reg, _il_read32(il, reg) | mask);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void __il_set_bit(const char *f, u32 l,
				 struct il_priv *il, u32 reg, u32 mask)
{
	u32 val = _il_read32(il, reg) | mask;
	IL_DEBUG_IO(il, "set_bit(0x%08X, 0x%08X) = 0x%08X\n", reg,
							mask, val);
	_il_write32(il, reg, val);
}
static inline void il_set_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	__il_set_bit(__FILE__, __LINE__, p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
#else
static inline void il_set_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_set_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
#endif

static inline void
_il_clear_bit(struct il_priv *il, u32 reg, u32 mask)
{
	_il_write32(il, reg, _il_read32(il, reg) & ~mask);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_clear_bit(const char *f, u32 l,
				   struct il_priv *il, u32 reg, u32 mask)
{
	u32 val = _il_read32(il, reg) & ~mask;
	IL_DEBUG_IO(il, "clear_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_il_write32(il, reg, val);
}
static inline void il_clear_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	__il_clear_bit(__FILE__, __LINE__, p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
#else
static inline void il_clear_bit(struct il_priv *p, u32 r, u32 m)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&p->reg_lock, reg_flags);
	_il_clear_bit(p, r, m);
	spin_unlock_irqrestore(&p->reg_lock, reg_flags);
}
#endif

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

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_grab_nic_access(const char *f, u32 l,
					       struct il_priv *il)
{
	IL_DEBUG_IO(il, "grabbing nic access - %s %d\n", f, l);
	return _il_grab_nic_access(il);
}
#define il_grab_nic_access(il) \
	__il_grab_nic_access(__FILE__, __LINE__, il)
#else
#define il_grab_nic_access(il) \
	_il_grab_nic_access(il)
#endif

static inline void _il_release_nic_access(struct il_priv *il)
{
	_il_clear_bit(il, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void __il_release_nic_access(const char *f, u32 l,
					    struct il_priv *il)
{

	IL_DEBUG_IO(il, "releasing nic access - %s %d\n", f, l);
	_il_release_nic_access(il);
}
#define il_release_nic_access(il) \
	__il_release_nic_access(__FILE__, __LINE__, il)
#else
#define il_release_nic_access(il) \
	_il_release_nic_access(il)
#endif

static inline u32 _il_read_direct32(struct il_priv *il, u32 reg)
{
	return _il_read32(il, reg);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline u32 __il_read_direct32(const char *f, u32 l,
					struct il_priv *il, u32 reg)
{
	u32 value = _il_read_direct32(il, reg);
	IL_DEBUG_IO(il,
			"read_direct32(0x%4X) = 0x%08x - %s %d\n", reg, value,
		     f, l);
	return value;
}
static inline u32 il_read_direct32(struct il_priv *il, u32 reg)
{
	u32 value;
	unsigned long reg_flags;

	spin_lock_irqsave(&il->reg_lock, reg_flags);
	il_grab_nic_access(il);
	value = __il_read_direct32(__FILE__, __LINE__, il, reg);
	il_release_nic_access(il);
	spin_unlock_irqrestore(&il->reg_lock, reg_flags);
	return value;
}

#else
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
#endif

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

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_poll_direct_bit(const char *f, u32 l,
					    struct il_priv *il,
					    u32 addr, u32 mask, int timeout)
{
	int ret  = _il_poll_direct_bit(il, addr, mask, timeout);

	if (unlikely(ret == -ETIMEDOUT))
		IL_DEBUG_IO(il, "poll_direct_bit(0x%08X, 0x%08X) - "
			     "timedout - %s %d\n", addr, mask, f, l);
	else
		IL_DEBUG_IO(il, "poll_direct_bit(0x%08X, 0x%08X) = 0x%08X "
			     "- %s %d\n", addr, mask, ret, f, l);
	return ret;
}
#define il_poll_direct_bit(il, addr, mask, timeout) \
__il_poll_direct_bit(__FILE__, __LINE__, il, addr, mask, timeout)
#else
#define il_poll_direct_bit _il_poll_direct_bit
#endif

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
