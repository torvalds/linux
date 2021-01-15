/* SPDX-License-Identifier: GPL-2.0-only
 * Copyright (C) 2020 Marvell.
 */

#ifndef __OTX2_CPT_COMMON_H
#define __OTX2_CPT_COMMON_H

#include <linux/pci.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/crypto.h>
#include "otx2_cpt_hw_types.h"
#include "rvu.h"

#define OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs) \
		(((blk) << 20) | ((slot) << 12) | (offs))

static inline void otx2_cpt_write64(void __iomem *reg_base, u64 blk, u64 slot,
				    u64 offs, u64 val)
{
	writeq_relaxed(val, reg_base +
		       OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs));
}

static inline u64 otx2_cpt_read64(void __iomem *reg_base, u64 blk, u64 slot,
				  u64 offs)
{
	return readq_relaxed(reg_base +
			     OTX2_CPT_RVU_FUNC_ADDR_S(blk, slot, offs));
}
#endif /* __OTX2_CPT_COMMON_H */
