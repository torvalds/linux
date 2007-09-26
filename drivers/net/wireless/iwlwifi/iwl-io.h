/******************************************************************************
 *
 * Copyright(c) 2003 - 2007 Intel Corporation. All rights reserved.
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
 * (in the case of *restricted calls) and the current line number is printed
 * in addition to any other debug output.
 *
 * The non-prefixed name is the #define that maps the caller into a
 * #define that provides the caller's __LINE__ to the double prefix version.
 *
 * If you wish to call the function without any debug or state checking,
 * you should use the single _ prefix version (as is used by dependent IO
 * routines, for example _iwl_read_restricted calls the non-check version of
 * _iwl_read32.)
 *
 * These declarations are *extremely* useful in quickly isolating code deltas
 * which result in misconfiguring of the hardware I/O.  In combination with
 * git-bisect and the IO debug level you can quickly determine the specific
 * commit which breaks the IO sequence to the hardware.
 *
 */

#define _iwl_write32(iwl, ofs, val) writel((val), (iwl)->hw_base + (ofs))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_write32(const char *f, u32 l, struct iwl_priv *iwl,
				 u32 ofs, u32 val)
{
	IWL_DEBUG_IO("write_direct32(0x%08X, 0x%08X) - %s %d\n",
		     (u32) (ofs), (u32) (val), f, l);
	_iwl_write32(iwl, ofs, val);
}
#define iwl_write32(iwl, ofs, val) \
	__iwl_write32(__FILE__, __LINE__, iwl, ofs, val)
#else
#define iwl_write32(iwl, ofs, val) _iwl_write32(iwl, ofs, val)
#endif

#define _iwl_read32(iwl, ofs) readl((iwl)->hw_base + (ofs))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read32(char *f, u32 l, struct iwl_priv *iwl, u32 ofs)
{
	IWL_DEBUG_IO("read_direct32(0x%08X) - %s %d\n", ofs, f, l);
	return _iwl_read32(iwl, ofs);
}
#define iwl_read32(iwl, ofs) __iwl_read32(__FILE__, __LINE__, iwl, ofs)
#else
#define iwl_read32(p, o) _iwl_read32(p, o)
#endif

