/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_TLB_H
#define __ASM_TLB_H

#include <asm/cpu-features.h>
#include <asm/mipsregs.h>

#define _UNIQUE_ENTRYHI(base, idx)					\
		(((base) + ((idx) << (PAGE_SHIFT + 1))) |		\
		 (cpu_has_tlbinv ? MIPS_ENTRYHI_EHINV : 0))
#define UNIQUE_ENTRYHI(idx)		_UNIQUE_ENTRYHI(CKSEG0, idx)
#define UNIQUE_GUEST_ENTRYHI(idx)	_UNIQUE_ENTRYHI(CKSEG1, idx)

static inline unsigned int num_wired_entries(void)
{
	unsigned int wired = read_c0_wired();

	if (cpu_has_mips_r6)
		wired &= MIPSR6_WIRED_WIRED;

	return wired;
}

#include <asm-generic/tlb.h>

#endif /* __ASM_TLB_H */
