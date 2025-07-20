/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Copyright IBM Corp. 2017
 *    Author(s): Claudio Imbrenda <imbrenda@linux.vnet.ibm.com>
 */

#ifndef PAGE_STATES_H
#define PAGE_STATES_H

#include <asm/page.h>

#define ESSA_GET_STATE			0
#define ESSA_SET_STABLE			1
#define ESSA_SET_UNUSED			2
#define ESSA_SET_VOLATILE		3
#define ESSA_SET_POT_VOLATILE		4
#define ESSA_SET_STABLE_RESIDENT	5
#define ESSA_SET_STABLE_IF_RESIDENT	6
#define ESSA_SET_STABLE_NODAT		7

#define ESSA_MAX	ESSA_SET_STABLE_NODAT

extern int cmma_flag;

static __always_inline unsigned long essa(unsigned long paddr, unsigned char cmd)
{
	unsigned long rc;

	asm volatile(
		"	.insn	rrf,0xb9ab0000,%[rc],%[paddr],%[cmd],0"
		: [rc] "=d" (rc)
		: [paddr] "d" (paddr),
		  [cmd] "i" (cmd));
	return rc;
}

static __always_inline void __set_page_state(void *addr, unsigned long num_pages, unsigned char cmd)
{
	unsigned long paddr = __pa(addr) & PAGE_MASK;

	while (num_pages--) {
		essa(paddr, cmd);
		paddr += PAGE_SIZE;
	}
}

static inline void __set_page_unused(void *addr, unsigned long num_pages)
{
	__set_page_state(addr, num_pages, ESSA_SET_UNUSED);
}

static inline void __set_page_stable_dat(void *addr, unsigned long num_pages)
{
	__set_page_state(addr, num_pages, ESSA_SET_STABLE);
}

static inline void __set_page_stable_nodat(void *addr, unsigned long num_pages)
{
	__set_page_state(addr, num_pages, ESSA_SET_STABLE_NODAT);
}

static inline void __arch_set_page_nodat(void *addr, unsigned long num_pages)
{
	if (!cmma_flag)
		return;
	if (cmma_flag < 2)
		__set_page_stable_dat(addr, num_pages);
	else
		__set_page_stable_nodat(addr, num_pages);
}

static inline void __arch_set_page_dat(void *addr, unsigned long num_pages)
{
	if (!cmma_flag)
		return;
	__set_page_stable_dat(addr, num_pages);
}

#endif
