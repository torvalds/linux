// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2003-2014, 2018-2022, 2024-2026 Intel Corporation
 * Copyright (C) 2015-2016 Intel Deutschland GmbH
 */
#include <linux/device.h>
#include <linux/export.h>

#include "iwl-drv.h"
#include "iwl-io.h"
#include "iwl-csr.h"
#include "iwl-debug.h"

void iwl_write8(struct iwl_trans *trans, u32 ofs, u8 val)
{
	trace_iwlwifi_dev_iowrite8(trans->dev, ofs, val);
	iwl_trans_write8(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write8);

void iwl_write32(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite32(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, val);
}
IWL_EXPORT_SYMBOL(iwl_write32);

void iwl_write64(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite64(trans->dev, ofs, val);
	iwl_trans_write32(trans, ofs, lower_32_bits(val));
	iwl_trans_write32(trans, ofs + 4, upper_32_bits(val));
}
IWL_EXPORT_SYMBOL(iwl_write64);

u32 iwl_read32(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read32(trans, ofs);

	trace_iwlwifi_dev_ioread32(trans->dev, ofs, val);
	return val;
}
IWL_EXPORT_SYMBOL(iwl_read32);

#define IWL_POLL_INTERVAL 10	/* microseconds */

int iwl_poll_bits_mask(struct iwl_trans *trans, u32 addr,
		       u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read32(trans, addr) & mask) == (bits & mask))
			return 0;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_bits_mask);

u32 iwl_read_direct32(struct iwl_trans *trans, u32 reg)
{
	if (iwl_trans_grab_nic_access(trans)) {
		u32 value = iwl_read32(trans, reg);

		iwl_trans_release_nic_access(trans);
		return value;
	}

	/* return as if we have a HW timeout/failure */
	return 0x5a5a5a5a;
}

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct32);

void iwl_write_direct64(struct iwl_trans *trans, u64 reg, u64 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write64(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}

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

u32 iwl_read_prph_no_grab(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read_prph(trans, ofs);
	trace_iwlwifi_dev_ioread_prph32(trans->dev, ofs, val);
	return val;
}

void iwl_write_prph_no_grab(struct iwl_trans *trans, u32 ofs, u32 val)
{
	trace_iwlwifi_dev_iowrite_prph32(trans->dev, ofs, val);
	iwl_trans_write_prph(trans, ofs, val);
}

void iwl_write_prph64_no_grab(struct iwl_trans *trans, u64 ofs, u64 val)
{
	trace_iwlwifi_dev_iowrite_prph64(trans->dev, ofs, val);
	iwl_write_prph_no_grab(trans, ofs, val & 0xffffffff);
	iwl_write_prph_no_grab(trans, ofs + 4, val >> 32);
}

u32 iwl_read_prph(struct iwl_trans *trans, u32 ofs)
{
	if (iwl_trans_grab_nic_access(trans)) {
		u32 val = iwl_read_prph_no_grab(trans, ofs);

		iwl_trans_release_nic_access(trans);

		return val;
	}

	/* return as if we have a HW timeout/failure */
	return 0x5a5a5a5a;
}
IWL_EXPORT_SYMBOL(iwl_read_prph);

void iwl_write_prph_delay(struct iwl_trans *trans, u32 ofs, u32 val, u32 delay_ms)
{
	if (iwl_trans_grab_nic_access(trans)) {
		mdelay(delay_ms);
		iwl_write_prph_no_grab(trans, ofs, val);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_prph_delay);

int iwl_poll_prph_bit(struct iwl_trans *trans, u32 addr,
		      u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_prph(trans, addr) & mask) == (bits & mask))
			return 0;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

int iwl_poll_umac_prph_bits_no_grab(struct iwl_trans *trans, u32 addr,
				    u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_read_umac_prph_no_grab(trans, addr) & mask) ==
		    (bits & mask))
			return 0;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write_prph_no_grab(trans, ofs,
				       iwl_read_prph_no_grab(trans, ofs) |
				       mask);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_prph);

void iwl_set_bits_mask_prph(struct iwl_trans *trans, u32 ofs,
			    u32 bits, u32 mask)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_write_prph_no_grab(trans, ofs,
				       (iwl_read_prph_no_grab(trans, ofs) &
					mask) | bits);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_set_bits_mask_prph);

void iwl_clear_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	u32 val;

	if (iwl_trans_grab_nic_access(trans)) {
		val = iwl_read_prph_no_grab(trans, ofs);
		iwl_write_prph_no_grab(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_clear_bits_prph);
