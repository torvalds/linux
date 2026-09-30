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

#define IWL_POLL_INTERVAL 10	/* microseconds */

int iwl_poll_bits_mask(struct iwl_trans *trans, u32 addr,
		       u32 bits, u32 mask, int timeout)
{
	int t = 0;

	do {
		if ((iwl_trans_read32(trans, addr) & mask) == (bits & mask))
			return 0;
		udelay(IWL_POLL_INTERVAL);
		t += IWL_POLL_INTERVAL;
	} while (t < timeout);

	return -ETIMEDOUT;
}
IWL_EXPORT_SYMBOL(iwl_poll_bits_mask);

void iwl_write_direct32(struct iwl_trans *trans, u32 reg, u32 value)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_trans_write32(trans, reg, value);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_direct32);

u32 iwl_read_prph_no_grab(struct iwl_trans *trans, u32 ofs)
{
	u32 val = iwl_trans_read_prph_no_grab(trans, ofs);
	trace_iwlwifi_dev_ioread_prph32(trans->dev, ofs, val);
	return val;
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
		iwl_trans_write_prph_no_grab(trans, ofs, val);
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_write_prph_delay);

void iwl_set_bits_prph(struct iwl_trans *trans, u32 ofs, u32 mask)
{
	if (iwl_trans_grab_nic_access(trans)) {
		iwl_trans_write_prph_no_grab(trans, ofs,
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
		iwl_trans_write_prph_no_grab(trans, ofs,
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
		iwl_trans_write_prph_no_grab(trans, ofs, (val & ~mask));
		iwl_trans_release_nic_access(trans);
	}
}
IWL_EXPORT_SYMBOL(iwl_clear_bits_prph);
