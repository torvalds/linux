#include <linux/mm.h>

#include <asm/pgtable.h>

#include "pte.h"

pte_t *kmemcheck_pte_lookup(unsigned long address)
{
	pte_t *pte;
	unsigned int level;

	pte = lookup_address(address, &level);
	if (!pte)
		return NULL;
	if (level != PG_LEVEL_4K)
		return NULL;
	if (!pte_hidden(*pte))
		return NULL;

	return pte;
}

