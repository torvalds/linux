/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_SKEY_H
#define __ASM_SKEY_H

#include <asm/rwonce.h>

struct skey_region {
	unsigned long start;
	unsigned long end;
};

#define SKEY_REGION(_start, _end)			\
	stringify_in_c(.section .skey_region,"a";)	\
	stringify_in_c(.balign 8;)			\
	stringify_in_c(.quad (_start);)			\
	stringify_in_c(.quad (_end);)			\
	stringify_in_c(.previous)

extern int skey_regions_initialized;
extern struct skey_region __skey_region_start[];
extern struct skey_region __skey_region_end[];

void __skey_regions_initialize(void);

static inline void skey_regions_initialize(void)
{
	if (READ_ONCE(skey_regions_initialized))
		return;
	__skey_regions_initialize();
}

#endif /* __ASM_SKEY_H */
