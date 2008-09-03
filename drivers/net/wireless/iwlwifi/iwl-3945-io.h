/******************************************************************************
 *
 * Copyright(c) 2003 - 2008 Intel Corporation. All rights reserved.
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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl3945_io_h__
#define __iwl3945_io_h__

#include <linux/io.h>

#include "iwl-3945-debug.h"

/*
 * IO, register, and NIC memory access functions
 *
 * NOTE on naming convention and macro usage for these
 *
 * A single _ prefix before a an access function means that no state
 * check or debug information is printed when that function is called.
 *
 * A double __ prefix before an access function means that state is checked
 * and the current line number is printed in addition to any other debug output.
 *
 * The non-prefixed name is the #define that maps the caller into a
 * #define that provides the caller's __LINE__ to the double prefix version.
 *
 * If you wish to call the function without any debug or state checking,
 * you should use the single _ prefix version (as is used by dependent IO
 * routines, for example _iwl3945_read_direct32 calls the non-check version of
 * _iwl3945_read32.)
 *
 * These declarations are *extremely* useful in quickly isolating code deltas
 * which result in misconfiguring of the hardware I/O.  In combination with
 * git-bisect and the IO debug level you can quickly determine the specific
 * commit which breaks the IO sequence to the hardware.
 *
 */

#define _iwl3945_write32(priv, ofs, val) iowrite32((val), (priv)->hw_base + (ofs))
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_write32(const char *f, u32 l, struct iwl3945_priv *priv,
				 u32 ofs, u32 val)
{
	IWL_DEBUG_IO("write32(0x%08X, 0x%08X) - %s %d\n", ofs, val, f, l);
	_iwl3945_write32(priv, ofs, val);
}
#define iwl3945_write32(priv, ofs, val) \
	__iwl3945_write32(__FILE__, __LINE__, priv, ofs, val)
#else
#define iwl3945_write32(priv, ofs, val) _iwl3945_write32(priv, ofs, val)
#endif

#define _iwl3945_read32(priv, ofs) ioread32((priv)->hw_base + (ofs))
#ifdef CONFIG_IWL3945_DEBUG
static inline u32 __iwl3945_read32(char *f, u32 l, struct iwl3945_priv *priv, u32 ofs)
{
	IWL_DEBUG_IO("read_direct32(0x%08X) - %s %d\n", ofs, f, l);
	return _iwl3945_read32(priv, ofs);
}
#define iwl3945_read32(priv, ofs)__iwl3945_read32(__FILE__, __LINE__, priv, ofs)
#else
#define iwl3945_read32(p, o) _iwl3945_read32(p, o)
#endif

static inline int _iwl3945_poll_bit(struct iwl3945_priv *priv, u32 addr,
				u32 bits, u32 mask, int timeout)
{
	int i = 0;

	do {
		if ((_iwl3945_read32(priv, addr) & mask) == (bits & mask))
			return i;
		mdelay(10);
		i += 10;
	} while (i < timeout);

	return -ETIMEDOUT;
}
#ifdef CONFIG_IWL3945_DEBUG
static inline int __iwl3945_poll_bit(const char *f, u32 l,
				 struct iwl3945_priv *priv, u32 addr,
				 u32 bits, u32 mask, int timeout)
{
	int ret = _iwl3945_poll_bit(priv, addr, bits, mask, timeout);
	IWL_DEBUG_IO("poll_bit(0x%08X, 0x%08X, 0x%08X) - %s- %s %d\n",
		      addr, bits, mask,
		      unlikely(ret  == -ETIMEDOUT)?"timeout":"", f, l);
	return ret;
}
#define iwl3945_poll_bit(priv, addr, bits, mask, timeout) \
	__iwl3945_poll_bit(__FILE__, __LINE__, priv, addr, bits, mask, timeout)
#else
#define iwl3945_poll_bit(p, a, b, m, t) _iwl3945_poll_bit(p, a, b, m, t)
#endif

