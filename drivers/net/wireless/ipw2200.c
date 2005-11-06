/******************************************************************************

  Copyright(c) 2003 - 2004 Intel Corporation. All rights reserved.

  802.11 status code portion of this file from ethereal-0.10.6:
    Copyright 2000, Axis Communications AB
    Ethereal - Network traffic analyzer
    By Gerald Combs <gerald@ethereal.com>
    Copyright 1998 Gerald Combs

  This program is free software; you can redistribute it and/or modify it
  under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc., 59
  Temple Place - Suite 330, Boston, MA  02111-1307, USA.

  The full GNU General Public License is included in this distribution in the
  file called LICENSE.

  Contact Information:
  James P. Ketrenos <ipw2100-admin@linux.intel.com>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

******************************************************************************/

#include "ipw2200.h"

#define IPW2200_VERSION "1.0.0"
#define DRV_DESCRIPTION	"Intel(R) PRO/Wireless 2200/2915 Network Driver"
#define DRV_COPYRIGHT	"Copyright(c) 2003-2004 Intel Corporation"
#define DRV_VERSION     IPW2200_VERSION

MODULE_DESCRIPTION(DRV_DESCRIPTION);
MODULE_VERSION(DRV_VERSION);
MODULE_AUTHOR(DRV_COPYRIGHT);
MODULE_LICENSE("GPL");

static int debug = 0;
static int channel = 0;
static char *ifname;
static int mode = 0;

static u32 ipw_debug_level;
static int associate = 1;
static int auto_create = 1;
static int disable = 0;
static const char ipw_modes[] = {
	'a', 'b', 'g', '?'
};

static void ipw_rx(struct ipw_priv *priv);
static int ipw_queue_tx_reclaim(struct ipw_priv *priv,
				struct clx2_tx_queue *txq, int qindex);
static int ipw_queue_reset(struct ipw_priv *priv);

static int ipw_queue_tx_hcmd(struct ipw_priv *priv, int hcmd, void *buf,
			     int len, int sync);

static void ipw_tx_queue_free(struct ipw_priv *);

static struct ipw_rx_queue *ipw_rx_queue_alloc(struct ipw_priv *);
static void ipw_rx_queue_free(struct ipw_priv *, struct ipw_rx_queue *);
static void ipw_rx_queue_replenish(void *);

static int ipw_up(struct ipw_priv *);
static void ipw_down(struct ipw_priv *);
static int ipw_config(struct ipw_priv *);
static int init_supported_rates(struct ipw_priv *priv,
				struct ipw_supported_rates *prates);

static u8 band_b_active_channel[MAX_B_CHANNELS] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0
};
static u8 band_a_active_channel[MAX_A_CHANNELS] = {
	36, 40, 44, 48, 149, 153, 157, 161, 165, 52, 56, 60, 64, 0
};

static int is_valid_channel(int mode_mask, int channel)
{
	int i;

	if (!channel)
		return 0;

	if (mode_mask & IEEE_A)
		for (i = 0; i < MAX_A_CHANNELS; i++)
			if (band_a_active_channel[i] == channel)
				return IEEE_A;

	if (mode_mask & (IEEE_B | IEEE_G))
		for (i = 0; i < MAX_B_CHANNELS; i++)
			if (band_b_active_channel[i] == channel)
				return mode_mask & (IEEE_B | IEEE_G);

	return 0;
}

static char *snprint_line(char *buf, size_t count,
			  const u8 * data, u32 len, u32 ofs)
{
	int out, i, j, l;
	char c;

	out = snprintf(buf, count, "%08X", ofs);

	for (l = 0, i = 0; i < 2; i++) {
		out += snprintf(buf + out, count - out, " ");
		for (j = 0; j < 8 && l < len; j++, l++)
			out += snprintf(buf + out, count - out, "%02X ",
					data[(i * 8 + j)]);
		for (; j < 8; j++)
			out += snprintf(buf + out, count - out, "   ");
	}

	out += snprintf(buf + out, count - out, " ");
	for (l = 0, i = 0; i < 2; i++) {
		out += snprintf(buf + out, count - out, " ");
		for (j = 0; j < 8 && l < len; j++, l++) {
			c = data[(i * 8 + j)];
			if (!isascii(c) || !isprint(c))
				c = '.';

			out += snprintf(buf + out, count - out, "%c", c);
		}

		for (; j < 8; j++)
			out += snprintf(buf + out, count - out, " ");
	}

	return buf;
}

static void printk_buf(int level, const u8 * data, u32 len)
{
	char line[81];
	u32 ofs = 0;
	if (!(ipw_debug_level & level))
		return;

	while (len) {
		printk(KERN_DEBUG "%s\n",
		       snprint_line(line, sizeof(line), &data[ofs],
				    min(len, 16U), ofs));
		ofs += 16;
		len -= min(len, 16U);
	}
}

static u32 _ipw_read_reg32(struct ipw_priv *priv, u32 reg);
#define ipw_read_reg32(a, b) _ipw_read_reg32(a, b)

static u8 _ipw_read_reg8(struct ipw_priv *ipw, u32 reg);
#define ipw_read_reg8(a, b) _ipw_read_reg8(a, b)

static void _ipw_write_reg8(struct ipw_priv *priv, u32 reg, u8 value);
static inline void ipw_write_reg8(struct ipw_priv *a, u32 b, u8 c)
{
	IPW_DEBUG_IO("%s %d: write_indirect8(0x%08X, 0x%08X)\n", __FILE__,
		     __LINE__, (u32) (b), (u32) (c));
	_ipw_write_reg8(a, b, c);
}

static void _ipw_write_reg16(struct ipw_priv *priv, u32 reg, u16 value);
static inline void ipw_write_reg16(struct ipw_priv *a, u32 b, u16 c)
{
	IPW_DEBUG_IO("%s %d: write_indirect16(0x%08X, 0x%08X)\n", __FILE__,
		     __LINE__, (u32) (b), (u32) (c));
	_ipw_write_reg16(a, b, c);
}

static void _ipw_write_reg32(struct ipw_priv *priv, u32 reg, u32 value);
static inline void ipw_write_reg32(struct ipw_priv *a, u32 b, u32 c)
{
	IPW_DEBUG_IO("%s %d: write_indirect32(0x%08X, 0x%08X)\n", __FILE__,
		     __LINE__, (u32) (b), (u32) (c));
	_ipw_write_reg32(a, b, c);
}

#define _ipw_write8(ipw, ofs, val) writeb((val), (ipw)->hw_base + (ofs))
#define ipw_write8(ipw, ofs, val) \
 IPW_DEBUG_IO("%s %d: write_direct8(0x%08X, 0x%08X)\n", __FILE__, __LINE__, (u32)(ofs), (u32)(val)); \
 _ipw_write8(ipw, ofs, val)

#define _ipw_write16(ipw, ofs, val) writew((val), (ipw)->hw_base + (ofs))
#define ipw_write16(ipw, ofs, val) \
 IPW_DEBUG_IO("%s %d: write_direct16(0x%08X, 0x%08X)\n", __FILE__, __LINE__, (u32)(ofs), (u32)(val)); \
 _ipw_write16(ipw, ofs, val)

#define _ipw_write32(ipw, ofs, val) writel((val), (ipw)->hw_base + (ofs))
#define ipw_write32(ipw, ofs, val) \
 IPW_DEBUG_IO("%s %d: write_direct32(0x%08X, 0x%08X)\n", __FILE__, __LINE__, (u32)(ofs), (u32)(val)); \
 _ipw_write32(ipw, ofs, val)

#define _ipw_read8(ipw, ofs) readb((ipw)->hw_base + (ofs))
static inline u8 __ipw_read8(char *f, u32 l, struct ipw_priv *ipw, u32 ofs)
{
	IPW_DEBUG_IO("%s %d: read_direct8(0x%08X)\n", f, l, (u32) (ofs));
	return _ipw_read8(ipw, ofs);
}

#define ipw_read8(ipw, ofs) __ipw_read8(__FILE__, __LINE__, ipw, ofs)

#define _ipw_read16(ipw, ofs) readw((ipw)->hw_base + (ofs))
static inline u16 __ipw_read16(char *f, u32 l, struct ipw_priv *ipw, u32 ofs)
{
	IPW_DEBUG_IO("%s %d: read_direct16(0x%08X)\n", f, l, (u32) (ofs));
	return _ipw_read16(ipw, ofs);
}

#define ipw_read16(ipw, ofs) __ipw_read16(__FILE__, __LINE__, ipw, ofs)

#define _ipw_read32(ipw, ofs) readl((ipw)->hw_base + (ofs))
static inline u32 __ipw_read32(char *f, u32 l, struct ipw_priv *ipw, u32 ofs)
{
	IPW_DEBUG_IO("%s %d: read_direct32(0x%08X)\n", f, l, (u32) (ofs));
	return _ipw_read32(ipw, ofs);
}

#define ipw_read32(ipw, ofs) __ipw_read32(__FILE__, __LINE__, ipw, ofs)

static void _ipw_read_indirect(struct ipw_priv *, u32, u8 *, int);
#define ipw_read_indirect(a, b, c, d) \
	IPW_DEBUG_IO("%s %d: read_inddirect(0x%08X) %d bytes\n", __FILE__, __LINE__, (u32)(b), d); \
	_ipw_read_indirect(a, b, c, d)

static void _ipw_write_indirect(struct ipw_priv *priv, u32 addr, u8 * data,
				int num);
#define ipw_write_indirect(a, b, c, d) \
	IPW_DEBUG_IO("%s %d: write_indirect(0x%08X) %d bytes\n", __FILE__, __LINE__, (u32)(b), d); \
        _ipw_write_indirect(a, b, c, d)

/* indirect write s */
static void _ipw_write_reg32(struct ipw_priv *priv, u32 reg, u32 value)
{
	IPW_DEBUG_IO(" %p : reg = 0x%8X : value = 0x%8X\n", priv, reg, value);
	_ipw_write32(priv, CX2_INDIRECT_ADDR, reg);
	_ipw_write32(priv, CX2_INDIRECT_DATA, value);
}

static void _ipw_write_reg8(struct ipw_priv *priv, u32 reg, u8 value)
{
	IPW_DEBUG_IO(" reg = 0x%8X : value = 0x%8X\n", reg, value);
	_ipw_write32(priv, CX2_INDIRECT_ADDR, reg & CX2_INDIRECT_ADDR_MASK);
	_ipw_write8(priv, CX2_INDIRECT_DATA, value);
	IPW_DEBUG_IO(" reg = 0x%8lX : value = 0x%8X\n",
		     (unsigned long)(priv->hw_base + CX2_INDIRECT_DATA), value);
}

static void _ipw_write_reg16(struct ipw_priv *priv, u32 reg, u16 value)
{
	IPW_DEBUG_IO(" reg = 0x%8X : value = 0x%8X\n", reg, value);
	_ipw_write32(priv, CX2_INDIRECT_ADDR, reg & CX2_INDIRECT_ADDR_MASK);
	_ipw_write16(priv, CX2_INDIRECT_DATA, value);
}

/* indirect read s */

static u8 _ipw_read_reg8(struct ipw_priv *priv, u32 reg)
{
	u32 word;
	_ipw_write32(priv, CX2_INDIRECT_ADDR, reg & CX2_INDIRECT_ADDR_MASK);
	IPW_DEBUG_IO(" reg = 0x%8X : \n", reg);
	word = _ipw_read32(priv, CX2_INDIRECT_DATA);
	return (word >> ((reg & 0x3) * 8)) & 0xff;
}

static u32 _ipw_read_reg32(struct ipw_priv *priv, u32 reg)
{
	u32 value;

	IPW_DEBUG_IO("%p : reg = 0x%08x\n", priv, reg);

	_ipw_write32(priv, CX2_INDIRECT_ADDR, reg);
	value = _ipw_read32(priv, CX2_INDIRECT_DATA);
	IPW_DEBUG_IO(" reg = 0x%4X : value = 0x%4x \n", reg, value);
	return value;
}

/* iterative/auto-increment 32 bit reads and writes */
static void _ipw_read_indirect(struct ipw_priv *priv, u32 addr, u8 * buf,
			       int num)
{
	u32 aligned_addr = addr & CX2_INDIRECT_ADDR_MASK;
	u32 dif_len = addr - aligned_addr;
	u32 aligned_len;
	u32 i;

	IPW_DEBUG_IO("addr = %i, buf = %p, num = %i\n", addr, buf, num);

	/* Read the first nibble byte by byte */
	if (unlikely(dif_len)) {
		/* Start reading at aligned_addr + dif_len */
		_ipw_write32(priv, CX2_INDIRECT_ADDR, aligned_addr);
		for (i = dif_len; i < 4; i++, buf++)
			*buf = _ipw_read8(priv, CX2_INDIRECT_DATA + i);
		num -= dif_len;
		aligned_addr += 4;
	}

	/* Read DWs through autoinc register */
	_ipw_write32(priv, CX2_AUTOINC_ADDR, aligned_addr);
	aligned_len = num & CX2_INDIRECT_ADDR_MASK;
	for (i = 0; i < aligned_len; i += 4, buf += 4, aligned_addr += 4)
		*(u32 *) buf = ipw_read32(priv, CX2_AUTOINC_DATA);

	/* Copy the last nibble */
	dif_len = num - aligned_len;
	_ipw_write32(priv, CX2_INDIRECT_ADDR, aligned_addr);
	for (i = 0; i < dif_len; i++, buf++)
		*buf = ipw_read8(priv, CX2_INDIRECT_DATA + i);
}

static void _ipw_write_indirect(struct ipw_priv *priv, u32 addr, u8 * buf,
				int num)
{
	u32 aligned_addr = addr & CX2_INDIRECT_ADDR_MASK;
	u32 dif_len = addr - aligned_addr;
	u32 aligned_len;
	u32 i;

	IPW_DEBUG_IO("addr = %i, buf = %p, num = %i\n", addr, buf, num);

	/* Write the first nibble byte by byte */
	if (unlikely(dif_len)) {
		/* Start writing at aligned_addr + dif_len */
		_ipw_write32(priv, CX2_INDIRECT_ADDR, aligned_addr);
		for (i = dif_len; i < 4; i++, buf++)
			_ipw_write8(priv, CX2_INDIRECT_DATA + i, *buf);
		num -= dif_len;
		aligned_addr += 4;
	}

	/* Write DWs through autoinc register */
	_ipw_write32(priv, CX2_AUTOINC_ADDR, aligned_addr);
	aligned_len = num & CX2_INDIRECT_ADDR_MASK;
	for (i = 0; i < aligned_len; i += 4, buf += 4, aligned_addr += 4)
		_ipw_write32(priv, CX2_AUTOINC_DATA, *(u32 *) buf);

	/* Copy the last nibble */
	dif_len = num - aligned_len;
	_ipw_write32(priv, CX2_INDIRECT_ADDR, aligned_addr);
	for (i = 0; i < dif_len; i++, buf++)
		_ipw_write8(priv, CX2_INDIRECT_DATA + i, *buf);
}

static void ipw_write_direct(struct ipw_priv *priv, u32 addr, void *buf,
			     int num)
{
	memcpy_toio((priv->hw_base + addr), buf, num);
}

static inline void ipw_set_bit(struct ipw_priv *priv, u32 reg, u32 mask)
{
	ipw_write32(priv, reg, ipw_read32(priv, reg) | mask);
}

static inline void ipw_clear_bit(struct ipw_priv *priv, u32 reg, u32 mask)
{
	ipw_write32(priv, reg, ipw_read32(priv, reg) & ~mask);
}

static inline void ipw_enable_interrupts(struct ipw_priv *priv)
{
	if (priv->status & STATUS_INT_ENABLED)
		return;
	priv->status |= STATUS_INT_ENABLED;
	ipw_write32(priv, CX2_INTA_MASK_R, CX2_INTA_MASK_ALL);
}

static inline void ipw_disable_interrupts(struct ipw_priv *priv)
{
	if (!(priv->status & STATUS_INT_ENABLED))
		return;
	priv->status &= ~STATUS_INT_ENABLED;
	ipw_write32(priv, CX2_INTA_MASK_R, ~CX2_INTA_MASK_ALL);
}

static char *ipw_error_desc(u32 val)
{
	switch (val) {
	case IPW_FW_ERROR_OK:
		return "ERROR_OK";
	case IPW_FW_ERROR_FAIL:
		return "ERROR_FAIL";
	case IPW_FW_ERROR_MEMORY_UNDERFLOW:
		return "MEMORY_UNDERFLOW";
	case IPW_FW_ERROR_MEMORY_OVERFLOW:
		return "MEMORY_OVERFLOW";
	case IPW_FW_ERROR_BAD_PARAM:
		return "ERROR_BAD_PARAM";
	case IPW_FW_ERROR_BAD_CHECKSUM:
		return "ERROR_BAD_CHECKSUM";
	case IPW_FW_ERROR_NMI_INTERRUPT:
		return "ERROR_NMI_INTERRUPT";
	case IPW_FW_ERROR_BAD_DATABASE:
		return "ERROR_BAD_DATABASE";
	case IPW_FW_ERROR_ALLOC_FAIL:
		return "ERROR_ALLOC_FAIL";
	case IPW_FW_ERROR_DMA_UNDERRUN:
		return "ERROR_DMA_UNDERRUN";
	case IPW_FW_ERROR_DMA_STATUS:
		return "ERROR_DMA_STATUS";
	case IPW_FW_ERROR_DINOSTATUS_ERROR:
		return "ERROR_DINOSTATUS_ERROR";
	case IPW_FW_ERROR_EEPROMSTATUS_ERROR:
		return "ERROR_EEPROMSTATUS_ERROR";
	case IPW_FW_ERROR_SYSASSERT:
		return "ERROR_SYSASSERT";
	case IPW_FW_ERROR_FATAL_ERROR:
		return "ERROR_FATALSTATUS_ERROR";
	default:
		return "UNKNOWNSTATUS_ERROR";
	}
}

static void ipw_dump_nic_error_log(struct ipw_priv *priv)
{
	u32 desc, time, blink1, blink2, ilink1, ilink2, idata, i, count, base;

	base = ipw_read32(priv, IPWSTATUS_ERROR_LOG);
	count = ipw_read_reg32(priv, base);

	if (ERROR_START_OFFSET <= count * ERROR_ELEM_SIZE) {
		IPW_ERROR("Start IPW Error Log Dump:\n");
		IPW_ERROR("Status: 0x%08X, Config: %08X\n",
			  priv->status, priv->config);
	}

	for (i = ERROR_START_OFFSET;
	     i <= count * ERROR_ELEM_SIZE; i += ERROR_ELEM_SIZE) {
		desc = ipw_read_reg32(priv, base + i);
		time = ipw_read_reg32(priv, base + i + 1 * sizeof(u32));
		blink1 = ipw_read_reg32(priv, base + i + 2 * sizeof(u32));
		blink2 = ipw_read_reg32(priv, base + i + 3 * sizeof(u32));
		ilink1 = ipw_read_reg32(priv, base + i + 4 * sizeof(u32));
		ilink2 = ipw_read_reg32(priv, base + i + 5 * sizeof(u32));
		idata = ipw_read_reg32(priv, base + i + 6 * sizeof(u32));

		IPW_ERROR("%s %i 0x%08x  0x%08x  0x%08x  0x%08x  0x%08x\n",
			  ipw_error_desc(desc), time, blink1, blink2,
			  ilink1, ilink2, idata);
	}
}

static void ipw_dump_nic_event_log(struct ipw_priv *priv)
{
	u32 ev, time, data, i, count, base;

	base = ipw_read32(priv, IPW_EVENT_LOG);
	count = ipw_read_reg32(priv, base);

	if (EVENT_START_OFFSET <= count * EVENT_ELEM_SIZE)
		IPW_ERROR("Start IPW Event Log Dump:\n");

	for (i = EVENT_START_OFFSET;
	     i <= count * EVENT_ELEM_SIZE; i += EVENT_ELEM_SIZE) {
		ev = ipw_read_reg32(priv, base + i);
		time = ipw_read_reg32(priv, base + i + 1 * sizeof(u32));
		data = ipw_read_reg32(priv, base + i + 2 * sizeof(u32));

#ifdef CONFIG_IPW_DEBUG
		IPW_ERROR("%i\t0x%08x\t%i\n", time, data, ev);
#endif
	}
}

static int ipw_get_ordinal(struct ipw_priv *priv, u32 ord, void *val, u32 * len)
{
	u32 addr, field_info, field_len, field_count, total_len;

	IPW_DEBUG_ORD("ordinal = %i\n", ord);

	if (!priv || !val || !len) {
		IPW_DEBUG_ORD("Invalid argument\n");
		return -EINVAL;
	}

	/* verify device ordinal tables have been initialized */
	if (!priv->table0_addr || !priv->table1_addr || !priv->table2_addr) {
		IPW_DEBUG_ORD("Access ordinals before initialization\n");
		return -EINVAL;
	}

	switch (IPW_ORD_TABLE_ID_MASK & ord) {
	case IPW_ORD_TABLE_0_MASK:
		/*
		 * TABLE 0: Direct access to a table of 32 bit values
		 *
		 * This is a very simple table with the data directly
		 * read from the table
		 */

		/* remove the table id from the ordinal */
		ord &= IPW_ORD_TABLE_VALUE_MASK;

		/* boundary check */
		if (ord > priv->table0_len) {
			IPW_DEBUG_ORD("ordinal value (%i) longer then "
				      "max (%i)\n", ord, priv->table0_len);
			return -EINVAL;
		}

		/* verify we have enough room to store the value */
		if (*len < sizeof(u32)) {
			IPW_DEBUG_ORD("ordinal buffer length too small, "
				      "need %zd\n", sizeof(u32));
			return -EINVAL;
		}

		IPW_DEBUG_ORD("Reading TABLE0[%i] from offset 0x%08x\n",
			      ord, priv->table0_addr + (ord << 2));

		*len = sizeof(u32);
		ord <<= 2;
		*((u32 *) val) = ipw_read32(priv, priv->table0_addr + ord);
		break;

	case IPW_ORD_TABLE_1_MASK:
		/*
		 * TABLE 1: Indirect access to a table of 32 bit values
		 *
		 * This is a fairly large table of u32 values each
		 * representing starting addr for the data (which is
		 * also a u32)
		 */

		/* remove the table id from the ordinal */
		ord &= IPW_ORD_TABLE_VALUE_MASK;

		/* boundary check */
		if (ord > priv->table1_len) {
			IPW_DEBUG_ORD("ordinal value too long\n");
			return -EINVAL;
		}

		/* verify we have enough room to store the value */
		if (*len < sizeof(u32)) {
			IPW_DEBUG_ORD("ordinal buffer length too small, "
				      "need %zd\n", sizeof(u32));
			return -EINVAL;
		}

		*((u32 *) val) =
		    ipw_read_reg32(priv, (priv->table1_addr + (ord << 2)));
		*len = sizeof(u32);
		break;

	case IPW_ORD_TABLE_2_MASK:
		/*
		 * TABLE 2: Indirect access to a table of variable sized values
		 *
		 * This table consist of six values, each containing
		 *     - dword containing the starting offset of the data
		 *     - dword containing the lengh in the first 16bits
		 *       and the count in the second 16bits
		 */

		/* remove the table id from the ordinal */
		ord &= IPW_ORD_TABLE_VALUE_MASK;

		/* boundary check */
		if (ord > priv->table2_len) {
			IPW_DEBUG_ORD("ordinal value too long\n");
			return -EINVAL;
		}

		/* get the address of statistic */
		addr = ipw_read_reg32(priv, priv->table2_addr + (ord << 3));

		/* get the second DW of statistics ;
		 * two 16-bit words - first is length, second is count */
		field_info =
		    ipw_read_reg32(priv,
				   priv->table2_addr + (ord << 3) +
				   sizeof(u32));

		/* get each entry length */
		field_len = *((u16 *) & field_info);

		/* get number of entries */
		field_count = *(((u16 *) & field_info) + 1);

		/* abort if not enought memory */
		total_len = field_len * field_count;
		if (total_len > *len) {
			*len = total_len;
			return -EINVAL;
		}

		*len = total_len;
		if (!total_len)
			return 0;

		IPW_DEBUG_ORD("addr = 0x%08x, total_len = %i, "
			      "field_info = 0x%08x\n",
			      addr, total_len, field_info);
		ipw_read_indirect(priv, addr, val, total_len);
		break;

	default:
		IPW_DEBUG_ORD("Invalid ordinal!\n");
		return -EINVAL;

	}

	return 0;
}

static void ipw_init_ordinals(struct ipw_priv *priv)
{
	priv->table0_addr = IPW_ORDINALS_TABLE_LOWER;
	priv->table0_len = ipw_read32(priv, priv->table0_addr);

	IPW_DEBUG_ORD("table 0 offset at 0x%08x, len = %i\n",
		      priv->table0_addr, priv->table0_len);

	priv->table1_addr = ipw_read32(priv, IPW_ORDINALS_TABLE_1);
	priv->table1_len = ipw_read_reg32(priv, priv->table1_addr);

	IPW_DEBUG_ORD("table 1 offset at 0x%08x, len = %i\n",
		      priv->table1_addr, priv->table1_len);

	priv->table2_addr = ipw_read32(priv, IPW_ORDINALS_TABLE_2);
	priv->table2_len = ipw_read_reg32(priv, priv->table2_addr);
	priv->table2_len &= 0x0000ffff;	/* use first two bytes */

	IPW_DEBUG_ORD("table 2 offset at 0x%08x, len = %i\n",
		      priv->table2_addr, priv->table2_len);

}

/*
 * The following adds a new attribute to the sysfs representation
 * of this device driver (i.e. a new file in /sys/bus/pci/drivers/ipw/)
 * used for controling the debug level.
 *
 * See the level definitions in ipw for details.
 */
static ssize_t show_debug_level(struct device_driver *d, char *buf)
{
	return sprintf(buf, "0x%08X\n", ipw_debug_level);
}
static ssize_t store_debug_level(struct device_driver *d,
				 const char *buf, size_t count)
{
	char *p = (char *)buf;
	u32 val;

	if (p[1] == 'x' || p[1] == 'X' || p[0] == 'x' || p[0] == 'X') {
		p++;
		if (p[0] == 'x' || p[0] == 'X')
			p++;
		val = simple_strtoul(p, &p, 16);
	} else
		val = simple_strtoul(p, &p, 10);
	if (p == buf)
		printk(KERN_INFO DRV_NAME
		       ": %s is not in hex or decimal form.\n", buf);
	else
		ipw_debug_level = val;

	return strnlen(buf, count);
}

static DRIVER_ATTR(debug_level, S_IWUSR | S_IRUGO,
		   show_debug_level, store_debug_level);

static ssize_t show_status(struct device *d,
			   struct device_attribute *attr, char *buf)
{
	struct ipw_priv *p = d->driver_data;
	return sprintf(buf, "0x%08x\n", (int)p->status);
}

static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);

static ssize_t show_cfg(struct device *d, struct device_attribute *attr,
			char *buf)
{
	struct ipw_priv *p = d->driver_data;
	return sprintf(buf, "0x%08x\n", (int)p->config);
}

static DEVICE_ATTR(cfg, S_IRUGO, show_cfg, NULL);

static ssize_t show_nic_type(struct device *d,
			     struct device_attribute *attr, char *buf)
{
	struct ipw_priv *p = d->driver_data;
	u8 type = p->eeprom[EEPROM_NIC_TYPE];

	switch (type) {
	case EEPROM_NIC_TYPE_STANDARD:
		return sprintf(buf, "STANDARD\n");
	case EEPROM_NIC_TYPE_DELL:
		return sprintf(buf, "DELL\n");
	case EEPROM_NIC_TYPE_FUJITSU:
		return sprintf(buf, "FUJITSU\n");
	case EEPROM_NIC_TYPE_IBM:
		return sprintf(buf, "IBM\n");
	case EEPROM_NIC_TYPE_HP:
		return sprintf(buf, "HP\n");
	}

	return sprintf(buf, "UNKNOWN\n");
}

static DEVICE_ATTR(nic_type, S_IRUGO, show_nic_type, NULL);

