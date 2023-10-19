/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASMARM_KRAIT_L2_ACCESSORS_H
#define __ASMARM_KRAIT_L2_ACCESSORS_H

extern void krait_set_l2_indirect_reg(u32 addr, u32 val);
extern u32 krait_get_l2_indirect_reg(u32 addr);

#endif
