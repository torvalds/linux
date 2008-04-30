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
 * James P. Ketrenos <ipw2100-admin@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 *****************************************************************************/

#ifndef __iwl_helpers_h__
#define __iwl_helpers_h__

#include <linux/ctype.h>

/*
 * The structures defined by the hardware/uCode interface
 * have bit-wise operations.  For each bit-field there is
 * a data symbol in the structure, the start bit position
 * and the length of the bit-field.
 *
 * iwl_get_bits and iwl_set_bits will return or set the
 * appropriate bits on a 32-bit value.
 *
 * IWL_GET_BITS and IWL_SET_BITS use symbol expansion to
 * expand out to the appropriate call to iwl_get_bits
 * and iwl_set_bits without having to reference all of the
 * numerical constants and defines provided in the hardware
 * definition
 */

/**
 * iwl_get_bits - Extract a hardware bit-field value
 * @src: source hardware value (__le32)
 * @pos: bit-position (0-based) of first bit of value
 * @len: length of bit-field
 *
 * iwl_get_bits will return the bit-field in cpu endian ordering.
 *
 * NOTE:  If used from IWL_GET_BITS then pos and len are compile-constants and
 *        will collapse to minimal code by the compiler.
 */
static inline u32 iwl_get_bits(__le32 src, u8 pos, u8 len)
{
	u32 tmp = le32_to_cpu(src);

	tmp >>= pos;
	tmp &= (1UL << len) - 1;
	return tmp;
}

/**
 * iwl_set_bits - Set a hardware bit-field value
 * @dst: Address of __le32 hardware value
 * @pos: bit-position (0-based) of first bit of value
 * @len: length of bit-field
 * @val: cpu endian value to encode into the bit-field
 *
 * iwl_set_bits will encode val into dst, masked to be len bits long at bit
 * position pos.
 *
 * NOTE:  If used IWL_SET_BITS pos and len will be compile-constants and
 *        will collapse to minimal code by the compiler.
 */
static inline void iwl_set_bits(__le32 *dst, u8 pos, u8 len, int val)
{
	u32 tmp = le32_to_cpu(*dst);

	tmp &= ~(((1UL << len) - 1) << pos);
	tmp |= (val & ((1UL << len) - 1)) << pos;
	*dst = cpu_to_le32(tmp);
}

static inline void iwl_set_bits16(__le16 *dst, u8 pos, u8 len, int val)
{
	u16 tmp = le16_to_cpu(*dst);

	tmp &= ~((1UL << (pos + len)) - (1UL << pos));
	tmp |= (val & ((1UL << len) - 1)) << pos;
	*dst = cpu_to_le16(tmp);
}

/*
 * The bit-field definitions in iwl-xxxx-hw.h are in the form of:
 *
 * struct example {
 *         __le32 val1;
 * #define IWL_name_POS 8
 * #define IWL_name_LEN 4
 * #define IWL_name_SYM val1
 * };
 *
 * The IWL_SET_BITS and IWL_GET_BITS macros are provided to allow the driver
 * to call:
 *
 * struct example bar;
 * u32 val = IWL_GET_BITS(bar, name);
 * val = val * 2;
 * IWL_SET_BITS(bar, name, val);
 *
 * All cpu / host ordering, masking, and shifts are performed by the macros
 * and iwl_{get,set}_bits.
 *
 */
#define IWL_SET_BITS(s, sym, v) \
	iwl_set_bits(&(s).IWL_ ## sym ## _SYM, IWL_ ## sym ## _POS, \
		     IWL_ ## sym ## _LEN, (v))

#define IWL_SET_BITS16(s, sym, v) \
	iwl_set_bits16(&(s).IWL_ ## sym ## _SYM, IWL_ ## sym ## _POS, \
		       IWL_ ## sym ## _LEN, (v))

#define IWL_GET_BITS(s, sym) \
	iwl_get_bits((s).IWL_ ## sym ## _SYM, IWL_ ## sym ## _POS, \
		      IWL_ ## sym ## _LEN)


#define KELVIN_TO_CELSIUS(x) ((x)-273)
#define CELSIUS_TO_KELVIN(x) ((x)+273)

#define IEEE80211_CHAN_W_RADAR_DETECT 0x00000010

static inline struct ieee80211_conf *ieee80211_get_hw_conf(
	struct ieee80211_hw *hw)
{
	return &hw->conf;
}

#define QOS_CONTROL_LEN 2


static inline int ieee80211_is_management(u16 fc)
{
	return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT;
}

static inline int ieee80211_is_control(u16 fc)
{
	return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL;
}

static inline int ieee80211_is_data(u16 fc)
{
	return (fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_DATA;
}

static inline int ieee80211_is_back_request(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BACK_REQ);
}

static inline int ieee80211_is_probe_response(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_RESP);
}

static inline int ieee80211_is_probe_request(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_REQ);
}

static inline int ieee80211_is_beacon(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_BEACON);
}

static inline int ieee80211_is_atim(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ATIM);
}

static inline int ieee80211_is_assoc_request(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ);
}

static inline int ieee80211_is_assoc_response(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_RESP);
}

static inline int ieee80211_is_auth(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ);
}

static inline int ieee80211_is_deauth(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ);
}

static inline int ieee80211_is_disassoc(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_ASSOC_REQ);
}

static inline int ieee80211_is_reassoc_request(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_REASSOC_REQ);
}

static inline int ieee80211_is_reassoc_response(u16 fc)
{
	return ((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
	       ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_REASSOC_RESP);
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

static inline u8 iwl_get_dma_hi_address(dma_addr_t addr)
{
	return sizeof(addr) > sizeof(u32) ? (addr >> 16) >> 16 : 0;
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