static ssize_t dump_error_log(struct device *d,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	char *p = (char *)buf;

	if (p[0] == '1')
		ipw_dump_nic_error_log((struct ipw_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_errors, S_IWUSR, NULL, dump_error_log);

static ssize_t dump_event_log(struct device *d,
			      struct device_attribute *attr, const char *buf,
			      size_t count)
{
	char *p = (char *)buf;

	if (p[0] == '1')
		ipw_dump_nic_event_log((struct ipw_priv *)d->driver_data);

	return strnlen(buf, count);
}

static DEVICE_ATTR(dump_events, S_IWUSR, NULL, dump_event_log);

static ssize_t show_ucode_version(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	u32 len = sizeof(u32), tmp = 0;
	struct ipw_priv *p = d->driver_data;

	if (ipw_get_ordinal(p, IPW_ORD_STAT_UCODE_VERSION, &tmp, &len))
		return 0;

	return sprintf(buf, "0x%08x\n", tmp);
}

static DEVICE_ATTR(ucode_version, S_IWUSR | S_IRUGO, show_ucode_version, NULL);

static ssize_t show_rtc(struct device *d, struct device_attribute *attr,
			char *buf)
{
	u32 len = sizeof(u32), tmp = 0;
	struct ipw_priv *p = d->driver_data;

	if (ipw_get_ordinal(p, IPW_ORD_STAT_RTC, &tmp, &len))
		return 0;

	return sprintf(buf, "0x%08x\n", tmp);
}

static DEVICE_ATTR(rtc, S_IWUSR | S_IRUGO, show_rtc, NULL);

/*
 * Add a device attribute to view/control the delay between eeprom
 * operations.
 */
static ssize_t show_eeprom_delay(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	int n = ((struct ipw_priv *)d->driver_data)->eeprom_delay;
	return sprintf(buf, "%i\n", n);
}
static ssize_t store_eeprom_delay(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ipw_priv *p = d->driver_data;
	sscanf(buf, "%i", &p->eeprom_delay);
	return strnlen(buf, count);
}

static DEVICE_ATTR(eeprom_delay, S_IWUSR | S_IRUGO,
		   show_eeprom_delay, store_eeprom_delay);

static ssize_t show_command_event_reg(struct device *d,
				      struct device_attribute *attr, char *buf)
{
	u32 reg = 0;
	struct ipw_priv *p = d->driver_data;

	reg = ipw_read_reg32(p, CX2_INTERNAL_CMD_EVENT);
	return sprintf(buf, "0x%08x\n", reg);
}
static ssize_t store_command_event_reg(struct device *d,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	u32 reg;
	struct ipw_priv *p = d->driver_data;

	sscanf(buf, "%x", &reg);
	ipw_write_reg32(p, CX2_INTERNAL_CMD_EVENT, reg);
	return strnlen(buf, count);
}

static DEVICE_ATTR(command_event_reg, S_IWUSR | S_IRUGO,
		   show_command_event_reg, store_command_event_reg);

static ssize_t show_mem_gpio_reg(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	u32 reg = 0;
	struct ipw_priv *p = d->driver_data;

	reg = ipw_read_reg32(p, 0x301100);
	return sprintf(buf, "0x%08x\n", reg);
}
static ssize_t store_mem_gpio_reg(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	u32 reg;
	struct ipw_priv *p = d->driver_data;

	sscanf(buf, "%x", &reg);
	ipw_write_reg32(p, 0x301100, reg);
	return strnlen(buf, count);
}

static DEVICE_ATTR(mem_gpio_reg, S_IWUSR | S_IRUGO,
		   show_mem_gpio_reg, store_mem_gpio_reg);

static ssize_t show_indirect_dword(struct device *d,
				   struct device_attribute *attr, char *buf)
{
	u32 reg = 0;
	struct ipw_priv *priv = d->driver_data;
	if (priv->status & STATUS_INDIRECT_DWORD)
		reg = ipw_read_reg32(priv, priv->indirect_dword);
	else
		reg = 0;

	return sprintf(buf, "0x%08x\n", reg);
}
static ssize_t store_indirect_dword(struct device *d,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct ipw_priv *priv = d->driver_data;

	sscanf(buf, "%x", &priv->indirect_dword);
	priv->status |= STATUS_INDIRECT_DWORD;
	return strnlen(buf, count);
}

static DEVICE_ATTR(indirect_dword, S_IWUSR | S_IRUGO,
		   show_indirect_dword, store_indirect_dword);

static ssize_t show_indirect_byte(struct device *d,
				  struct device_attribute *attr, char *buf)
{
	u8 reg = 0;
	struct ipw_priv *priv = d->driver_data;
	if (priv->status & STATUS_INDIRECT_BYTE)
		reg = ipw_read_reg8(priv, priv->indirect_byte);
	else
		reg = 0;

	return sprintf(buf, "0x%02x\n", reg);
}
static ssize_t store_indirect_byte(struct device *d,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct ipw_priv *priv = d->driver_data;

	sscanf(buf, "%x", &priv->indirect_byte);
	priv->status |= STATUS_INDIRECT_BYTE;
	return strnlen(buf, count);
}

static DEVICE_ATTR(indirect_byte, S_IWUSR | S_IRUGO,
		   show_indirect_byte, store_indirect_byte);

static ssize_t show_direct_dword(struct device *d,
				 struct device_attribute *attr, char *buf)
{
	u32 reg = 0;
	struct ipw_priv *priv = d->driver_data;

	if (priv->status & STATUS_DIRECT_DWORD)
		reg = ipw_read32(priv, priv->direct_dword);
	else
		reg = 0;

	return sprintf(buf, "0x%08x\n", reg);
}
static ssize_t store_direct_dword(struct device *d,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct ipw_priv *priv = d->driver_data;

	sscanf(buf, "%x", &priv->direct_dword);
	priv->status |= STATUS_DIRECT_DWORD;
	return strnlen(buf, count);
}

static DEVICE_ATTR(direct_dword, S_IWUSR | S_IRUGO,
		   show_direct_dword, store_direct_dword);

static inline int rf_kill_active(struct ipw_priv *priv)
{
	if (0 == (ipw_read32(priv, 0x30) & 0x10000))
		priv->status |= STATUS_RF_KILL_HW;
	else
		priv->status &= ~STATUS_RF_KILL_HW;

	return (priv->status & STATUS_RF_KILL_HW) ? 1 : 0;
}

static ssize_t show_rf_kill(struct device *d, struct device_attribute *attr,
			    char *buf)
{
	/* 0 - RF kill not enabled
	   1 - SW based RF kill active (sysfs)
	   2 - HW based RF kill active
	   3 - Both HW and SW baed RF kill active */
	struct ipw_priv *priv = d->driver_data;
	int val = ((priv->status & STATUS_RF_KILL_SW) ? 0x1 : 0x0) |
	    (rf_kill_active(priv) ? 0x2 : 0x0);
	return sprintf(buf, "%i\n", val);
}

static int ipw_radio_kill_sw(struct ipw_priv *priv, int disable_radio)
{
	if ((disable_radio ? 1 : 0) ==
	    (priv->status & STATUS_RF_KILL_SW ? 1 : 0))
		return 0;

	IPW_DEBUG_RF_KILL("Manual SW RF Kill set to: RADIO  %s\n",
			  disable_radio ? "OFF" : "ON");

	if (disable_radio) {
		priv->status |= STATUS_RF_KILL_SW;

		if (priv->workqueue) {
			cancel_delayed_work(&priv->request_scan);
		}
		wake_up_interruptible(&priv->wait_command_queue);
		queue_work(priv->workqueue, &priv->down);
	} else {
		priv->status &= ~STATUS_RF_KILL_SW;
		if (rf_kill_active(priv)) {
			IPW_DEBUG_RF_KILL("Can not turn radio back on - "
					  "disabled by HW switch\n");
			/* Make sure the RF_KILL check timer is running */
			cancel_delayed_work(&priv->rf_kill);
			queue_delayed_work(priv->workqueue, &priv->rf_kill,
					   2 * HZ);
		} else
			queue_work(priv->workqueue, &priv->up);
	}

	return 1;
}

static ssize_t store_rf_kill(struct device *d, struct device_attribute *attr,
			     const char *buf, size_t count)
{
	struct ipw_priv *priv = d->driver_data;

	ipw_radio_kill_sw(priv, buf[0] == '1');

	return count;
}

static DEVICE_ATTR(rf_kill, S_IWUSR | S_IRUGO, show_rf_kill, store_rf_kill);

static void ipw_irq_tasklet(struct ipw_priv *priv)
{
	u32 inta, inta_mask, handled = 0;
	unsigned long flags;
	int rc = 0;

	spin_lock_irqsave(&priv->lock, flags);

	inta = ipw_read32(priv, CX2_INTA_RW);
	inta_mask = ipw_read32(priv, CX2_INTA_MASK_R);
	inta &= (CX2_INTA_MASK_ALL & inta_mask);

	/* Add any cached INTA values that need to be handled */
	inta |= priv->isr_inta;

	/* handle all the justifications for the interrupt */
	if (inta & CX2_INTA_BIT_RX_TRANSFER) {
		ipw_rx(priv);
		handled |= CX2_INTA_BIT_RX_TRANSFER;
	}

	if (inta & CX2_INTA_BIT_TX_CMD_QUEUE) {
		IPW_DEBUG_HC("Command completed.\n");
		rc = ipw_queue_tx_reclaim(priv, &priv->txq_cmd, -1);
		priv->status &= ~STATUS_HCMD_ACTIVE;
		wake_up_interruptible(&priv->wait_command_queue);
		handled |= CX2_INTA_BIT_TX_CMD_QUEUE;
	}

	if (inta & CX2_INTA_BIT_TX_QUEUE_1) {
		IPW_DEBUG_TX("TX_QUEUE_1\n");
		rc = ipw_queue_tx_reclaim(priv, &priv->txq[0], 0);
		handled |= CX2_INTA_BIT_TX_QUEUE_1;
	}

	if (inta & CX2_INTA_BIT_TX_QUEUE_2) {
		IPW_DEBUG_TX("TX_QUEUE_2\n");
		rc = ipw_queue_tx_reclaim(priv, &priv->txq[1], 1);
		handled |= CX2_INTA_BIT_TX_QUEUE_2;
	}

	if (inta & CX2_INTA_BIT_TX_QUEUE_3) {
		IPW_DEBUG_TX("TX_QUEUE_3\n");
		rc = ipw_queue_tx_reclaim(priv, &priv->txq[2], 2);
		handled |= CX2_INTA_BIT_TX_QUEUE_3;
	}

	if (inta & CX2_INTA_BIT_TX_QUEUE_4) {
		IPW_DEBUG_TX("TX_QUEUE_4\n");
		rc = ipw_queue_tx_reclaim(priv, &priv->txq[3], 3);
		handled |= CX2_INTA_BIT_TX_QUEUE_4;
	}

	if (inta & CX2_INTA_BIT_STATUS_CHANGE) {
		IPW_WARNING("STATUS_CHANGE\n");
		handled |= CX2_INTA_BIT_STATUS_CHANGE;
	}

	if (inta & CX2_INTA_BIT_BEACON_PERIOD_EXPIRED) {
		IPW_WARNING("TX_PERIOD_EXPIRED\n");
		handled |= CX2_INTA_BIT_BEACON_PERIOD_EXPIRED;
	}

	if (inta & CX2_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE) {
		IPW_WARNING("HOST_CMD_DONE\n");
		handled |= CX2_INTA_BIT_SLAVE_MODE_HOST_CMD_DONE;
	}

	if (inta & CX2_INTA_BIT_FW_INITIALIZATION_DONE) {
		IPW_WARNING("FW_INITIALIZATION_DONE\n");
		handled |= CX2_INTA_BIT_FW_INITIALIZATION_DONE;
	}

	if (inta & CX2_INTA_BIT_FW_CARD_DISABLE_PHY_OFF_DONE) {
		IPW_WARNING("PHY_OFF_DONE\n");
		handled |= CX2_INTA_BIT_FW_CARD_DISABLE_PHY_OFF_DONE;
	}

	if (inta & CX2_INTA_BIT_RF_KILL_DONE) {
		IPW_DEBUG_RF_KILL("RF_KILL_DONE\n");
		priv->status |= STATUS_RF_KILL_HW;
		wake_up_interruptible(&priv->wait_command_queue);
		netif_carrier_off(priv->net_dev);
		netif_stop_queue(priv->net_dev);
		cancel_delayed_work(&priv->request_scan);
		queue_delayed_work(priv->workqueue, &priv->rf_kill, 2 * HZ);
		handled |= CX2_INTA_BIT_RF_KILL_DONE;
	}

	if (inta & CX2_INTA_BIT_FATAL_ERROR) {
		IPW_ERROR("Firmware error detected.  Restarting.\n");
#ifdef CONFIG_IPW_DEBUG
		if (ipw_debug_level & IPW_DL_FW_ERRORS) {
			ipw_dump_nic_error_log(priv);
			ipw_dump_nic_event_log(priv);
		}
#endif
		queue_work(priv->workqueue, &priv->adapter_restart);
		handled |= CX2_INTA_BIT_FATAL_ERROR;
	}

	if (inta & CX2_INTA_BIT_PARITY_ERROR) {
		IPW_ERROR("Parity error\n");
		handled |= CX2_INTA_BIT_PARITY_ERROR;
	}

	if (handled != inta) {
		IPW_ERROR("Unhandled INTA bits 0x%08x\n", inta & ~handled);
	}

	/* enable all interrupts */
	ipw_enable_interrupts(priv);

	spin_unlock_irqrestore(&priv->lock, flags);
}

#ifdef CONFIG_IPW_DEBUG
#define IPW_CMD(x) case IPW_CMD_ ## x : return #x
static char *get_cmd_string(u8 cmd)
{
	switch (cmd) {
		IPW_CMD(HOST_COMPLETE);
		IPW_CMD(POWER_DOWN);
		IPW_CMD(SYSTEM_CONFIG);
		IPW_CMD(MULTICAST_ADDRESS);
		IPW_CMD(SSID);
		IPW_CMD(ADAPTER_ADDRESS);
		IPW_CMD(PORT_TYPE);
		IPW_CMD(RTS_THRESHOLD);
		IPW_CMD(FRAG_THRESHOLD);
		IPW_CMD(POWER_MODE);
		IPW_CMD(WEP_KEY);
		IPW_CMD(TGI_TX_KEY);
		IPW_CMD(SCAN_REQUEST);
		IPW_CMD(SCAN_REQUEST_EXT);
		IPW_CMD(ASSOCIATE);
		IPW_CMD(SUPPORTED_RATES);
		IPW_CMD(SCAN_ABORT);
		IPW_CMD(TX_FLUSH);
		IPW_CMD(QOS_PARAMETERS);
		IPW_CMD(DINO_CONFIG);
		IPW_CMD(RSN_CAPABILITIES);
		IPW_CMD(RX_KEY);
		IPW_CMD(CARD_DISABLE);
		IPW_CMD(SEED_NUMBER);
		IPW_CMD(TX_POWER);
		IPW_CMD(COUNTRY_INFO);
		IPW_CMD(AIRONET_INFO);
		IPW_CMD(AP_TX_POWER);
		IPW_CMD(CCKM_INFO);
		IPW_CMD(CCX_VER_INFO);
		IPW_CMD(SET_CALIBRATION);
		IPW_CMD(SENSITIVITY_CALIB);
		IPW_CMD(RETRY_LIMIT);
		IPW_CMD(IPW_PRE_POWER_DOWN);
		IPW_CMD(VAP_BEACON_TEMPLATE);
		IPW_CMD(VAP_DTIM_PERIOD);
		IPW_CMD(EXT_SUPPORTED_RATES);
		IPW_CMD(VAP_LOCAL_TX_PWR_CONSTRAINT);
		IPW_CMD(VAP_QUIET_INTERVALS);
		IPW_CMD(VAP_CHANNEL_SWITCH);
		IPW_CMD(VAP_MANDATORY_CHANNELS);
		IPW_CMD(VAP_CELL_PWR_LIMIT);
		IPW_CMD(VAP_CF_PARAM_SET);
		IPW_CMD(VAP_SET_BEACONING_STATE);
		IPW_CMD(MEASUREMENT);
		IPW_CMD(POWER_CAPABILITY);
		IPW_CMD(SUPPORTED_CHANNELS);
		IPW_CMD(TPC_REPORT);
		IPW_CMD(WME_INFO);
		IPW_CMD(PRODUCTION_COMMAND);
	default:
		return "UNKNOWN";
	}
}
#endif				/* CONFIG_IPW_DEBUG */

#define HOST_COMPLETE_TIMEOUT HZ
static int ipw_send_cmd(struct ipw_priv *priv, struct host_cmd *cmd)
{
	int rc = 0;

	if (priv->status & STATUS_HCMD_ACTIVE) {
		IPW_ERROR("Already sending a command\n");
		return -1;
	}

	priv->status |= STATUS_HCMD_ACTIVE;

	IPW_DEBUG_HC("Sending %s command (#%d), %d bytes\n",
		     get_cmd_string(cmd->cmd), cmd->cmd, cmd->len);
	printk_buf(IPW_DL_HOST_COMMAND, (u8 *) cmd->param, cmd->len);

	rc = ipw_queue_tx_hcmd(priv, cmd->cmd, &cmd->param, cmd->len, 0);
	if (rc)
		return rc;

	rc = wait_event_interruptible_timeout(priv->wait_command_queue,
					      !(priv->
						status & STATUS_HCMD_ACTIVE),
					      HOST_COMPLETE_TIMEOUT);
	if (rc == 0) {
		IPW_DEBUG_INFO("Command completion failed out after %dms.\n",
			       jiffies_to_msecs(HOST_COMPLETE_TIMEOUT));
		priv->status &= ~STATUS_HCMD_ACTIVE;
		return -EIO;
	}
	if (priv->status & STATUS_RF_KILL_MASK) {
		IPW_DEBUG_INFO("Command aborted due to RF Kill Switch\n");
		return -EIO;
	}

	return 0;
}

static int ipw_send_host_complete(struct ipw_priv *priv)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_HOST_COMPLETE,
		.len = 0
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send HOST_COMPLETE command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_system_config(struct ipw_priv *priv,
				  struct ipw_sys_config *config)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SYSTEM_CONFIG,
		.len = sizeof(*config)
	};

	if (!priv || !config) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, config, sizeof(*config));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SYSTEM_CONFIG command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_ssid(struct ipw_priv *priv, u8 * ssid, int len)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SSID,
		.len = min(len, IW_ESSID_MAX_SIZE)
	};

	if (!priv || !ssid) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, ssid, cmd.len);
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SSID command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_adapter_address(struct ipw_priv *priv, u8 * mac)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_ADAPTER_ADDRESS,
		.len = ETH_ALEN
	};

	if (!priv || !mac) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	IPW_DEBUG_INFO("%s: Setting MAC to " MAC_FMT "\n",
		       priv->net_dev->name, MAC_ARG(mac));

	memcpy(&cmd.param, mac, ETH_ALEN);

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send ADAPTER_ADDRESS command\n");
		return -1;
	}

	return 0;
}

static void ipw_adapter_restart(void *adapter)
{
	struct ipw_priv *priv = adapter;

	if (priv->status & STATUS_RF_KILL_MASK)
		return;

	ipw_down(priv);
	if (ipw_up(priv)) {
		IPW_ERROR("Failed to up device\n");
		return;
	}
}

#define IPW_SCAN_CHECK_WATCHDOG (5 * HZ)

static void ipw_scan_check(void *data)
{
	struct ipw_priv *priv = data;
	if (priv->status & (STATUS_SCANNING | STATUS_SCAN_ABORTING)) {
		IPW_DEBUG_SCAN("Scan completion watchdog resetting "
			       "adapter (%dms).\n",
			       IPW_SCAN_CHECK_WATCHDOG / 100);
		ipw_adapter_restart(priv);
	}
}

static int ipw_send_scan_request_ext(struct ipw_priv *priv,
				     struct ipw_scan_request_ext *request)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SCAN_REQUEST_EXT,
		.len = sizeof(*request)
	};

	if (!priv || !request) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, request, sizeof(*request));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SCAN_REQUEST_EXT command\n");
		return -1;
	}

	queue_delayed_work(priv->workqueue, &priv->scan_check,
			   IPW_SCAN_CHECK_WATCHDOG);
	return 0;
}

static int ipw_send_scan_abort(struct ipw_priv *priv)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SCAN_ABORT,
		.len = 0
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SCAN_ABORT command\n");
		return -1;
	}

	return 0;
}

static int ipw_set_sensitivity(struct ipw_priv *priv, u16 sens)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SENSITIVITY_CALIB,
		.len = sizeof(struct ipw_sensitivity_calib)
	};
	struct ipw_sensitivity_calib *calib = (struct ipw_sensitivity_calib *)
	    &cmd.param;
	calib->beacon_rssi_raw = sens;
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SENSITIVITY CALIB command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_associate(struct ipw_priv *priv,
			      struct ipw_associate *associate)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_ASSOCIATE,
		.len = sizeof(*associate)
	};

	if (!priv || !associate) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, associate, sizeof(*associate));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send ASSOCIATE command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_supported_rates(struct ipw_priv *priv,
				    struct ipw_supported_rates *rates)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SUPPORTED_RATES,
		.len = sizeof(*rates)
	};

	if (!priv || !rates) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, rates, sizeof(*rates));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SUPPORTED_RATES command\n");
		return -1;
	}

	return 0;
}

static int ipw_set_random_seed(struct ipw_priv *priv)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_SEED_NUMBER,
		.len = sizeof(u32)
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	get_random_bytes(&cmd.param, sizeof(u32));

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send SEED_NUMBER command\n");
		return -1;
	}

	return 0;
}

#if 0
static int ipw_send_card_disable(struct ipw_priv *priv, u32 phy_off)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_CARD_DISABLE,
		.len = sizeof(u32)
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	*((u32 *) & cmd.param) = phy_off;

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send CARD_DISABLE command\n");
		return -1;
	}

	return 0;
}
#endif

static int ipw_send_tx_power(struct ipw_priv *priv, struct ipw_tx_power *power)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_TX_POWER,
		.len = sizeof(*power)
	};

	if (!priv || !power) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, power, sizeof(*power));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send TX_POWER command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_rts_threshold(struct ipw_priv *priv, u16 rts)
{
	struct ipw_rts_threshold rts_threshold = {
		.rts_threshold = rts,
	};
	struct host_cmd cmd = {
		.cmd = IPW_CMD_RTS_THRESHOLD,
		.len = sizeof(rts_threshold)
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, &rts_threshold, sizeof(rts_threshold));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send RTS_THRESHOLD command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_frag_threshold(struct ipw_priv *priv, u16 frag)
{
	struct ipw_frag_threshold frag_threshold = {
		.frag_threshold = frag,
	};
	struct host_cmd cmd = {
		.cmd = IPW_CMD_FRAG_THRESHOLD,
		.len = sizeof(frag_threshold)
	};

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	memcpy(&cmd.param, &frag_threshold, sizeof(frag_threshold));
	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send FRAG_THRESHOLD command\n");
		return -1;
	}

	return 0;
}

static int ipw_send_power_mode(struct ipw_priv *priv, u32 mode)
{
	struct host_cmd cmd = {
		.cmd = IPW_CMD_POWER_MODE,
		.len = sizeof(u32)
	};
	u32 *param = (u32 *) (&cmd.param);

	if (!priv) {
		IPW_ERROR("Invalid args\n");
		return -1;
	}

	/* If on battery, set to 3, if AC set to CAM, else user
	 * level */
	switch (mode) {
	case IPW_POWER_BATTERY:
		*param = IPW_POWER_INDEX_3;
		break;
	case IPW_POWER_AC:
		*param = IPW_POWER_MODE_CAM;
		break;
	default:
		*param = mode;
		break;
	}

	if (ipw_send_cmd(priv, &cmd)) {
		IPW_ERROR("failed to send POWER_MODE command\n");
		return -1;
	}

	return 0;
}

/*
 * The IPW device contains a Microwire compatible EEPROM that stores
 * various data like the MAC address.  Usually the firmware has exclusive
 * access to the eeprom, but during device initialization (before the
 * device driver has sent the HostComplete command to the firmware) the
 * device driver has read access to the EEPROM by way of indirect addressing
 * through a couple of memory mapped registers.
 *
 * The following is a simplified implementation for pulling data out of the
 * the eeprom, along with some helper functions to find information in
 * the per device private data's copy of the eeprom.
 *
 * NOTE: To better understand how these functions work (i.e what is a chip
 *       select and why do have to keep driving the eeprom clock?), read
 *       just about any data sheet for a Microwire compatible EEPROM.
 */

/* write a 32 bit value into the indirect accessor register */
static inline void eeprom_write_reg(struct ipw_priv *p, u32 data)
{
	ipw_write_reg32(p, FW_MEM_REG_EEPROM_ACCESS, data);

	/* the eeprom requires some time to complete the operation */
	udelay(p->eeprom_delay);

	return;
}

/* perform a chip select operation */
static inline void eeprom_cs(struct ipw_priv *priv)
{
	eeprom_write_reg(priv, 0);
	eeprom_write_reg(priv, EEPROM_BIT_CS);
	eeprom_write_reg(priv, EEPROM_BIT_CS | EEPROM_BIT_SK);
	eeprom_write_reg(priv, EEPROM_BIT_CS);
}

/* perform a chip select operation */
static inline void eeprom_disable_cs(struct ipw_priv *priv)
{
	eeprom_write_reg(priv, EEPROM_BIT_CS);
	eeprom_write_reg(priv, 0);
	eeprom_write_reg(priv, EEPROM_BIT_SK);
}

/* push a single bit down to the eeprom */
static inline void eeprom_write_bit(struct ipw_priv *p, u8 bit)
{
	int d = (bit ? EEPROM_BIT_DI : 0);
	eeprom_write_reg(p, EEPROM_BIT_CS | d);
	eeprom_write_reg(p, EEPROM_BIT_CS | d | EEPROM_BIT_SK);
}

/* push an opcode followed by an address down to the eeprom */
static void eeprom_op(struct ipw_priv *priv, u8 op, u8 addr)
{
	int i;

	eeprom_cs(priv);
	eeprom_write_bit(priv, 1);
	eeprom_write_bit(priv, op & 2);
	eeprom_write_bit(priv, op & 1);
	for (i = 7; i >= 0; i--) {
		eeprom_write_bit(priv, addr & (1 << i));
	}
}

/* pull 16 bits off the eeprom, one bit at a time */
static u16 eeprom_read_u16(struct ipw_priv *priv, u8 addr)
{
	int i;
	u16 r = 0;

	/* Send READ Opcode */
	eeprom_op(priv, EEPROM_CMD_READ, addr);

	/* Send dummy bit */
	eeprom_write_reg(priv, EEPROM_BIT_CS);

	/* Read the byte off the eeprom one bit at a time */
	for (i = 0; i < 16; i++) {
		u32 data = 0;
		eeprom_write_reg(priv, EEPROM_BIT_CS | EEPROM_BIT_SK);
		eeprom_write_reg(priv, EEPROM_BIT_CS);
		data = ipw_read_reg32(priv, FW_MEM_REG_EEPROM_ACCESS);
		r = (r << 1) | ((data & EEPROM_BIT_DO) ? 1 : 0);
	}

	/* Send another dummy bit */
	eeprom_write_reg(priv, 0);
	eeprom_disable_cs(priv);

	return r;
}

/* helper function for pulling the mac address out of the private */
/* data's copy of the eeprom data                                 */
static void eeprom_parse_mac(struct ipw_priv *priv, u8 * mac)
{
	u8 *ee = (u8 *) priv->eeprom;
	memcpy(mac, &ee[EEPROM_MAC_ADDRESS], 6);
}

/*
 * Either the device driver (i.e. the host) or the firmware can
 * load eeprom data into the designated region in SRAM.  If neither
 * happens then the FW will shutdown with a fatal error.
 *
 * In order to signal the FW to load the EEPROM, the EEPROM_LOAD_DISABLE
 * bit needs region of shared SRAM needs to be non-zero.
 */
static void ipw_eeprom_init_sram(struct ipw_priv *priv)
{
	int i;
	u16 *eeprom = (u16 *) priv->eeprom;

	IPW_DEBUG_TRACE(">>\n");

	/* read entire contents of eeprom into private buffer */
	for (i = 0; i < 128; i++)
		eeprom[i] = eeprom_read_u16(priv, (u8) i);

	/*
	   If the data looks correct, then copy it to our private
	   copy.  Otherwise let the firmware know to perform the operation
	   on it's own
	 */
	if ((priv->eeprom + EEPROM_VERSION) != 0) {
		IPW_DEBUG_INFO("Writing EEPROM data into SRAM\n");

		/* write the eeprom data to sram */
		for (i = 0; i < CX2_EEPROM_IMAGE_SIZE; i++)
			ipw_write8(priv, IPW_EEPROM_DATA + i, priv->eeprom[i]);

		/* Do not load eeprom data on fatal error or suspend */
		ipw_write32(priv, IPW_EEPROM_LOAD_DISABLE, 0);
	} else {
		IPW_DEBUG_INFO("Enabling FW initializationg of SRAM\n");

		/* Load eeprom data on fatal error or suspend */
		ipw_write32(priv, IPW_EEPROM_LOAD_DISABLE, 1);
	}

	IPW_DEBUG_TRACE("<<\n");
}

static inline void ipw_zero_memory(struct ipw_priv *priv, u32 start, u32 count)
{
	count >>= 2;
	if (!count)
		return;
	_ipw_write32(priv, CX2_AUTOINC_ADDR, start);
	while (count--)
		_ipw_write32(priv, CX2_AUTOINC_DATA, 0);
}

static inline void ipw_fw_dma_reset_command_blocks(struct ipw_priv *priv)
{
	ipw_zero_memory(priv, CX2_SHARED_SRAM_DMA_CONTROL,
			CB_NUMBER_OF_ELEMENTS_SMALL *
			sizeof(struct command_block));
}

static int ipw_fw_dma_enable(struct ipw_priv *priv)
{				/* start dma engine but no transfers yet */

	IPW_DEBUG_FW(">> : \n");

	/* Start the dma */
	ipw_fw_dma_reset_command_blocks(priv);

	/* Write CB base address */
	ipw_write_reg32(priv, CX2_DMA_I_CB_BASE, CX2_SHARED_SRAM_DMA_CONTROL);

	IPW_DEBUG_FW("<< : \n");
	return 0;
}

static void ipw_fw_dma_abort(struct ipw_priv *priv)
{
	u32 control = 0;

	IPW_DEBUG_FW(">> :\n");

	//set the Stop and Abort bit
	control = DMA_CONTROL_SMALL_CB_CONST_VALUE | DMA_CB_STOP_AND_ABORT;
	ipw_write_reg32(priv, CX2_DMA_I_DMA_CONTROL, control);
	priv->sram_desc.last_cb_index = 0;

	IPW_DEBUG_FW("<< \n");
}

static int ipw_fw_dma_write_command_block(struct ipw_priv *priv, int index,
					  struct command_block *cb)
{
	u32 address =
	    CX2_SHARED_SRAM_DMA_CONTROL +
	    (sizeof(struct command_block) * index);
	IPW_DEBUG_FW(">> :\n");

	ipw_write_indirect(priv, address, (u8 *) cb,
			   (int)sizeof(struct command_block));

	IPW_DEBUG_FW("<< :\n");
	return 0;

}

static int ipw_fw_dma_kick(struct ipw_priv *priv)
{
	u32 control = 0;
	u32 index = 0;

	IPW_DEBUG_FW(">> :\n");

	for (index = 0; index < priv->sram_desc.last_cb_index; index++)
		ipw_fw_dma_write_command_block(priv, index,
					       &priv->sram_desc.cb_list[index]);

	/* Enable the DMA in the CSR register */
	ipw_clear_bit(priv, CX2_RESET_REG,
		      CX2_RESET_REG_MASTER_DISABLED |
		      CX2_RESET_REG_STOP_MASTER);

	/* Set the Start bit. */
	control = DMA_CONTROL_SMALL_CB_CONST_VALUE | DMA_CB_START;
	ipw_write_reg32(priv, CX2_DMA_I_DMA_CONTROL, control);

	IPW_DEBUG_FW("<< :\n");
	return 0;
}

static void ipw_fw_dma_dump_command_block(struct ipw_priv *priv)
{
	u32 address;
	u32 register_value = 0;
	u32 cb_fields_address = 0;

	IPW_DEBUG_FW(">> :\n");
	address = ipw_read_reg32(priv, CX2_DMA_I_CURRENT_CB);
	IPW_DEBUG_FW_INFO("Current CB is 0x%x \n", address);

	/* Read the DMA Controlor register */
	register_value = ipw_read_reg32(priv, CX2_DMA_I_DMA_CONTROL);
	IPW_DEBUG_FW_INFO("CX2_DMA_I_DMA_CONTROL is 0x%x \n", register_value);

	/* Print the CB values */
	cb_fields_address = address;
	register_value = ipw_read_reg32(priv, cb_fields_address);
	IPW_DEBUG_FW_INFO("Current CB ControlField is 0x%x \n", register_value);

	cb_fields_address += sizeof(u32);
	register_value = ipw_read_reg32(priv, cb_fields_address);
	IPW_DEBUG_FW_INFO("Current CB Source Field is 0x%x \n", register_value);

	cb_fields_address += sizeof(u32);
	register_value = ipw_read_reg32(priv, cb_fields_address);
	IPW_DEBUG_FW_INFO("Current CB Destination Field is 0x%x \n",
			  register_value);

	cb_fields_address += sizeof(u32);
	register_value = ipw_read_reg32(priv, cb_fields_address);
	IPW_DEBUG_FW_INFO("Current CB Status Field is 0x%x \n", register_value);

	IPW_DEBUG_FW(">> :\n");
}

