/* SPDX-License-Identifier: GPL-2.0 */
/* x86-specific clocksource additions */

#ifndef _ASM_X86_CLOCKSOURCE_H
#define _ASM_X86_CLOCKSOURCE_H

#include <asm/vdso/clocksource.h>

struct arch_clocksource_data {
	int vclock_mode;
};

#endif /* _ASM_X86_CLOCKSOURCE_H */
