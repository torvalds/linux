/******************************************************************************
 *
 * Copyright(c) 2003 - 2013 Intel Corporation. All rights reserved.
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

#include "iwl-drv.h"
#include "iwl-io.h"
#include "iwl-csr.h"
#include "iwl-debug.h"
#include "iwl-fh.h"

#define IWL_POLL_INTERVAL 10	/* microseconds */

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
IWL_EXPORT_SYMBOL(iwl_poll_bit);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	u32 value = 0x5a5a5a5a;
	unsigned long flags;
	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		value = iwl_read32(trans, reg);
		iwl_trans_release_nic_access(trans, &flags);
	}

	return value;
}
IWL_EXPORT_SYMBOL(iwl_read_direct32);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		iwl_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct32);

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
IWL_EXPORT_SYMBOL(iwl_poll_direct_bit);

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
	u32 val = 0x5a5a5a5a;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		val = __iwl_read_prph(trans, ofs);
		iwl_trans_release_nic_access(trans, &flags);
	}
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read_prph);

void iwl_write_prph(struct iwl_trans *trans, u32 ofs, u32 val)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		__iwl_write_prph(trans, ofs, val);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_prph);

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		__iwl_write_prph(trans, ofs,
				 __iwl_read_prph(trans, ofs) | mask);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask)
{
	unsigned long flags;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		__iwl_write_prph(trans, ofs,
				 (__iwl_read_prph(trans, ofs) & mask) | bits);
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	unsigned long flags;
	u32 val;

	if (iwl_trans_grab_nic_access(trans, false, &flags)) {
		val = __iwl_read_prph(trans, ofs);
		__iwl_write_prph(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans, &flags);
	}
}
IWL_EXPORT_SYMBOL(iwl_clear_bits_prph);

static const char *get_fh_string(int cmd)
{
#define IWL_CMD(x) case x: return #x
	switch (cmd) {
	IWL_CMD(FH_RSCSR_CHNL0_STTS_WPTR_REG);
	IWL_CMD(FH_RSCSR_CHNL0_RBDCB_BASE_REG);
	IWL_CMD(FH_RSCSR_CHNL0_WPTR);
	IWL_CMD(FH_MEM_RCSR_CHNL0_CONFIG_REG);
	IWL_CMD(FH_MEM_RSSR_SHARED_CTRL_REG);
	IWL_CMD(FH_MEM_RSSR_RX_STATUS_REG);
	IWL_CMD(FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV);
	IWL_CMD(FH_TSSR_TX_STATUS_REG);
	IWL_CMD(FH_TSSR_TX_ERROR_REG);
	default:
		return "UNKNOWN";
	}
#undef IWL_CMD
}

int iwl_dump_fh(struct iwl_trans *trans, char **buf)
{
	int i;
	static const u32 fh_tbl[] = {
		FH_RSCSR_CHNL0_STTS_WPTR_REG,
		FH_RSCSR_CHNL0_RBDCB_BASE_REG,
		FH_RSCSR_CHNL0_WPTR,
		FH_MEM_RCSR_CHNL0_CONFIG_REG,
		FH_MEM_RSSR_SHARED_CTRL_REG,
		FH_MEM_RSSR_RX_STATUS_REG,
		FH_MEM_RSSR_RX_ENABLE_ERR_IRQ2DRV,
		FH_TSSR_TX_STATUS_REG,
		FH_TSSR_TX_ERROR_REG
	};

#ifdef CONFIG_IWLWIFI_DEBUGFS
	if (buf) {
		int pos = 0;
		size_t bufsz = ARRAY_SIZE(fh_tbl) * 48 + 40;

		*buf = kmalloc(bufsz, GFP_KERNEL);
		if (!*buf)
			return -ENOMEM;

		pos += scnprintf(*buf + pos, bufsz - pos,
				"FH register values:\n");

		for (i = 0; i < ARRAY_SIZE(fh_tbl); i++)
			pos += scnprintf(*buf + pos, bufsz - pos,
				"  %34s: 0X%08x\n",
				get_fh_string(fh_tbl[i]),
				iwl_read_direct32(trans, fh_tbl[i]));

		return pos;
	}
#endif

	IWL_ERR(trans, "FH register values:\n");
	for (i = 0; i <  ARRAY_SIZE(fh_tbl); i++)
		IWL_ERR(trans, "  %34s: 0X%08x\n",
			get_fh_string(fh_tbl[i]),
			iwl_read_direct32(trans, fh_tbl[i]));

	return 0;
}
