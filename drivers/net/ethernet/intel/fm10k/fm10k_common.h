/* Intel(R) Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2016 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _FM10K_COMMON_H_
#define _FM10K_COMMON_H_

#include "fm10k_type.h"

#define FM10K_REMOVED(hw_addr) unlikely(!(hw_addr))

/* PCI configuration read */
u16 fm10k_read_pci_cfg_word(struct fm10k_hw *hw, u32 reg);

/* read operations, indexed using DWORDS */
u32 fm10k_read_reg(struct fm10k_hw *hw, int reg);

/* write operations, indexed using DWORDS */
#define fm10k_write_reg(hw, reg, val) \
do { \
	u32 __iomem *hw_addr = READ_ONCE((hw)->hw_addr); \
	if (!FM10K_REMOVED(hw_addr)) \
		writel((val), &hw_addr[(reg)]); \
} while (0)

/* Switch register write operations, index using DWORDS */
#define fm10k_write_sw_reg(hw, reg, val) \
do { \
	u32 __iomem *sw_addr = READ_ONCE((hw)->sw_addr); \
	if (!FM10K_REMOVED(sw_addr)) \
		writel((val), &sw_addr[(reg)]); \
} while (0)

/* read ctrl register which has no clear on read fields as PCIe flush */
#define fm10k_write_flush(hw) fm10k_read_reg((hw), FM10K_CTRL)
s32 fm10k_get_bus_info_generic(struct fm10k_hw *hw);
s32 fm10k_get_invariants_generic(struct fm10k_hw *hw);
s32 fm10k_disable_queues_generic(struct fm10k_hw *hw, u16 q_cnt);
s32 fm10k_start_hw_generic(struct fm10k_hw *hw);
s32 fm10k_stop_hw_generic(struct fm10k_hw *hw);
u32 fm10k_read_hw_stats_32b(struct fm10k_hw *hw, u32 addr,
			    struct fm10k_hw_stat *stat);
#define fm10k_update_hw_base_32b(stat, delta) ((stat)->base_l += (delta))
void fm10k_update_hw_stats_q(struct fm10k_hw *hw, struct fm10k_hw_stats_q *q,
			     u32 idx, u32 count);
#define fm10k_unbind_hw_stats_32b(s) ((s)->base_h = 0)
void fm10k_unbind_hw_stats_q(struct fm10k_hw_stats_q *q, u32 idx, u32 count);
s32 fm10k_get_host_state_generic(struct fm10k_hw *hw, bool *host_ready);
#endif /* _FM10K_COMMON_H_ */
