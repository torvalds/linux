/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NSC/Cyrix CPU indexed register access. Must be inlined instead of
 * macros to ensure correct access ordering
 * Access order is always 0x22 (=offset), 0x23 (=value)
 */

#include <asm/pc-conf-reg.h>

static inline u8 getCx86(u8 reg)
{
	return pc_conf_get(reg);
}

static inline void setCx86(u8 reg, u8 data)
{
	pc_conf_set(reg, data);
}
