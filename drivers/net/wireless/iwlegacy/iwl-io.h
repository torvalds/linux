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

static inline void _il_write8(struct il_priv *priv, u32 ofs, u8 val)
{
	iowrite8(val, priv->hw_base + ofs);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_write8(const char *f, u32 l, struct il_priv *priv,
				 u32 ofs, u8 val)
{
	IL_DEBUG_IO(priv, "write8(0x%08X, 0x%02X) - %s %d\n", ofs, val, f, l);
	_il_write8(priv, ofs, val);
}
#define il_write8(priv, ofs, val) \
	__il_write8(__FILE__, __LINE__, priv, ofs, val)
#else
#define il_write8(priv, ofs, val) _il_write8(priv, ofs, val)
#endif


static inline void _il_write32(struct il_priv *priv, u32 ofs, u32 val)
{
	iowrite32(val, priv->hw_base + ofs);
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_write32(const char *f, u32 l, struct il_priv *priv,
				 u32 ofs, u32 val)
{
	IL_DEBUG_IO(priv, "write32(0x%08X, 0x%08X) - %s %d\n", ofs, val, f, l);
	_il_write32(priv, ofs, val);
}
#define il_write32(priv, ofs, val) \
	__il_write32(__FILE__, __LINE__, priv, ofs, val)
#else
#define il_write32(priv, ofs, val) _il_write32(priv, ofs, val)
#endif

static inline u32 _il_read32(struct il_priv *priv, u32 ofs)
{
	u32 val = ioread32(priv->hw_base + ofs);
	return val;
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline u32
__il_read32(char *f, u32 l, struct il_priv *priv, u32 ofs)
{
	IL_DEBUG_IO(priv, "read_direct32(0x%08X) - %s %d\n", ofs, f, l);
	return _il_read32(priv, ofs);
}
#define il_read32(priv, ofs) __il_read32(__FILE__, __LINE__, priv, ofs)
#else
#define il_read32(p, o) _il_read32(p, o)
#endif

#define IL_POLL_INTERVAL 10	/* microseconds */
static inline int
_il_poll_bit(struct il_priv *priv, u32 addr,
				u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((_il_read32(priv, addr) & mask) == (bits & mask))
			return t;
		udelay(IL_POLL_INTERVAL);
		t += IL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_poll_bit(const char *f, u32 l,
				 struct il_priv *priv, u32 addr,
				 u32 bits, u32 mask, int timeout)
{
	int ret = _il_poll_bit(priv, addr, bits, mask, timeout);
	IL_DEBUG_IO(priv, "poll_bit(0x%08X, 0x%08X, 0x%08X) - %s- %s %d\n",
		     addr, bits, mask,
		     unlikely(ret  == -ETIMEDOUT) ? "timeout" : "", f, l);
	return ret;
}
#define il_poll_bit(priv, addr, bits, mask, timeout) \
	__il_poll_bit(__FILE__, __LINE__, priv, addr, \
	bits, mask, timeout)
#else
#define il_poll_bit(p, a, b, m, t) _il_poll_bit(p, a, b, m, t)
#endif

static inline void _il_set_bit(struct il_priv *priv, u32 reg, u32 mask)
{
	_il_write32(priv, reg, _il_read32(priv, reg) | mask);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void __il_set_bit(const char *f, u32 l,
				 struct il_priv *priv, u32 reg, u32 mask)
{
	u32 val = _il_read32(priv, reg) | mask;
	IL_DEBUG_IO(priv, "set_bit(0x%08X, 0x%08X) = 0x%08X\n", reg,
							mask, val);
	_il_write32(priv, reg, val);
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
_il_clear_bit(struct il_priv *priv, u32 reg, u32 mask)
{
	_il_write32(priv, reg, _il_read32(priv, reg) & ~mask);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void
__il_clear_bit(const char *f, u32 l,
				   struct il_priv *priv, u32 reg, u32 mask)
{
	u32 val = _il_read32(priv, reg) & ~mask;
	IL_DEBUG_IO(priv, "clear_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_il_write32(priv, reg, val);
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

static inline int _il_grab_nic_access(struct il_priv *priv)
{
	int ret;
	u32 val;

	/* this bit wakes up the NIC */
	_il_set_bit(priv, CSR_GP_CNTRL,
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
	ret = _il_poll_bit(priv, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (ret < 0) {
		val = _il_read32(priv, CSR_GP_CNTRL);
		IL_ERR(priv,
			"MAC is in deep sleep!.  CSR_GP_CNTRL = 0x%08X\n", val);
		_il_write32(priv, CSR_RESET,
				CSR_RESET_REG_FLAG_FORCE_NMI);
		return -EIO;
	}

	return 0;
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_grab_nic_access(const char *f, u32 l,
					       struct il_priv *priv)
{
	IL_DEBUG_IO(priv, "grabbing nic access - %s %d\n", f, l);
	return _il_grab_nic_access(priv);
}
#define il_grab_nic_access(priv) \
	__il_grab_nic_access(__FILE__, __LINE__, priv)
#else
#define il_grab_nic_access(priv) \
	_il_grab_nic_access(priv)
#endif

static inline void _il_release_nic_access(struct il_priv *priv)
{
	_il_clear_bit(priv, CSR_GP_CNTRL,
			CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline void __il_release_nic_access(const char *f, u32 l,
					    struct il_priv *priv)
{

	IL_DEBUG_IO(priv, "releasing nic access - %s %d\n", f, l);
	_il_release_nic_access(priv);
}
#define il_release_nic_access(priv) \
	__il_release_nic_access(__FILE__, __LINE__, priv)
#else
#define il_release_nic_access(priv) \
	_il_release_nic_access(priv)
#endif

static inline u32 _il_read_direct32(struct il_priv *priv, u32 reg)
{
	return _il_read32(priv, reg);
}
#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline u32 __il_read_direct32(const char *f, u32 l,
					struct il_priv *priv, u32 reg)
{
	u32 value = _il_read_direct32(priv, reg);
	IL_DEBUG_IO(priv,
			"read_direct32(0x%4X) = 0x%08x - %s %d\n", reg, value,
		     f, l);
	return value;
}
static inline u32 il_read_direct32(struct il_priv *priv, u32 reg)
{
	u32 value;
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	value = __il_read_direct32(__FILE__, __LINE__, priv, reg);
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
	return value;
}

#else
static inline u32 il_read_direct32(struct il_priv *priv, u32 reg)
{
	u32 value;
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	value = _il_read_direct32(priv, reg);
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
	return value;

}
#endif

static inline void _il_write_direct32(struct il_priv *priv,
					 u32 reg, u32 value)
{
	_il_write32(priv, reg, value);
}
static inline void
il_write_direct32(struct il_priv *priv, u32 reg, u32 value)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	if (!il_grab_nic_access(priv)) {
		_il_write_direct32(priv, reg, value);
		il_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

static inline void il_write_reg_buf(struct il_priv *priv,
					       u32 reg, u32 len, u32 *values)
{
	u32 count = sizeof(u32);

	if ((priv != NULL) && (values != NULL)) {
		for (; 0 < len; len -= count, reg += count, values++)
			il_write_direct32(priv, reg, *values);
	}
}

static inline int _il_poll_direct_bit(struct il_priv *priv, u32 addr,
				       u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((il_read_direct32(priv, addr) & mask) == mask)
			return t;
		udelay(IL_POLL_INTERVAL);
		t += IL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

#ifdef CONFIG_IWLWIFI_LEGACY_DEBUG
static inline int __il_poll_direct_bit(const char *f, u32 l,
					    struct il_priv *priv,
					    u32 addr, u32 mask, int timeout)
{
	int ret  = _il_poll_direct_bit(priv, addr, mask, timeout);

	if (unlikely(ret == -ETIMEDOUT))
		IL_DEBUG_IO(priv, "poll_direct_bit(0x%08X, 0x%08X) - "
			     "timedout - %s %d\n", addr, mask, f, l);
	else
		IL_DEBUG_IO(priv, "poll_direct_bit(0x%08X, 0x%08X) = 0x%08X "
			     "- %s %d\n", addr, mask, ret, f, l);
	return ret;
}
#define il_poll_direct_bit(priv, addr, mask, timeout) \
__il_poll_direct_bit(__FILE__, __LINE__, priv, addr, mask, timeout)
#else
#define il_poll_direct_bit _il_poll_direct_bit
#endif

static inline u32 _il_read_prph(struct il_priv *priv, u32 reg)
{
	_il_write_direct32(priv, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	rmb();
	return _il_read_direct32(priv, HBUS_TARG_PRPH_RDAT);
}
static inline u32 il_read_prph(struct il_priv *priv, u32 reg)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	val = _il_read_prph(priv, reg);
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
	return val;
}

static inline void _il_write_prph(struct il_priv *priv,
					     u32 addr, u32 val)
{
	_il_write_direct32(priv, HBUS_TARG_PRPH_WADDR,
			      ((addr & 0x0000FFFF) | (3 << 24)));
	wmb();
	_il_write_direct32(priv, HBUS_TARG_PRPH_WDAT, val);
}

static inline void
il_write_prph(struct il_priv *priv, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	if (!il_grab_nic_access(priv)) {
		_il_write_prph(priv, addr, val);
		il_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

#define _il_set_bits_prph(priv, reg, mask) \
_il_write_prph(priv, reg, (_il_read_prph(priv, reg) | mask))

static inline void
il_set_bits_prph(struct il_priv *priv, u32 reg, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	_il_set_bits_prph(priv, reg, mask);
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

#define _il_set_bits_mask_prph(priv, reg, bits, mask) \
_il_write_prph(priv, reg,				\
		 ((_il_read_prph(priv, reg) & mask) | bits))

static inline void il_set_bits_mask_prph(struct il_priv *priv, u32 reg,
				u32 bits, u32 mask)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	_il_set_bits_mask_prph(priv, reg, bits, mask);
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

static inline void il_clear_bits_prph(struct il_priv
						 *priv, u32 reg, u32 mask)
{
	unsigned long reg_flags;
	u32 val;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);
	val = _il_read_prph(priv, reg);
	_il_write_prph(priv, reg, (val & ~mask));
	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

static inline u32 il_read_targ_mem(struct il_priv *priv, u32 addr)
{
	unsigned long reg_flags;
	u32 value;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	il_grab_nic_access(priv);

	_il_write_direct32(priv, HBUS_TARG_MEM_RADDR, addr);
	rmb();
	value = _il_read_direct32(priv, HBUS_TARG_MEM_RDAT);

	il_release_nic_access(priv);
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
	return value;
}

static inline void
il_write_targ_mem(struct il_priv *priv, u32 addr, u32 val)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	if (!il_grab_nic_access(priv)) {
		_il_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
		wmb();
		_il_write_direct32(priv, HBUS_TARG_MEM_WDAT, val);
		il_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}

static inline void
il_write_targ_mem_buf(struct il_priv *priv, u32 addr,
					  u32 len, u32 *values)
{
	unsigned long reg_flags;

	spin_lock_irqsave(&priv->reg_lock, reg_flags);
	if (!il_grab_nic_access(priv)) {
		_il_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
		wmb();
		for (; 0 < len; len -= sizeof(u32), values++)
			_il_write_direct32(priv,
					HBUS_TARG_MEM_WDAT, *values);

		il_release_nic_access(priv);
	}
	spin_unlock_irqrestore(&priv->reg_lock, reg_flags);
}
#endif
