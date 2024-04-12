/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_XTENSA_CACHETYPE_H
#define __ASM_XTENSA_CACHETYPE_H

#include <asm/cache.h>
#include <asm/page.h>

#define cpu_dcache_is_aliasing()	(DCACHE_WAY_SIZE > PAGE_SIZE)

#endif