static inline int _iwl_poll_bit(struct iwl_priv *priv, u32 addr,
				u32 bits, u32 mask, int timeout)
{
	int i = 0;

	do {
		if ((_iwl_read32(priv, addr) & mask) == (bits & mask))
			return i;
		mdelay(10);
		i += 10;
	} while (i < timeout);

	return -ETIMEDOUT;
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_poll_bit(const char *f, u32 l,
				 struct iwl_priv *priv, u32 addr,
				 u32 bits, u32 mask, int timeout)
{
	int rc = _iwl_poll_bit(priv, addr, bits, mask, timeout);
	if (unlikely(rc == -ETIMEDOUT))
		IWL_DEBUG_IO
		    ("poll_bit(0x%08X, 0x%08X, 0x%08X) - timedout - %s %d\n",
		     addr, bits, mask, f, l);
	else
		IWL_DEBUG_IO
		    ("poll_bit(0x%08X, 0x%08X, 0x%08X) = 0x%08X - %s %d\n",
		     addr, bits, mask, rc, f, l);
	return rc;
}
#define iwl_poll_bit(iwl, addr, bits, mask, timeout) \
	__iwl_poll_bit(__FILE__, __LINE__, iwl, addr, bits, mask, timeout)
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
	IWL_DEBUG_IO("set_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
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
	IWL_DEBUG_IO("clear_bit(0x%08X, 0x%08X) = 0x%08X\n", reg, mask, val);
	_iwl_write32(priv, reg, val);
}
#define iwl_clear_bit(p, r, m) __iwl_clear_bit(__FILE__, __LINE__, p, r, m)
#else
#define iwl_clear_bit(p, r, m) _iwl_clear_bit(p, r, m)
#endif

static inline int _iwl_grab_restricted_access(struct iwl_priv *priv)
{
	int rc;
	u32 gp_ctl;

#ifdef CONFIG_IWLWIFI_DEBUG
	if (atomic_read(&priv->restrict_refcnt))
		return 0;
#endif
	if (test_bit(STATUS_RF_KILL_HW, &priv->status) ||
	    test_bit(STATUS_RF_KILL_SW, &priv->status)) {
		IWL_WARNING("WARNING: Requesting MAC access during RFKILL "
			"wakes up NIC\n");

		/* 10 msec allows time for NIC to complete its data save */
		gp_ctl = _iwl_read32(priv, CSR_GP_CNTRL);
		if (gp_ctl & CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY) {
			IWL_DEBUG_RF_KILL("Wait for complete power-down, "
				"gpctl = 0x%08x\n", gp_ctl);
			mdelay(10);
		} else
			IWL_DEBUG_RF_KILL("power-down complete, "
					  "gpctl = 0x%08x\n", gp_ctl);
	}

	/* this bit wakes up the NIC */
	_iwl_set_bit(priv, CSR_GP_CNTRL, CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
	rc = _iwl_poll_bit(priv, CSR_GP_CNTRL,
			   CSR_GP_CNTRL_REG_VAL_MAC_ACCESS_EN,
			   (CSR_GP_CNTRL_REG_FLAG_MAC_CLOCK_READY |
			    CSR_GP_CNTRL_REG_FLAG_GOING_TO_SLEEP), 50);
	if (rc < 0) {
		IWL_ERROR("MAC is in deep sleep!\n");
		return -EIO;
	}

#ifdef CONFIG_IWLWIFI_DEBUG
	atomic_inc(&priv->restrict_refcnt);
#endif
	return 0;
}

#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_grab_restricted_access(const char *f, u32 l,
					       struct iwl_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt))
		IWL_DEBUG_INFO("Grabbing access while already held at "
			       "line %d.\n", l);

	IWL_DEBUG_IO("grabbing restricted access - %s %d\n", f, l);

	return _iwl_grab_restricted_access(priv);
}
#define iwl_grab_restricted_access(priv) \
	__iwl_grab_restricted_access(__FILE__, __LINE__, priv)
#else
#define iwl_grab_restricted_access(priv) \
	_iwl_grab_restricted_access(priv)
#endif

