/* memory.c: Prom routine for acquiring various bits of information
 *           about RAM on the machine, both virtual and physical.
 *
 * Copyright (C) 1995, 2008 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1997 Michael A. Griffith (grif@acm.org)
 */

#include <linux/kernel.h>
#include <linux/sort.h>
#include <linux/init.h>

#include <asm/openprom.h>
#include <asm/sun4prom.h>
#include <asm/oplib.h>
#include <asm/page.h>

static int __init prom_meminit_v0(void)
{
	struct linux_mlist_v0 *p;
	int index;

	index = 0;
	for (p = *(romvec->pv_v0mem.v0_available); p; p = p->theres_more) {
		sp_banks[index].base_addr = (unsigned long) p->start_adr;
		sp_banks[index].num_bytes = p->num_bytes;
		index++;
	}

	return index;
}

static int __init prom_meminit_v2(void)
{
	struct linux_prom_registers reg[64];
	int node, size, num_ents, i;

	node = prom_searchsiblings(prom_getchild(prom_root_node), "memory");
	size = prom_getproperty(node, "available", (char *) reg, sizeof(reg));
	num_ents = size / sizeof(struct linux_prom_registers);

	for (i = 0; i < num_ents; i++) {
		sp_banks[i].base_addr = reg[i].phys_addr;
		sp_banks[i].num_bytes = reg[i].reg_size;
	}

	return num_ents;
}

static int __init prom_meminit_sun4(void)
{
#ifdef CONFIG_SUN4
	sp_banks[0].base_addr = 0;
	sp_banks[0].num_bytes = *(sun4_romvec->memoryavail);
#endif
	return 1;
}

static int sp_banks_cmp(const void *a, const void *b)
{
	const struct sparc_phys_banks *x = a, *y = b;

	if (x->base_addr > y->base_addr)
		return 1;
	if (x->base_addr < y->base_addr)
		return -1;
	return 0;
}

/* Initialize the memory lists based upon the prom version. */
void __init prom_meminit(void)
{
	int i, num_ents = 0;

	switch (prom_vers) {
	case PROM_V0:
		num_ents = prom_meminit_v0();
		break;

	case PROM_V2:
	case PROM_V3:
		num_ents = prom_meminit_v2();
		break;

	case PROM_SUN4:
		num_ents = prom_meminit_sun4();
		break;

	default:
		break;
	}
	sort(sp_banks, num_ents, sizeof(struct sparc_phys_banks),
	     sp_banks_cmp, NULL);

	/* Sentinel.  */
	sp_banks[num_ents].base_addr = 0xdeadbeef;
	sp_banks[num_ents].num_bytes = 0;

	for (i = 0; i < num_ents; i++)
		sp_banks[i].num_bytes &= PAGE_MASK;
}
