/******************************************************************************
 *
 * Copyright(c) 2003 - 2009 Intel Corporation. All rights reserved.
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

#ifndef __iwl_io_h__
#define __iwl_io_h__

#include <linux/io.h>

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
 * routines, for example _iwl_read_direct32 calls the non-check version of
 * _iwl_read32.)
 *
 * These declarations are *extremely* useful in quickly isolating code deltas
 * which result in misconfiguration of the hardware I/O.  In combination with
 * git-bisect and the IO debug level you can quickly determine the specific
 * commit which breaks the IO sequence to the hardware.
 *
 */

#define _iwl_write32(priv, ofs, val) iowrite32((val), (priv)->hw_base + (ofs))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_write32(const char *f, u32 l, struct iwl_priv *priv,
				 u32 ofs, u32 val)
{
	IWL_DEBUG_IO(priv, "write32(0x%08X, 0x%08X) - %s %d\n", ofs, val, f, l);
	_iwl_write32(priv, ofs, val);
}
#define iwl_write32(priv, ofs, val) \
	__iwl_write32(__FILE__, __LINE__, priv, ofs, val)
#else
#define iwl_write32(priv, ofs, val) _iwl_write32(priv, ofs, val)
#endif

#define _iwl_read32(priv, ofs) ioread32((priv)->hw_base + (ofs))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read32(char *f, u32 l, struct iwl_priv *priv, u32 ofs)
{
	IWL_DEBUG_IO(priv, "read_direct32(0x%08X) - %s %d\n", ofs, f, l);
	return _iwl_read32(priv, ofs);
}
#define iwl_read32(priv, ofs) __iwl_read32(__FILE__, __LINE__, priv, ofs)
#else
#define iwl_read32(p, o) _iwl_read32(p, o)
#endif

