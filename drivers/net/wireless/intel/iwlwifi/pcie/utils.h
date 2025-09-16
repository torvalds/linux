// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025 Intel Corporation
 */

#ifndef __iwl_pcie_utils_h__
#define __iwl_pcie_utils_h__

#include "iwl-io.h"

void iwl_trans_pcie_dump_regs(struct iwl_trans *trans, struct pci_dev *pdev);

static inline void _iwl_trans_set_bits_mask(struct iwl_trans *trans,
					    u32 reg, u32 mask, u32 value)
{
	u32 v;

#ifdef CONFIG_IWLWIFI_DEBUG
	WARN_ON_ONCE(value & ~mask);
#endif

	v = iwl_read32(trans, reg);
	v &= ~mask;
	v |= value;
	iwl_write32(trans, reg, v);
}

static inline void iwl_trans_clear_bit(struct iwl_trans *trans,
				       u32 reg, u32 mask)
{
	_iwl_trans_set_bits_mask(trans, reg, mask, 0);
}

static inline void iwl_trans_set_bit(struct iwl_trans *trans,
				     u32 reg, u32 mask)
{
	_iwl_trans_set_bits_mask(trans, reg, mask, mask);
}

#endif /* __iwl_pcie_utils_h__ */
