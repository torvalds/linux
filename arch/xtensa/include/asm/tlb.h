/*
 * include/asm-xtensa/tlb.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_TLB_H
#define _XTENSA_TLB_H

#include <asm/cache.h>
#include <asm/page.h>

#include <asm-generic/tlb.h>

#define __pte_free_tlb(tlb, pte, address)	pte_free((tlb)->mm, pte)

#endif	/* _XTENSA_TLB_H */