static inline void _iwl_release_restricted_access(struct iwl_priv *priv)
{
#ifdef CONFIG_IWLWIFI_DEBUG
	if (atomic_dec_and_test(&priv->restrict_refcnt))
#endif
		_iwl_clear_bit(priv, CSR_GP_CNTRL,
			       CSR_GP_CNTRL_REG_FLAG_MAC_ACCESS_REQ);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_release_restricted_access(const char *f, u32 l,
						   struct iwl_priv *priv)
{
	if (atomic_read(&priv->restrict_refcnt) <= 0)
		IWL_ERROR("Release unheld restricted access at line %d.\n", l);

	IWL_DEBUG_IO("releasing restricted access - %s %d\n", f, l);
	_iwl_release_restricted_access(priv);
}
#define iwl_release_restricted_access(priv) \
	__iwl_release_restricted_access(__FILE__, __LINE__, priv)
#else
#define iwl_release_restricted_access(priv) \
	_iwl_release_restricted_access(priv)
#endif

static inline u32 _iwl_read_restricted(struct iwl_priv *priv, u32 reg)
{
	return _iwl_read32(priv, reg);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read_restricted(const char *f, u32 l,
					struct iwl_priv *priv, u32 reg)
{
	u32 value = _iwl_read_restricted(priv, reg);
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from %s %d\n", f, l);
	IWL_DEBUG_IO("read_restricted(0x%4X) = 0x%08x - %s %d \n", reg, value,
		     f, l);
	return value;
}
#define iwl_read_restricted(priv, reg) \
	__iwl_read_restricted(__FILE__, __LINE__, priv, reg)
#else
#define iwl_read_restricted _iwl_read_restricted
#endif

static inline void _iwl_write_restricted(struct iwl_priv *priv,
					 u32 reg, u32 value)
{
	_iwl_write32(priv, reg, value);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static void __iwl_write_restricted(u32 line,
				   struct iwl_priv *priv, u32 reg, u32 value)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from line %d\n", line);
	_iwl_write_restricted(priv, reg, value);
}
#define iwl_write_restricted(priv, reg, value) \
	__iwl_write_restricted(__LINE__, priv, reg, value)
#else
#define iwl_write_restricted _iwl_write_restricted
#endif

static inline void iwl_write_buffer_restricted(struct iwl_priv *priv,
					       u32 reg, u32 len, u32 *values)
{
	u32 count = sizeof(u32);

	if ((priv != NULL) && (values != NULL)) {
		for (; 0 < len; len -= count, reg += count, values++)
			_iwl_write_restricted(priv, reg, *values);
	}
}

static inline int _iwl_poll_restricted_bit(struct iwl_priv *priv,
					   u32 addr, u32 mask, int timeout)
{
	int i = 0;

	do {
		if ((_iwl_read_restricted(priv, addr) & mask) == mask)
			return i;
		mdelay(10);
		i += 10;
	} while (i < timeout);

	return -ETIMEDOUT;
}

#ifdef CONFIG_IWLWIFI_DEBUG
static inline int __iwl_poll_restricted_bit(const char *f, u32 l,
					    struct iwl_priv *priv,
					    u32 addr, u32 mask, int timeout)
{
	int rc = _iwl_poll_restricted_bit(priv, addr, mask, timeout);

	if (unlikely(rc == -ETIMEDOUT))
		IWL_DEBUG_IO("poll_restricted_bit(0x%08X, 0x%08X) - "
			     "timedout - %s %d\n", addr, mask, f, l);
	else
		IWL_DEBUG_IO("poll_restricted_bit(0x%08X, 0x%08X) = 0x%08X "
			     "- %s %d\n", addr, mask, rc, f, l);
	return rc;
}
#define iwl_poll_restricted_bit(iwl, addr, mask, timeout) \
	__iwl_poll_restricted_bit(__FILE__, __LINE__, iwl, addr, mask, timeout)
#else
#define iwl_poll_restricted_bit _iwl_poll_restricted_bit
#endif

static inline u32 _iwl_read_restricted_reg(struct iwl_priv *priv, u32 reg)
{
	_iwl_write_restricted(priv, HBUS_TARG_PRPH_RADDR, reg | (3 << 24));
	return _iwl_read_restricted(priv, HBUS_TARG_PRPH_RDAT);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline u32 __iwl_read_restricted_reg(u32 line,
					    struct iwl_priv *priv, u32 reg)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from line %d\n", line);
	return _iwl_read_restricted_reg(priv, reg);
}

#define iwl_read_restricted_reg(priv, reg) \
	__iwl_read_restricted_reg(__LINE__, priv, reg)
#else
#define iwl_read_restricted_reg _iwl_read_restricted_reg
#endif

static inline void _iwl_write_restricted_reg(struct iwl_priv *priv,
					     u32 addr, u32 val)
{
	_iwl_write_restricted(priv, HBUS_TARG_PRPH_WADDR,
			      ((addr & 0x0000FFFF) | (3 << 24)));
	_iwl_write_restricted(priv, HBUS_TARG_PRPH_WDAT, val);
}
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_write_restricted_reg(u32 line,
					      struct iwl_priv *priv,
					      u32 addr, u32 val)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from line %d\n", line);
	_iwl_write_restricted_reg(priv, addr, val);
}

#define iwl_write_restricted_reg(priv, addr, val) \
	__iwl_write_restricted_reg(__LINE__, priv, addr, val);
#else
#define iwl_write_restricted_reg _iwl_write_restricted_reg
#endif

#define _iwl_set_bits_restricted_reg(priv, reg, mask) \
	_iwl_write_restricted_reg(priv, reg, \
				  (_iwl_read_restricted_reg(priv, reg) | mask))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_set_bits_restricted_reg(u32 line, struct iwl_priv
						 *priv, u32 reg, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from line %d\n", line);
	_iwl_set_bits_restricted_reg(priv, reg, mask);
}
#define iwl_set_bits_restricted_reg(priv, reg, mask) \
	__iwl_set_bits_restricted_reg(__LINE__, priv, reg, mask)
#else
#define iwl_set_bits_restricted_reg _iwl_set_bits_restricted_reg
#endif

#define _iwl_set_bits_mask_restricted_reg(priv, reg, bits, mask) \
	_iwl_write_restricted_reg( \
	    priv, reg, ((_iwl_read_restricted_reg(priv, reg) & mask) | bits))
#ifdef CONFIG_IWLWIFI_DEBUG
static inline void __iwl_set_bits_mask_restricted_reg(u32 line,
		struct iwl_priv *priv, u32 reg, u32 bits, u32 mask)
{
	if (!atomic_read(&priv->restrict_refcnt))
		IWL_ERROR("Unrestricted access from line %d\n", line);
	_iwl_set_bits_mask_restricted_reg(priv, reg, bits, mask);
}

#define iwl_set_bits_mask_restricted_reg(priv, reg, bits, mask) \
	__iwl_set_bits_mask_restricted_reg(__LINE__, priv, reg, bits, mask)
#else
#define iwl_set_bits_mask_restricted_reg _iwl_set_bits_mask_restricted_reg
#endif

static inline void iwl_clear_bits_restricted_reg(struct iwl_priv
						 *priv, u32 reg, u32 mask)
{
	u32 val = _iwl_read_restricted_reg(priv, reg);
	_iwl_write_restricted_reg(priv, reg, (val & ~mask));
}

static inline u32 iwl_read_restricted_mem(struct iwl_priv *priv, u32 addr)
{
	iwl_write_restricted(priv, HBUS_TARG_MEM_RADDR, addr);
	return iwl_read_restricted(priv, HBUS_TARG_MEM_RDAT);
}

static inline void iwl_write_restricted_mem(struct iwl_priv *priv, u32 addr,
					    u32 val)
{
	iwl_write_restricted(priv, HBUS_TARG_MEM_WADDR, addr);
	iwl_write_restricted(priv, HBUS_TARG_MEM_WDAT, val);
}

static inline void iwl_write_restricted_mems(struct iwl_priv *priv, u32 addr,
					     u32 len, u32 *values)
{
	iwl_write_restricted(priv, HBUS_TARG_MEM_WADDR, addr);
	for (; 0 < len; len -= sizeof(u32), values++)
		iwl_write_restricted(priv, HBUS_TARG_MEM_WDAT, *values);
}

static inline void iwl_write_restricted_regs(struct iwl_priv *priv, u32 reg,
					     u32 len, u8 *values)
{
	u32 reg_offset = reg;
	u32 aligment = reg & 0x3;

	/* write any non-dword-aligned stuff at the beginning */
	if (len < sizeof(u32)) {
		if ((aligment + len) <= sizeof(u32)) {
			u8 size;
			u32 value = 0;
			size = len - 1;
			memcpy(&value, values, len);
			reg_offset = (reg_offset & 0x0000FFFF);

			_iwl_write_restricted(priv,
					      HBUS_TARG_PRPH_WADDR,
					      (reg_offset | (size << 24)));
			_iwl_write_restricted(priv, HBUS_TARG_PRPH_WDAT,
					      value);
		}

		return;
	}

	/* now write all the dword-aligned stuff */
	for (; reg_offset < (reg + len);
	     reg_offset += sizeof(u32), values += sizeof(u32))
		_iwl_write_restricted_reg(priv, reg_offset, *((u32 *) values));
}

#endif