static inline void _iwl3945_set_bit(struct iwl3945_priv *priv, u32 reg, u32 mask)
{
	_iwl3945_write32(priv, reg, _iwl3945_read32(priv, reg) | mask);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_set_bit(const char *f, u32 l,
				 struct iwl3945_priv *priv, u32 reg, u32 mask)
{
	u32 val = _iwl3945_read32(priv, reg) | mask;
	IWL_DEBUG_IO("set_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_iwl3945_write32(priv, reg, val);
}
#define iwl3945_set_bit(p, r, m) __iwl3945_set_bit(__FILE__, __LINE__, p, r, m)
#else
#define iwl3945_set_bit(p, r, m) _iwl3945_set_bit(p, r, m)
#endif

static inline void _iwl3945_clear_bit(struct iwl3945_priv *priv, u32 reg, u32 mask)
{
	_iwl3945_write32(priv, reg, _iwl3945_read32(priv, reg) & ~mask);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_clear_bit(const char *f, u32 l,
				   struct iwl3945_priv *priv, u32 reg, u32 mask)
{
	u32 val = _iwl3945_read32(priv, reg) & ~mask;
	IWL_DEBUG_IO("clear_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_iwl3945_write32(priv, reg, val);
}
#define iwl3945_clear_bit(p, r, m) __iwl3945_clear_bit(__FILE__, __LINE__, p, r, m)
#else
#define iwl3945_clear_bit(p, r, m) _iwl3945_clear_bit(p, r, m)
#endif

static inline int _iwl3945_grab_nic_access(struct iwl3945_priv *priv)
{
	int ret;
#ifdef CONFIG_IWL3945_DEBUG
	if (atomic_read(&priv->restrict_refcnt))
		return 0;
#endif
	/* this bit wakes up the NIC */
	_iwl3945_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	ret = _iwl3945_poll_bit(priv, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 50);
	if (ret < 0) {
		IWL_ERROR("MAC is in deep sleep!\n");
		return -EIO;
	}

#ifdef CONFIG_IWL3945_DEBUG
	atomic_inc(&priv->restrict_refcnt);
#endif
	return 0;
}

#ifdef CONFIG_IWL3945_DEBUG
static inline int __iwl3945_grab_nic_access(const char *f, u32 l,
					       struct iwl3945_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt))
		IWL_DEBUG_INFO("Grabbing access while already held at "
			       "line %d.\n", l);

	IWL_DEBUG_IO("grabbing nic access - %s %d\n", f, l);
	return _iwl3945_grab_nic_access(priv);
}
#define iwl3945_grab_nic_access(priv) \
	__iwl3945_grab_nic_access(__FILE__, __LINE__, priv)
#else
#define iwl3945_grab_nic_access(priv) \
	_iwl3945_grab_nic_access(priv)
#endif

static inline void _iwl3945_release_nic_access(struct iwl3945_priv *priv)
{
#ifdef CONFIG_IWL3945_DEBUG
	if (atomic_dec_and_test(&priv->restrict_refcnt))
#endif
		_iwl3945_clear_bit(priv, CSR_GP_CNTRL,
			       CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_release_nic_access(const char *f, u32 l,
					    struct iwl3945_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt) <= 0)
		IWL_ERROR("Release unheld nic access at line %d.\n", l);

	IWL_DEBUG_IO("releasing nic access - %s %d\n", f, l);
	_iwl3945_release_nic_access(priv);
}
#define iwl3945_release_nic_access(priv) \
	__iwl3945_release_nic_access(__FILE__, __LINE__, priv)
#else
#define iwl3945_release_nic_access(priv) \
	_iwl3945_release_nic_access(priv)
#endif

static inline u32 _iwl3945_read_direct32(struct iwl3945_priv *priv, u32 reg)
{
	return _iwl3945_read32(priv, reg);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline u32 __iwl3945_read_direct32(const char *f, u32 l,
					struct iwl3945_priv *priv, u32 reg)
{
	u32 value = _iwl3945_read_direct32(priv, reg);
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access not held from %s %d\n", f, l);
	IWL_DEBUG_IO("read_direct32(0x%4X) = 0x%08x - %s %d \n", reg, value,
		     f, l);
	return value;
}
#define iwl3945_read_direct32(priv, reg) \
	__iwl3945_read_direct32(__FILE__, __LINE__, priv, reg)
#else
#define iwl3945_read_direct32 _iwl3945_read_direct32
#endif

static inline void _iwl3945_write_direct32(struct iwl3945_priv *priv,
					 u32 reg, u32 value)
{
	_iwl3945_write32(priv, reg, value);
}
#ifdef CONFIG_IWL3945_DEBUG
static void __iwl3945_write_direct32(u32 line,
				   struct iwl3945_priv *priv, u32 reg, u32 value)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access not held from line %d\n", line);
	_iwl3945_write_direct32(priv, reg, value);
}
#define iwl3945_write_direct32(priv, reg, value) \
	__iwl3945_write_direct32(__LINE__, priv, reg, value)
#else
#define iwl3945_write_direct32 _iwl3945_write_direct32
#endif

static inline void iwl3945_write_reg_buf(struct iwl3945_priv *priv,
					       u32 reg, u32 len, u32 *values)
{
	u32 count = sizeof(u32);

	if ((priv != NULL) && (values != NULL)) {
		for (; 0 < len; len -= count, reg += count, values++)
			_iwl3945_write_direct32(priv, reg, *values);
	}
}

static inline int _iwl3945_poll_direct_bit(struct iwl3945_priv *priv,
					   u32 addr, u32 mask, int timeout)
{
	int i = 0;

	do {
		if ((_iwl3945_read_direct32(priv, addr) & mask) == mask)
			return i;
		mdelay(10);
		i += 10;
	} while (i < timeout);

	return -ETIMEDOUT;
}

#ifdef CONFIG_IWL3945_DEBUG
static inline int __iwl3945_poll_direct_bit(const char *f, u32 l,
					    struct iwl3945_priv *priv,
					    u32 addr, u32 mask, int timeout)
{
	int ret  = _iwl3945_poll_direct_bit(priv, addr, mask, timeout);

	if (unlikely(ret == -ETIMEDOUT))
		IWL_DEBUG_IO("poll_direct_bit(0x%08X, 0x%08X) - "
			     "timedout - %s %d\n", addr, mask, f, l);
	else
		IWL_DEBUG_IO("poll_direct_bit(0x%08X, 0x%08X) = 0x%08X "
			     "- %s %d\n", addr, mask, ret, f, l);
	return ret;
}
#define iwl3945_poll_direct_bit(priv, addr, mask, timeout) \
	__iwl3945_poll_direct_bit(__FILE__, __LINE__, priv, addr, mask, timeout)
#else
#define iwl3945_poll_direct_bit _iwl3945_poll_direct_bit
#endif

static inline u32 _iwl3945_read_prph(struct iwl3945_priv *priv, u32 reg)
{
	_iwl3945_write_direct32(priv, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	return _iwl3945_read_direct32(priv, HBUS_TARG_PRPH_RDAT);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline u32 __iwl3945_read_prph(u32 line, struct iwl3945_priv *priv, u32 reg)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access not held from line %d\n", line);
	return _iwl3945_read_prph(priv, reg);
}

#define iwl3945_read_prph(priv, reg) \
	__iwl3945_read_prph(__LINE__, priv, reg)
#else
#define iwl3945_read_prph _iwl3945_read_prph
#endif

static inline void _iwl3945_write_prph(struct iwl3945_priv *priv,
					     u32 addr, u32 val)
{
	_iwl3945_write_direct32(priv, HBUS_TARG_PRPH_WADDR,
			      ((addr & 0x0000FFFF) | (3 << 24)));
	_iwl3945_write_direct32(priv, HBUS_TARG_PRPH_WDAT, val);
}
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_write_prph(u32 line, struct iwl3945_priv *priv,
					      u32 addr, u32 val)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access from line %d\n", line);
	_iwl3945_write_prph(priv, addr, val);
}

#define iwl3945_write_prph(priv, addr, val) \
	__iwl3945_write_prph(__LINE__, priv, addr, val);
#else
#define iwl3945_write_prph _iwl3945_write_prph
#endif

#define _iwl3945_set_bits_prph(priv, reg, mask) \
	_iwl3945_write_prph(priv, reg, (_iwl3945_read_prph(priv, reg) | mask))
#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_set_bits_prph(u32 line, struct iwl3945_priv *priv,
					u32 reg, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access not held from line %d\n", line);

	_iwl3945_set_bits_prph(priv, reg, mask);
}
#define iwl3945_set_bits_prph(priv, reg, mask) \
	__iwl3945_set_bits_prph(__LINE__, priv, reg, mask)
#else
#define iwl3945_set_bits_prph _iwl3945_set_bits_prph
#endif

#define _iwl3945_set_bits_mask_prph(priv, reg, bits, mask) \
	_iwl3945_write_prph(priv, reg, ((_iwl3945_read_prph(priv, reg) & mask) | bits))

#ifdef CONFIG_IWL3945_DEBUG
static inline void __iwl3945_set_bits_mask_prph(u32 line,
		struct iwl3945_priv *priv, u32 reg, u32 bits, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Nic access not held from line %d\n", line);
	_iwl3945_set_bits_mask_prph(priv, reg, bits, mask);
}
#define iwl3945_set_bits_mask_prph(priv, reg, bits, mask) \
	__iwl3945_set_bits_mask_prph(__LINE__, priv, reg, bits, mask)
#else
#define iwl3945_set_bits_mask_prph _iwl3945_set_bits_mask_prph
#endif

static inline void iwl3945_clear_bits_prph(struct iwl3945_priv
						 *priv, u32 reg, u32 mask)
{
	u32 val = _iwl3945_read_prph(priv, reg);
	_iwl3945_write_prph(priv, reg, (val & ~mask));
}

static inline u32 iwl3945_read_targ_mem(struct iwl3945_priv *priv, u32 addr)
{
	iwl3945_write_direct32(priv, HBUS_TARG_MEM_RADDR, addr);
	return iwl3945_read_direct32(priv, HBUS_TARG_MEM_RDAT);
}

static inline void iwl3945_write_targ_mem(struct iwl3945_priv *priv, u32 addr, u32 val)
{
	iwl3945_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
	iwl3945_write_direct32(priv, HBUS_TARG_MEM_WDAT, val);
}

static inline void iwl3945_write_targ_mem_buf(struct iwl3945_priv *priv, u32 addr,
					  u32 len, u32 *values)
{
	iwl3945_write_direct32(priv, HBUS_TARG_MEM_WADDR, addr);
	for (; 0 < len; len -= sizeof(u32), values++)
		iwl3945_write_direct32(priv, HBUS_TARG_MEM_WDAT, *values);
}
#endif
