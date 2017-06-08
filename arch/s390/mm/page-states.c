/*
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
#include <linux/gfp.h>
#include <linux/init.h>

#include <asm/page-states.h>

static int cmma_flag = 1;

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

static inline int cmma_test_essa(void)
{
	register unsigned long tmp asm("0") = 0;
	register int rc asm("1") = -EOPNOTSUPP;

	asm volatile(
		"       .insn rrf,0xb9ab0000,%1,%1,0,0\n"
		"0:     la      %0,0\n"
		"1:\n"
		EX_TABLE(0b,1b)
		: "+&d" (rc), "+&d" (tmp));
	return rc;
}

void __init cmma_init(void)
{
	if (!cmma_flag)
		return;
	if (cmma_test_essa())
		cmma_flag = 0;
}

static inline void set_page_unstable(struct page *page, int order)
{
	int i, rc;

	for (i = 0; i < (1 << order); i++)
		asm volatile(".insn rrf,0xb9ab0000,%0,%1,%2,0"
			     : "=&d" (rc)
			     : "a" (page_to_phys(page + i)),
			       "i" (ESSA_SET_UNUSED));
}

void arch_free_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	set_page_unstable(page, order);
}

static inline void set_page_stable(struct page *page, int order)
{
	int i, rc;

	for (i = 0; i < (1 << order); i++)
		asm volatile(".insn rrf,0xb9ab0000,%0,%1,%2,0"
			     : "=&d" (rc)
			     : "a" (page_to_phys(page + i)),
			       "i" (ESSA_SET_STABLE));
}

void arch_alloc_page(struct page *page, int order)
{
	if (!cmma_flag)
		return;
	set_page_stable(page, order);
}

void arch_set_page_states(int make_stable)
{
	unsigned long flags, order, t;
	struct list_head *l;
	struct page *page;
	struct zone *zone;

	if (!cmma_flag)
		return;
	if (make_stable)
		drain_local_pages(NULL);
	for_each_populated_zone(zone) {
		spin_lock_irqsave(&zone->lock, flags);
		for_each_migratetype_order(order, t) {
			list_for_each(l, &zone->free_area[order].free_list[t]) {
				page = list_entry(l, struct page, lru);
				if (make_stable)
					set_page_stable(page, order);
				else
					set_page_unstable(page, order);
			}
		}
		spin_unlock_irqrestore(&zone->lock, flags);
	}
}