static int ipw_fw_dma_command_block_index(struct ipw_priv *priv)
{
	u32 current_cb_address = 0;
	u32 current_cb_index = 0;

	IPW_DEBUG_FW("<< :\n");
	current_cb_address = ipw_read_reg32(priv, CX2_DMA_I_CURRENT_CB);

	current_cb_index = (current_cb_address - CX2_SHARED_SRAM_DMA_CONTROL) /
	    sizeof(struct command_block);

	IPW_DEBUG_FW_INFO("Current CB index 0x%x address = 0x%X \n",
			  current_cb_index, current_cb_address);

	IPW_DEBUG_FW(">> :\n");
	return current_cb_index;

}

static int ipw_fw_dma_add_command_block(struct ipw_priv *priv,
					u32 src_address,
					u32 dest_address,
					u32 length,
					int interrupt_enabled, int is_last)
{

	u32 control = CB_VALID | CB_SRC_LE | CB_DEST_LE | CB_SRC_AUTOINC |
	    CB_SRC_IO_GATED | CB_DEST_AUTOINC | CB_SRC_SIZE_LONG |
	    CB_DEST_SIZE_LONG;
	struct command_block *cb;
	u32 last_cb_element = 0;

	IPW_DEBUG_FW_INFO("src_address=0x%x dest_address=0x%x length=0x%x\n",
			  src_address, dest_address, length);

	if (priv->sram_desc.last_cb_index >= CB_NUMBER_OF_ELEMENTS_SMALL)
		return -1;

	last_cb_element = priv->sram_desc.last_cb_index;
	cb = &priv->sram_desc.cb_list[last_cb_element];
	priv->sram_desc.last_cb_index++;

	/* Calculate the new CB control word */
	if (interrupt_enabled)
		control |= CB_INT_ENABLED;

	if (is_last)
		control |= CB_LAST_VALID;

	control |= length;

	/* Calculate the CB Element's checksum value */
	cb->status = control ^ src_address ^ dest_address;

	/* Copy the Source and Destination addresses */
	cb->dest_addr = dest_address;
	cb->source_addr = src_address;

	/* Copy the Control Word last */
	cb->control = control;

	return 0;
}

static int ipw_fw_dma_add_buffer(struct ipw_priv *priv,
				 u32 src_phys, u32 dest_address, u32 length)
{
	u32 bytes_left = length;
	u32 src_offset = 0;
	u32 dest_offset = 0;
	int status = 0;
	IPW_DEBUG_FW(">> \n");
	IPW_DEBUG_FW_INFO("src_phys=0x%x dest_address=0x%x length=0x%x\n",
			  src_phys, dest_address, length);
	while (bytes_left > CB_MAX_LENGTH) {
		status = ipw_fw_dma_add_command_block(priv,
						      src_phys + src_offset,
						      dest_address +
						      dest_offset,
						      CB_MAX_LENGTH, 0, 0);
		if (status) {
			IPW_DEBUG_FW_INFO(": Failed\n");
			return -1;
		} else
			IPW_DEBUG_FW_INFO(": Added new cb\n");

		src_offset += CB_MAX_LENGTH;
		dest_offset += CB_MAX_LENGTH;
		bytes_left -= CB_MAX_LENGTH;
	}

	/* add the buffer tail */
	if (bytes_left > 0) {
		status =
		    ipw_fw_dma_add_command_block(priv, src_phys + src_offset,
						 dest_address + dest_offset,
						 bytes_left, 0, 0);
		if (status) {
			IPW_DEBUG_FW_INFO(": Failed on the buffer tail\n");
			return -1;
		} else
			IPW_DEBUG_FW_INFO
			    (": Adding new cb - the buffer tail\n");
	}

	IPW_DEBUG_FW("<< \n");
	return 0;
}

static int ipw_fw_dma_wait(struct ipw_priv *priv)
{
	u32 current_index = 0;
	u32 watchdog = 0;

	IPW_DEBUG_FW(">> : \n");

	current_index = ipw_fw_dma_command_block_index(priv);
	IPW_DEBUG_FW_INFO("sram_desc.last_cb_index:0x%8X\n",
			  (int)priv->sram_desc.last_cb_index);

	while (current_index < priv->sram_desc.last_cb_index) {
		udelay(50);
		current_index = ipw_fw_dma_command_block_index(priv);

		watchdog++;

		if (watchdog > 400) {
			IPW_DEBUG_FW_INFO("Timeout\n");
			ipw_fw_dma_dump_command_block(priv);
			ipw_fw_dma_abort(priv);
			return -1;
		}
	}

	ipw_fw_dma_abort(priv);

	/*Disable the DMA in the CSR register */
	ipw_set_bit(priv, CX2_RESET_REG,
		    CX2_RESET_REG_MASTER_DISABLED | CX2_RESET_REG_STOP_MASTER);

	IPW_DEBUG_FW("<< dmaWaitSync \n");
	return 0;
}

static void ipw_remove_current_network(struct ipw_priv *priv)
{
	struct list_head *element, *safe;
	struct ieee80211_network *network = NULL;
	list_for_each_safe(element, safe, &priv->ieee->network_list) {
		network = list_entry(element, struct ieee80211_network, list);
		if (!memcmp(network->bssid, priv->bssid, ETH_ALEN)) {
			list_del(element);
			list_add_tail(&network->list,
				      &priv->ieee->network_free_list);
		}
	}
}

/**
 * Check that card is still alive.
 * Reads debug register from domain0.
 * If card is present, pre-defined value should
 * be found there.
 *
 * @param priv
 * @return 1 if card is present, 0 otherwise
 */
static inline int ipw_alive(struct ipw_priv *priv)
{
	return ipw_read32(priv, 0x90) == 0xd55555d5;
}

static inline int ipw_poll_bit(struct ipw_priv *priv, u32 addr, u32 mask,
			       int timeout)
{
	int i = 0;

	do {
		if ((ipw_read32(priv, addr) & mask) == mask)
			return i;
		mdelay(10);
		i += 10;
	} while (i < timeout);

	return -ETIME;
}

/* These functions load the firmware and micro code for the operation of
 * the ipw hardware.  It assumes the buffer has all the bits for the
 * image and the caller is handling the memory allocation and clean up.
 */

static int ipw_stop_master(struct ipw_priv *priv)
{
	int rc;

	IPW_DEBUG_TRACE(">> \n");
	/* stop master. typical delay - 0 */
	ipw_set_bit(priv, CX2_RESET_REG, CX2_RESET_REG_STOP_MASTER);

	rc = ipw_poll_bit(priv, CX2_RESET_REG,
			  CX2_RESET_REG_MASTER_DISABLED, 100);
	if (rc < 0) {
		IPW_ERROR("stop master failed in 10ms\n");
		return -1;
	}

	IPW_DEBUG_INFO("stop master %dms\n", rc);

	return rc;
}

static void ipw_arc_release(struct ipw_priv *priv)
{
	IPW_DEBUG_TRACE(">> \n");
	mdelay(5);

	ipw_clear_bit(priv, CX2_RESET_REG, CBD_RESET_REG_PRINCETON_RESET);

	/* no one knows timing, for safety add some delay */
	mdelay(5);
}

struct fw_header {
	u32 version;
	u32 mode;
};

struct fw_chunk {
	u32 address;
	u32 length;
};

#define IPW_FW_MAJOR_VERSION 2
#define IPW_FW_MINOR_VERSION 2

#define IPW_FW_MINOR(x) ((x & 0xff) >> 8)
#define IPW_FW_MAJOR(x) (x & 0xff)

#define IPW_FW_VERSION ((IPW_FW_MINOR_VERSION << 8) | \
                         IPW_FW_MAJOR_VERSION)

#define IPW_FW_PREFIX "ipw-" __stringify(IPW_FW_MAJOR_VERSION) \
"." __stringify(IPW_FW_MINOR_VERSION) "-"

#if IPW_FW_MAJOR_VERSION >= 2 && IPW_FW_MINOR_VERSION > 0
#define IPW_FW_NAME(x) IPW_FW_PREFIX "" x ".fw"
#else
#define IPW_FW_NAME(x) "ipw2200_" x ".fw"
#endif

static int ipw_load_ucode(struct ipw_priv *priv, u8 * data, size_t len)
{
	int rc = 0, i, addr;
	u8 cr = 0;
	u16 *image;

	image = (u16 *) data;

	IPW_DEBUG_TRACE(">> \n");

	rc = ipw_stop_master(priv);

	if (rc < 0)
		return rc;

//      spin_lock_irqsave(&priv->lock, flags);

	for (addr = CX2_SHARED_LOWER_BOUND;
	     addr < CX2_REGISTER_DOMAIN1_END; addr += 4) {
		ipw_write32(priv, addr, 0);
	}

	/* no ucode (yet) */
	memset(&priv->dino_alive, 0, sizeof(priv->dino_alive));
	/* destroy DMA queues */
	/* reset sequence */

	ipw_write_reg32(priv, CX2_MEM_HALT_AND_RESET, CX2_BIT_HALT_RESET_ON);
	ipw_arc_release(priv);
	ipw_write_reg32(priv, CX2_MEM_HALT_AND_RESET, CX2_BIT_HALT_RESET_OFF);
	mdelay(1);

	/* reset PHY */
	ipw_write_reg32(priv, CX2_INTERNAL_CMD_EVENT, CX2_BASEBAND_POWER_DOWN);
	mdelay(1);

	ipw_write_reg32(priv, CX2_INTERNAL_CMD_EVENT, 0);
	mdelay(1);

	/* enable ucode store */
	ipw_write_reg8(priv, DINO_CONTROL_REG, 0x0);
	ipw_write_reg8(priv, DINO_CONTROL_REG, DINO_ENABLE_CS);
	mdelay(1);

	/* write ucode */
	/**
	 * @bug
	 * Do NOT set indirect address register once and then
	 * store data to indirect data register in the loop.
	 * It seems very reasonable, but in this case DINO do not
	 * accept ucode. It is essential to set address each time.
	 */
	/* load new ipw uCode */
	for (i = 0; i < len / 2; i++)
		ipw_write_reg16(priv, CX2_BASEBAND_CONTROL_STORE, image[i]);

	/* enable DINO */
	ipw_write_reg8(priv, CX2_BASEBAND_CONTROL_STATUS, 0);
	ipw_write_reg8(priv, CX2_BASEBAND_CONTROL_STATUS, DINO_ENABLE_SYSTEM);

	/* this is where the igx / win driver deveates from the VAP driver. */

	/* wait for alive response */
	for (i = 0; i < 100; i++) {
		/* poll for incoming data */
		cr = ipw_read_reg8(priv, CX2_BASEBAND_CONTROL_STATUS);
		if (cr & DINO_RXFIFO_DATA)
			break;
		mdelay(1);
	}

	if (cr & DINO_RXFIFO_DATA) {
		/* alive_command_responce size is NOT multiple of 4 */
		u32 response_buffer[(sizeof(priv->dino_alive) + 3) / 4];

		for (i = 0; i < ARRAY_SIZE(response_buffer); i++)
			response_buffer[i] =
			    ipw_read_reg32(priv, CX2_BASEBAND_RX_FIFO_READ);
		memcpy(&priv->dino_alive, response_buffer,
		       sizeof(priv->dino_alive));
		if (priv->dino_alive.alive_command == 1
		    && priv->dino_alive.ucode_valid == 1) {
			rc = 0;
			IPW_DEBUG_INFO
			    ("Microcode OK, rev. %d (0x%x) dev. %d (0x%x) "
			     "of %02d/%02d/%02d %02d:%02d\n",
			     priv->dino_alive.software_revision,
			     priv->dino_alive.software_revision,
			     priv->dino_alive.device_identifier,
			     priv->dino_alive.device_identifier,
			     priv->dino_alive.time_stamp[0],
			     priv->dino_alive.time_stamp[1],
			     priv->dino_alive.time_stamp[2],
			     priv->dino_alive.time_stamp[3],
			     priv->dino_alive.time_stamp[4]);
		} else {
			IPW_DEBUG_INFO("Microcode is not alive\n");
			rc = -EINVAL;
		}
	} else {
		IPW_DEBUG_INFO("No alive response from DINO\n");
		rc = -ETIME;
	}

	/* disable DINO, otherwise for some reason
	   firmware have problem getting alive resp. */
	ipw_write_reg8(priv, CX2_BASEBAND_CONTROL_STATUS, 0);

//      spin_unlock_irqrestore(&priv->lock, flags);

	return rc;
}

static int ipw_load_firmware(struct ipw_priv *priv, u8 * data, size_t len)
{
	int rc = -1;
	int offset = 0;
	struct fw_chunk *chunk;
	dma_addr_t shared_phys;
	u8 *shared_virt;

	IPW_DEBUG_TRACE("<< : \n");
	shared_virt = pci_alloc_consistent(priv->pci_dev, len, &shared_phys);

	if (!shared_virt)
		return -ENOMEM;

	memmove(shared_virt, data, len);

	/* Start the Dma */
	rc = ipw_fw_dma_enable(priv);

	if (priv->sram_desc.last_cb_index > 0) {
		/* the DMA is already ready this would be a bug. */
		BUG();
		goto out;
	}

	do {
		chunk = (struct fw_chunk *)(data + offset);
		offset += sizeof(struct fw_chunk);
		/* build DMA packet and queue up for sending */
		/* dma to chunk->address, the chunk->length bytes from data +
		 * offeset*/
		/* Dma loading */
		rc = ipw_fw_dma_add_buffer(priv, shared_phys + offset,
					   chunk->address, chunk->length);
		if (rc) {
			IPW_DEBUG_INFO("dmaAddBuffer Failed\n");
			goto out;
		}

		offset += chunk->length;
	} while (offset < len);

	/* Run the DMA and wait for the answer */
	rc = ipw_fw_dma_kick(priv);
	if (rc) {
		IPW_ERROR("dmaKick Failed\n");
		goto out;
	}

	rc = ipw_fw_dma_wait(priv);
	if (rc) {
		IPW_ERROR("dmaWaitSync Failed\n");
		goto out;
	}
      out:
	pci_free_consistent(priv->pci_dev, len, shared_virt, shared_phys);
	return rc;
}

/* stop nic */
static int ipw_stop_nic(struct ipw_priv *priv)
{
	int rc = 0;

	/* stop */
	ipw_write32(priv, CX2_RESET_REG, CX2_RESET_REG_STOP_MASTER);

	rc = ipw_poll_bit(priv, CX2_RESET_REG,
			  CX2_RESET_REG_MASTER_DISABLED, 500);
	if (rc < 0) {
		IPW_ERROR("wait for reg master disabled failed\n");
		return rc;
	}

	ipw_set_bit(priv, CX2_RESET_REG, CBD_RESET_REG_PRINCETON_RESET);

	return rc;
}

static void ipw_start_nic(struct ipw_priv *priv)
{
	IPW_DEBUG_TRACE(">>\n");

	/* prvHwStartNic  release ARC */
	ipw_clear_bit(priv, CX2_RESET_REG,
		      CX2_RESET_REG_MASTER_DISABLED |
		      CX2_RESET_REG_STOP_MASTER |
		      CBD_RESET_REG_PRINCETON_RESET);

	/* enable power management */
	ipw_set_bit(priv, CX2_GP_CNTRL_RW,
		    CX2_GP_CNTRL_BIT_HOST_ALLOWS_STANDBY);

	IPW_DEBUG_TRACE("<<\n");
}

static int ipw_init_nic(struct ipw_priv *priv)
{
	int rc;

	IPW_DEBUG_TRACE(">>\n");
	/* reset */
	/*prvHwInitNic */
	/* set "initialization complete" bit to move adapter to D0 state */
	ipw_set_bit(priv, CX2_GP_CNTRL_RW, CX2_GP_CNTRL_BIT_INIT_DONE);

	/* low-level PLL activation */
	ipw_write32(priv, CX2_READ_INT_REGISTER,
		    CX2_BIT_INT_HOST_SRAM_READ_INT_REGISTER);

	/* wait for clock stabilization */
	rc = ipw_poll_bit(priv, CX2_GP_CNTRL_RW,
			  CX2_GP_CNTRL_BIT_CLOCK_READY, 250);
	if (rc < 0)
		IPW_DEBUG_INFO("FAILED wait for clock stablization\n");

	/* assert SW reset */
	ipw_set_bit(priv, CX2_RESET_REG, CX2_RESET_REG_SW_RESET);

	udelay(10);

	/* set "initialization complete" bit to move adapter to D0 state */
	ipw_set_bit(priv, CX2_GP_CNTRL_RW, CX2_GP_CNTRL_BIT_INIT_DONE);

	IPW_DEBUG_TRACE(">>\n");
	return 0;
}

/* Call this function from process context, it will sleep in request_firmware.
 * Probe is an ok place to call this from.
 */
static int ipw_reset_nic(struct ipw_priv *priv)
{
	int rc = 0;

	IPW_DEBUG_TRACE(">>\n");

	rc = ipw_init_nic(priv);

	/* Clear the 'host command active' bit... */
	priv->status &= ~STATUS_HCMD_ACTIVE;
	wake_up_interruptible(&priv->wait_command_queue);

	IPW_DEBUG_TRACE("<<\n");
	return rc;
}

static int ipw_get_fw(struct ipw_priv *priv,
		      const struct firmware **fw, const char *name)
{
	struct fw_header *header;
	int rc;

	/* ask firmware_class module to get the boot firmware off disk */
	rc = request_firmware(fw, name, &priv->pci_dev->dev);
	if (rc < 0) {
		IPW_ERROR("%s load failed: Reason %d\n", name, rc);
		return rc;
	}

	header = (struct fw_header *)(*fw)->data;
	if (IPW_FW_MAJOR(header->version) != IPW_FW_MAJOR_VERSION) {
		IPW_ERROR("'%s' firmware version not compatible (%d != %d)\n",
			  name,
			  IPW_FW_MAJOR(header->version), IPW_FW_MAJOR_VERSION);
		return -EINVAL;
	}

	IPW_DEBUG_INFO("Loading firmware '%s' file v%d.%d (%zd bytes)\n",
		       name,
		       IPW_FW_MAJOR(header->version),
		       IPW_FW_MINOR(header->version),
		       (*fw)->size - sizeof(struct fw_header));
	return 0;
}

#define CX2_RX_BUF_SIZE (3000)

static inline void ipw_rx_queue_reset(struct ipw_priv *priv,
				      struct ipw_rx_queue *rxq)
{
	unsigned long flags;
	int i;

	spin_lock_irqsave(&rxq->lock, flags);

	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++) {
		/* In the reset function, these buffers may have been allocated
		 * to an SKB, so we need to unmap and free potential storage */
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev, rxq->pool[i].dma_addr,
					 CX2_RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(rxq->pool[i].skb);
		}
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);
	}

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->processed = RX_QUEUE_SIZE - 1;
	rxq->free_count = 0;
	spin_unlock_irqrestore(&rxq->lock, flags);
}

#ifdef CONFIG_PM
static int fw_loaded = 0;
static const struct firmware *bootfw = NULL;
static const struct firmware *firmware = NULL;
static const struct firmware *ucode = NULL;
#endif

static int ipw_load(struct ipw_priv *priv)
{
#ifndef CONFIG_PM
	const struct firmware *bootfw = NULL;
	const struct firmware *firmware = NULL;
	const struct firmware *ucode = NULL;
#endif
	int rc = 0, retries = 3;

#ifdef CONFIG_PM
	if (!fw_loaded) {
#endif
		rc = ipw_get_fw(priv, &bootfw, IPW_FW_NAME("boot"));
		if (rc)
			goto error;

		switch (priv->ieee->iw_mode) {
		case IW_MODE_ADHOC:
			rc = ipw_get_fw(priv, &ucode,
					IPW_FW_NAME("ibss_ucode"));
			if (rc)
				goto error;

			rc = ipw_get_fw(priv, &firmware, IPW_FW_NAME("ibss"));
			break;

#ifdef CONFIG_IPW_PROMISC
		case IW_MODE_MONITOR:
			rc = ipw_get_fw(priv, &ucode,
					IPW_FW_NAME("ibss_ucode"));
			if (rc)
				goto error;

			rc = ipw_get_fw(priv, &firmware,
					IPW_FW_NAME("sniffer"));
			break;
#endif
		case IW_MODE_INFRA:
			rc = ipw_get_fw(priv, &ucode, IPW_FW_NAME("bss_ucode"));
			if (rc)
				goto error;

			rc = ipw_get_fw(priv, &firmware, IPW_FW_NAME("bss"));
			break;

		default:
			rc = -EINVAL;
		}

		if (rc)
			goto error;

#ifdef CONFIG_PM
		fw_loaded = 1;
	}
#endif

	if (!priv->rxq)
		priv->rxq = ipw_rx_queue_alloc(priv);
	else
		ipw_rx_queue_reset(priv, priv->rxq);
	if (!priv->rxq) {
		IPW_ERROR("Unable to initialize Rx queue\n");
		goto error;
	}

      retry:
	/* Ensure interrupts are disabled */
	ipw_write32(priv, CX2_INTA_MASK_R, ~CX2_INTA_MASK_ALL);
	priv->status &= ~STATUS_INT_ENABLED;

	/* ack pending interrupts */
	ipw_write32(priv, CX2_INTA_RW, CX2_INTA_MASK_ALL);

	ipw_stop_nic(priv);

	rc = ipw_reset_nic(priv);
	if (rc) {
		IPW_ERROR("Unable to reset NIC\n");
		goto error;
	}

	ipw_zero_memory(priv, CX2_NIC_SRAM_LOWER_BOUND,
			CX2_NIC_SRAM_UPPER_BOUND - CX2_NIC_SRAM_LOWER_BOUND);

	/* DMA the initial boot firmware into the device */
	rc = ipw_load_firmware(priv, bootfw->data + sizeof(struct fw_header),
			       bootfw->size - sizeof(struct fw_header));
	if (rc < 0) {
		IPW_ERROR("Unable to load boot firmware\n");
		goto error;
	}

	/* kick start the device */
	ipw_start_nic(priv);

	/* wait for the device to finish it's initial startup sequence */
	rc = ipw_poll_bit(priv, CX2_INTA_RW,
			  CX2_INTA_BIT_FW_INITIALIZATION_DONE, 500);
	if (rc < 0) {
		IPW_ERROR("device failed to boot initial fw image\n");
		goto error;
	}
	IPW_DEBUG_INFO("initial device response after %dms\n", rc);

	/* ack fw init done interrupt */
	ipw_write32(priv, CX2_INTA_RW, CX2_INTA_BIT_FW_INITIALIZATION_DONE);

	/* DMA the ucode into the device */
	rc = ipw_load_ucode(priv, ucode->data + sizeof(struct fw_header),
			    ucode->size - sizeof(struct fw_header));
	if (rc < 0) {
		IPW_ERROR("Unable to load ucode\n");
		goto error;
	}

	/* stop nic */
	ipw_stop_nic(priv);

	/* DMA bss firmware into the device */
	rc = ipw_load_firmware(priv, firmware->data +
			       sizeof(struct fw_header),
			       firmware->size - sizeof(struct fw_header));
	if (rc < 0) {
		IPW_ERROR("Unable to load firmware\n");
		goto error;
	}

	ipw_write32(priv, IPW_EEPROM_LOAD_DISABLE, 0);

	rc = ipw_queue_reset(priv);
	if (rc) {
		IPW_ERROR("Unable to initialize queues\n");
		goto error;
	}

	/* Ensure interrupts are disabled */
	ipw_write32(priv, CX2_INTA_MASK_R, ~CX2_INTA_MASK_ALL);

	/* kick start the device */
	ipw_start_nic(priv);

	if (ipw_read32(priv, CX2_INTA_RW) & CX2_INTA_BIT_PARITY_ERROR) {
		if (retries > 0) {
			IPW_WARNING("Parity error.  Retrying init.\n");
			retries--;
			goto retry;
		}

		IPW_ERROR("TODO: Handle parity error -- schedule restart?\n");
		rc = -EIO;
		goto error;
	}

	/* wait for the device */
	rc = ipw_poll_bit(priv, CX2_INTA_RW,
			  CX2_INTA_BIT_FW_INITIALIZATION_DONE, 500);
	if (rc < 0) {
		IPW_ERROR("device failed to start after 500ms\n");
		goto error;
	}
	IPW_DEBUG_INFO("device response after %dms\n", rc);

	/* ack fw init done interrupt */
	ipw_write32(priv, CX2_INTA_RW, CX2_INTA_BIT_FW_INITIALIZATION_DONE);

	/* read eeprom data and initialize the eeprom region of sram */
	priv->eeprom_delay = 1;
	ipw_eeprom_init_sram(priv);

	/* enable interrupts */
	ipw_enable_interrupts(priv);

	/* Ensure our queue has valid packets */
	ipw_rx_queue_replenish(priv);

	ipw_write32(priv, CX2_RX_READ_INDEX, priv->rxq->read);

	/* ack pending interrupts */
	ipw_write32(priv, CX2_INTA_RW, CX2_INTA_MASK_ALL);

#ifndef CONFIG_PM
	release_firmware(bootfw);
	release_firmware(ucode);
	release_firmware(firmware);
#endif
	return 0;

      error:
	if (priv->rxq) {
		ipw_rx_queue_free(priv, priv->rxq);
		priv->rxq = NULL;
	}
	ipw_tx_queue_free(priv);
	if (bootfw)
		release_firmware(bootfw);
	if (ucode)
		release_firmware(ucode);
	if (firmware)
		release_firmware(firmware);
#ifdef CONFIG_PM
	fw_loaded = 0;
	bootfw = ucode = firmware = NULL;
#endif

	return rc;
}

/**
 * DMA services
 *
 * Theory of operation
 *
 * A queue is a circular buffers with 'Read' and 'Write' pointers.
 * 2 empty entries always kept in the buffer to protect from overflow.
 *
 * For Tx queue, there are low mark and high mark limits. If, after queuing
 * the packet for Tx, free space become < low mark, Tx queue stopped. When
 * reclaiming packets (on 'tx done IRQ), if free space become > high mark,
 * Tx queue resumed.
 *
 * The IPW operates with six queues, one receive queue in the device's
 * sram, one transmit queue for sending commands to the device firmware,
 * and four transmit queues for data.
 *
 * The four transmit queues allow for performing quality of service (qos)
 * transmissions as per the 802.11 protocol.  Currently Linux does not
 * provide a mechanism to the user for utilizing prioritized queues, so
 * we only utilize the first data transmit queue (queue1).
 */

/**
 * Driver allocates buffers of this size for Rx
 */

static inline int ipw_queue_space(const struct clx2_queue *q)
{
	int s = q->last_used - q->first_empty;
	if (s <= 0)
		s += q->n_bd;
	s -= 2;			/* keep some reserve to not confuse empty and full situations */
	if (s < 0)
		s = 0;
	return s;
}

static inline int ipw_queue_inc_wrap(int index, int n_bd)
{
	return (++index == n_bd) ? 0 : index;
}

/**
 * Initialize common DMA queue structure
 *
 * @param q                queue to init
 * @param count            Number of BD's to allocate. Should be power of 2
 * @param read_register    Address for 'read' register
 *                         (not offset within BAR, full address)
 * @param write_register   Address for 'write' register
 *                         (not offset within BAR, full address)
 * @param base_register    Address for 'base' register
 *                         (not offset within BAR, full address)
 * @param size             Address for 'size' register
 *                         (not offset within BAR, full address)
 */
static void ipw_queue_init(struct ipw_priv *priv, struct clx2_queue *q,
			   int count, u32 read, u32 write, u32 base, u32 size)
{
	q->n_bd = count;

	q->low_mark = q->n_bd / 4;
	if (q->low_mark < 4)
		q->low_mark = 4;

	q->high_mark = q->n_bd / 8;
	if (q->high_mark < 2)
		q->high_mark = 2;

	q->first_empty = q->last_used = 0;
	q->reg_r = read;
	q->reg_w = write;

	ipw_write32(priv, base, q->dma_addr);
	ipw_write32(priv, size, count);
	ipw_write32(priv, read, 0);
	ipw_write32(priv, write, 0);

	_ipw_read32(priv, 0x90);
}

static int ipw_queue_tx_init(struct ipw_priv *priv,
			     struct clx2_tx_queue *q,
			     int count, u32 read, u32 write, u32 base, u32 size)
{
	struct pci_dev *dev = priv->pci_dev;

	q->txb = kmalloc(sizeof(q->txb[0]) * count, GFP_KERNEL);
	if (!q->txb) {
		IPW_ERROR("vmalloc for auxilary BD structures failed\n");
		return -ENOMEM;
	}

	q->bd =
	    pci_alloc_consistent(dev, sizeof(q->bd[0]) * count, &q->q.dma_addr);
	if (!q->bd) {
		IPW_ERROR("pci_alloc_consistent(%zd) failed\n",
			  sizeof(q->bd[0]) * count);
		kfree(q->txb);
		q->txb = NULL;
		return -ENOMEM;
	}

	ipw_queue_init(priv, &q->q, count, read, write, base, size);
	return 0;
}

/**
 * Free one TFD, those at index [txq->q.last_used].
 * Do NOT advance any indexes
 *
 * @param dev
 * @param txq
 */
static void ipw_queue_tx_free_tfd(struct ipw_priv *priv,
				  struct clx2_tx_queue *txq)
{
	struct tfd_frame *bd = &txq->bd[txq->q.last_used];
	struct pci_dev *dev = priv->pci_dev;
	int i;

	/* classify bd */
	if (bd->control_flags.message_type == TX_HOST_COMMAND_TYPE)
		/* nothing to cleanup after for host commands */
		return;

	/* sanity check */
	if (bd->u.data.num_chunks > NUM_TFD_CHUNKS) {
		IPW_ERROR("Too many chunks: %i\n", bd->u.data.num_chunks);
		/** @todo issue fatal error, it is quite serious situation */
		return;
	}

	/* unmap chunks if any */
	for (i = 0; i < bd->u.data.num_chunks; i++) {
		pci_unmap_single(dev, bd->u.data.chunk_ptr[i],
				 bd->u.data.chunk_len[i], PCI_DMA_TODEVICE);
		if (txq->txb[txq->q.last_used]) {
			ieee80211_txb_free(txq->txb[txq->q.last_used]);
			txq->txb[txq->q.last_used] = NULL;
		}
	}
}

/**
 * Deallocate DMA queue.
 *
 * Empty queue by removing and destroying all BD's.
 * Free all buffers.
 *
 * @param dev
 * @param q
 */
static void ipw_queue_tx_free(struct ipw_priv *priv, struct clx2_tx_queue *txq)
{
	struct clx2_queue *q = &txq->q;
	struct pci_dev *dev = priv->pci_dev;

	if (q->n_bd == 0)
		return;

	/* first, empty all BD's */
	for (; q->first_empty != q->last_used;
	     q->last_used = ipw_queue_inc_wrap(q->last_used, q->n_bd)) {
		ipw_queue_tx_free_tfd(priv, txq);
	}

	/* free buffers belonging to queue itself */
	pci_free_consistent(dev, sizeof(txq->bd[0]) * q->n_bd, txq->bd,
			    q->dma_addr);
	kfree(txq->txb);

	/* 0 fill whole structure */
	memset(txq, 0, sizeof(*txq));
}

