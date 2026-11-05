// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2025-2026 Intel Corporation
 */

#ifndef __iwl_pcie_utils_h__
#define __iwl_pcie_utils_h__

#include "iwl-trans.h"
#include "internal.h"

void iwl_trans_pcie_dump_regs(struct iwl_trans *trans, struct pci_dev *pdev);

u32 iwl_pcie_read_direct32(struct iwl_trans *trans, u32 reg);
int iwl_pcie_poll_direct_bit(struct iwl_trans *trans,
			     u32 addr, u32 mask, int timeout);
int iwl_pcie_poll_prph_bit(struct iwl_trans *trans, u32 addr,
			   u32 bits, u32 mask, int timeout);
int iwl_pcie_poll_umac_prph_bit(struct iwl_trans *trans, u32 addr,
				u32 bits, u32 mask, int timeout);
int iwl_pcie_poll_umac_prph_bits_no_grab(struct iwl_trans *trans, u32 addr,
					 u32 bits, u32 mask, int timeout);
void iwl_pcie_write_prph64_no_grab(struct iwl_trans *trans, u32 ofs, u64 val);
void iwl_pcie_write_direct64(struct iwl_trans *trans, u64 reg, u64 value);

static inline void iwl_pcie_write64(struct iwl_trans *trans, u64 ofs, u64 val)
{
	iwl_trans_pcie_write32(trans, ofs, lower_32_bits(val));
	iwl_trans_pcie_write32(trans, ofs + 4, upper_32_bits(val));
}

static inline void iwl_pcie_write_umac_prph_no_grab(struct iwl_trans *trans,
						    u32 ofs, u32 val)
{
	iwl_pcie_write_prph_no_grab(trans, ofs + trans->mac_cfg->umac_prph_offset,
				    val);
}

static inline void iwl_pcie_set_bits_mask(struct iwl_trans *trans,
					  u32 reg, u32 mask, u32 value)
{
	u32 v;

#ifdef CONFIG_IWLWIFI_DEBUG
	WARN_ON_ONCE(value & ~mask);
#endif

	v = iwl_trans_pcie_read32(trans, reg);
	v &= ~mask;
	v |= value;
	iwl_trans_pcie_write32(trans, reg, v);
}

static inline void iwl_pcie_clear_bit(struct iwl_trans *trans,
				       u32 reg, u32 mask)
{
	iwl_pcie_set_bits_mask(trans, reg, mask, 0);
}

static inline void iwl_pcie_set_bit(struct iwl_trans *trans,
				     u32 reg, u32 mask)
{
	iwl_pcie_set_bits_mask(trans, reg, mask, mask);
}

#endif /* __iwl_pcie_utils_h__ */
