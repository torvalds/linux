/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_MMU_H_
#define _ASM_POWERPC_NOHASH_MMU_H_

#ifdef CONFIG_PPC64
#include <asm/nohash/64/mmu.h>
#else
#include <asm/nohash/32/mmu.h>
#endif

#endif /* _ASM_POWERPC_NOHASH_MMU_H_ */