/**
 * Destroy all DMA queues and structures
 *
 * @param priv
 */
static void ipw_tx_queue_free(struct ipw_priv *priv)
{
	/* Tx CMD queue */
	ipw_queue_tx_free(priv, &priv->txq_cmd);

	/* Tx queues */
	ipw_queue_tx_free(priv, &priv->txq[0]);
	ipw_queue_tx_free(priv, &priv->txq[1]);
	ipw_queue_tx_free(priv, &priv->txq[2]);
	ipw_queue_tx_free(priv, &priv->txq[3]);
}

static void inline __maybe_wake_tx(struct ipw_priv *priv)
{
	if (netif_running(priv->net_dev)) {
		switch (priv->port_type) {
		case DCR_TYPE_MU_BSS:
		case DCR_TYPE_MU_IBSS:
			if (!(priv->status & STATUS_ASSOCIATED)) {
				return;
			}
		}
		netif_wake_queue(priv->net_dev);
	}

}

static inline void ipw_create_bssid(struct ipw_priv *priv, u8 * bssid)
{
	/* First 3 bytes are manufacturer */
	bssid[0] = priv->mac_addr[0];
	bssid[1] = priv->mac_addr[1];
	bssid[2] = priv->mac_addr[2];

	/* Last bytes are random */
	get_random_bytes(&bssid[3], ETH_ALEN - 3);

	bssid[0] &= 0xfe;	/* clear multicast bit */
	bssid[0] |= 0x02;	/* set local assignment bit (IEEE802) */
}

static inline u8 ipw_add_station(struct ipw_priv *priv, u8 * bssid)
{
	struct ipw_station_entry entry;
	int i;

	for (i = 0; i < priv->num_stations; i++) {
		if (!memcmp(priv->stations[i], bssid, ETH_ALEN)) {
			/* Another node is active in network */
			priv->missed_adhoc_beacons = 0;
			if (!(priv->config & CFG_STATIC_CHANNEL))
				/* when other nodes drop out, we drop out */
				priv->config &= ~CFG_ADHOC_PERSIST;

			return i;
		}
	}

	if (i == MAX_STATIONS)
		return IPW_INVALID_STATION;

	IPW_DEBUG_SCAN("Adding AdHoc station: " MAC_FMT "\n", MAC_ARG(bssid));

	entry.reserved = 0;
	entry.support_mode = 0;
	memcpy(entry.mac_addr, bssid, ETH_ALEN);
	memcpy(priv->stations[i], bssid, ETH_ALEN);
	ipw_write_direct(priv, IPW_STATION_TABLE_LOWER + i * sizeof(entry),
			 &entry, sizeof(entry));
	priv->num_stations++;

	return i;
}

static inline u8 ipw_find_station(struct ipw_priv *priv, u8 * bssid)
{
	int i;

	for (i = 0; i < priv->num_stations; i++)
		if (!memcmp(priv->stations[i], bssid, ETH_ALEN))
			return i;

	return IPW_INVALID_STATION;
}

static void ipw_send_disassociate(struct ipw_priv *priv, int quiet)
{
	int err;

	if (!(priv->status & (STATUS_ASSOCIATING | STATUS_ASSOCIATED))) {
		IPW_DEBUG_ASSOC("Disassociating while not associated.\n");
		return;
	}

	IPW_DEBUG_ASSOC("Disassocation attempt from " MAC_FMT " "
			"on channel %d.\n",
			MAC_ARG(priv->assoc_request.bssid),
			priv->assoc_request.channel);

	priv->status &= ~(STATUS_ASSOCIATING | STATUS_ASSOCIATED);
	priv->status |= STATUS_DISASSOCIATING;

	if (quiet)
		priv->assoc_request.assoc_type = HC_DISASSOC_QUIET;
	else
		priv->assoc_request.assoc_type = HC_DISASSOCIATE;
	err = ipw_send_associate(priv, &priv->assoc_request);
	if (err) {
		IPW_DEBUG_HC("Attempt to send [dis]associate command "
			     "failed.\n");
		return;
	}

}

static void ipw_disassociate(void *data)
{
	ipw_send_disassociate(data, 0);
}

static void notify_wx_assoc_event(struct ipw_priv *priv)
{
	union iwreq_data wrqu;
	wrqu.ap_addr.sa_family = ARPHRD_ETHER;
	if (priv->status & STATUS_ASSOCIATED)
		memcpy(wrqu.ap_addr.sa_data, priv->bssid, ETH_ALEN);
	else
		memset(wrqu.ap_addr.sa_data, 0, ETH_ALEN);
	wireless_send_event(priv->net_dev, SIOCGIWAP, &wrqu, NULL);
}

struct ipw_status_code {
	u16 status;
	const char *reason;
};

static const struct ipw_status_code ipw_status_codes[] = {
	{0x00, "Successful"},
	{0x01, "Unspecified failure"},
	{0x0A, "Cannot support all requested capabilities in the "
	 "Capability information field"},
	{0x0B, "Reassociation denied due to inability to confirm that "
	 "association exists"},
	{0x0C, "Association denied due to reason outside the scope of this "
	 "standard"},
	{0x0D,
	 "Responding station does not support the specified authentication "
	 "algorithm"},
	{0x0E,
	 "Received an Authentication frame with authentication sequence "
	 "transaction sequence number out of expected sequence"},
	{0x0F, "Authentication rejected because of challenge failure"},
	{0x10, "Authentication rejected due to timeout waiting for next "
	 "frame in sequence"},
	{0x11, "Association denied because AP is unable to handle additional "
	 "associated stations"},
	{0x12,
	 "Association denied due to requesting station not supporting all "
	 "of the datarates in the BSSBasicServiceSet Parameter"},
	{0x13,
	 "Association denied due to requesting station not supporting "
	 "short preamble operation"},
	{0x14,
	 "Association denied due to requesting station not supporting "
	 "PBCC encoding"},
	{0x15,
	 "Association denied due to requesting station not supporting "
	 "channel agility"},
	{0x19,
	 "Association denied due to requesting station not supporting "
	 "short slot operation"},
	{0x1A,
	 "Association denied due to requesting station not supporting "
	 "DSSS-OFDM operation"},
	{0x28, "Invalid Information Element"},
	{0x29, "Group Cipher is not valid"},
	{0x2A, "Pairwise Cipher is not valid"},
	{0x2B, "AKMP is not valid"},
	{0x2C, "Unsupported RSN IE version"},
	{0x2D, "Invalid RSN IE Capabilities"},
	{0x2E, "Cipher suite is rejected per security policy"},
};

#ifdef CONFIG_IPW_DEBUG
static const char *ipw_get_status_code(u16 status)
{
	int i;
	for (i = 0; i < ARRAY_SIZE(ipw_status_codes); i++)
		if (ipw_status_codes[i].status == status)
			return ipw_status_codes[i].reason;
	return "Unknown status value.";
}
#endif

static void inline average_init(struct average *avg)
{
	memset(avg, 0, sizeof(*avg));
}

static void inline average_add(struct average *avg, s16 val)
{
	avg->sum -= avg->entries[avg->pos];
	avg->sum += val;
	avg->entries[avg->pos++] = val;
	if (unlikely(avg->pos == AVG_ENTRIES)) {
		avg->init = 1;
		avg->pos = 0;
	}
}

static s16 inline average_value(struct average *avg)
{
	if (!unlikely(avg->init)) {
		if (avg->pos)
			return avg->sum / avg->pos;
		return 0;
	}

	return avg->sum / AVG_ENTRIES;
}

static void ipw_reset_stats(struct ipw_priv *priv)
{
	u32 len = sizeof(u32);

	priv->quality = 0;

	average_init(&priv->average_missed_beacons);
	average_init(&priv->average_rssi);
	average_init(&priv->average_noise);

	priv->last_rate = 0;
	priv->last_missed_beacons = 0;
	priv->last_rx_packets = 0;
	priv->last_tx_packets = 0;
	priv->last_tx_failures = 0;

	/* Firmware managed, reset only when NIC is restarted, so we have to
	 * normalize on the current value */
	ipw_get_ordinal(priv, IPW_ORD_STAT_RX_ERR_CRC,
			&priv->last_rx_err, &len);
	ipw_get_ordinal(priv, IPW_ORD_STAT_TX_FAILURE,
			&priv->last_tx_failures, &len);

	/* Driver managed, reset with each association */
	priv->missed_adhoc_beacons = 0;
	priv->missed_beacons = 0;
	priv->tx_packets = 0;
	priv->rx_packets = 0;

}

static inline u32 ipw_get_max_rate(struct ipw_priv *priv)
{
	u32 i = 0x80000000;
	u32 mask = priv->rates_mask;
	/* If currently associated in B mode, restrict the maximum
	 * rate match to B rates */
	if (priv->assoc_request.ieee_mode == IPW_B_MODE)
		mask &= IEEE80211_CCK_RATES_MASK;

	/* TODO: Verify that the rate is supported by the current rates
	 * list. */

	while (i && !(mask & i))
		i >>= 1;
	switch (i) {
	case IEEE80211_CCK_RATE_1MB_MASK:	return 1000000;
	case IEEE80211_CCK_RATE_2MB_MASK:	return 2000000;
	case IEEE80211_CCK_RATE_5MB_MASK:	return 5500000;
	case IEEE80211_OFDM_RATE_6MB_MASK:	return 6000000;
	case IEEE80211_OFDM_RATE_9MB_MASK:	return 9000000;
	case IEEE80211_CCK_RATE_11MB_MASK:	return 11000000;
	case IEEE80211_OFDM_RATE_12MB_MASK:	return 12000000;
	case IEEE80211_OFDM_RATE_18MB_MASK:	return 18000000;
	case IEEE80211_OFDM_RATE_24MB_MASK:	return 24000000;
	case IEEE80211_OFDM_RATE_36MB_MASK:	return 36000000;
	case IEEE80211_OFDM_RATE_48MB_MASK:	return 48000000;
	case IEEE80211_OFDM_RATE_54MB_MASK:	return 54000000;
	}

	if (priv->ieee->mode == IEEE_B)
		return 11000000;
	else
		return 54000000;
}

static u32 ipw_get_current_rate(struct ipw_priv *priv)
{
	u32 rate, len = sizeof(rate);
	int err;

	if (!(priv->status & STATUS_ASSOCIATED))
		return 0;

	if (priv->tx_packets > IPW_REAL_RATE_RX_PACKET_THRESHOLD) {
		err = ipw_get_ordinal(priv, IPW_ORD_STAT_TX_CURR_RATE, &rate,
				      &len);
		if (err) {
			IPW_DEBUG_INFO("failed querying ordinals.\n");
			return 0;
		}
	} else
		return ipw_get_max_rate(priv);

	switch (rate) {
	case IPW_TX_RATE_1MB:	return 1000000;
	case IPW_TX_RATE_2MB:	return 2000000;
	case IPW_TX_RATE_5MB:	return 5500000;
	case IPW_TX_RATE_6MB:	return 6000000;
	case IPW_TX_RATE_9MB:	return 9000000;
	case IPW_TX_RATE_11MB:	return 11000000;
	case IPW_TX_RATE_12MB:	return 12000000;
	case IPW_TX_RATE_18MB:	return 18000000;
	case IPW_TX_RATE_24MB:	return 24000000;
	case IPW_TX_RATE_36MB:	return 36000000;
	case IPW_TX_RATE_48MB:	return 48000000;
	case IPW_TX_RATE_54MB:	return 54000000;
	}

	return 0;
}

#define PERFECT_RSSI (-50)
#define WORST_RSSI   (-85)
#define IPW_STATS_INTERVAL (2 * HZ)
static void ipw_gather_stats(struct ipw_priv *priv)
{
	u32 rx_err, rx_err_delta, rx_packets_delta;
	u32 tx_failures, tx_failures_delta, tx_packets_delta;
	u32 missed_beacons_percent, missed_beacons_delta;
	u32 quality = 0;
	u32 len = sizeof(u32);
	s16 rssi;
	u32 beacon_quality, signal_quality, tx_quality, rx_quality,
	    rate_quality;

	if (!(priv->status & STATUS_ASSOCIATED)) {
		priv->quality = 0;
		return;
	}

	/* Update the statistics */
	ipw_get_ordinal(priv, IPW_ORD_STAT_MISSED_BEACONS,
			&priv->missed_beacons, &len);
	missed_beacons_delta = priv->missed_beacons - priv->last_missed_beacons;
	priv->last_missed_beacons = priv->missed_beacons;
	if (priv->assoc_request.beacon_interval) {
		missed_beacons_percent = missed_beacons_delta *
		    (HZ * priv->assoc_request.beacon_interval) /
		    (IPW_STATS_INTERVAL * 10);
	} else {
		missed_beacons_percent = 0;
	}
	average_add(&priv->average_missed_beacons, missed_beacons_percent);

	ipw_get_ordinal(priv, IPW_ORD_STAT_RX_ERR_CRC, &rx_err, &len);
	rx_err_delta = rx_err - priv->last_rx_err;
	priv->last_rx_err = rx_err;

	ipw_get_ordinal(priv, IPW_ORD_STAT_TX_FAILURE, &tx_failures, &len);
	tx_failures_delta = tx_failures - priv->last_tx_failures;
	priv->last_tx_failures = tx_failures;

	rx_packets_delta = priv->rx_packets - priv->last_rx_packets;
	priv->last_rx_packets = priv->rx_packets;

	tx_packets_delta = priv->tx_packets - priv->last_tx_packets;
	priv->last_tx_packets = priv->tx_packets;

	/* Calculate quality based on the following:
	 *
	 * Missed beacon: 100% = 0, 0% = 70% missed
	 * Rate: 60% = 1Mbs, 100% = Max
	 * Rx and Tx errors represent a straight % of total Rx/Tx
	 * RSSI: 100% = > -50,  0% = < -80
	 * Rx errors: 100% = 0, 0% = 50% missed
	 *
	 * The lowest computed quality is used.
	 *
	 */
#define BEACON_THRESHOLD 5
	beacon_quality = 100 - missed_beacons_percent;
	if (beacon_quality < BEACON_THRESHOLD)
		beacon_quality = 0;
	else
		beacon_quality = (beacon_quality - BEACON_THRESHOLD) * 100 /
		    (100 - BEACON_THRESHOLD);
	IPW_DEBUG_STATS("Missed beacon: %3d%% (%d%%)\n",
			beacon_quality, missed_beacons_percent);

	priv->last_rate = ipw_get_current_rate(priv);
	rate_quality = priv->last_rate * 40 / priv->last_rate + 60;
	IPW_DEBUG_STATS("Rate quality : %3d%% (%dMbs)\n",
			rate_quality, priv->last_rate / 1000000);

	if (rx_packets_delta > 100 && rx_packets_delta + rx_err_delta)
		rx_quality = 100 - (rx_err_delta * 100) /
		    (rx_packets_delta + rx_err_delta);
	else
		rx_quality = 100;
	IPW_DEBUG_STATS("Rx quality   : %3d%% (%u errors, %u packets)\n",
			rx_quality, rx_err_delta, rx_packets_delta);

	if (tx_packets_delta > 100 && tx_packets_delta + tx_failures_delta)
		tx_quality = 100 - (tx_failures_delta * 100) /
		    (tx_packets_delta + tx_failures_delta);
	else
		tx_quality = 100;
	IPW_DEBUG_STATS("Tx quality   : %3d%% (%u errors, %u packets)\n",
			tx_quality, tx_failures_delta, tx_packets_delta);

	rssi = average_value(&priv->average_rssi);
	if (rssi > PERFECT_RSSI)
		signal_quality = 100;
	else if (rssi < WORST_RSSI)
		signal_quality = 0;
	else
		signal_quality = (rssi - WORST_RSSI) * 100 /
		    (PERFECT_RSSI - WORST_RSSI);
	IPW_DEBUG_STATS("Signal level : %3d%% (%d dBm)\n",
			signal_quality, rssi);

	quality = min(beacon_quality,
		      min(rate_quality,
			  min(tx_quality, min(rx_quality, signal_quality))));
	if (quality == beacon_quality)
		IPW_DEBUG_STATS("Quality (%d%%): Clamped to missed beacons.\n",
				quality);
	if (quality == rate_quality)
		IPW_DEBUG_STATS("Quality (%d%%): Clamped to rate quality.\n",
				quality);
	if (quality == tx_quality)
		IPW_DEBUG_STATS("Quality (%d%%): Clamped to Tx quality.\n",
				quality);
	if (quality == rx_quality)
		IPW_DEBUG_STATS("Quality (%d%%): Clamped to Rx quality.\n",
				quality);
	if (quality == signal_quality)
		IPW_DEBUG_STATS("Quality (%d%%): Clamped to signal quality.\n",
				quality);

	priv->quality = quality;

	queue_delayed_work(priv->workqueue, &priv->gather_stats,
			   IPW_STATS_INTERVAL);
}

/**
 * Handle host notification packet.
 * Called from interrupt routine
 */
static inline void ipw_rx_notification(struct ipw_priv *priv,
				       struct ipw_rx_notification *notif)
{
	IPW_DEBUG_NOTIF("type = %i (%d bytes)\n", notif->subtype, notif->size);

	switch (notif->subtype) {
	case HOST_NOTIFICATION_STATUS_ASSOCIATED:{
			struct notif_association *assoc = &notif->u.assoc;

			switch (assoc->state) {
			case CMAS_ASSOCIATED:{
					IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
						  IPW_DL_ASSOC,
						  "associated: '%s' " MAC_FMT
						  " \n",
						  escape_essid(priv->essid,
							       priv->essid_len),
						  MAC_ARG(priv->bssid));

					switch (priv->ieee->iw_mode) {
					case IW_MODE_INFRA:
						memcpy(priv->ieee->bssid,
						       priv->bssid, ETH_ALEN);
						break;

					case IW_MODE_ADHOC:
						memcpy(priv->ieee->bssid,
						       priv->bssid, ETH_ALEN);

						/* clear out the station table */
						priv->num_stations = 0;

						IPW_DEBUG_ASSOC
						    ("queueing adhoc check\n");
						queue_delayed_work(priv->
								   workqueue,
								   &priv->
								   adhoc_check,
								   priv->
								   assoc_request.
								   beacon_interval);
						break;
					}

					priv->status &= ~STATUS_ASSOCIATING;
					priv->status |= STATUS_ASSOCIATED;

					netif_carrier_on(priv->net_dev);
					if (netif_queue_stopped(priv->net_dev)) {
						IPW_DEBUG_NOTIF
						    ("waking queue\n");
						netif_wake_queue(priv->net_dev);
					} else {
						IPW_DEBUG_NOTIF
						    ("starting queue\n");
						netif_start_queue(priv->
								  net_dev);
					}

					ipw_reset_stats(priv);
					/* Ensure the rate is updated immediately */
					priv->last_rate =
					    ipw_get_current_rate(priv);
					schedule_work(&priv->gather_stats);
					notify_wx_assoc_event(priv);

/*			queue_delayed_work(priv->workqueue,
					   &priv->request_scan,
					   SCAN_ASSOCIATED_INTERVAL);
*/
					break;
				}

			case CMAS_AUTHENTICATED:{
					if (priv->
					    status & (STATUS_ASSOCIATED |
						      STATUS_AUTH)) {
#ifdef CONFIG_IPW_DEBUG
						struct notif_authenticate *auth
						    = &notif->u.auth;
						IPW_DEBUG(IPW_DL_NOTIF |
							  IPW_DL_STATE |
							  IPW_DL_ASSOC,
							  "deauthenticated: '%s' "
							  MAC_FMT
							  ": (0x%04X) - %s \n",
							  escape_essid(priv->
								       essid,
								       priv->
								       essid_len),
							  MAC_ARG(priv->bssid),
							  ntohs(auth->status),
							  ipw_get_status_code
							  (ntohs
							   (auth->status)));
#endif

						priv->status &=
						    ~(STATUS_ASSOCIATING |
						      STATUS_AUTH |
						      STATUS_ASSOCIATED);

						netif_carrier_off(priv->
								  net_dev);
						netif_stop_queue(priv->net_dev);
						queue_work(priv->workqueue,
							   &priv->request_scan);
						notify_wx_assoc_event(priv);
						break;
					}

					IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
						  IPW_DL_ASSOC,
						  "authenticated: '%s' " MAC_FMT
						  "\n",
						  escape_essid(priv->essid,
							       priv->essid_len),
						  MAC_ARG(priv->bssid));
					break;
				}

			case CMAS_INIT:{
					IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
						  IPW_DL_ASSOC,
						  "disassociated: '%s' " MAC_FMT
						  " \n",
						  escape_essid(priv->essid,
							       priv->essid_len),
						  MAC_ARG(priv->bssid));

					priv->status &=
					    ~(STATUS_DISASSOCIATING |
					      STATUS_ASSOCIATING |
					      STATUS_ASSOCIATED | STATUS_AUTH);

					netif_stop_queue(priv->net_dev);
					if (!(priv->status & STATUS_ROAMING)) {
						netif_carrier_off(priv->
								  net_dev);
						notify_wx_assoc_event(priv);

						/* Cancel any queued work ... */
						cancel_delayed_work(&priv->
								    request_scan);
						cancel_delayed_work(&priv->
								    adhoc_check);

						/* Queue up another scan... */
						queue_work(priv->workqueue,
							   &priv->request_scan);

						cancel_delayed_work(&priv->
								    gather_stats);
					} else {
						priv->status |= STATUS_ROAMING;
						queue_work(priv->workqueue,
							   &priv->request_scan);
					}

					ipw_reset_stats(priv);
					break;
				}

			default:
				IPW_ERROR("assoc: unknown (%d)\n",
					  assoc->state);
				break;
			}

			break;
		}

	case HOST_NOTIFICATION_STATUS_AUTHENTICATE:{
			struct notif_authenticate *auth = &notif->u.auth;
			switch (auth->state) {
			case CMAS_AUTHENTICATED:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE,
					  "authenticated: '%s' " MAC_FMT " \n",
					  escape_essid(priv->essid,
						       priv->essid_len),
					  MAC_ARG(priv->bssid));
				priv->status |= STATUS_AUTH;
				break;

			case CMAS_INIT:
				if (priv->status & STATUS_AUTH) {
					IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
						  IPW_DL_ASSOC,
						  "authentication failed (0x%04X): %s\n",
						  ntohs(auth->status),
						  ipw_get_status_code(ntohs
								      (auth->
								       status)));
				}
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC,
					  "deauthenticated: '%s' " MAC_FMT "\n",
					  escape_essid(priv->essid,
						       priv->essid_len),
					  MAC_ARG(priv->bssid));

				priv->status &= ~(STATUS_ASSOCIATING |
						  STATUS_AUTH |
						  STATUS_ASSOCIATED);

				netif_carrier_off(priv->net_dev);
				netif_stop_queue(priv->net_dev);
				queue_work(priv->workqueue,
					   &priv->request_scan);
				notify_wx_assoc_event(priv);
				break;

			case CMAS_TX_AUTH_SEQ_1:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_1\n");
				break;
			case CMAS_RX_AUTH_SEQ_2:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_2\n");
				break;
			case CMAS_AUTH_SEQ_1_PASS:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_1_PASS\n");
				break;
			case CMAS_AUTH_SEQ_1_FAIL:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_1_FAIL\n");
				break;
			case CMAS_TX_AUTH_SEQ_3:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_3\n");
				break;
			case CMAS_RX_AUTH_SEQ_4:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "RX_AUTH_SEQ_4\n");
				break;
			case CMAS_AUTH_SEQ_2_PASS:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUTH_SEQ_2_PASS\n");
				break;
			case CMAS_AUTH_SEQ_2_FAIL:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "AUT_SEQ_2_FAIL\n");
				break;
			case CMAS_TX_ASSOC:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "TX_ASSOC\n");
				break;
			case CMAS_RX_ASSOC_RESP:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "RX_ASSOC_RESP\n");
				break;
			case CMAS_ASSOCIATED:
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE |
					  IPW_DL_ASSOC, "ASSOCIATED\n");
				break;
			default:
				IPW_DEBUG_NOTIF("auth: failure - %d\n",
						auth->state);
				break;
			}
			break;
		}

	case HOST_NOTIFICATION_STATUS_SCAN_CHANNEL_RESULT:{
			struct notif_channel_result *x =
			    &notif->u.channel_result;

			if (notif->size == sizeof(*x)) {
				IPW_DEBUG_SCAN("Scan result for channel %d\n",
					       x->channel_num);
			} else {
				IPW_DEBUG_SCAN("Scan result of wrong size %d "
					       "(should be %zd)\n",
					       notif->size, sizeof(*x));
			}
			break;
		}

	case HOST_NOTIFICATION_STATUS_SCAN_COMPLETED:{
			struct notif_scan_complete *x = &notif->u.scan_complete;
			if (notif->size == sizeof(*x)) {
				IPW_DEBUG_SCAN
				    ("Scan completed: type %d, %d channels, "
				     "%d status\n", x->scan_type,
				     x->num_channels, x->status);
			} else {
				IPW_ERROR("Scan completed of wrong size %d "
					  "(should be %zd)\n",
					  notif->size, sizeof(*x));
			}

			priv->status &=
			    ~(STATUS_SCANNING | STATUS_SCAN_ABORTING);

			cancel_delayed_work(&priv->scan_check);

			if (!(priv->status & (STATUS_ASSOCIATED |
					      STATUS_ASSOCIATING |
					      STATUS_ROAMING |
					      STATUS_DISASSOCIATING)))
				queue_work(priv->workqueue, &priv->associate);
			else if (priv->status & STATUS_ROAMING) {
				/* If a scan completed and we are in roam mode, then
				 * the scan that completed was the one requested as a
				 * result of entering roam... so, schedule the
				 * roam work */
				queue_work(priv->workqueue, &priv->roam);
			} else if (priv->status & STATUS_SCAN_PENDING)
				queue_work(priv->workqueue,
					   &priv->request_scan);

			priv->ieee->scans++;
			break;
		}

	case HOST_NOTIFICATION_STATUS_FRAG_LENGTH:{
			struct notif_frag_length *x = &notif->u.frag_len;

			if (notif->size == sizeof(*x)) {
				IPW_ERROR("Frag length: %d\n", x->frag_length);
			} else {
				IPW_ERROR("Frag length of wrong size %d "
					  "(should be %zd)\n",
					  notif->size, sizeof(*x));
			}
			break;
		}

	case HOST_NOTIFICATION_STATUS_LINK_DETERIORATION:{
			struct notif_link_deterioration *x =
			    &notif->u.link_deterioration;
			if (notif->size == sizeof(*x)) {
				IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE,
					  "link deterioration: '%s' " MAC_FMT
					  " \n", escape_essid(priv->essid,
							      priv->essid_len),
					  MAC_ARG(priv->bssid));
				memcpy(&priv->last_link_deterioration, x,
				       sizeof(*x));
			} else {
				IPW_ERROR("Link Deterioration of wrong size %d "
					  "(should be %zd)\n",
					  notif->size, sizeof(*x));
			}
			break;
		}

	case HOST_NOTIFICATION_DINO_CONFIG_RESPONSE:{
			IPW_ERROR("Dino config\n");
			if (priv->hcmd
			    && priv->hcmd->cmd == HOST_CMD_DINO_CONFIG) {
				/* TODO: Do anything special? */
			} else {
				IPW_ERROR("Unexpected DINO_CONFIG_RESPONSE\n");
			}
			break;
		}

	case HOST_NOTIFICATION_STATUS_BEACON_STATE:{
			struct notif_beacon_state *x = &notif->u.beacon_state;
			if (notif->size != sizeof(*x)) {
				IPW_ERROR
				    ("Beacon state of wrong size %d (should "
				     "be %zd)\n", notif->size, sizeof(*x));
				break;
			}

			if (x->state == HOST_NOTIFICATION_STATUS_BEACON_MISSING) {
				if (priv->status & STATUS_SCANNING) {
					/* Stop scan to keep fw from getting
					 * stuck... */
					queue_work(priv->workqueue,
						   &priv->abort_scan);
				}

				if (x->number > priv->missed_beacon_threshold &&
				    priv->status & STATUS_ASSOCIATED) {
					IPW_DEBUG(IPW_DL_INFO | IPW_DL_NOTIF |
						  IPW_DL_STATE,
						  "Missed beacon: %d - disassociate\n",
						  x->number);
					queue_work(priv->workqueue,
						   &priv->disassociate);
				} else if (x->number > priv->roaming_threshold) {
					IPW_DEBUG(IPW_DL_NOTIF | IPW_DL_STATE,
						  "Missed beacon: %d - initiate "
						  "roaming\n", x->number);
					queue_work(priv->workqueue,
						   &priv->roam);
				} else {
					IPW_DEBUG_NOTIF("Missed beacon: %d\n",
							x->number);
				}

				priv->notif_missed_beacons = x->number;

			}

			break;
		}

	case HOST_NOTIFICATION_STATUS_TGI_TX_KEY:{
			struct notif_tgi_tx_key *x = &notif->u.tgi_tx_key;
			if (notif->size == sizeof(*x)) {
				IPW_ERROR("TGi Tx Key: state 0x%02x sec type "
					  "0x%02x station %d\n",
					  x->key_state, x->security_type,
					  x->station_index);
				break;
			}

			IPW_ERROR
			    ("TGi Tx Key of wrong size %d (should be %zd)\n",
			     notif->size, sizeof(*x));
			break;
		}

	case HOST_NOTIFICATION_CALIB_KEEP_RESULTS:{
			struct notif_calibration *x = &notif->u.calibration;

			if (notif->size == sizeof(*x)) {
				memcpy(&priv->calib, x, sizeof(*x));
				IPW_DEBUG_INFO("TODO: Calibration\n");
				break;
			}

			IPW_ERROR
			    ("Calibration of wrong size %d (should be %zd)\n",
			     notif->size, sizeof(*x));
			break;
		}

	case HOST_NOTIFICATION_NOISE_STATS:{
			if (notif->size == sizeof(u32)) {
				priv->last_noise =
				    (u8) (notif->u.noise.value & 0xff);
				average_add(&priv->average_noise,
					    priv->last_noise);
				break;
			}

			IPW_ERROR
			    ("Noise stat is wrong size %d (should be %zd)\n",
			     notif->size, sizeof(u32));
			break;
		}

	default:
		IPW_ERROR("Unknown notification: "
			  "subtype=%d,flags=0x%2x,size=%d\n",
			  notif->subtype, notif->flags, notif->size);
	}
}

/**
 * Destroys all DMA structures and initialise them again
 *
 * @param priv
 * @return error code
 */
