/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_NOHASH_32_MMU_H_
#define _ASM_POWERPC_NOHASH_32_MMU_H_

#if defined(CONFIG_40x)
/* 40x-style software loaded TLB */
#include <asm/nohash/32/mmu-40x.h>
#elif defined(CONFIG_44x)
/* 44x-style software loaded TLB */
#include <asm/nohash/32/mmu-44x.h>
#elif defined(CONFIG_PPC_BOOK3E_MMU)
/* Freescale Book-E software loaded TLB or Book-3e (ISA 2.06+) MMU */
#include <asm/nohash/mmu-book3e.h>
#elif defined (CONFIG_PPC_8xx)
/* Motorola/Freescale 8xx software loaded TLB */
#include <asm/nohash/32/mmu-8xx.h>
#endif

#endif /* _ASM_POWERPC_NOHASH_32_MMU_H_ */