#define IWL_POLL_INTERVAL 10	/* microseconds */
static inline int _iwl_poll_bit(struct iwl_priv *priv, u32 addr,
				u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((_iwl_read32(priv, addr) & mask) == (bits & mask))
			return t;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_poll_bit(const char *f, u32 l,
				 struct iwl_priv *priv, u32 addr,
				 u32 bits, u32 mask, int timeout)
{
	int ret = _iwl_poll_bit(priv, addr, bits, mask, timeout);
	IWL_DEBUG_IO(priv, "poll_bit(0x%08X, 0x%08X, 0x%08X) - %s- %s %d\n",
		     addr, bits, mask,
		     unlikely(ret  == -ETIMEDOUT) ? "timeout" : "", f, l);
	return ret;
}
#define iwl_poll_bit(priv, addr, bits, mask, timeout) \
	__iwl_poll_bit(__FILE__, __LINE__, priv, addr, bits, mask, timeout)
#else
#define iwl_poll_bit(p, a, b, m, t) _iwl_poll_bit(p, a, b, m, t)
#endif

static inline void _iwl_set_bit(struct iwl_priv *priv, u32 reg, u32 mask)
{
	_iwl_write32(priv, reg, _iwl_read32(priv, reg) | mask);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_set_bit(const char *f, u32 l,
				 struct iwl_priv *priv, u32 reg, u32 mask)
{
	u32 val = _iwl_read32(priv, reg) | mask;
	IWL_DEBUG_IO(priv, "set_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_iwl_write32(priv, reg, val);
}
#define iwl_set_bit(p, r, m) __iwl_set_bit(__FILE__, __LINE__, p, r, m)
#else
#define iwl_set_bit(p, r, m) _iwl_set_bit(p, r, m)
#endif

static inline void _iwl_clear_bit(struct iwl_priv *priv, u32 reg, u32 mask)
{
	_iwl_write32(priv, reg, _iwl_read32(priv, reg) & ~mask);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_clear_bit(const char *f, u32 l,
				   struct iwl_priv *priv, u32 reg, u32 mask)
{
	u32 val = _iwl_read32(priv, reg) & ~mask;
	IWL_DEBUG_IO(priv, "clear_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_iwl_write32(priv, reg, val);
}
#define iwl_clear_bit(p, r, m) __iwl_clear_bit(__FILE__, __LINE__, p, r, m)
#else
#define iwl_clear_bit(p, r, m) _iwl_clear_bit(p, r, m)
#endif

static inline int _iwl_grab_nic_access(struct iwl_priv *priv)
{
	int ret;
	u32 val;
#ifdef CONFIG_IWLWIFI_DEBUG
	if (atomic_read(&priv->restrict_refcnt))
		return 0;
#endif
	/* this bit wakes up the NIC */
	_iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	ret = _iwl_poll_bit(priv, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 15000);
	if (ret < 0) {
		val = _iwl_read32(priv, CSR_GP_CNTRL);
		IWL_ERR(priv, "MAC is in deep sleep!.  CSR_GP_CNTRL = 0x%08X\n", val);
		return -EIO;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	atomic_inc(&priv->restrict_refcnt);
#endif
	return 0;
}

#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_grab_nic_access(const char *f, u32 l,
					       struct iwl_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Grabbing access while already held %s %d.\n", f, l);

	IWL_DEBUG_IO(priv, "grabbing nic access - %s %d\n", f, l);
	return _iwl_grab_nic_access(priv);
}
#define iwl_grab_nic_access(priv) \
	__iwl_grab_nic_access(__FILE__, __LINE__, priv)
#else
#define iwl_grab_nic_access(priv) \
	_iwl_grab_nic_access(priv)
#endif

static inline void _iwl_release_nic_access(struct iwl_priv *priv)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	if (atomic_dec_and_test(&priv->restrict_refcnt))
#endif
		_iwl_clear_bit(priv, CSR_GP_CNTRL,
			       CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_release_nic_access(const char *f, u32 l,
					    struct iwl_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt) <= 0)
		IWL_ERR(priv, "Release unheld nic access at line %s %d.\n", f, l);

	IWL_DEBUG_IO(priv, "releasing nic access - %s %d\n", f, l);
	_iwl_release_nic_access(priv);
}
#define iwl_release_nic_access(priv) \
	__iwl_release_nic_access(__FILE__, __LINE__, priv)
#else
#define iwl_release_nic_access(priv) \
	_iwl_release_nic_access(priv)
#endif

static inline u32 _iwl_read_direct32(struct iwl_priv *priv, u32 reg)
{
	return _iwl_read32(priv, reg);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read_direct32(const char *f, u32 l,
					struct iwl_priv *priv, u32 reg)
{
	u32 value = _iwl_read_direct32(priv, reg);
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s %d\n", f, l);
	IWL_DEBUG_IO(priv, "read_direct32(0x%4X) = 0x%08x - %s %d \n", reg, value,
		     f, l);
	return value;
}
#define iwl_read_direct32(priv, reg) \
	__iwl_read_direct32(__FILE__, __LINE__, priv, reg)
#else
#define iwl_read_direct32 _iwl_read_direct32
#endif

static inline void _iwl_write_direct32(struct iwl_priv *priv,
					 u32 reg, u32 value)
{
	_iwl_write32(priv, reg, value);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static void __iwl_write_direct32(const char *f , u32 line,
				   struct iwl_priv *priv, u32 reg, u32 value)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s line %d\n", f, line);
	_iwl_write_direct32(priv, reg, value);
}
#define iwl_write_direct32(priv, reg, value) \
	__iwl_write_direct32(__func__, __LINE__, priv, reg, value)
#else
#define iwl_write_direct32 _iwl_write_direct32
#endif

static inline void iwl_write_reg_buf(struct iwl_priv *priv,
					       u32 reg, u32 len, u32 *values)
{
	u32 count = sizeof(u32);

	if ((priv != NULL) && (values != NULL)) {
		for (; 0 < len; len -= count, reg += count, values++)
			_iwl_write_direct32(priv, reg, *values);
	}
}

static inline int _iwl_poll_direct_bit(struct iwl_priv *priv, u32 addr,
				       u32 mask, int timeout)
{
	return _iwl_poll_bit(priv, addr, mask, mask, timeout);
}

#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_poll_direct_bit(const char *f, u32 l,
					    struct iwl_priv *priv,
					    u32 addr, u32 mask, int timeout)
{
	int ret  = _iwl_poll_direct_bit(priv, addr, mask, timeout);

	if (unlikely(ret == -ETIMEDOUT))
		IWL_DEBUG_IO(priv, "poll_direct_bit(0x%08X, 0x%08X) - "
			     "timedout - %s %d\n", addr, mask, f, l);
	else
		IWL_DEBUG_IO(priv, "poll_direct_bit(0x%08X, 0x%08X) = 0x%08X "
			     "- %s %d\n", addr, mask, ret, f, l);
	return ret;
}
#define iwl_poll_direct_bit(priv, addr, mask, timeout) \
	__iwl_poll_direct_bit(__FILE__, __LINE__, priv, addr, mask, timeout)
#else
#define iwl_poll_direct_bit _iwl_poll_direct_bit
#endif

static inline u32 _iwl_read_prph(struct iwl_priv *priv, u32 reg)
{
	_iwl_write_direct32(priv, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	rmb();
	return _iwl_read_direct32(priv, HBUS_TARG_PRPH_RDAT);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read_prph(const char *f, u32 line,
				  struct iwl_priv *priv, u32 reg)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s line %d\n", f, line);
	return _iwl_read_prph(priv, reg);
}

#define iwl_read_prph(priv, reg) \
	__iwl_read_prph(__func__, __LINE__, priv, reg)
#else
#define iwl_read_prph _iwl_read_prph
#endif

static inline void _iwl_write_prph(struct iwl_priv *priv,
					     u32 addr, u32 val)
{
	_iwl_write_direct32(priv, HBUS_TARG_PRPH_WADDR,
			      ((addr & 0x0000FFFF) | (3 << 24)));
	wmb();
	_iwl_write_direct32(priv, HBUS_TARG_PRPH_WDAT, val);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_write_prph(const char *f, u32 line,
				    struct iwl_priv *priv, u32 addr, u32 val)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s line %d\n", f, line);
	_iwl_write_prph(priv, addr, val);
}

#define iwl_write_prph(priv, addr, val) \
	__iwl_write_prph(__func__, __LINE__, priv, addr, val);
#else
#define iwl_write_prph _iwl_write_prph
#endif

#define _iwl_set_bits_prph(priv, reg, mask) \
	_iwl_write_prph(priv, reg, (_iwl_read_prph(priv, reg) | mask))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_set_bits_prph(const char *f, u32 line,
				       struct iwl_priv *priv,
				       u32 reg, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s line %d\n", f, line);

	_iwl_set_bits_prph(priv, reg, mask);
}
#define iwl_set_bits_prph(priv, reg, mask) \
	__iwl_set_bits_prph(__func__, __LINE__, priv, reg, mask)
#else
#define iwl_set_bits_prph _iwl_set_bits_prph
#endif

#define _iwl_set_bits_mask_prph(priv, reg, bits, mask) \
	_iwl_write_prph(priv, reg, ((_iwl_read_prph(priv, reg) & mask) | bits))

#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_set_bits_mask_prph(const char *f, u32 line,
		struct iwl_priv *priv, u32 reg, u32 bits, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERR(priv, "Nic access not held from %s line %d\n", f, line);
	_iwl_set_bits_mask_prph(priv, reg, bits, mask);
}
#define iwl_set_bits_mask_prph(priv, reg, bits, mask) \
	__iwl_set_bits_mask_prph(__func__, __LINE__, priv, reg, bits, mask)
#else
#define iwl_set_bits_mask_prph _iwl_set_bits_mask_prph
#endif

static inline void iwl_clear_bits_prph(struct iwl_priv
						 *priv, u32 reg, u32 mask)
{
	u32 val = _iwl_read_prph(priv, reg);
	_iwl_write_prph(priv, reg, (val & ~mask));
}

static inline u32 iwl_read_targ_mem(struct iwl_priv *priv, u32 addr)
{
	iwl_write_direct32(priv, HBUS_TARG_MEM_RADDR, addr);
	rmb();
	return iwl_read_direct32(priv, HBUS_TARG_MEM_RDAT);
}

static inline void iwl_write_targ_mem(struct iwl_priv *priv, u32 addr, u32 val)
{
	iwl_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
	wmb();
	iwl_write_direct32(priv, HBUS_TARG_MEM_WDAT, val);
}

static inline void iwl_write_targ_mem_buf(struct iwl_priv *priv, u32 addr,
					  u32 len, u32 *values)
{
	iwl_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
	wmb();
	for (; 0 < len; len -= sizeof(u32), values++)
		iwl_write_direct32(priv, HBUS_TARG_MEM_WDAT, *values);
}
#endif