static int ipw_queue_reset(struct ipw_priv *priv)
{
	int rc = 0;
	/** @todo customize queue sizes */
	int nTx = 64, nTxCmd = 8;
	ipw_tx_queue_free(priv);
	/* Tx CMD queue */
	rc = ipw_queue_tx_init(priv, &priv->txq_cmd, nTxCmd,
			       CX2_TX_CMD_QUEUE_READ_INDEX,
			       CX2_TX_CMD_QUEUE_WRITE_INDEX,
			       CX2_TX_CMD_QUEUE_BD_BASE,
			       CX2_TX_CMD_QUEUE_BD_SIZE);
	if (rc) {
		IPW_ERROR("Tx Cmd queue init failed\n");
		goto error;
	}
	/* Tx queue(s) */
	rc = ipw_queue_tx_init(priv, &priv->txq[0], nTx,
			       CX2_TX_QUEUE_0_READ_INDEX,
			       CX2_TX_QUEUE_0_WRITE_INDEX,
			       CX2_TX_QUEUE_0_BD_BASE, CX2_TX_QUEUE_0_BD_SIZE);
	if (rc) {
		IPW_ERROR("Tx 0 queue init failed\n");
		goto error;
	}
	rc = ipw_queue_tx_init(priv, &priv->txq[1], nTx,
			       CX2_TX_QUEUE_1_READ_INDEX,
			       CX2_TX_QUEUE_1_WRITE_INDEX,
			       CX2_TX_QUEUE_1_BD_BASE, CX2_TX_QUEUE_1_BD_SIZE);
	if (rc) {
		IPW_ERROR("Tx 1 queue init failed\n");
		goto error;
	}
	rc = ipw_queue_tx_init(priv, &priv->txq[2], nTx,
			       CX2_TX_QUEUE_2_READ_INDEX,
			       CX2_TX_QUEUE_2_WRITE_INDEX,
			       CX2_TX_QUEUE_2_BD_BASE, CX2_TX_QUEUE_2_BD_SIZE);
	if (rc) {
		IPW_ERROR("Tx 2 queue init failed\n");
		goto error;
	}
	rc = ipw_queue_tx_init(priv, &priv->txq[3], nTx,
			       CX2_TX_QUEUE_3_READ_INDEX,
			       CX2_TX_QUEUE_3_WRITE_INDEX,
			       CX2_TX_QUEUE_3_BD_BASE, CX2_TX_QUEUE_3_BD_SIZE);
	if (rc) {
		IPW_ERROR("Tx 3 queue init failed\n");
		goto error;
	}
	/* statistics */
	priv->rx_bufs_min = 0;
	priv->rx_pend_max = 0;
	return rc;

      error:
	ipw_tx_queue_free(priv);
	return rc;
}

/**
 * Reclaim Tx queue entries no more used by NIC.
 *
 * When FW adwances 'R' index, all entries between old and
 * new 'R' index need to be reclaimed. As result, some free space
 * forms. If there is enough free space (> low mark), wake Tx queue.
 *
 * @note Need to protect against garbage in 'R' index
 * @param priv
 * @param txq
 * @param qindex
 * @return Number of used entries remains in the queue
 */
static int ipw_queue_tx_reclaim(struct ipw_priv *priv,
				struct clx2_tx_queue *txq, int qindex)
{
	u32 hw_tail;
	int used;
	struct clx2_queue *q = &txq->q;

	hw_tail = ipw_read32(priv, q->reg_r);
	if (hw_tail >= q->n_bd) {
		IPW_ERROR
		    ("Read index for DMA queue (%d) is out of range [0-%d)\n",
		     hw_tail, q->n_bd);
		goto done;
	}
	for (; q->last_used != hw_tail;
	     q->last_used = ipw_queue_inc_wrap(q->last_used, q->n_bd)) {
		ipw_queue_tx_free_tfd(priv, txq);
		priv->tx_packets++;
	}
      done:
	if (ipw_queue_space(q) > q->low_mark && qindex >= 0) {
		__maybe_wake_tx(priv);
	}
	used = q->first_empty - q->last_used;
	if (used < 0)
		used += q->n_bd;

	return used;
}

static int ipw_queue_tx_hcmd(struct ipw_priv *priv, int hcmd, void *buf,
			     int len, int sync)
{
	struct clx2_tx_queue *txq = &priv->txq_cmd;
	struct clx2_queue *q = &txq->q;
	struct tfd_frame *tfd;

	if (ipw_queue_space(q) < (sync ? 1 : 2)) {
		IPW_ERROR("No space for Tx\n");
		return -EBUSY;
	}

	tfd = &txq->bd[q->first_empty];
	txq->txb[q->first_empty] = NULL;

	memset(tfd, 0, sizeof(*tfd));
	tfd->control_flags.message_type = TX_HOST_COMMAND_TYPE;
	tfd->control_flags.control_bits = TFD_NEED_IRQ_MASK;
	priv->hcmd_seq++;
	tfd->u.cmd.index = hcmd;
	tfd->u.cmd.length = len;
	memcpy(tfd->u.cmd.payload, buf, len);
	q->first_empty = ipw_queue_inc_wrap(q->first_empty, q->n_bd);
	ipw_write32(priv, q->reg_w, q->first_empty);
	_ipw_read32(priv, 0x90);

	return 0;
}

/*
 * Rx theory of operation
 *
 * The host allocates 32 DMA target addresses and passes the host address
 * to the firmware at register CX2_RFDS_TABLE_LOWER + N * RFD_SIZE where N is
 * 0 to 31
 *
 * Rx Queue Indexes
 * The host/firmware share two index registers for managing the Rx buffers.
 *
 * The READ index maps to the first position that the firmware may be writing
 * to -- the driver can read up to (but not including) this position and get
 * good data.
 * The READ index is managed by the firmware once the card is enabled.
 *
 * The WRITE index maps to the last position the driver has read from -- the
 * position preceding WRITE is the last slot the firmware can place a packet.
 *
 * The queue is empty (no good data) if WRITE = READ - 1, and is full if
 * WRITE = READ.
 *
 * During initialization the host sets up the READ queue position to the first
 * INDEX position, and WRITE to the last (READ - 1 wrapped)
 *
 * When the firmware places a packet in a buffer it will advance the READ index
 * and fire the RX interrupt.  The driver can then query the READ index and
 * process as many packets as possible, moving the WRITE index forward as it
 * resets the Rx queue buffers with new memory.
 *
 * The management in the driver is as follows:
 * + A list of pre-allocated SKBs is stored in ipw->rxq->rx_free.  When
 *   ipw->rxq->free_count drops to or below RX_LOW_WATERMARK, work is scheduled
 *   to replensish the ipw->rxq->rx_free.
 * + In ipw_rx_queue_replenish (scheduled) if 'processed' != 'read' then the
 *   ipw->rxq is replenished and the READ INDEX is updated (updating the
 *   'processed' and 'read' driver indexes as well)
 * + A received packet is processed and handed to the kernel network stack,
 *   detached from the ipw->rxq.  The driver 'processed' index is updated.
 * + The Host/Firmware ipw->rxq is replenished at tasklet time from the rx_free
 *   list. If there are no allocated buffers in ipw->rxq->rx_free, the READ
 *   INDEX is not incremented and ipw->status(RX_STALLED) is set.  If there
 *   were enough free buffers and RX_STALLED is set it is cleared.
 *
 *
 * Driver sequence:
 *
 * ipw_rx_queue_alloc()       Allocates rx_free
 * ipw_rx_queue_replenish()   Replenishes rx_free list from rx_used, and calls
 *                            ipw_rx_queue_restock
 * ipw_rx_queue_restock()     Moves available buffers from rx_free into Rx
 *                            queue, updates firmware pointers, and updates
 *                            the WRITE index.  If insufficient rx_free buffers
 *                            are available, schedules ipw_rx_queue_replenish
 *
 * -- enable interrupts --
 * ISR - ipw_rx()             Detach ipw_rx_mem_buffers from pool up to the
 *                            READ INDEX, detaching the SKB from the pool.
 *                            Moves the packet buffer from queue to rx_used.
 *                            Calls ipw_rx_queue_restock to refill any empty
 *                            slots.
 * ...
 *
 */

/*
 * If there are slots in the RX queue that  need to be restocked,
 * and we have free pre-allocated buffers, fill the ranks as much
 * as we can pulling from rx_free.
 *
 * This moves the 'write' index forward to catch up with 'processed', and
 * also updates the memory address in the firmware to reference the new
 * target buffer.
 */
static void ipw_rx_queue_restock(struct ipw_priv *priv)
{
	struct ipw_rx_queue *rxq = priv->rxq;
	struct list_head *element;
	struct ipw_rx_mem_buffer *rxb;
	unsigned long flags;
	int write;

	spin_lock_irqsave(&rxq->lock, flags);
	write = rxq->write;
	while ((rxq->write != rxq->processed) && (rxq->free_count)) {
		element = rxq->rx_free.next;
		rxb = list_entry(element, struct ipw_rx_mem_buffer, list);
		list_del(element);

		ipw_write32(priv, CX2_RFDS_TABLE_LOWER + rxq->write * RFD_SIZE,
			    rxb->dma_addr);
		rxq->queue[rxq->write] = rxb;
		rxq->write = (rxq->write + 1) % RX_QUEUE_SIZE;
		rxq->free_count--;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);

	/* If the pre-allocated buffer pool is dropping low, schedule to
	 * refill it */
	if (rxq->free_count <= RX_LOW_WATERMARK)
		queue_work(priv->workqueue, &priv->rx_replenish);

	/* If we've added more space for the firmware to place data, tell it */
	if (write != rxq->write)
		ipw_write32(priv, CX2_RX_WRITE_INDEX, rxq->write);
}

/*
 * Move all used packet from rx_used to rx_free, allocating a new SKB for each.
 * Also restock the Rx queue via ipw_rx_queue_restock.
 *
 * This is called as a scheduled work item (except for during intialization)
 */
static void ipw_rx_queue_replenish(void *data)
{
	struct ipw_priv *priv = data;
	struct ipw_rx_queue *rxq = priv->rxq;
	struct list_head *element;
	struct ipw_rx_mem_buffer *rxb;
	unsigned long flags;

	spin_lock_irqsave(&rxq->lock, flags);
	while (!list_empty(&rxq->rx_used)) {
		element = rxq->rx_used.next;
		rxb = list_entry(element, struct ipw_rx_mem_buffer, list);
		rxb->skb = alloc_skb(CX2_RX_BUF_SIZE, GFP_ATOMIC);
		if (!rxb->skb) {
			printk(KERN_CRIT "%s: Can not allocate SKB buffers.\n",
			       priv->net_dev->name);
			/* We don't reschedule replenish work here -- we will
			 * call the restock method and if it still needs
			 * more buffers it will schedule replenish */
			break;
		}
		list_del(element);

		rxb->rxb = (struct ipw_rx_buffer *)rxb->skb->data;
		rxb->dma_addr =
		    pci_map_single(priv->pci_dev, rxb->skb->data,
				   CX2_RX_BUF_SIZE, PCI_DMA_FROMDEVICE);

		list_add_tail(&rxb->list, &rxq->rx_free);
		rxq->free_count++;
	}
	spin_unlock_irqrestore(&rxq->lock, flags);

	ipw_rx_queue_restock(priv);
}

/* Assumes that the skb field of the buffers in 'pool' is kept accurate.
 * If an SKB has been detached, the POOL needs to have it's SKB set to NULL
 * This free routine walks the list of POOL entries and if SKB is set to
 * non NULL it is unmapped and freed
 */
static void ipw_rx_queue_free(struct ipw_priv *priv, struct ipw_rx_queue *rxq)
{
	int i;

	if (!rxq)
		return;

	for (i = 0; i < RX_QUEUE_SIZE + RX_FREE_BUFFERS; i++) {
		if (rxq->pool[i].skb != NULL) {
			pci_unmap_single(priv->pci_dev, rxq->pool[i].dma_addr,
					 CX2_RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
			dev_kfree_skb(rxq->pool[i].skb);
		}
	}

	kfree(rxq);
}

static struct ipw_rx_queue *ipw_rx_queue_alloc(struct ipw_priv *priv)
{
	struct ipw_rx_queue *rxq;
	int i;

	rxq = (struct ipw_rx_queue *)kmalloc(sizeof(*rxq), GFP_KERNEL);
	if (unlikely(!rxq)) {
		IPW_ERROR("memory allocation failed\n");
		return NULL;
	}
	memset(rxq, 0, sizeof(*rxq));
	spin_lock_init(&rxq->lock);
	INIT_LIST_HEAD(&rxq->rx_free);
	INIT_LIST_HEAD(&rxq->rx_used);

	/* Fill the rx_used queue with _all_ of the Rx buffers */
	for (i = 0; i < RX_FREE_BUFFERS + RX_QUEUE_SIZE; i++)
		list_add_tail(&rxq->pool[i].list, &rxq->rx_used);

	/* Set us so that we have processed and used all buffers, but have
	 * not restocked the Rx queue with fresh buffers */
	rxq->read = rxq->write = 0;
	rxq->processed = RX_QUEUE_SIZE - 1;
	rxq->free_count = 0;

	return rxq;
}

static int ipw_is_rate_in_mask(struct ipw_priv *priv, int ieee_mode, u8 rate)
{
	rate &= ~IEEE80211_BASIC_RATE_MASK;
	if (ieee_mode == IEEE_A) {
		switch (rate) {
		case IEEE80211_OFDM_RATE_6MB:
			return priv->rates_mask & IEEE80211_OFDM_RATE_6MB_MASK ?
			    1 : 0;
		case IEEE80211_OFDM_RATE_9MB:
			return priv->rates_mask & IEEE80211_OFDM_RATE_9MB_MASK ?
			    1 : 0;
		case IEEE80211_OFDM_RATE_12MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_12MB_MASK ? 1 : 0;
		case IEEE80211_OFDM_RATE_18MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_18MB_MASK ? 1 : 0;
		case IEEE80211_OFDM_RATE_24MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_24MB_MASK ? 1 : 0;
		case IEEE80211_OFDM_RATE_36MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_36MB_MASK ? 1 : 0;
		case IEEE80211_OFDM_RATE_48MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_48MB_MASK ? 1 : 0;
		case IEEE80211_OFDM_RATE_54MB:
			return priv->
			    rates_mask & IEEE80211_OFDM_RATE_54MB_MASK ? 1 : 0;
		default:
			return 0;
		}
	}

	/* B and G mixed */
	switch (rate) {
	case IEEE80211_CCK_RATE_1MB:
		return priv->rates_mask & IEEE80211_CCK_RATE_1MB_MASK ? 1 : 0;
	case IEEE80211_CCK_RATE_2MB:
		return priv->rates_mask & IEEE80211_CCK_RATE_2MB_MASK ? 1 : 0;
	case IEEE80211_CCK_RATE_5MB:
		return priv->rates_mask & IEEE80211_CCK_RATE_5MB_MASK ? 1 : 0;
	case IEEE80211_CCK_RATE_11MB:
		return priv->rates_mask & IEEE80211_CCK_RATE_11MB_MASK ? 1 : 0;
	}

	/* If we are limited to B modulations, bail at this point */
	if (ieee_mode == IEEE_B)
		return 0;

	/* G */
	switch (rate) {
	case IEEE80211_OFDM_RATE_6MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_6MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_9MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_9MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_12MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_12MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_18MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_18MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_24MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_24MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_36MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_36MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_48MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_48MB_MASK ? 1 : 0;
	case IEEE80211_OFDM_RATE_54MB:
		return priv->rates_mask & IEEE80211_OFDM_RATE_54MB_MASK ? 1 : 0;
	}

	return 0;
}

static int ipw_compatible_rates(struct ipw_priv *priv,
				const struct ieee80211_network *network,
				struct ipw_supported_rates *rates)
{
	int num_rates, i;

	memset(rates, 0, sizeof(*rates));
	num_rates = min(network->rates_len, (u8) IPW_MAX_RATES);
	rates->num_rates = 0;
	for (i = 0; i < num_rates; i++) {
		if (!ipw_is_rate_in_mask
		    (priv, network->mode, network->rates[i])) {
			IPW_DEBUG_SCAN("Rate %02X masked : 0x%08X\n",
				       network->rates[i], priv->rates_mask);
			continue;
		}

		rates->supported_rates[rates->num_rates++] = network->rates[i];
	}

	num_rates =
	    min(network->rates_ex_len, (u8) (IPW_MAX_RATES - num_rates));
	for (i = 0; i < num_rates; i++) {
		if (!ipw_is_rate_in_mask
		    (priv, network->mode, network->rates_ex[i])) {
			IPW_DEBUG_SCAN("Rate %02X masked : 0x%08X\n",
				       network->rates_ex[i], priv->rates_mask);
			continue;
		}

		rates->supported_rates[rates->num_rates++] =
		    network->rates_ex[i];
	}

	return rates->num_rates;
}

static inline void ipw_copy_rates(struct ipw_supported_rates *dest,
				  const struct ipw_supported_rates *src)
{
	u8 i;
	for (i = 0; i < src->num_rates; i++)
		dest->supported_rates[i] = src->supported_rates[i];
	dest->num_rates = src->num_rates;
}

/* TODO: Look at sniffed packets in the air to determine if the basic rate
 * mask should ever be used -- right now all callers to add the scan rates are
 * set with the modulation = CCK, so BASIC_RATE_MASK is never set... */
static void ipw_add_cck_scan_rates(struct ipw_supported_rates *rates,
				   u8 modulation, u32 rate_mask)
{
	u8 basic_mask = (IEEE80211_OFDM_MODULATION == modulation) ?
	    IEEE80211_BASIC_RATE_MASK : 0;

	if (rate_mask & IEEE80211_CCK_RATE_1MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_1MB;

	if (rate_mask & IEEE80211_CCK_RATE_2MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_BASIC_RATE_MASK | IEEE80211_CCK_RATE_2MB;

	if (rate_mask & IEEE80211_CCK_RATE_5MB_MASK)
		rates->supported_rates[rates->num_rates++] = basic_mask |
		    IEEE80211_CCK_RATE_5MB;

	if (rate_mask & IEEE80211_CCK_RATE_11MB_MASK)
		rates->supported_rates[rates->num_rates++] = basic_mask |
		    IEEE80211_CCK_RATE_11MB;
}

static void ipw_add_ofdm_scan_rates(struct ipw_supported_rates *rates,
				    u8 modulation, u32 rate_mask)
{
	u8 basic_mask = (IEEE80211_OFDM_MODULATION == modulation) ?
	    IEEE80211_BASIC_RATE_MASK : 0;

	if (rate_mask & IEEE80211_OFDM_RATE_6MB_MASK)
		rates->supported_rates[rates->num_rates++] = basic_mask |
		    IEEE80211_OFDM_RATE_6MB;

	if (rate_mask & IEEE80211_OFDM_RATE_9MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_OFDM_RATE_9MB;

	if (rate_mask & IEEE80211_OFDM_RATE_12MB_MASK)
		rates->supported_rates[rates->num_rates++] = basic_mask |
		    IEEE80211_OFDM_RATE_12MB;

	if (rate_mask & IEEE80211_OFDM_RATE_18MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_OFDM_RATE_18MB;

	if (rate_mask & IEEE80211_OFDM_RATE_24MB_MASK)
		rates->supported_rates[rates->num_rates++] = basic_mask |
		    IEEE80211_OFDM_RATE_24MB;

	if (rate_mask & IEEE80211_OFDM_RATE_36MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_OFDM_RATE_36MB;

	if (rate_mask & IEEE80211_OFDM_RATE_48MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_OFDM_RATE_48MB;

	if (rate_mask & IEEE80211_OFDM_RATE_54MB_MASK)
		rates->supported_rates[rates->num_rates++] =
		    IEEE80211_OFDM_RATE_54MB;
}

struct ipw_network_match {
	struct ieee80211_network *network;
	struct ipw_supported_rates rates;
};

static int ipw_best_network(struct ipw_priv *priv,
			    struct ipw_network_match *match,
			    struct ieee80211_network *network, int roaming)
{
	struct ipw_supported_rates rates;

	/* Verify that this network's capability is compatible with the
	 * current mode (AdHoc or Infrastructure) */
	if ((priv->ieee->iw_mode == IW_MODE_INFRA &&
	     !(network->capability & WLAN_CAPABILITY_ESS)) ||
	    (priv->ieee->iw_mode == IW_MODE_ADHOC &&
	     !(network->capability & WLAN_CAPABILITY_IBSS))) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded due to "
				"capability mismatch.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid));
		return 0;
	}

	/* If we do not have an ESSID for this AP, we can not associate with
	 * it */
	if (network->flags & NETWORK_EMPTY_ESSID) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of hidden ESSID.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid));
		return 0;
	}

	if (unlikely(roaming)) {
		/* If we are roaming, then ensure check if this is a valid
		 * network to try and roam to */
		if ((network->ssid_len != match->network->ssid_len) ||
		    memcmp(network->ssid, match->network->ssid,
			   network->ssid_len)) {
			IPW_DEBUG_ASSOC("Netowrk '%s (" MAC_FMT ")' excluded "
					"because of non-network ESSID.\n",
					escape_essid(network->ssid,
						     network->ssid_len),
					MAC_ARG(network->bssid));
			return 0;
		}
	} else {
		/* If an ESSID has been configured then compare the broadcast
		 * ESSID to ours */
		if ((priv->config & CFG_STATIC_ESSID) &&
		    ((network->ssid_len != priv->essid_len) ||
		     memcmp(network->ssid, priv->essid,
			    min(network->ssid_len, priv->essid_len)))) {
			char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
			strncpy(escaped,
				escape_essid(network->ssid, network->ssid_len),
				sizeof(escaped));
			IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
					"because of ESSID mismatch: '%s'.\n",
					escaped, MAC_ARG(network->bssid),
					escape_essid(priv->essid,
						     priv->essid_len));
			return 0;
		}
	}

	/* If the old network rate is better than this one, don't bother
	 * testing everything else. */
	if (match->network && match->network->stats.rssi > network->stats.rssi) {
		char escaped[IW_ESSID_MAX_SIZE * 2 + 1];
		strncpy(escaped,
			escape_essid(network->ssid, network->ssid_len),
			sizeof(escaped));
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded because "
				"'%s (" MAC_FMT ")' has a stronger signal.\n",
				escaped, MAC_ARG(network->bssid),
				escape_essid(match->network->ssid,
					     match->network->ssid_len),
				MAC_ARG(match->network->bssid));
		return 0;
	}

	/* If this network has already had an association attempt within the
	 * last 3 seconds, do not try and associate again... */
	if (network->last_associate &&
	    time_after(network->last_associate + (HZ * 5UL), jiffies)) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of storming (%lu since last "
				"assoc attempt).\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid),
				(jiffies - network->last_associate) / HZ);
		return 0;
	}

	/* Now go through and see if the requested network is valid... */
	if (priv->ieee->scan_age != 0 &&
	    jiffies - network->last_scanned > priv->ieee->scan_age) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of age: %lums.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid),
				(jiffies - network->last_scanned) / (HZ / 100));
		return 0;
	}

	if ((priv->config & CFG_STATIC_CHANNEL) &&
	    (network->channel != priv->channel)) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of channel mismatch: %d != %d.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid),
				network->channel, priv->channel);
		return 0;
	}

	/* Verify privacy compatability */
	if (((priv->capability & CAP_PRIVACY_ON) ? 1 : 0) !=
	    ((network->capability & WLAN_CAPABILITY_PRIVACY) ? 1 : 0)) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of privacy mismatch: %s != %s.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid),
				priv->capability & CAP_PRIVACY_ON ? "on" :
				"off",
				network->capability &
				WLAN_CAPABILITY_PRIVACY ? "on" : "off");
		return 0;
	}

	if ((priv->config & CFG_STATIC_BSSID) &&
	    memcmp(network->bssid, priv->bssid, ETH_ALEN)) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of BSSID mismatch: " MAC_FMT ".\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid), MAC_ARG(priv->bssid));
		return 0;
	}

	/* Filter out any incompatible freq / mode combinations */
	if (!ieee80211_is_valid_mode(priv->ieee, network->mode)) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of invalid frequency/mode "
				"combination.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid));
		return 0;
	}

	ipw_compatible_rates(priv, network, &rates);
	if (rates.num_rates == 0) {
		IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' excluded "
				"because of no compatible rates.\n",
				escape_essid(network->ssid, network->ssid_len),
				MAC_ARG(network->bssid));
		return 0;
	}

	/* TODO: Perform any further minimal comparititive tests.  We do not
	 * want to put too much policy logic here; intelligent scan selection
	 * should occur within a generic IEEE 802.11 user space tool.  */

	/* Set up 'new' AP to this network */
	ipw_copy_rates(&match->rates, &rates);
	match->network = network;

	IPW_DEBUG_ASSOC("Network '%s (" MAC_FMT ")' is a viable match.\n",
			escape_essid(network->ssid, network->ssid_len),
			MAC_ARG(network->bssid));

	return 1;
}

static void ipw_adhoc_create(struct ipw_priv *priv,
			     struct ieee80211_network *network)
{
	/*
	 * For the purposes of scanning, we can set our wireless mode
	 * to trigger scans across combinations of bands, but when it
	 * comes to creating a new ad-hoc network, we have tell the FW
	 * exactly which band to use.
	 *
	 * We also have the possibility of an invalid channel for the
	 * chossen band.  Attempting to create a new ad-hoc network
	 * with an invalid channel for wireless mode will trigger a
	 * FW fatal error.
	 */
	network->mode = is_valid_channel(priv->ieee->mode, priv->channel);
	if (network->mode) {
		network->channel = priv->channel;
	} else {
		IPW_WARNING("Overriding invalid channel\n");
		if (priv->ieee->mode & IEEE_A) {
			network->mode = IEEE_A;
			priv->channel = band_a_active_channel[0];
		} else if (priv->ieee->mode & IEEE_G) {
			network->mode = IEEE_G;
			priv->channel = band_b_active_channel[0];
		} else {
			network->mode = IEEE_B;
			priv->channel = band_b_active_channel[0];
		}
	}

	network->channel = priv->channel;
	priv->config |= CFG_ADHOC_PERSIST;
	ipw_create_bssid(priv, network->bssid);
	network->ssid_len = priv->essid_len;
	memcpy(network->ssid, priv->essid, priv->essid_len);
	memset(&network->stats, 0, sizeof(network->stats));
	network->capability = WLAN_CAPABILITY_IBSS;
	if (priv->capability & CAP_PRIVACY_ON)
		network->capability |= WLAN_CAPABILITY_PRIVACY;
	network->rates_len = min(priv->rates.num_rates, MAX_RATES_LENGTH);
	memcpy(network->rates, priv->rates.supported_rates, network->rates_len);
	network->rates_ex_len = priv->rates.num_rates - network->rates_len;
	memcpy(network->rates_ex,
	       &priv->rates.supported_rates[network->rates_len],
	       network->rates_ex_len);
	network->last_scanned = 0;
	network->flags = 0;
	network->last_associate = 0;
	network->time_stamp[0] = 0;
	network->time_stamp[1] = 0;
	network->beacon_interval = 100;	/* Default */
	network->listen_interval = 10;	/* Default */
	network->atim_window = 0;	/* Default */
#ifdef CONFIG_IEEE80211_WPA
	network->wpa_ie_len = 0;
	network->rsn_ie_len = 0;
#endif				/* CONFIG_IEEE80211_WPA */
}

static void ipw_send_wep_keys(struct ipw_priv *priv)
{
	struct ipw_wep_key *key;
	int i;
	struct host_cmd cmd = {
		.cmd = IPW_CMD_WEP_KEY,
		.len = sizeof(*key)
	};

	key = (struct ipw_wep_key *)&cmd.param;
	key->cmd_id = DINO_CMD_WEP_KEY;
	key->seq_num = 0;

	for (i = 0; i < 4; i++) {
		key->key_index = i;
		if (!(priv->sec.flags & (1 << i))) {
			key->key_size = 0;
		} else {
			key->key_size = priv->sec.key_sizes[i];
			memcpy(key->key, priv->sec.keys[i], key->key_size);
		}

		if (ipw_send_cmd(priv, &cmd)) {
			IPW_ERROR("failed to send WEP_KEY command\n");
			return;
		}
	}
}

static void ipw_adhoc_check(void *data)
{
	struct ipw_priv *priv = data;

	if (priv->missed_adhoc_beacons++ > priv->missed_beacon_threshold &&
	    !(priv->config & CFG_ADHOC_PERSIST)) {
		IPW_DEBUG_SCAN("Disassociating due to missed beacons\n");
		ipw_remove_current_network(priv);
		ipw_disassociate(priv);
		return;
	}

	queue_delayed_work(priv->workqueue, &priv->adhoc_check,
			   priv->assoc_request.beacon_interval);
}

#ifdef CONFIG_IPW_DEBUG
static void ipw_debug_config(struct ipw_priv *priv)
{
	IPW_DEBUG_INFO("Scan completed, no valid APs matched "
		       "[CFG 0x%08X]\n", priv->config);
	if (priv->config & CFG_STATIC_CHANNEL)
		IPW_DEBUG_INFO("Channel locked to %d\n", priv->channel);
	else
		IPW_DEBUG_INFO("Channel unlocked.\n");
	if (priv->config & CFG_STATIC_ESSID)
		IPW_DEBUG_INFO("ESSID locked to '%s'\n",
			       escape_essid(priv->essid, priv->essid_len));
	else
		IPW_DEBUG_INFO("ESSID unlocked.\n");
	if (priv->config & CFG_STATIC_BSSID)
		IPW_DEBUG_INFO("BSSID locked to %d\n", priv->channel);
	else
		IPW_DEBUG_INFO("BSSID unlocked.\n");
	if (priv->capability & CAP_PRIVACY_ON)
		IPW_DEBUG_INFO("PRIVACY on\n");
	else
		IPW_DEBUG_INFO("PRIVACY off\n");
	IPW_DEBUG_INFO("RATE MASK: 0x%08X\n", priv->rates_mask);
}
#else
#define ipw_debug_config(x) do {} while (0)
#endif

static inline void ipw_set_fixed_rate(struct ipw_priv *priv,
				      struct ieee80211_network *network)
{
	/* TODO: Verify that this works... */
	struct ipw_fixed_rate fr = {
		.tx_rates = priv->rates_mask
	};
	u32 reg;
	u16 mask = 0;

	/* Identify 'current FW band' and match it with the fixed
	 * Tx rates */

