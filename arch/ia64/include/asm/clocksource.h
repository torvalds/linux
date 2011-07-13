/* IA64-specific clocksource additions */

#ifndef _ASM_IA64_CLOCKSOURCE_H
#define _ASM_IA64_CLOCKSOURCE_H

#define __ARCH_HAS_CLOCKSOURCE_DATA

struct arch_clocksource_data {
	void *fsys_mmio;        /* used by fsyscall asm code */
};

#endif /* _ASM_IA64_CLOCKSOURCE_H */
