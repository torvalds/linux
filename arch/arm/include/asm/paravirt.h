/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_PARAVIRT_H
#define _ASM_ARM_PARAVIRT_H

#ifdef CONFIG_PARAVIRT
#include <linux/static_call_types.h>

u64 dummy_steal_clock(int cpu);

DECLARE_STATIC_CALL(pv_steal_clock, dummy_steal_clock);

static inline u64 paravirt_steal_clock(int cpu)
{
	return static_call(pv_steal_clock)(cpu);
}
#endif

#endif