	switch (priv->ieee->freq_band) {
	case IEEE80211_52GHZ_BAND:	/* A only */
		/* IEEE_A */
		if (priv->rates_mask & ~IEEE80211_OFDM_RATES_MASK) {
			/* Invalid fixed rate mask */
			fr.tx_rates = 0;
			break;
		}

		fr.tx_rates >>= IEEE80211_OFDM_SHIFT_MASK_A;
		break;

	default:		/* 2.4Ghz or Mixed */
		/* IEEE_B */
		if (network->mode == IEEE_B) {
			if (fr.tx_rates & ~IEEE80211_CCK_RATES_MASK) {
				/* Invalid fixed rate mask */
				fr.tx_rates = 0;
			}
			break;
		}

		/* IEEE_G */
		if (fr.tx_rates & ~(IEEE80211_CCK_RATES_MASK |
				    IEEE80211_OFDM_RATES_MASK)) {
			/* Invalid fixed rate mask */
			fr.tx_rates = 0;
			break;
		}

		if (IEEE80211_OFDM_RATE_6MB_MASK & fr.tx_rates) {
			mask |= (IEEE80211_OFDM_RATE_6MB_MASK >> 1);
			fr.tx_rates &= ~IEEE80211_OFDM_RATE_6MB_MASK;
		}

		if (IEEE80211_OFDM_RATE_9MB_MASK & fr.tx_rates) {
			mask |= (IEEE80211_OFDM_RATE_9MB_MASK >> 1);
			fr.tx_rates &= ~IEEE80211_OFDM_RATE_9MB_MASK;
		}

		if (IEEE80211_OFDM_RATE_12MB_MASK & fr.tx_rates) {
			mask |= (IEEE80211_OFDM_RATE_12MB_MASK >> 1);
			fr.tx_rates &= ~IEEE80211_OFDM_RATE_12MB_MASK;
		}

		fr.tx_rates |= mask;
		break;
	}

	reg = ipw_read32(priv, IPW_MEM_FIXED_OVERRIDE);
	ipw_write_reg32(priv, reg, *(u32 *) & fr);
}

static int ipw_associate_network(struct ipw_priv *priv,
				 struct ieee80211_network *network,
				 struct ipw_supported_rates *rates, int roaming)
{
	int err;

	if (priv->config & CFG_FIXED_RATE)
		ipw_set_fixed_rate(priv, network);

	if (!(priv->config & CFG_STATIC_ESSID)) {
		priv->essid_len = min(network->ssid_len,
				      (u8) IW_ESSID_MAX_SIZE);
		memcpy(priv->essid, network->ssid, priv->essid_len);
	}

	network->last_associate = jiffies;

	memset(&priv->assoc_request, 0, sizeof(priv->assoc_request));
	priv->assoc_request.channel = network->channel;
	if ((priv->capability & CAP_PRIVACY_ON) &&
	    (priv->capability & CAP_SHARED_KEY)) {
		priv->assoc_request.auth_type = AUTH_SHARED_KEY;
		priv->assoc_request.auth_key = priv->sec.active_key;
	} else {
		priv->assoc_request.auth_type = AUTH_OPEN;
		priv->assoc_request.auth_key = 0;
	}

	if (priv->capability & CAP_PRIVACY_ON)
		ipw_send_wep_keys(priv);

	/*
	 * It is valid for our ieee device to support multiple modes, but
	 * when it comes to associating to a given network we have to choose
	 * just one mode.
	 */
	if (network->mode & priv->ieee->mode & IEEE_A)
		priv->assoc_request.ieee_mode = IPW_A_MODE;
	else if (network->mode & priv->ieee->mode & IEEE_G)
		priv->assoc_request.ieee_mode = IPW_G_MODE;
	else if (network->mode & priv->ieee->mode & IEEE_B)
		priv->assoc_request.ieee_mode = IPW_B_MODE;

	IPW_DEBUG_ASSOC("%sssocation attempt: '%s', channel %d, "
			"802.11%c [%d], enc=%s%s%s%c%c\n",
			roaming ? "Rea" : "A",
			escape_essid(priv->essid, priv->essid_len),
			network->channel,
			ipw_modes[priv->assoc_request.ieee_mode],
			rates->num_rates,
			priv->capability & CAP_PRIVACY_ON ? "on " : "off",
			priv->capability & CAP_PRIVACY_ON ?
			(priv->capability & CAP_SHARED_KEY ? "(shared)" :
			 "(open)") : "",
			priv->capability & CAP_PRIVACY_ON ? " key=" : "",
			priv->capability & CAP_PRIVACY_ON ?
			'1' + priv->sec.active_key : '.',
			priv->capability & CAP_PRIVACY_ON ? '.' : ' ');

	priv->assoc_request.beacon_interval = network->beacon_interval;
	if ((priv->ieee->iw_mode == IW_MODE_ADHOC) &&
	    (network->time_stamp[0] == 0) && (network->time_stamp[1] == 0)) {
		priv->assoc_request.assoc_type = HC_IBSS_START;
		priv->assoc_request.assoc_tsf_msw = 0;
		priv->assoc_request.assoc_tsf_lsw = 0;
	} else {
		if (unlikely(roaming))
			priv->assoc_request.assoc_type = HC_REASSOCIATE;
		else
			priv->assoc_request.assoc_type = HC_ASSOCIATE;
		priv->assoc_request.assoc_tsf_msw = network->time_stamp[1];
		priv->assoc_request.assoc_tsf_lsw = network->time_stamp[0];
	}

	memcpy(&priv->assoc_request.bssid, network->bssid, ETH_ALEN);

	if (priv->ieee->iw_mode == IW_MODE_ADHOC) {
		memset(&priv->assoc_request.dest, 0xFF, ETH_ALEN);
		priv->assoc_request.atim_window = network->atim_window;
	} else {
		memcpy(&priv->assoc_request.dest, network->bssid, ETH_ALEN);
		priv->assoc_request.atim_window = 0;
	}

	priv->assoc_request.capability = network->capability;
	priv->assoc_request.listen_interval = network->listen_interval;

	err = ipw_send_ssid(priv, priv->essid, priv->essid_len);
	if (err) {
		IPW_DEBUG_HC("Attempt to send SSID command failed.\n");
		return err;
	}

	rates->ieee_mode = priv->assoc_request.ieee_mode;
	rates->purpose = IPW_RATE_CONNECT;
	ipw_send_supported_rates(priv, rates);

	if (priv->assoc_request.ieee_mode == IPW_G_MODE)
		priv->sys_config.dot11g_auto_detection = 1;
	else
		priv->sys_config.dot11g_auto_detection = 0;
	err = ipw_send_system_config(priv, &priv->sys_config);
	if (err) {
		IPW_DEBUG_HC("Attempt to send sys config command failed.\n");
		return err;
	}

	IPW_DEBUG_ASSOC("Association sensitivity: %d\n", network->stats.rssi);
	err = ipw_set_sensitivity(priv, network->stats.rssi);
	if (err) {
		IPW_DEBUG_HC("Attempt to send associate command failed.\n");
		return err;
	}

	/*
	 * If preemption is enabled, it is possible for the association
	 * to complete before we return from ipw_send_associate.  Therefore
	 * we have to be sure and update our priviate data first.
	 */
	priv->channel = network->channel;
	memcpy(priv->bssid, network->bssid, ETH_ALEN);
	priv->status |= STATUS_ASSOCIATING;
	priv->status &= ~STATUS_SECURITY_UPDATED;

	priv->assoc_network = network;

	err = ipw_send_associate(priv, &priv->assoc_request);
	if (err) {
		IPW_DEBUG_HC("Attempt to send associate command failed.\n");
		return err;
	}

	IPW_DEBUG(IPW_DL_STATE, "associating: '%s' " MAC_FMT " \n",
		  escape_essid(priv->essid, priv->essid_len),
		  MAC_ARG(priv->bssid));

	return 0;
}

static void ipw_roam(void *data)
{
	struct ipw_priv *priv = data;
	struct ieee80211_network *network = NULL;
	struct ipw_network_match match = {
		.network = priv->assoc_network
	};

	/* The roaming process is as follows:
	 *
	 * 1.  Missed beacon threshold triggers the roaming process by
	 *     setting the status ROAM bit and requesting a scan.
	 * 2.  When the scan completes, it schedules the ROAM work
	 * 3.  The ROAM work looks at all of the known networks for one that
	 *     is a better network than the currently associated.  If none
	 *     found, the ROAM process is over (ROAM bit cleared)
	 * 4.  If a better network is found, a disassociation request is
	 *     sent.
	 * 5.  When the disassociation completes, the roam work is again
	 *     scheduled.  The second time through, the driver is no longer
	 *     associated, and the newly selected network is sent an
	 *     association request.
	 * 6.  At this point ,the roaming process is complete and the ROAM
	 *     status bit is cleared.
	 */

	/* If we are no longer associated, and the roaming bit is no longer
	 * set, then we are not actively roaming, so just return */
	if (!(priv->status & (STATUS_ASSOCIATED | STATUS_ROAMING)))
		return;

	if (priv->status & STATUS_ASSOCIATED) {
		/* First pass through ROAM process -- look for a better
		 * network */
		u8 rssi = priv->assoc_network->stats.rssi;
		priv->assoc_network->stats.rssi = -128;
		list_for_each_entry(network, &priv->ieee->network_list, list) {
			if (network != priv->assoc_network)
				ipw_best_network(priv, &match, network, 1);
		}
		priv->assoc_network->stats.rssi = rssi;

		if (match.network == priv->assoc_network) {
			IPW_DEBUG_ASSOC("No better APs in this network to "
					"roam to.\n");
			priv->status &= ~STATUS_ROAMING;
			ipw_debug_config(priv);
			return;
		}

		ipw_send_disassociate(priv, 1);
		priv->assoc_network = match.network;

		return;
	}

	/* Second pass through ROAM process -- request association */
	ipw_compatible_rates(priv, priv->assoc_network, &match.rates);
	ipw_associate_network(priv, priv->assoc_network, &match.rates, 1);
	priv->status &= ~STATUS_ROAMING;
}

static void ipw_associate(void *data)
{
	struct ipw_priv *priv = data;

	struct ieee80211_network *network = NULL;
	struct ipw_network_match match = {
		.network = NULL
	};
	struct ipw_supported_rates *rates;
	struct list_head *element;

	if (!(priv->config & CFG_ASSOCIATE) &&
	    !(priv->config & (CFG_STATIC_ESSID |
			      CFG_STATIC_CHANNEL | CFG_STATIC_BSSID))) {
		IPW_DEBUG_ASSOC("Not attempting association (associate=0)\n");
		return;
	}

	list_for_each_entry(network, &priv->ieee->network_list, list)
	    ipw_best_network(priv, &match, network, 0);

	network = match.network;
	rates = &match.rates;

	if (network == NULL &&
	    priv->ieee->iw_mode == IW_MODE_ADHOC &&
	    priv->config & CFG_ADHOC_CREATE &&
	    priv->config & CFG_STATIC_ESSID &&
	    !list_empty(&priv->ieee->network_free_list)) {
		element = priv->ieee->network_free_list.next;
		network = list_entry(element, struct ieee80211_network, list);
		ipw_adhoc_create(priv, network);
		rates = &priv->rates;
		list_del(element);
		list_add_tail(&network->list, &priv->ieee->network_list);
	}

	/* If we reached the end of the list, then we don't have any valid
	 * matching APs */
	if (!network) {
		ipw_debug_config(priv);

		queue_delayed_work(priv->workqueue, &priv->request_scan,
				   SCAN_INTERVAL);

		return;
	}

	ipw_associate_network(priv, network, rates, 0);
}

static inline void ipw_handle_data_packet(struct ipw_priv *priv,
					  struct ipw_rx_mem_buffer *rxb,
					  struct ieee80211_rx_stats *stats)
{
	struct ipw_rx_packet *pkt = (struct ipw_rx_packet *)rxb->skb->data;

	/* We received data from the HW, so stop the watchdog */
	priv->net_dev->trans_start = jiffies;

	/* We only process data packets if the
	 * interface is open */
	if (unlikely((pkt->u.frame.length + IPW_RX_FRAME_SIZE) >
		     skb_tailroom(rxb->skb))) {
		priv->ieee->stats.rx_errors++;
		priv->wstats.discard.misc++;
		IPW_DEBUG_DROP("Corruption detected! Oh no!\n");
		return;
	} else if (unlikely(!netif_running(priv->net_dev))) {
		priv->ieee->stats.rx_dropped++;
		priv->wstats.discard.misc++;
		IPW_DEBUG_DROP("Dropping packet while interface is not up.\n");
		return;
	}

	/* Advance skb->data to the start of the actual payload */
	skb_reserve(rxb->skb, offsetof(struct ipw_rx_packet, u.frame.data));

	/* Set the size of the skb to the size of the frame */
	skb_put(rxb->skb, pkt->u.frame.length);

	IPW_DEBUG_RX("Rx packet of %d bytes.\n", rxb->skb->len);

	if (!ieee80211_rx(priv->ieee, rxb->skb, stats))
		priv->ieee->stats.rx_errors++;
	else			/* ieee80211_rx succeeded, so it now owns the SKB */
		rxb->skb = NULL;
}

/*
 * Main entry function for recieving a packet with 80211 headers.  This
 * should be called when ever the FW has notified us that there is a new
 * skb in the recieve queue.
 */
static void ipw_rx(struct ipw_priv *priv)
{
	struct ipw_rx_mem_buffer *rxb;
	struct ipw_rx_packet *pkt;
	struct ieee80211_hdr_4addr *header;
	u32 r, w, i;
	u8 network_packet;

	r = ipw_read32(priv, CX2_RX_READ_INDEX);
	w = ipw_read32(priv, CX2_RX_WRITE_INDEX);
	i = (priv->rxq->processed + 1) % RX_QUEUE_SIZE;

	while (i != r) {
		rxb = priv->rxq->queue[i];
#ifdef CONFIG_IPW_DEBUG
		if (unlikely(rxb == NULL)) {
			printk(KERN_CRIT "Queue not allocated!\n");
			break;
		}
#endif
		priv->rxq->queue[i] = NULL;

		pci_dma_sync_single_for_cpu(priv->pci_dev, rxb->dma_addr,
					    CX2_RX_BUF_SIZE,
					    PCI_DMA_FROMDEVICE);

		pkt = (struct ipw_rx_packet *)rxb->skb->data;
		IPW_DEBUG_RX("Packet: type=%02X seq=%02X bits=%02X\n",
			     pkt->header.message_type,
			     pkt->header.rx_seq_num, pkt->header.control_bits);

		switch (pkt->header.message_type) {
		case RX_FRAME_TYPE:	/* 802.11 frame */  {
				struct ieee80211_rx_stats stats = {
					.rssi = pkt->u.frame.rssi_dbm -
					    IPW_RSSI_TO_DBM,
					.signal = pkt->u.frame.signal,
					.rate = pkt->u.frame.rate,
					.mac_time = jiffies,
					.received_channel =
					    pkt->u.frame.received_channel,
					.freq =
					    (pkt->u.frame.
					     control & (1 << 0)) ?
					    IEEE80211_24GHZ_BAND :
					    IEEE80211_52GHZ_BAND,
					.len = pkt->u.frame.length,
				};

				if (stats.rssi != 0)
					stats.mask |= IEEE80211_STATMASK_RSSI;
				if (stats.signal != 0)
					stats.mask |= IEEE80211_STATMASK_SIGNAL;
				if (stats.rate != 0)
					stats.mask |= IEEE80211_STATMASK_RATE;

				priv->rx_packets++;

#ifdef CONFIG_IPW_PROMISC
				if (priv->ieee->iw_mode == IW_MODE_MONITOR) {
					ipw_handle_data_packet(priv, rxb,
							       &stats);
					break;
				}
#endif

				header =
				    (struct ieee80211_hdr_4addr *)(rxb->skb->
								   data +
								   IPW_RX_FRAME_SIZE);
				/* TODO: Check Ad-Hoc dest/source and make sure
				 * that we are actually parsing these packets
				 * correctly -- we should probably use the
				 * frame control of the packet and disregard
				 * the current iw_mode */
				switch (priv->ieee->iw_mode) {
				case IW_MODE_ADHOC:
					network_packet =
					    !memcmp(header->addr1,
						    priv->net_dev->dev_addr,
						    ETH_ALEN) ||
					    !memcmp(header->addr3,
						    priv->bssid, ETH_ALEN) ||
					    is_broadcast_ether_addr(header->
								    addr1)
					    || is_multicast_ether_addr(header->
								       addr1);
					break;

				case IW_MODE_INFRA:
				default:
					network_packet =
					    !memcmp(header->addr3,
						    priv->bssid, ETH_ALEN) ||
					    !memcmp(header->addr1,
						    priv->net_dev->dev_addr,
						    ETH_ALEN) ||
					    is_broadcast_ether_addr(header->
								    addr1)
					    || is_multicast_ether_addr(header->
								       addr1);
					break;
				}

				if (network_packet && priv->assoc_network) {
					priv->assoc_network->stats.rssi =
					    stats.rssi;
					average_add(&priv->average_rssi,
						    stats.rssi);
					priv->last_rx_rssi = stats.rssi;
				}

				IPW_DEBUG_RX("Frame: len=%u\n",
					     pkt->u.frame.length);

				if (pkt->u.frame.length < frame_hdr_len(header)) {
					IPW_DEBUG_DROP
					    ("Received packet is too small. "
					     "Dropping.\n");
					priv->ieee->stats.rx_errors++;
					priv->wstats.discard.misc++;
					break;
				}

				switch (WLAN_FC_GET_TYPE(header->frame_ctl)) {
				case IEEE80211_FTYPE_MGMT:
					ieee80211_rx_mgt(priv->ieee, header,
							 &stats);
					if (priv->ieee->iw_mode == IW_MODE_ADHOC
					    &&
					    ((WLAN_FC_GET_STYPE
					      (header->frame_ctl) ==
					      IEEE80211_STYPE_PROBE_RESP)
					     ||
					     (WLAN_FC_GET_STYPE
					      (header->frame_ctl) ==
					      IEEE80211_STYPE_BEACON))
					    && !memcmp(header->addr3,
						       priv->bssid, ETH_ALEN))
						ipw_add_station(priv,
								header->addr2);
					break;

				case IEEE80211_FTYPE_CTL:
					break;

				case IEEE80211_FTYPE_DATA:
					if (network_packet)
						ipw_handle_data_packet(priv,
								       rxb,
								       &stats);
					else
						IPW_DEBUG_DROP("Dropping: "
							       MAC_FMT ", "
							       MAC_FMT ", "
							       MAC_FMT "\n",
							       MAC_ARG(header->
								       addr1),
							       MAC_ARG(header->
								       addr2),
							       MAC_ARG(header->
								       addr3));
					break;
				}
				break;
			}

		case RX_HOST_NOTIFICATION_TYPE:{
				IPW_DEBUG_RX
				    ("Notification: subtype=%02X flags=%02X size=%d\n",
				     pkt->u.notification.subtype,
				     pkt->u.notification.flags,
				     pkt->u.notification.size);
				ipw_rx_notification(priv, &pkt->u.notification);
				break;
			}

		default:
			IPW_DEBUG_RX("Bad Rx packet of type %d\n",
				     pkt->header.message_type);
			break;
		}

		/* For now we just don't re-use anything.  We can tweak this
		 * later to try and re-use notification packets and SKBs that
		 * fail to Rx correctly */
		if (rxb->skb != NULL) {
			dev_kfree_skb_any(rxb->skb);
			rxb->skb = NULL;
		}

		pci_unmap_single(priv->pci_dev, rxb->dma_addr,
				 CX2_RX_BUF_SIZE, PCI_DMA_FROMDEVICE);
		list_add_tail(&rxb->list, &priv->rxq->rx_used);

		i = (i + 1) % RX_QUEUE_SIZE;
	}

	/* Backtrack one entry */
	priv->rxq->processed = (i ? i : RX_QUEUE_SIZE) - 1;

	ipw_rx_queue_restock(priv);
}

static void ipw_abort_scan(struct ipw_priv *priv)
{
	int err;

	if (priv->status & STATUS_SCAN_ABORTING) {
		IPW_DEBUG_HC("Ignoring concurrent scan abort request.\n");
		return;
	}
	priv->status |= STATUS_SCAN_ABORTING;

	err = ipw_send_scan_abort(priv);
	if (err)
		IPW_DEBUG_HC("Request to abort scan failed.\n");
}

static int ipw_request_scan(struct ipw_priv *priv)
{
	struct ipw_scan_request_ext scan;
	int channel_index = 0;
	int i, err, scan_type;

	if (priv->status & STATUS_EXIT_PENDING) {
		IPW_DEBUG_SCAN("Aborting scan due to device shutdown\n");
		priv->status |= STATUS_SCAN_PENDING;
		return 0;
	}

	if (priv->status & STATUS_SCANNING) {
		IPW_DEBUG_HC("Concurrent scan requested.  Aborting first.\n");
		priv->status |= STATUS_SCAN_PENDING;
		ipw_abort_scan(priv);
		return 0;
	}

	if (priv->status & STATUS_SCAN_ABORTING) {
		IPW_DEBUG_HC("Scan request while abort pending.  Queuing.\n");
		priv->status |= STATUS_SCAN_PENDING;
		return 0;
	}

	if (priv->status & STATUS_RF_KILL_MASK) {
		IPW_DEBUG_HC("Aborting scan due to RF Kill activation\n");
		priv->status |= STATUS_SCAN_PENDING;
		return 0;
	}

	memset(&scan, 0, sizeof(scan));

	scan.dwell_time[IPW_SCAN_ACTIVE_BROADCAST_SCAN] = 20;
	scan.dwell_time[IPW_SCAN_ACTIVE_BROADCAST_AND_DIRECT_SCAN] = 20;
	scan.dwell_time[IPW_SCAN_PASSIVE_FULL_DWELL_SCAN] = 20;

	scan.full_scan_index = ieee80211_get_scans(priv->ieee);
	/* If we are roaming, then make this a directed scan for the current
	 * network.  Otherwise, ensure that every other scan is a fast
	 * channel hop scan */
	if ((priv->status & STATUS_ROAMING)
	    || (!(priv->status & STATUS_ASSOCIATED)
		&& (priv->config & CFG_STATIC_ESSID)
		&& (scan.full_scan_index % 2))) {
		err = ipw_send_ssid(priv, priv->essid, priv->essid_len);
		if (err) {
			IPW_DEBUG_HC("Attempt to send SSID command failed.\n");
			return err;
		}

		scan_type = IPW_SCAN_ACTIVE_BROADCAST_AND_DIRECT_SCAN;
	} else {
		scan_type = IPW_SCAN_ACTIVE_BROADCAST_SCAN;
	}

	if (priv->ieee->freq_band & IEEE80211_52GHZ_BAND) {
		int start = channel_index;
		for (i = 0; i < MAX_A_CHANNELS; i++) {
			if (band_a_active_channel[i] == 0)
				break;
			if ((priv->status & STATUS_ASSOCIATED) &&
			    band_a_active_channel[i] == priv->channel)
				continue;
			channel_index++;
			scan.channels_list[channel_index] =
			    band_a_active_channel[i];
			ipw_set_scan_type(&scan, channel_index, scan_type);
		}

		if (start != channel_index) {
			scan.channels_list[start] = (u8) (IPW_A_MODE << 6) |
			    (channel_index - start);
			channel_index++;
		}
	}

	if (priv->ieee->freq_band & IEEE80211_24GHZ_BAND) {
		int start = channel_index;
		for (i = 0; i < MAX_B_CHANNELS; i++) {
			if (band_b_active_channel[i] == 0)
				break;
			if ((priv->status & STATUS_ASSOCIATED) &&
			    band_b_active_channel[i] == priv->channel)
				continue;
			channel_index++;
			scan.channels_list[channel_index] =
			    band_b_active_channel[i];
			ipw_set_scan_type(&scan, channel_index, scan_type);
		}

		if (start != channel_index) {
			scan.channels_list[start] = (u8) (IPW_B_MODE << 6) |
			    (channel_index - start);
		}
	}

	err = ipw_send_scan_request_ext(priv, &scan);
	if (err) {
		IPW_DEBUG_HC("Sending scan command failed: %08X\n", err);
		return -EIO;
	}

	priv->status |= STATUS_SCANNING;
	priv->status &= ~STATUS_SCAN_PENDING;

	return 0;
}

/*
 * This file defines the Wireless Extension handlers.  It does not
 * define any methods of hardware manipulation and relies on the
 * functions defined in ipw_main to provide the HW interaction.
 *
 * The exception to this is the use of the ipw_get_ordinal()
 * function used to poll the hardware vs. making unecessary calls.
 *
 */

static int ipw_wx_get_name(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	if (!(priv->status & STATUS_ASSOCIATED))
		strcpy(wrqu->name, "unassociated");
	else
		snprintf(wrqu->name, IFNAMSIZ, "IEEE 802.11%c",
			 ipw_modes[priv->assoc_request.ieee_mode]);
	IPW_DEBUG_WX("Name: %s\n", wrqu->name);
	return 0;
}

static int ipw_set_channel(struct ipw_priv *priv, u8 channel)
{
	if (channel == 0) {
		IPW_DEBUG_INFO("Setting channel to ANY (0)\n");
		priv->config &= ~CFG_STATIC_CHANNEL;
		if (!(priv->status & (STATUS_SCANNING | STATUS_ASSOCIATED |
				      STATUS_ASSOCIATING))) {
			IPW_DEBUG_ASSOC("Attempting to associate with new "
					"parameters.\n");
			ipw_associate(priv);
		}

		return 0;
	}

	priv->config |= CFG_STATIC_CHANNEL;

	if (priv->channel == channel) {
		IPW_DEBUG_INFO("Request to set channel to current value (%d)\n",
			       channel);
		return 0;
	}

	IPW_DEBUG_INFO("Setting channel to %i\n", (int)channel);
	priv->channel = channel;

	/* If we are currently associated, or trying to associate
	 * then see if this is a new channel (causing us to disassociate) */
	if (priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		IPW_DEBUG_ASSOC("Disassociating due to channel change.\n");
		ipw_disassociate(priv);
	} else {
		ipw_associate(priv);
	}

	return 0;
}

static int ipw_wx_set_freq(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	struct iw_freq *fwrq = &wrqu->freq;

	/* if setting by freq convert to channel */
	if (fwrq->e == 1) {
		if ((fwrq->m >= (int)2.412e8 && fwrq->m <= (int)2.487e8)) {
			int f = fwrq->m / 100000;
			int c = 0;

			while ((c < REG_MAX_CHANNEL) &&
			       (f != ipw_frequencies[c]))
				c++;

			/* hack to fall through */
			fwrq->e = 0;
			fwrq->m = c + 1;
		}
	}

	if (fwrq->e > 0 || fwrq->m > 1000)
		return -EOPNOTSUPP;

	IPW_DEBUG_WX("SET Freq/Channel -> %d \n", fwrq->m);
	return ipw_set_channel(priv, (u8) fwrq->m);
}

static int ipw_wx_get_freq(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	wrqu->freq.e = 0;

	/* If we are associated, trying to associate, or have a statically
	 * configured CHANNEL then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_CHANNEL ||
	    priv->status & (STATUS_ASSOCIATING | STATUS_ASSOCIATED))
		wrqu->freq.m = priv->channel;
	else
		wrqu->freq.m = 0;

	IPW_DEBUG_WX("GET Freq/Channel -> %d \n", priv->channel);
	return 0;
}

static int ipw_wx_set_mode(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int err = 0;

	IPW_DEBUG_WX("Set MODE: %d\n", wrqu->mode);

	if (wrqu->mode == priv->ieee->iw_mode)
		return 0;

	switch (wrqu->mode) {
#ifdef CONFIG_IPW_PROMISC
	case IW_MODE_MONITOR:
#endif
	case IW_MODE_ADHOC:
	case IW_MODE_INFRA:
		break;
	case IW_MODE_AUTO:
		wrqu->mode = IW_MODE_INFRA;
		break;
	default:
		return -EINVAL;
	}

#ifdef CONFIG_IPW_PROMISC
	if (priv->ieee->iw_mode == IW_MODE_MONITOR)
		priv->net_dev->type = ARPHRD_ETHER;

	if (wrqu->mode == IW_MODE_MONITOR)
		priv->net_dev->type = ARPHRD_IEEE80211;
#endif				/* CONFIG_IPW_PROMISC */

#ifdef CONFIG_PM
	/* Free the existing firmware and reset the fw_loaded
	 * flag so ipw_load() will bring in the new firmawre */
	if (fw_loaded) {
		fw_loaded = 0;
	}

	release_firmware(bootfw);
	release_firmware(ucode);
	release_firmware(firmware);
	bootfw = ucode = firmware = NULL;
#endif

	priv->ieee->iw_mode = wrqu->mode;
	ipw_adapter_restart(priv);

	return err;
}

static int ipw_wx_get_mode(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	wrqu->mode = priv->ieee->iw_mode;
	IPW_DEBUG_WX("Get MODE -> %d\n", wrqu->mode);

	return 0;
}

#define DEFAULT_RTS_THRESHOLD     2304U
#define MIN_RTS_THRESHOLD         1U
#define MAX_RTS_THRESHOLD         2304U
#define DEFAULT_BEACON_INTERVAL   100U
#define	DEFAULT_SHORT_RETRY_LIMIT 7U
#define	DEFAULT_LONG_RETRY_LIMIT  4U

/* Values are in microsecond */
static const s32 timeout_duration[] = {
	350000,
	250000,
	75000,
	37000,
	25000,
};

static const s32 period_duration[] = {
	400000,
	700000,
	1000000,
	1000000,
	1000000
};

static int ipw_wx_get_range(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	struct iw_range *range = (struct iw_range *)extra;
	u16 val;
	int i;

	wrqu->data.length = sizeof(*range);
	memset(range, 0, sizeof(*range));

	/* 54Mbs == ~27 Mb/s real (802.11g) */
	range->throughput = 27 * 1000 * 1000;

	range->max_qual.qual = 100;
	/* TODO: Find real max RSSI and stick here */
	range->max_qual.level = 0;
	range->max_qual.noise = 0;
	range->max_qual.updated = 7;	/* Updated all three */

	range->avg_qual.qual = 70;
	/* TODO: Find real 'good' to 'bad' threshol value for RSSI */
	range->avg_qual.level = 0;	/* FIXME to real average level */
	range->avg_qual.noise = 0;
	range->avg_qual.updated = 7;	/* Updated all three */

	range->num_bitrates = min(priv->rates.num_rates, (u8) IW_MAX_BITRATES);

	for (i = 0; i < range->num_bitrates; i++)
		range->bitrate[i] = (priv->rates.supported_rates[i] & 0x7F) *
		    500000;

	range->max_rts = DEFAULT_RTS_THRESHOLD;
	range->min_frag = MIN_FRAG_THRESHOLD;
	range->max_frag = MAX_FRAG_THRESHOLD;

	range->encoding_size[0] = 5;
	range->encoding_size[1] = 13;
	range->num_encoding_sizes = 2;
	range->max_encoding_tokens = WEP_KEYS;

	/* Set the Wireless Extension versions */
	range->we_version_compiled = WIRELESS_EXT;
	range->we_version_source = 16;

	range->num_channels = FREQ_COUNT;

	val = 0;
	for (i = 0; i < FREQ_COUNT; i++) {
		range->freq[val].i = i + 1;
		range->freq[val].m = ipw_frequencies[i] * 100000;
		range->freq[val].e = 1;
		val++;

		if (val == IW_MAX_FREQUENCIES)
			break;
	}
	range->num_frequency = val;

	IPW_DEBUG_WX("GET Range\n");
	return 0;
}

