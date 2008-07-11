/*
 * arch/s390/mm/page-states.c
 *
 * Copyright IBM Corp. 2008
 *
 * Guest page hinting for unused pages.
 *
 * Author(s): Martin Schwidefsky <schwidefsky@de.ibm.com>
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/init.h>

#define ESSA_SET_STABLE		1
#define ESSA_SET_UNUSED		2

static int cmma_flag;

static int __init cmma(char *str)
{
	char *parm;
	parm = strstrip(str);
	if (strcmp(parm, "yes") == 0 || strcmp(parm, "on") == 0) {
		cmma_flag = 1;
		return 1;
	}
	cmma_flag = 0;
	if (strcmp(parm, "no") == 0 || strcmp(parm, "off") == 0)
		return 1;
	return 0;
}

__setup("cmma=", cmma);

void __init cmma_init(void)
{
	register unsigned long tmp asm("0") = 0;
	register int rc asm("1") = -EOPNOTSUPP;

	if (!cmma_flag)
		return;
	asm volatile(
		"       .insn rrf,0xb9ab0000,%1,%1,0,0\n"
		"0:     la      %0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+&d" (rc), "+&d" (tmp));
	if (rc)
		cmma_flag = 0;
}

void arch_free_page(struct page *page, int order)
{
	int i, rc;

	if (!cmma_flag)
		return;
	for (i = 0; i < (1 << order); i++)
		asm volatile(".insn rrf,0xb9ab0000,%0,%1,%2,0"
			     : "=&d" (rc)
			     : "a" ((page_to_pfn(page) + i) << PAGE_SHIFT),
			       "i" (ESSA_SET_UNUSED));
}

void arch_alloc_page(struct page *page, int order)
{
	int i, rc;

	if (!cmma_flag)
		return;
	for (i = 0; i < (1 << order); i++)
		asm volatile(".insn rrf,0xb9ab0000,%0,%1,%2,0"
			     : "=&d" (rc)
			     : "a" ((page_to_pfn(page) + i) << PAGE_SHIFT),
			       "i" (ESSA_SET_STABLE));
}
