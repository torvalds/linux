/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HYPERV_TIMER_H
#define _ASM_X86_HYPERV_TIMER_H

#include <asm/msr.h>

#define hv_get_raw_timer() rdtsc_ordered()

#endif