static int ipw_wx_set_wap(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	static const unsigned char any[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff
	};
	static const unsigned char off[] = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00
	};

	if (wrqu->ap_addr.sa_family != ARPHRD_ETHER)
		return -EINVAL;

	if (!memcmp(any, wrqu->ap_addr.sa_data, ETH_ALEN) ||
	    !memcmp(off, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		/* we disable mandatory BSSID association */
		IPW_DEBUG_WX("Setting AP BSSID to ANY\n");
		priv->config &= ~CFG_STATIC_BSSID;
		if (!(priv->status & (STATUS_SCANNING | STATUS_ASSOCIATED |
				      STATUS_ASSOCIATING))) {
			IPW_DEBUG_ASSOC("Attempting to associate with new "
					"parameters.\n");
			ipw_associate(priv);
		}

		return 0;
	}

	priv->config |= CFG_STATIC_BSSID;
	if (!memcmp(priv->bssid, wrqu->ap_addr.sa_data, ETH_ALEN)) {
		IPW_DEBUG_WX("BSSID set to current BSSID.\n");
		return 0;
	}

	IPW_DEBUG_WX("Setting mandatory BSSID to " MAC_FMT "\n",
		     MAC_ARG(wrqu->ap_addr.sa_data));

	memcpy(priv->bssid, wrqu->ap_addr.sa_data, ETH_ALEN);

	/* If we are currently associated, or trying to associate
	 * then see if this is a new BSSID (causing us to disassociate) */
	if (priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		IPW_DEBUG_ASSOC("Disassociating due to BSSID change.\n");
		ipw_disassociate(priv);
	} else {
		ipw_associate(priv);
	}

	return 0;
}

static int ipw_wx_get_wap(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	/* If we are associated, trying to associate, or have a statically
	 * configured BSSID then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_BSSID ||
	    priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		wrqu->ap_addr.sa_family = ARPHRD_ETHER;
		memcpy(wrqu->ap_addr.sa_data, &priv->bssid, ETH_ALEN);
	} else
		memset(wrqu->ap_addr.sa_data, 0, ETH_ALEN);

	IPW_DEBUG_WX("Getting WAP BSSID: " MAC_FMT "\n",
		     MAC_ARG(wrqu->ap_addr.sa_data));
	return 0;
}

static int ipw_wx_set_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	char *essid = "";	/* ANY */
	int length = 0;

	if (wrqu->essid.flags && wrqu->essid.length) {
		length = wrqu->essid.length - 1;
		essid = extra;
	}
	if (length == 0) {
		IPW_DEBUG_WX("Setting ESSID to ANY\n");
		priv->config &= ~CFG_STATIC_ESSID;
		if (!(priv->status & (STATUS_SCANNING | STATUS_ASSOCIATED |
				      STATUS_ASSOCIATING))) {
			IPW_DEBUG_ASSOC("Attempting to associate with new "
					"parameters.\n");
			ipw_associate(priv);
		}

		return 0;
	}

	length = min(length, IW_ESSID_MAX_SIZE);

	priv->config |= CFG_STATIC_ESSID;

	if (priv->essid_len == length && !memcmp(priv->essid, extra, length)) {
		IPW_DEBUG_WX("ESSID set to current ESSID.\n");
		return 0;
	}

	IPW_DEBUG_WX("Setting ESSID: '%s' (%d)\n", escape_essid(essid, length),
		     length);

	priv->essid_len = length;
	memcpy(priv->essid, essid, priv->essid_len);

	/* If we are currently associated, or trying to associate
	 * then see if this is a new ESSID (causing us to disassociate) */
	if (priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		IPW_DEBUG_ASSOC("Disassociating due to ESSID change.\n");
		ipw_disassociate(priv);
	} else {
		ipw_associate(priv);
	}

	return 0;
}

static int ipw_wx_get_essid(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	/* If we are associated, trying to associate, or have a statically
	 * configured ESSID then return that; otherwise return ANY */
	if (priv->config & CFG_STATIC_ESSID ||
	    priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		IPW_DEBUG_WX("Getting essid: '%s'\n",
			     escape_essid(priv->essid, priv->essid_len));
		memcpy(extra, priv->essid, priv->essid_len);
		wrqu->essid.length = priv->essid_len;
		wrqu->essid.flags = 1;	/* active */
	} else {
		IPW_DEBUG_WX("Getting essid: ANY\n");
		wrqu->essid.length = 0;
		wrqu->essid.flags = 0;	/* active */
	}

	return 0;
}

static int ipw_wx_set_nick(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	IPW_DEBUG_WX("Setting nick to '%s'\n", extra);
	if (wrqu->data.length > IW_ESSID_MAX_SIZE)
		return -E2BIG;

	wrqu->data.length = min((size_t) wrqu->data.length, sizeof(priv->nick));
	memset(priv->nick, 0, sizeof(priv->nick));
	memcpy(priv->nick, extra, wrqu->data.length);
	IPW_DEBUG_TRACE("<<\n");
	return 0;

}

static int ipw_wx_get_nick(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	IPW_DEBUG_WX("Getting nick\n");
	wrqu->data.length = strlen(priv->nick) + 1;
	memcpy(extra, priv->nick, wrqu->data.length);
	wrqu->data.flags = 1;	/* active */
	return 0;
}

static int ipw_wx_set_rate(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	IPW_DEBUG_WX("0x%p, 0x%p, 0x%p\n", dev, info, wrqu);
	return -EOPNOTSUPP;
}

static int ipw_wx_get_rate(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	wrqu->bitrate.value = priv->last_rate;

	IPW_DEBUG_WX("GET Rate -> %d \n", wrqu->bitrate.value);
	return 0;
}

static int ipw_wx_set_rts(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	if (wrqu->rts.disabled)
		priv->rts_threshold = DEFAULT_RTS_THRESHOLD;
	else {
		if (wrqu->rts.value < MIN_RTS_THRESHOLD ||
		    wrqu->rts.value > MAX_RTS_THRESHOLD)
			return -EINVAL;

		priv->rts_threshold = wrqu->rts.value;
	}

	ipw_send_rts_threshold(priv, priv->rts_threshold);
	IPW_DEBUG_WX("SET RTS Threshold -> %d \n", priv->rts_threshold);
	return 0;
}

static int ipw_wx_get_rts(struct net_device *dev,
			  struct iw_request_info *info,
			  union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	wrqu->rts.value = priv->rts_threshold;
	wrqu->rts.fixed = 0;	/* no auto select */
	wrqu->rts.disabled = (wrqu->rts.value == DEFAULT_RTS_THRESHOLD);

	IPW_DEBUG_WX("GET RTS Threshold -> %d \n", wrqu->rts.value);
	return 0;
}

static int ipw_wx_set_txpow(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	struct ipw_tx_power tx_power;
	int i;

	if (ipw_radio_kill_sw(priv, wrqu->power.disabled))
		return -EINPROGRESS;

	if (wrqu->power.flags != IW_TXPOW_DBM)
		return -EINVAL;

	if ((wrqu->power.value > 20) || (wrqu->power.value < -12))
		return -EINVAL;

	priv->tx_power = wrqu->power.value;

	memset(&tx_power, 0, sizeof(tx_power));

	/* configure device for 'G' band */
	tx_power.ieee_mode = IPW_G_MODE;
	tx_power.num_channels = 11;
	for (i = 0; i < 11; i++) {
		tx_power.channels_tx_power[i].channel_number = i + 1;
		tx_power.channels_tx_power[i].tx_power = priv->tx_power;
	}
	if (ipw_send_tx_power(priv, &tx_power))
		goto error;

	/* configure device to also handle 'B' band */
	tx_power.ieee_mode = IPW_B_MODE;
	if (ipw_send_tx_power(priv, &tx_power))
		goto error;

	return 0;

      error:
	return -EIO;
}

static int ipw_wx_get_txpow(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	wrqu->power.value = priv->tx_power;
	wrqu->power.fixed = 1;
	wrqu->power.flags = IW_TXPOW_DBM;
	wrqu->power.disabled = (priv->status & STATUS_RF_KILL_MASK) ? 1 : 0;

	IPW_DEBUG_WX("GET TX Power -> %s %d \n",
		     wrqu->power.disabled ? "ON" : "OFF", wrqu->power.value);

	return 0;
}

static int ipw_wx_set_frag(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	if (wrqu->frag.disabled)
		priv->ieee->fts = DEFAULT_FTS;
	else {
		if (wrqu->frag.value < MIN_FRAG_THRESHOLD ||
		    wrqu->frag.value > MAX_FRAG_THRESHOLD)
			return -EINVAL;

		priv->ieee->fts = wrqu->frag.value & ~0x1;
	}

	ipw_send_frag_threshold(priv, wrqu->frag.value);
	IPW_DEBUG_WX("SET Frag Threshold -> %d \n", wrqu->frag.value);
	return 0;
}

static int ipw_wx_get_frag(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	wrqu->frag.value = priv->ieee->fts;
	wrqu->frag.fixed = 0;	/* no auto select */
	wrqu->frag.disabled = (wrqu->frag.value == DEFAULT_FTS);

	IPW_DEBUG_WX("GET Frag Threshold -> %d \n", wrqu->frag.value);

	return 0;
}

static int ipw_wx_set_retry(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	IPW_DEBUG_WX("0x%p, 0x%p, 0x%p\n", dev, info, wrqu);
	return -EOPNOTSUPP;
}

static int ipw_wx_get_retry(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	IPW_DEBUG_WX("0x%p, 0x%p, 0x%p\n", dev, info, wrqu);
	return -EOPNOTSUPP;
}

static int ipw_wx_set_scan(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	IPW_DEBUG_WX("Start scan\n");
	if (ipw_request_scan(priv))
		return -EIO;
	return 0;
}

static int ipw_wx_get_scan(struct net_device *dev,
			   struct iw_request_info *info,
			   union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	return ieee80211_wx_get_scan(priv->ieee, info, wrqu, extra);
}

static int ipw_wx_set_encode(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *key)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	return ieee80211_wx_set_encode(priv->ieee, info, wrqu, key);
}

static int ipw_wx_get_encode(struct net_device *dev,
			     struct iw_request_info *info,
			     union iwreq_data *wrqu, char *key)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	return ieee80211_wx_get_encode(priv->ieee, info, wrqu, key);
}

static int ipw_wx_set_power(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int err;

	if (wrqu->power.disabled) {
		priv->power_mode = IPW_POWER_LEVEL(priv->power_mode);
		err = ipw_send_power_mode(priv, IPW_POWER_MODE_CAM);
		if (err) {
			IPW_DEBUG_WX("failed setting power mode.\n");
			return err;
		}

		IPW_DEBUG_WX("SET Power Management Mode -> off\n");

		return 0;
	}

	switch (wrqu->power.flags & IW_POWER_MODE) {
	case IW_POWER_ON:	/* If not specified */
	case IW_POWER_MODE:	/* If set all mask */
	case IW_POWER_ALL_R:	/* If explicitely state all */
		break;
	default:		/* Otherwise we don't support it */
		IPW_DEBUG_WX("SET PM Mode: %X not supported.\n",
			     wrqu->power.flags);
		return -EOPNOTSUPP;
	}

	/* If the user hasn't specified a power management mode yet, default
	 * to BATTERY */
	if (IPW_POWER_LEVEL(priv->power_mode) == IPW_POWER_AC)
		priv->power_mode = IPW_POWER_ENABLED | IPW_POWER_BATTERY;
	else
		priv->power_mode = IPW_POWER_ENABLED | priv->power_mode;
	err = ipw_send_power_mode(priv, IPW_POWER_LEVEL(priv->power_mode));
	if (err) {
		IPW_DEBUG_WX("failed setting power mode.\n");
		return err;
	}

	IPW_DEBUG_WX("SET Power Management Mode -> 0x%02X\n", priv->power_mode);

	return 0;
}

static int ipw_wx_get_power(struct net_device *dev,
			    struct iw_request_info *info,
			    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	if (!(priv->power_mode & IPW_POWER_ENABLED)) {
		wrqu->power.disabled = 1;
	} else {
		wrqu->power.disabled = 0;
	}

	IPW_DEBUG_WX("GET Power Management Mode -> %02X\n", priv->power_mode);

	return 0;
}

static int ipw_wx_set_powermode(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int mode = *(int *)extra;
	int err;

	if ((mode < 1) || (mode > IPW_POWER_LIMIT)) {
		mode = IPW_POWER_AC;
		priv->power_mode = mode;
	} else {
		priv->power_mode = IPW_POWER_ENABLED | mode;
	}

	if (priv->power_mode != mode) {
		err = ipw_send_power_mode(priv, mode);

		if (err) {
			IPW_DEBUG_WX("failed setting power mode.\n");
			return err;
		}
	}

	return 0;
}

#define MAX_WX_STRING 80
static int ipw_wx_get_powermode(struct net_device *dev,
				struct iw_request_info *info,
				union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int level = IPW_POWER_LEVEL(priv->power_mode);
	char *p = extra;

	p += snprintf(p, MAX_WX_STRING, "Power save level: %d ", level);

	switch (level) {
	case IPW_POWER_AC:
		p += snprintf(p, MAX_WX_STRING - (p - extra), "(AC)");
		break;
	case IPW_POWER_BATTERY:
		p += snprintf(p, MAX_WX_STRING - (p - extra), "(BATTERY)");
		break;
	default:
		p += snprintf(p, MAX_WX_STRING - (p - extra),
			      "(Timeout %dms, Period %dms)",
			      timeout_duration[level - 1] / 1000,
			      period_duration[level - 1] / 1000);
	}

	if (!(priv->power_mode & IPW_POWER_ENABLED))
		p += snprintf(p, MAX_WX_STRING - (p - extra), " OFF");

	wrqu->data.length = p - extra + 1;

	return 0;
}

static int ipw_wx_set_wireless_mode(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int mode = *(int *)extra;
	u8 band = 0, modulation = 0;

	if (mode == 0 || mode & ~IEEE_MODE_MASK) {
		IPW_WARNING("Attempt to set invalid wireless mode: %d\n", mode);
		return -EINVAL;
	}

	if (priv->adapter == IPW_2915ABG) {
		priv->ieee->abg_true = 1;
		if (mode & IEEE_A) {
			band |= IEEE80211_52GHZ_BAND;
			modulation |= IEEE80211_OFDM_MODULATION;
		} else
			priv->ieee->abg_true = 0;
	} else {
		if (mode & IEEE_A) {
			IPW_WARNING("Attempt to set 2200BG into "
				    "802.11a mode\n");
			return -EINVAL;
		}

		priv->ieee->abg_true = 0;
	}

	if (mode & IEEE_B) {
		band |= IEEE80211_24GHZ_BAND;
		modulation |= IEEE80211_CCK_MODULATION;
	} else
		priv->ieee->abg_true = 0;

	if (mode & IEEE_G) {
		band |= IEEE80211_24GHZ_BAND;
		modulation |= IEEE80211_OFDM_MODULATION;
	} else
		priv->ieee->abg_true = 0;

	priv->ieee->mode = mode;
	priv->ieee->freq_band = band;
	priv->ieee->modulation = modulation;
	init_supported_rates(priv, &priv->rates);

	/* If we are currently associated, or trying to associate
	 * then see if this is a new configuration (causing us to
	 * disassociate) */
	if (priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) {
		/* The resulting association will trigger
		 * the new rates to be sent to the device */
		IPW_DEBUG_ASSOC("Disassociating due to mode change.\n");
		ipw_disassociate(priv);
	} else
		ipw_send_supported_rates(priv, &priv->rates);

	IPW_DEBUG_WX("PRIV SET MODE: %c%c%c\n",
		     mode & IEEE_A ? 'a' : '.',
		     mode & IEEE_B ? 'b' : '.', mode & IEEE_G ? 'g' : '.');
	return 0;
}

static int ipw_wx_get_wireless_mode(struct net_device *dev,
				    struct iw_request_info *info,
				    union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	switch (priv->ieee->freq_band) {
	case IEEE80211_24GHZ_BAND:
		switch (priv->ieee->modulation) {
		case IEEE80211_CCK_MODULATION:
			strncpy(extra, "802.11b (2)", MAX_WX_STRING);
			break;
		case IEEE80211_OFDM_MODULATION:
			strncpy(extra, "802.11g (4)", MAX_WX_STRING);
			break;
		default:
			strncpy(extra, "802.11bg (6)", MAX_WX_STRING);
			break;
		}
		break;

	case IEEE80211_52GHZ_BAND:
		strncpy(extra, "802.11a (1)", MAX_WX_STRING);
		break;

	default:		/* Mixed Band */
		switch (priv->ieee->modulation) {
		case IEEE80211_CCK_MODULATION:
			strncpy(extra, "802.11ab (3)", MAX_WX_STRING);
			break;
		case IEEE80211_OFDM_MODULATION:
			strncpy(extra, "802.11ag (5)", MAX_WX_STRING);
			break;
		default:
			strncpy(extra, "802.11abg (7)", MAX_WX_STRING);
			break;
		}
		break;
	}

	IPW_DEBUG_WX("PRIV GET MODE: %s\n", extra);

	wrqu->data.length = strlen(extra) + 1;

	return 0;
}

#ifdef CONFIG_IPW_PROMISC
static int ipw_wx_set_promisc(struct net_device *dev,
			      struct iw_request_info *info,
			      union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int *parms = (int *)extra;
	int enable = (parms[0] > 0);

	IPW_DEBUG_WX("SET PROMISC: %d %d\n", enable, parms[1]);
	if (enable) {
		if (priv->ieee->iw_mode != IW_MODE_MONITOR) {
			priv->net_dev->type = ARPHRD_IEEE80211;
			ipw_adapter_restart(priv);
		}

		ipw_set_channel(priv, parms[1]);
	} else {
		if (priv->ieee->iw_mode != IW_MODE_MONITOR)
			return 0;
		priv->net_dev->type = ARPHRD_ETHER;
		ipw_adapter_restart(priv);
	}
	return 0;
}

static int ipw_wx_reset(struct net_device *dev,
			struct iw_request_info *info,
			union iwreq_data *wrqu, char *extra)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	IPW_DEBUG_WX("RESET\n");
	ipw_adapter_restart(priv);
	return 0;
}
#endif				// CONFIG_IPW_PROMISC

/* Rebase the WE IOCTLs to zero for the handler array */
#define IW_IOCTL(x) [(x)-SIOCSIWCOMMIT]
static iw_handler ipw_wx_handlers[] = {
	IW_IOCTL(SIOCGIWNAME)	= ipw_wx_get_name,
	IW_IOCTL(SIOCSIWFREQ)	= ipw_wx_set_freq,
	IW_IOCTL(SIOCGIWFREQ)	= ipw_wx_get_freq,
	IW_IOCTL(SIOCSIWMODE)	= ipw_wx_set_mode,
	IW_IOCTL(SIOCGIWMODE)	= ipw_wx_get_mode,
	IW_IOCTL(SIOCGIWRANGE)	= ipw_wx_get_range,
	IW_IOCTL(SIOCSIWAP)	= ipw_wx_set_wap,
	IW_IOCTL(SIOCGIWAP)	= ipw_wx_get_wap,
	IW_IOCTL(SIOCSIWSCAN)	= ipw_wx_set_scan,
	IW_IOCTL(SIOCGIWSCAN)	= ipw_wx_get_scan,
	IW_IOCTL(SIOCSIWESSID)	= ipw_wx_set_essid,
	IW_IOCTL(SIOCGIWESSID)	= ipw_wx_get_essid,
	IW_IOCTL(SIOCSIWNICKN)	= ipw_wx_set_nick,
	IW_IOCTL(SIOCGIWNICKN)	= ipw_wx_get_nick,
	IW_IOCTL(SIOCSIWRATE)	= ipw_wx_set_rate,
	IW_IOCTL(SIOCGIWRATE)	= ipw_wx_get_rate,
	IW_IOCTL(SIOCSIWRTS)	= ipw_wx_set_rts,
	IW_IOCTL(SIOCGIWRTS)	= ipw_wx_get_rts,
	IW_IOCTL(SIOCSIWFRAG)	= ipw_wx_set_frag,
	IW_IOCTL(SIOCGIWFRAG)	= ipw_wx_get_frag,
	IW_IOCTL(SIOCSIWTXPOW)	= ipw_wx_set_txpow,
	IW_IOCTL(SIOCGIWTXPOW)	= ipw_wx_get_txpow,
	IW_IOCTL(SIOCSIWRETRY)	= ipw_wx_set_retry,
	IW_IOCTL(SIOCGIWRETRY)	= ipw_wx_get_retry,
	IW_IOCTL(SIOCSIWENCODE)	= ipw_wx_set_encode,
	IW_IOCTL(SIOCGIWENCODE)	= ipw_wx_get_encode,
	IW_IOCTL(SIOCSIWPOWER)	= ipw_wx_set_power,
	IW_IOCTL(SIOCGIWPOWER)	= ipw_wx_get_power,
};

#define IPW_PRIV_SET_POWER	SIOCIWFIRSTPRIV
#define IPW_PRIV_GET_POWER	SIOCIWFIRSTPRIV+1
#define IPW_PRIV_SET_MODE	SIOCIWFIRSTPRIV+2
#define IPW_PRIV_GET_MODE	SIOCIWFIRSTPRIV+3
#define IPW_PRIV_SET_PROMISC	SIOCIWFIRSTPRIV+4
#define IPW_PRIV_RESET		SIOCIWFIRSTPRIV+5

static struct iw_priv_args ipw_priv_args[] = {
	{
	 .cmd = IPW_PRIV_SET_POWER,
	 .set_args = IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 .name = "set_power"},
	{
	 .cmd = IPW_PRIV_GET_POWER,
	 .get_args = IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
	 .name = "get_power"},
	{
	 .cmd = IPW_PRIV_SET_MODE,
	 .set_args = IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 1,
	 .name = "set_mode"},
	{
	 .cmd = IPW_PRIV_GET_MODE,
	 .get_args = IW_PRIV_TYPE_CHAR | IW_PRIV_SIZE_FIXED | MAX_WX_STRING,
	 .name = "get_mode"},
#ifdef CONFIG_IPW_PROMISC
	{
	 IPW_PRIV_SET_PROMISC,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 2, 0, "monitor"},
	{
	 IPW_PRIV_RESET,
	 IW_PRIV_TYPE_INT | IW_PRIV_SIZE_FIXED | 0, 0, "reset"},
#endif				/* CONFIG_IPW_PROMISC */
};

static iw_handler ipw_priv_handler[] = {
	ipw_wx_set_powermode,
	ipw_wx_get_powermode,
	ipw_wx_set_wireless_mode,
	ipw_wx_get_wireless_mode,
#ifdef CONFIG_IPW_PROMISC
	ipw_wx_set_promisc,
	ipw_wx_reset,
#endif
};

static struct iw_handler_def ipw_wx_handler_def = {
	.standard		= ipw_wx_handlers,
	.num_standard		= ARRAY_SIZE(ipw_wx_handlers),
	.num_private		= ARRAY_SIZE(ipw_priv_handler),
	.num_private_args	= ARRAY_SIZE(ipw_priv_args),
	.private		= ipw_priv_handler,
	.private_args		= ipw_priv_args,
};

/*
 * Get wireless statistics.
 * Called by /proc/net/wireless
 * Also called by SIOCGIWSTATS
 */
static struct iw_statistics *ipw_get_wireless_stats(struct net_device *dev)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	struct iw_statistics *wstats;

	wstats = &priv->wstats;

	/* if hw is disabled, then ipw2100_get_ordinal() can't be called.
	 * ipw2100_wx_wireless_stats seems to be called before fw is
	 * initialized.  STATUS_ASSOCIATED will only be set if the hw is up
	 * and associated; if not associcated, the values are all meaningless
	 * anyway, so set them all to NULL and INVALID */
	if (!(priv->status & STATUS_ASSOCIATED)) {
		wstats->miss.beacon = 0;
		wstats->discard.retries = 0;
		wstats->qual.qual = 0;
		wstats->qual.level = 0;
		wstats->qual.noise = 0;
		wstats->qual.updated = 7;
		wstats->qual.updated |= IW_QUAL_NOISE_INVALID |
		    IW_QUAL_QUAL_INVALID | IW_QUAL_LEVEL_INVALID;
		return wstats;
	}

	wstats->qual.qual = priv->quality;
	wstats->qual.level = average_value(&priv->average_rssi);
	wstats->qual.noise = average_value(&priv->average_noise);
	wstats->qual.updated = IW_QUAL_QUAL_UPDATED | IW_QUAL_LEVEL_UPDATED |
	    IW_QUAL_NOISE_UPDATED;

	wstats->miss.beacon = average_value(&priv->average_missed_beacons);
	wstats->discard.retries = priv->last_tx_failures;
	wstats->discard.code = priv->ieee->ieee_stats.rx_discards_undecryptable;

/*	if (ipw_get_ordinal(priv, IPW_ORD_STAT_TX_RETRY, &tx_retry, &len))
	goto fail_get_ordinal;
	wstats->discard.retries += tx_retry; */

	return wstats;
}

/* net device stuff */

static inline void init_sys_config(struct ipw_sys_config *sys_config)
{
	memset(sys_config, 0, sizeof(struct ipw_sys_config));
	sys_config->bt_coexistence = 1;	/* We may need to look into prvStaBtConfig */
	sys_config->answer_broadcast_ssid_probe = 0;
	sys_config->accept_all_data_frames = 0;
	sys_config->accept_non_directed_frames = 1;
	sys_config->exclude_unicast_unencrypted = 0;
	sys_config->disable_unicast_decryption = 1;
	sys_config->exclude_multicast_unencrypted = 0;
	sys_config->disable_multicast_decryption = 1;
	sys_config->antenna_diversity = CFG_SYS_ANTENNA_BOTH;
	sys_config->pass_crc_to_host = 0;	/* TODO: See if 1 gives us FCS */
	sys_config->dot11g_auto_detection = 0;
	sys_config->enable_cts_to_self = 0;
	sys_config->bt_coexist_collision_thr = 0;
	sys_config->pass_noise_stats_to_host = 1;
}

static int ipw_net_open(struct net_device *dev)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	IPW_DEBUG_INFO("dev->open\n");
	/* we should be verifying the device is ready to be opened */
	if (!(priv->status & STATUS_RF_KILL_MASK) &&
	    (priv->status & STATUS_ASSOCIATED))
		netif_start_queue(dev);
	return 0;
}

static int ipw_net_stop(struct net_device *dev)
{
	IPW_DEBUG_INFO("dev->close\n");
	netif_stop_queue(dev);
	return 0;
}

/*
todo:

modify to send one tfd per fragment instead of using chunking.  otherwise
we need to heavily modify the ieee80211_skb_to_txb.
*/

static inline void ipw_tx_skb(struct ipw_priv *priv, struct ieee80211_txb *txb)
{
	struct ieee80211_hdr_3addr *hdr = (struct ieee80211_hdr_3addr *)
	    txb->fragments[0]->data;
	int i = 0;
	struct tfd_frame *tfd;
	struct clx2_tx_queue *txq = &priv->txq[0];
	struct clx2_queue *q = &txq->q;
	u8 id, hdr_len, unicast;
	u16 remaining_bytes;

	switch (priv->ieee->iw_mode) {
	case IW_MODE_ADHOC:
		hdr_len = IEEE80211_3ADDR_LEN;
		unicast = !is_broadcast_ether_addr(hdr->addr1) &&
		    !is_multicast_ether_addr(hdr->addr1);
		id = ipw_find_station(priv, hdr->addr1);
		if (id == IPW_INVALID_STATION) {
			id = ipw_add_station(priv, hdr->addr1);
			if (id == IPW_INVALID_STATION) {
				IPW_WARNING("Attempt to send data to "
					    "invalid cell: " MAC_FMT "\n",
					    MAC_ARG(hdr->addr1));
				goto drop;
			}
		}
		break;

	case IW_MODE_INFRA:
	default:
		unicast = !is_broadcast_ether_addr(hdr->addr3) &&
		    !is_multicast_ether_addr(hdr->addr3);
		hdr_len = IEEE80211_3ADDR_LEN;
		id = 0;
		break;
	}

	tfd = &txq->bd[q->first_empty];
	txq->txb[q->first_empty] = txb;
	memset(tfd, 0, sizeof(*tfd));
	tfd->u.data.station_number = id;

	tfd->control_flags.message_type = TX_FRAME_TYPE;
	tfd->control_flags.control_bits = TFD_NEED_IRQ_MASK;

	tfd->u.data.cmd_id = DINO_CMD_TX;
	tfd->u.data.len = txb->payload_size;
	remaining_bytes = txb->payload_size;
	if (unlikely(!unicast))
		tfd->u.data.tx_flags = DCT_FLAG_NO_WEP;
	else
		tfd->u.data.tx_flags = DCT_FLAG_NO_WEP | DCT_FLAG_ACK_REQD;

	if (priv->assoc_request.ieee_mode == IPW_B_MODE)
		tfd->u.data.tx_flags_ext = DCT_FLAG_EXT_MODE_CCK;
	else
		tfd->u.data.tx_flags_ext = DCT_FLAG_EXT_MODE_OFDM;

	if (priv->config & CFG_PREAMBLE)
		tfd->u.data.tx_flags |= DCT_FLAG_SHORT_PREMBL;

	memcpy(&tfd->u.data.tfd.tfd_24.mchdr, hdr, hdr_len);

	/* payload */
	tfd->u.data.num_chunks = min((u8) (NUM_TFD_CHUNKS - 2), txb->nr_frags);
	for (i = 0; i < tfd->u.data.num_chunks; i++) {
		IPW_DEBUG_TX("Dumping TX packet frag %i of %i (%d bytes):\n",
			     i, tfd->u.data.num_chunks,
			     txb->fragments[i]->len - hdr_len);
		printk_buf(IPW_DL_TX, txb->fragments[i]->data + hdr_len,
			   txb->fragments[i]->len - hdr_len);

		tfd->u.data.chunk_ptr[i] =
		    pci_map_single(priv->pci_dev,
				   txb->fragments[i]->data + hdr_len,
				   txb->fragments[i]->len - hdr_len,
				   PCI_DMA_TODEVICE);
		tfd->u.data.chunk_len[i] = txb->fragments[i]->len - hdr_len;
	}

	if (i != txb->nr_frags) {
		struct sk_buff *skb;
		u16 remaining_bytes = 0;
		int j;

		for (j = i; j < txb->nr_frags; j++)
			remaining_bytes += txb->fragments[j]->len - hdr_len;

		printk(KERN_INFO "Trying to reallocate for %d bytes\n",
		       remaining_bytes);
		skb = alloc_skb(remaining_bytes, GFP_ATOMIC);
		if (skb != NULL) {
			tfd->u.data.chunk_len[i] = remaining_bytes;
			for (j = i; j < txb->nr_frags; j++) {
				int size = txb->fragments[j]->len - hdr_len;
				printk(KERN_INFO "Adding frag %d %d...\n",
				       j, size);
				memcpy(skb_put(skb, size),
				       txb->fragments[j]->data + hdr_len, size);
			}
			dev_kfree_skb_any(txb->fragments[i]);
			txb->fragments[i] = skb;
			tfd->u.data.chunk_ptr[i] =
			    pci_map_single(priv->pci_dev, skb->data,
					   tfd->u.data.chunk_len[i],
					   PCI_DMA_TODEVICE);
			tfd->u.data.num_chunks++;
		}
	}

	/* kick DMA */
	q->first_empty = ipw_queue_inc_wrap(q->first_empty, q->n_bd);
	ipw_write32(priv, q->reg_w, q->first_empty);

	if (ipw_queue_space(q) < q->high_mark)
		netif_stop_queue(priv->net_dev);

	return;

      drop:
	IPW_DEBUG_DROP("Silently dropping Tx packet.\n");
	ieee80211_txb_free(txb);
}

static int ipw_net_hard_start_xmit(struct ieee80211_txb *txb,
				   struct net_device *dev, int pri)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	unsigned long flags;

	IPW_DEBUG_TX("dev->xmit(%d bytes)\n", txb->payload_size);

	spin_lock_irqsave(&priv->lock, flags);

	if (!(priv->status & STATUS_ASSOCIATED)) {
		IPW_DEBUG_INFO("Tx attempt while not associated.\n");
		priv->ieee->stats.tx_carrier_errors++;
		netif_stop_queue(dev);
		goto fail_unlock;
	}

	ipw_tx_skb(priv, txb);

	spin_unlock_irqrestore(&priv->lock, flags);
	return 0;

      fail_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
	return 1;
}

static struct net_device_stats *ipw_net_get_stats(struct net_device *dev)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	priv->ieee->stats.tx_packets = priv->tx_packets;
	priv->ieee->stats.rx_packets = priv->rx_packets;
	return &priv->ieee->stats;
}

static void ipw_net_set_multicast_list(struct net_device *dev)
{

}

static int ipw_net_set_mac_address(struct net_device *dev, void *p)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	struct sockaddr *addr = p;
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	priv->config |= CFG_CUSTOM_MAC;
	memcpy(priv->mac_addr, addr->sa_data, ETH_ALEN);
	printk(KERN_INFO "%s: Setting MAC to " MAC_FMT "\n",
	       priv->net_dev->name, MAC_ARG(priv->mac_addr));
	ipw_adapter_restart(priv);
	return 0;
}

static void ipw_ethtool_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *info)
{
	struct ipw_priv *p = ieee80211_priv(dev);
	char vers[64];
	char date[32];
	u32 len;

	strcpy(info->driver, DRV_NAME);
	strcpy(info->version, DRV_VERSION);

	len = sizeof(vers);
	ipw_get_ordinal(p, IPW_ORD_STAT_FW_VERSION, vers, &len);
	len = sizeof(date);
	ipw_get_ordinal(p, IPW_ORD_STAT_FW_DATE, date, &len);

	snprintf(info->fw_version, sizeof(info->fw_version), "%s (%s)",
		 vers, date);
	strcpy(info->bus_info, pci_name(p->pci_dev));
	info->eedump_len = CX2_EEPROM_IMAGE_SIZE;
}

static u32 ipw_ethtool_get_link(struct net_device *dev)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	return (priv->status & STATUS_ASSOCIATED) != 0;
}

static int ipw_ethtool_get_eeprom_len(struct net_device *dev)
{
	return CX2_EEPROM_IMAGE_SIZE;
}

static int ipw_ethtool_get_eeprom(struct net_device *dev,
				  struct ethtool_eeprom *eeprom, u8 * bytes)
{
	struct ipw_priv *p = ieee80211_priv(dev);

	if (eeprom->offset + eeprom->len > CX2_EEPROM_IMAGE_SIZE)
		return -EINVAL;

	memcpy(bytes, &((u8 *) p->eeprom)[eeprom->offset], eeprom->len);
	return 0;
}

static int ipw_ethtool_set_eeprom(struct net_device *dev,
				  struct ethtool_eeprom *eeprom, u8 * bytes)
{
	struct ipw_priv *p = ieee80211_priv(dev);
	int i;

	if (eeprom->offset + eeprom->len > CX2_EEPROM_IMAGE_SIZE)
		return -EINVAL;

	memcpy(&((u8 *) p->eeprom)[eeprom->offset], bytes, eeprom->len);
	for (i = IPW_EEPROM_DATA;
	     i < IPW_EEPROM_DATA + CX2_EEPROM_IMAGE_SIZE; i++)
		ipw_write8(p, i, p->eeprom[i]);

	return 0;
}

static struct ethtool_ops ipw_ethtool_ops = {
	.get_link	= ipw_ethtool_get_link,
	.get_drvinfo	= ipw_ethtool_get_drvinfo,
	.get_eeprom_len	= ipw_ethtool_get_eeprom_len,
	.get_eeprom	= ipw_ethtool_get_eeprom,
	.set_eeprom	= ipw_ethtool_set_eeprom,
};

static irqreturn_t ipw_isr(int irq, void *data, struct pt_regs *regs)
{
	struct ipw_priv *priv = data;
	u32 inta, inta_mask;

	if (!priv)
		return IRQ_NONE;

	spin_lock(&priv->lock);

	if (!(priv->status & STATUS_INT_ENABLED)) {
		/* Shared IRQ */
		goto none;
	}

	inta = ipw_read32(priv, CX2_INTA_RW);
	inta_mask = ipw_read32(priv, CX2_INTA_MASK_R);

	if (inta == 0xFFFFFFFF) {
		/* Hardware disappeared */
		IPW_WARNING("IRQ INTA == 0xFFFFFFFF\n");
		goto none;
	}

	if (!(inta & (CX2_INTA_MASK_ALL & inta_mask))) {
		/* Shared interrupt */
		goto none;
	}

	/* tell the device to stop sending interrupts */
	ipw_disable_interrupts(priv);

	/* ack current interrupts */
	inta &= (CX2_INTA_MASK_ALL & inta_mask);
	ipw_write32(priv, CX2_INTA_RW, inta);

	/* Cache INTA value for our tasklet */
	priv->isr_inta = inta;

	tasklet_schedule(&priv->irq_tasklet);

	spin_unlock(&priv->lock);

	return IRQ_HANDLED;
      none:
	spin_unlock(&priv->lock);
	return IRQ_NONE;
}

static void ipw_rf_kill(void *adapter)
{
	struct ipw_priv *priv = adapter;
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);

	if (rf_kill_active(priv)) {
		IPW_DEBUG_RF_KILL("RF Kill active, rescheduling GPIO check\n");
		if (priv->workqueue)
			queue_delayed_work(priv->workqueue,
					   &priv->rf_kill, 2 * HZ);
		goto exit_unlock;
	}

	/* RF Kill is now disabled, so bring the device back up */

	if (!(priv->status & STATUS_RF_KILL_MASK)) {
		IPW_DEBUG_RF_KILL("HW RF Kill no longer active, restarting "
				  "device\n");

		/* we can not do an adapter restart while inside an irq lock */
		queue_work(priv->workqueue, &priv->adapter_restart);
	} else
		IPW_DEBUG_RF_KILL("HW RF Kill deactivated.  SW RF Kill still "
				  "enabled\n");

      exit_unlock:
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int ipw_setup_deferred_work(struct ipw_priv *priv)
{
	int ret = 0;

	priv->workqueue = create_workqueue(DRV_NAME);
	init_waitqueue_head(&priv->wait_command_queue);

	INIT_WORK(&priv->adhoc_check, ipw_adhoc_check, priv);
	INIT_WORK(&priv->associate, ipw_associate, priv);
	INIT_WORK(&priv->disassociate, ipw_disassociate, priv);
	INIT_WORK(&priv->rx_replenish, ipw_rx_queue_replenish, priv);
	INIT_WORK(&priv->adapter_restart, ipw_adapter_restart, priv);
	INIT_WORK(&priv->rf_kill, ipw_rf_kill, priv);
	INIT_WORK(&priv->up, (void (*)(void *))ipw_up, priv);
	INIT_WORK(&priv->down, (void (*)(void *))ipw_down, priv);
	INIT_WORK(&priv->request_scan,
		  (void (*)(void *))ipw_request_scan, priv);
	INIT_WORK(&priv->gather_stats,
		  (void (*)(void *))ipw_gather_stats, priv);
	INIT_WORK(&priv->abort_scan, (void (*)(void *))ipw_abort_scan, priv);
	INIT_WORK(&priv->roam, ipw_roam, priv);
	INIT_WORK(&priv->scan_check, ipw_scan_check, priv);

	tasklet_init(&priv->irq_tasklet, (void (*)(unsigned long))
		     ipw_irq_tasklet, (unsigned long)priv);

	return ret;
}

static void shim__set_security(struct net_device *dev,
			       struct ieee80211_security *sec)
{
	struct ipw_priv *priv = ieee80211_priv(dev);
	int i;

	for (i = 0; i < 4; i++) {
		if (sec->flags & (1 << i)) {
			priv->sec.key_sizes[i] = sec->key_sizes[i];
			if (sec->key_sizes[i] == 0)
				priv->sec.flags &= ~(1 << i);
			else
				memcpy(priv->sec.keys[i], sec->keys[i],
				       sec->key_sizes[i]);
			priv->sec.flags |= (1 << i);
			priv->status |= STATUS_SECURITY_UPDATED;
		}
	}

	if ((sec->flags & SEC_ACTIVE_KEY) &&
	    priv->sec.active_key != sec->active_key) {
		if (sec->active_key <= 3) {
			priv->sec.active_key = sec->active_key;
			priv->sec.flags |= SEC_ACTIVE_KEY;
		} else
			priv->sec.flags &= ~SEC_ACTIVE_KEY;
		priv->status |= STATUS_SECURITY_UPDATED;
	}

	if ((sec->flags & SEC_AUTH_MODE) &&
	    (priv->sec.auth_mode != sec->auth_mode)) {
		priv->sec.auth_mode = sec->auth_mode;
		priv->sec.flags |= SEC_AUTH_MODE;
		if (sec->auth_mode == WLAN_AUTH_SHARED_KEY)
			priv->capability |= CAP_SHARED_KEY;
		else
			priv->capability &= ~CAP_SHARED_KEY;
		priv->status |= STATUS_SECURITY_UPDATED;
	}

	if (sec->flags & SEC_ENABLED && priv->sec.enabled != sec->enabled) {
		priv->sec.flags |= SEC_ENABLED;
		priv->sec.enabled = sec->enabled;
		priv->status |= STATUS_SECURITY_UPDATED;
		if (sec->enabled)
			priv->capability |= CAP_PRIVACY_ON;
		else
			priv->capability &= ~CAP_PRIVACY_ON;
	}

	if (sec->flags & SEC_LEVEL && priv->sec.level != sec->level) {
		priv->sec.level = sec->level;
		priv->sec.flags |= SEC_LEVEL;
		priv->status |= STATUS_SECURITY_UPDATED;
	}

	/* To match current functionality of ipw2100 (which works well w/
	 * various supplicants, we don't force a disassociate if the
	 * privacy capability changes ... */
#if 0
	if ((priv->status & (STATUS_ASSOCIATED | STATUS_ASSOCIATING)) &&
	    (((priv->assoc_request.capability &
	       WLAN_CAPABILITY_PRIVACY) && !sec->enabled) ||
	     (!(priv->assoc_request.capability &
		WLAN_CAPABILITY_PRIVACY) && sec->enabled))) {
		IPW_DEBUG_ASSOC("Disassociating due to capability "
				"change.\n");
		ipw_disassociate(priv);
	}
#endif
}

static int init_supported_rates(struct ipw_priv *priv,
				struct ipw_supported_rates *rates)
{
	/* TODO: Mask out rates based on priv->rates_mask */

	memset(rates, 0, sizeof(*rates));
	/* configure supported rates */
	switch (priv->ieee->freq_band) {
	case IEEE80211_52GHZ_BAND:
		rates->ieee_mode = IPW_A_MODE;
		rates->purpose = IPW_RATE_CAPABILITIES;
		ipw_add_ofdm_scan_rates(rates, IEEE80211_CCK_MODULATION,
					IEEE80211_OFDM_DEFAULT_RATES_MASK);
		break;

	default:		/* Mixed or 2.4Ghz */
		rates->ieee_mode = IPW_G_MODE;
		rates->purpose = IPW_RATE_CAPABILITIES;
		ipw_add_cck_scan_rates(rates, IEEE80211_CCK_MODULATION,
				       IEEE80211_CCK_DEFAULT_RATES_MASK);
		if (priv->ieee->modulation & IEEE80211_OFDM_MODULATION) {
			ipw_add_ofdm_scan_rates(rates, IEEE80211_CCK_MODULATION,
						IEEE80211_OFDM_DEFAULT_RATES_MASK);
		}
		break;
	}

	return 0;
}

static int ipw_config(struct ipw_priv *priv)
{
	int i;
	struct ipw_tx_power tx_power;

	memset(&priv->sys_config, 0, sizeof(priv->sys_config));
	memset(&tx_power, 0, sizeof(tx_power));

	/* This is only called from ipw_up, which resets/reloads the firmware
	   so, we don't need to first disable the card before we configure
	   it */

	/* configure device for 'G' band */
	tx_power.ieee_mode = IPW_G_MODE;
	tx_power.num_channels = 11;
	for (i = 0; i < 11; i++) {
		tx_power.channels_tx_power[i].channel_number = i + 1;
		tx_power.channels_tx_power[i].tx_power = priv->tx_power;
	}
	if (ipw_send_tx_power(priv, &tx_power))
		goto error;

	/* configure device to also handle 'B' band */
	tx_power.ieee_mode = IPW_B_MODE;
	if (ipw_send_tx_power(priv, &tx_power))
		goto error;

	/* initialize adapter address */
	if (ipw_send_adapter_address(priv, priv->net_dev->dev_addr))
		goto error;

	/* set basic system config settings */
	init_sys_config(&priv->sys_config);
	if (ipw_send_system_config(priv, &priv->sys_config))
		goto error;

	init_supported_rates(priv, &priv->rates);
	if (ipw_send_supported_rates(priv, &priv->rates))
		goto error;

	/* Set request-to-send threshold */
	if (priv->rts_threshold) {
		if (ipw_send_rts_threshold(priv, priv->rts_threshold))
			goto error;
	}

	if (ipw_set_random_seed(priv))
		goto error;

	/* final state transition to the RUN state */
	if (ipw_send_host_complete(priv))
		goto error;

	/* If configured to try and auto-associate, kick off a scan */
	if ((priv->config & CFG_ASSOCIATE) && ipw_request_scan(priv))
		goto error;

	return 0;

      error:
	return -EIO;
}

#define MAX_HW_RESTARTS 5
static int ipw_up(struct ipw_priv *priv)
{
	int rc, i;

	if (priv->status & STATUS_EXIT_PENDING)
		return -EIO;

	for (i = 0; i < MAX_HW_RESTARTS; i++) {
		/* Load the microcode, firmware, and eeprom.
		 * Also start the clocks. */
		rc = ipw_load(priv);
		if (rc) {
			IPW_ERROR("Unable to load firmware: 0x%08X\n", rc);
			return rc;
		}

		ipw_init_ordinals(priv);
		if (!(priv->config & CFG_CUSTOM_MAC))
			eeprom_parse_mac(priv, priv->mac_addr);
		memcpy(priv->net_dev->dev_addr, priv->mac_addr, ETH_ALEN);

		if (priv->status & STATUS_RF_KILL_MASK)
			return 0;

		rc = ipw_config(priv);
		if (!rc) {
			IPW_DEBUG_INFO("Configured device on count %i\n", i);
			priv->notif_missed_beacons = 0;
			netif_start_queue(priv->net_dev);
			return 0;
		} else {
			IPW_DEBUG_INFO("Device configuration failed: 0x%08X\n",
				       rc);
		}

		IPW_DEBUG_INFO("Failed to config device on retry %d of %d\n",
			       i, MAX_HW_RESTARTS);

		/* We had an error bringing up the hardware, so take it
		 * all the way back down so we can try again */
		ipw_down(priv);
	}

	/* tried to restart and config the device for as long as our
	 * patience could withstand */
	IPW_ERROR("Unable to initialize device after %d attempts.\n", i);
	return -EIO;
}

static void ipw_down(struct ipw_priv *priv)
{
	/* Attempt to disable the card */
#if 0
	ipw_send_card_disable(priv, 0);
#endif

	/* tell the device to stop sending interrupts */
	ipw_disable_interrupts(priv);

	/* Clear all bits but the RF Kill */
	priv->status &= STATUS_RF_KILL_MASK;

	netif_carrier_off(priv->net_dev);
	netif_stop_queue(priv->net_dev);

	ipw_stop_nic(priv);
}

/* Called by register_netdev() */
static int ipw_net_init(struct net_device *dev)
{
	struct ipw_priv *priv = ieee80211_priv(dev);

	if (priv->status & STATUS_RF_KILL_SW) {
		IPW_WARNING("Radio disabled by module parameter.\n");
		return 0;
	} else if (rf_kill_active(priv)) {
		IPW_WARNING("Radio Frequency Kill Switch is On:\n"
			    "Kill switch must be turned off for "
			    "wireless networking to work.\n");
		queue_delayed_work(priv->workqueue, &priv->rf_kill, 2 * HZ);
		return 0;
	}

	if (ipw_up(priv))
		return -EIO;

	return 0;
}

/* PCI driver stuff */
static struct pci_device_id card_ids[] = {
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2701, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2702, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2711, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2712, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2721, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2722, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2731, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2732, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2741, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x103c, 0x2741, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2742, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2751, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2752, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2753, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2754, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2761, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x1043, 0x8086, 0x2762, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x104f, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},
	{PCI_VENDOR_ID_INTEL, 0x4220, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	/* BG */
	{PCI_VENDOR_ID_INTEL, 0x4221, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	/* 2225BG */
	{PCI_VENDOR_ID_INTEL, 0x4223, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	/* ABG */
	{PCI_VENDOR_ID_INTEL, 0x4224, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0},	/* ABG */

	/* required last entry */
	{0,}
};

MODULE_DEVICE_TABLE(pci, card_ids);

static struct attribute *ipw_sysfs_entries[] = {
	&dev_attr_rf_kill.attr,
	&dev_attr_direct_dword.attr,
	&dev_attr_indirect_byte.attr,
	&dev_attr_indirect_dword.attr,
	&dev_attr_mem_gpio_reg.attr,
	&dev_attr_command_event_reg.attr,
	&dev_attr_nic_type.attr,
	&dev_attr_status.attr,
	&dev_attr_cfg.attr,
	&dev_attr_dump_errors.attr,
	&dev_attr_dump_events.attr,
	&dev_attr_eeprom_delay.attr,
	&dev_attr_ucode_version.attr,
	&dev_attr_rtc.attr,
	NULL
};

static struct attribute_group ipw_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = ipw_sysfs_entries,
};

static int ipw_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int err = 0;
	struct net_device *net_dev;
	void __iomem *base;
	u32 length, val;
	struct ipw_priv *priv;
	int band, modulation;

	net_dev = alloc_ieee80211(sizeof(struct ipw_priv));
	if (net_dev == NULL) {
		err = -ENOMEM;
		goto out;
	}

	priv = ieee80211_priv(net_dev);
	priv->ieee = netdev_priv(net_dev);
	priv->net_dev = net_dev;
	priv->pci_dev = pdev;
#ifdef CONFIG_IPW_DEBUG
	ipw_debug_level = debug;
#endif
	spin_lock_init(&priv->lock);

	if (pci_enable_device(pdev)) {
		err = -ENODEV;
		goto out_free_ieee80211;
	}

	pci_set_master(pdev);

	err = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (!err)
		err = pci_set_consistent_dma_mask(pdev, DMA_32BIT_MASK);
	if (err) {
		printk(KERN_WARNING DRV_NAME ": No suitable DMA available.\n");
		goto out_pci_disable_device;
	}

	pci_set_drvdata(pdev, priv);

	err = pci_request_regions(pdev, DRV_NAME);
	if (err)
		goto out_pci_disable_device;

	/* We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	length = pci_resource_len(pdev, 0);
	priv->hw_len = length;

	base = ioremap_nocache(pci_resource_start(pdev, 0), length);
	if (!base) {
		err = -ENODEV;
		goto out_pci_release_regions;
	}

	priv->hw_base = base;
	IPW_DEBUG_INFO("pci_resource_len = 0x%08x\n", length);
	IPW_DEBUG_INFO("pci_resource_base = %p\n", base);

	err = ipw_setup_deferred_work(priv);
	if (err) {
		IPW_ERROR("Unable to setup deferred work\n");
		goto out_iounmap;
	}

	/* Initialize module parameter values here */
	if (ifname)
		strncpy(net_dev->name, ifname, IFNAMSIZ);

	if (associate)
		priv->config |= CFG_ASSOCIATE;
	else
		IPW_DEBUG_INFO("Auto associate disabled.\n");

	if (auto_create)
		priv->config |= CFG_ADHOC_CREATE;
	else
		IPW_DEBUG_INFO("Auto adhoc creation disabled.\n");

	if (disable) {
		priv->status |= STATUS_RF_KILL_SW;
		IPW_DEBUG_INFO("Radio disabled.\n");
	}

	if (channel != 0) {
		priv->config |= CFG_STATIC_CHANNEL;
		priv->channel = channel;
		IPW_DEBUG_INFO("Bind to static channel %d\n", channel);
		IPW_DEBUG_INFO("Bind to static channel %d\n", channel);
		/* TODO: Validate that provided channel is in range */
	}

	switch (mode) {
	case 1:
		priv->ieee->iw_mode = IW_MODE_ADHOC;
		break;
#ifdef CONFIG_IPW_PROMISC
	case 2:
		priv->ieee->iw_mode = IW_MODE_MONITOR;
		break;
#endif
	default:
	case 0:
		priv->ieee->iw_mode = IW_MODE_INFRA;
		break;
	}

	if ((priv->pci_dev->device == 0x4223) ||
	    (priv->pci_dev->device == 0x4224)) {
		printk(KERN_INFO DRV_NAME
		       ": Detected Intel PRO/Wireless 2915ABG Network "
		       "Connection\n");
		priv->ieee->abg_true = 1;
		band = IEEE80211_52GHZ_BAND | IEEE80211_24GHZ_BAND;
		modulation = IEEE80211_OFDM_MODULATION |
		    IEEE80211_CCK_MODULATION;
		priv->adapter = IPW_2915ABG;
		priv->ieee->mode = IEEE_A | IEEE_G | IEEE_B;
	} else {
		if (priv->pci_dev->device == 0x4221)
			printk(KERN_INFO DRV_NAME
			       ": Detected Intel PRO/Wireless 2225BG Network "
			       "Connection\n");
		else
			printk(KERN_INFO DRV_NAME
			       ": Detected Intel PRO/Wireless 2200BG Network "
			       "Connection\n");

		priv->ieee->abg_true = 0;
		band = IEEE80211_24GHZ_BAND;
		modulation = IEEE80211_OFDM_MODULATION |
		    IEEE80211_CCK_MODULATION;
		priv->adapter = IPW_2200BG;
		priv->ieee->mode = IEEE_G | IEEE_B;
	}

	priv->ieee->freq_band = band;
	priv->ieee->modulation = modulation;

	priv->rates_mask = IEEE80211_DEFAULT_RATES_MASK;

	priv->missed_beacon_threshold = IPW_MB_DISASSOCIATE_THRESHOLD_DEFAULT;
	priv->roaming_threshold = IPW_MB_ROAMING_THRESHOLD_DEFAULT;

	priv->rts_threshold = DEFAULT_RTS_THRESHOLD;

	/* If power management is turned on, default to AC mode */
	priv->power_mode = IPW_POWER_AC;
	priv->tx_power = IPW_DEFAULT_TX_POWER;

	err = request_irq(pdev->irq, ipw_isr, SA_SHIRQ, DRV_NAME, priv);
	if (err) {
		IPW_ERROR("Error allocating IRQ %d\n", pdev->irq);
		goto out_destroy_workqueue;
	}

	SET_MODULE_OWNER(net_dev);
	SET_NETDEV_DEV(net_dev, &pdev->dev);

	priv->ieee->hard_start_xmit = ipw_net_hard_start_xmit;
	priv->ieee->set_security = shim__set_security;

	net_dev->open = ipw_net_open;
	net_dev->stop = ipw_net_stop;
	net_dev->init = ipw_net_init;
	net_dev->get_stats = ipw_net_get_stats;
	net_dev->set_multicast_list = ipw_net_set_multicast_list;
	net_dev->set_mac_address = ipw_net_set_mac_address;
	net_dev->get_wireless_stats = ipw_get_wireless_stats;
	net_dev->wireless_handlers = &ipw_wx_handler_def;
	net_dev->ethtool_ops = &ipw_ethtool_ops;
	net_dev->irq = pdev->irq;
	net_dev->base_addr = (unsigned long)priv->hw_base;
	net_dev->mem_start = pci_resource_start(pdev, 0);
	net_dev->mem_end = net_dev->mem_start + pci_resource_len(pdev, 0) - 1;

	err = sysfs_create_group(&pdev->dev.kobj, &ipw_attribute_group);
	if (err) {
		IPW_ERROR("failed to create sysfs device attributes\n");
		goto out_release_irq;
	}

	err = register_netdev(net_dev);
	if (err) {
		IPW_ERROR("failed to register network device\n");
		goto out_remove_group;
	}

	return 0;

      out_remove_group:
	sysfs_remove_group(&pdev->dev.kobj, &ipw_attribute_group);
      out_release_irq:
	free_irq(pdev->irq, priv);
      out_destroy_workqueue:
	destroy_workqueue(priv->workqueue);
	priv->workqueue = NULL;
      out_iounmap:
	iounmap(priv->hw_base);
      out_pci_release_regions:
	pci_release_regions(pdev);
      out_pci_disable_device:
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
      out_free_ieee80211:
	free_ieee80211(priv->net_dev);
      out:
	return err;
}

static void ipw_pci_remove(struct pci_dev *pdev)
{
	struct ipw_priv *priv = pci_get_drvdata(pdev);
	if (!priv)
		return;

	priv->status |= STATUS_EXIT_PENDING;

	sysfs_remove_group(&pdev->dev.kobj, &ipw_attribute_group);

	ipw_down(priv);

	unregister_netdev(priv->net_dev);

	if (priv->rxq) {
		ipw_rx_queue_free(priv, priv->rxq);
		priv->rxq = NULL;
	}
	ipw_tx_queue_free(priv);

	/* ipw_down will ensure that there is no more pending work
	 * in the workqueue's, so we can safely remove them now. */
	if (priv->workqueue) {
		cancel_delayed_work(&priv->adhoc_check);
		cancel_delayed_work(&priv->gather_stats);
		cancel_delayed_work(&priv->request_scan);
		cancel_delayed_work(&priv->rf_kill);
		cancel_delayed_work(&priv->scan_check);
		destroy_workqueue(priv->workqueue);
		priv->workqueue = NULL;
	}

	free_irq(pdev->irq, priv);
	iounmap(priv->hw_base);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
	free_ieee80211(priv->net_dev);

#ifdef CONFIG_PM
	if (fw_loaded) {
		release_firmware(bootfw);
		release_firmware(ucode);
		release_firmware(firmware);
		fw_loaded = 0;
	}
#endif
}

#ifdef CONFIG_PM
static int ipw_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ipw_priv *priv = pci_get_drvdata(pdev);
	struct net_device *dev = priv->net_dev;

	printk(KERN_INFO "%s: Going into suspend...\n", dev->name);

	/* Take down the device; powers it off, etc. */
	ipw_down(priv);

	/* Remove the PRESENT state of the device */
	netif_device_detach(dev);

	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int ipw_pci_resume(struct pci_dev *pdev)
{
	struct ipw_priv *priv = pci_get_drvdata(pdev);
	struct net_device *dev = priv->net_dev;
	u32 val;

	printk(KERN_INFO "%s: Coming out of suspend...\n", dev->name);

	pci_set_power_state(pdev, 0);
	pci_enable_device(pdev);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,10)
	pci_restore_state(pdev, priv->pm_state);
#else
	pci_restore_state(pdev);
#endif
	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep PCI Tx retries
	 * from interfering with C3 CPU state. pci_restore_state won't help
	 * here since it only restores the first 64 bytes pci config header.
	 */
	pci_read_config_dword(pdev, 0x40, &val);
	if ((val & 0x0000ff00) != 0)
		pci_write_config_dword(pdev, 0x40, val & 0xffff00ff);

	/* Set the device back into the PRESENT state; this will also wake
	 * the queue of needed */
	netif_device_attach(dev);

	/* Bring the device back up */
	queue_work(priv->workqueue, &priv->up);

	return 0;
}
#endif

/* driver initialization stuff */
static struct pci_driver ipw_driver = {
	.name = DRV_NAME,
	.id_table = card_ids,
	.probe = ipw_pci_probe,
	.remove = __devexit_p(ipw_pci_remove),
#ifdef CONFIG_PM
	.suspend = ipw_pci_suspend,
	.resume = ipw_pci_resume,
#endif
};

static int __init ipw_init(void)
{
	int ret;

	printk(KERN_INFO DRV_NAME ": " DRV_DESCRIPTION ", " DRV_VERSION "\n");
	printk(KERN_INFO DRV_NAME ": " DRV_COPYRIGHT "\n");

	ret = pci_module_init(&ipw_driver);
	if (ret) {
		IPW_ERROR("Unable to initialize PCI module\n");
		return ret;
	}

	ret = driver_create_file(&ipw_driver.driver, &driver_attr_debug_level);
	if (ret) {
		IPW_ERROR("Unable to create driver sysfs file\n");
		pci_unregister_driver(&ipw_driver);
		return ret;
	}

	return ret;
}

static void __exit ipw_exit(void)
{
	driver_remove_file(&ipw_driver.driver, &driver_attr_debug_level);
	pci_unregister_driver(&ipw_driver);
}

module_param(disable, int, 0444);
MODULE_PARM_DESC(disable, "manually disable the radio (default 0 [radio on])");

module_param(associate, int, 0444);
MODULE_PARM_DESC(associate, "auto associate when scanning (default on)");

module_param(auto_create, int, 0444);
MODULE_PARM_DESC(auto_create, "auto create adhoc network (default on)");

module_param(debug, int, 0444);
MODULE_PARM_DESC(debug, "debug output mask");

module_param(channel, int, 0444);
MODULE_PARM_DESC(channel, "channel to limit associate to (default 0 [ANY])");

module_param(ifname, charp, 0444);
MODULE_PARM_DESC(ifname, "network device name (default eth%d)");

#ifdef CONFIG_IPW_PROMISC
module_param(mode, int, 0444);
MODULE_PARM_DESC(mode, "network mode (0=BSS,1=IBSS,2=Monitor)");
#else
module_param(mode, int, 0444);
MODULE_PARM_DESC(mode, "network mode (0=BSS,1=IBSS)");
#endif

module_exit(ipw_exit);
module_init(ipw_init);
