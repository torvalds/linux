/* $Id: memory.c,v 1.5 1999/08/31 06:55:04 davem Exp $
 * memory.c: Prom routine for acquiring various bits of information
 *           about RAM on the machine, both virtual and physical.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/init.h>

#include <asm/openprom.h>
#include <asm/oplib.h>

/* This routine, for consistency, returns the ram parameters in the
 * V0 prom memory descriptor format.  I choose this format because I
 * think it was the easiest to work with.  I feel the religious
 * arguments now... ;)  Also, I return the linked lists sorted to
 * prevent paging_init() upset stomach as I have not yet written
 * the pepto-bismol kernel module yet.
 */

struct linux_prom64_registers prom_reg_memlist[64];
struct linux_prom64_registers prom_reg_tmp[64];

struct linux_mlist_p1275 prom_phys_total[64];
struct linux_mlist_p1275 prom_prom_taken[64];
struct linux_mlist_p1275 prom_phys_avail[64];

struct linux_mlist_p1275 *prom_ptot_ptr = prom_phys_total;
struct linux_mlist_p1275 *prom_ptak_ptr = prom_prom_taken;
struct linux_mlist_p1275 *prom_pavl_ptr = prom_phys_avail;

struct linux_mem_p1275 prom_memlist;


/* Internal Prom library routine to sort a linux_mlist_p1275 memory
 * list.  Used below in initialization.
 */
static void __init
prom_sortmemlist(struct linux_mlist_p1275 *thislist)
{
	int swapi = 0;
	int i, mitr;
	unsigned long tmpaddr, tmpsize;
	unsigned long lowest;

	for(i=0; thislist[i].theres_more; i++) {
		lowest = thislist[i].start_adr;
		for(mitr = i+1; thislist[mitr-1].theres_more; mitr++)
			if(thislist[mitr].start_adr < lowest) {
				lowest = thislist[mitr].start_adr;
				swapi = mitr;
			}
		if(lowest == thislist[i].start_adr) continue;
		tmpaddr = thislist[swapi].start_adr;
		tmpsize = thislist[swapi].num_bytes;
		for(mitr = swapi; mitr > i; mitr--) {
			thislist[mitr].start_adr = thislist[mitr-1].start_adr;
			thislist[mitr].num_bytes = thislist[mitr-1].num_bytes;
		}
		thislist[i].start_adr = tmpaddr;
		thislist[i].num_bytes = tmpsize;
	}
}

/* Initialize the memory lists based upon the prom version. */
void __init prom_meminit(void)
{
	int node = 0;
	unsigned int iter, num_regs;

	node = prom_finddevice("/memory");
	num_regs = prom_getproperty(node, "available",
				    (char *) prom_reg_memlist,
				    sizeof(prom_reg_memlist));
	num_regs = (num_regs/sizeof(struct linux_prom64_registers));
	for(iter=0; iter<num_regs; iter++) {
		prom_phys_avail[iter].start_adr =
			prom_reg_memlist[iter].phys_addr;
		prom_phys_avail[iter].num_bytes =
			prom_reg_memlist[iter].reg_size;
		prom_phys_avail[iter].theres_more =
			&prom_phys_avail[iter+1];
	}
	prom_phys_avail[iter-1].theres_more = NULL;

	num_regs = prom_getproperty(node, "reg",
				    (char *) prom_reg_memlist,
				    sizeof(prom_reg_memlist));
	num_regs = (num_regs/sizeof(struct linux_prom64_registers));
	for(iter=0; iter<num_regs; iter++) {
		prom_phys_total[iter].start_adr =
			prom_reg_memlist[iter].phys_addr;
		prom_phys_total[iter].num_bytes =
			prom_reg_memlist[iter].reg_size;
		prom_phys_total[iter].theres_more =
			&prom_phys_total[iter+1];
	}
	prom_phys_total[iter-1].theres_more = NULL;

	node = prom_finddevice("/virtual-memory");
	num_regs = prom_getproperty(node, "available",
				    (char *) prom_reg_memlist,
				    sizeof(prom_reg_memlist));
	num_regs = (num_regs/sizeof(struct linux_prom64_registers));

	/* Convert available virtual areas to taken virtual
	 * areas.  First sort, then convert.
	 */
	for(iter=0; iter<num_regs; iter++) {
		prom_prom_taken[iter].start_adr =
			prom_reg_memlist[iter].phys_addr;
		prom_prom_taken[iter].num_bytes =
			prom_reg_memlist[iter].reg_size;
		prom_prom_taken[iter].theres_more =
			&prom_prom_taken[iter+1];
	}
	prom_prom_taken[iter-1].theres_more = NULL;

	prom_sortmemlist(prom_prom_taken);

	/* Finally, convert. */
	for(iter=0; iter<num_regs; iter++) {
		prom_prom_taken[iter].start_adr =
			prom_prom_taken[iter].start_adr +
			prom_prom_taken[iter].num_bytes;
		prom_prom_taken[iter].num_bytes =
			prom_prom_taken[iter+1].start_adr -
			prom_prom_taken[iter].start_adr;
	}
	prom_prom_taken[iter-1].num_bytes =
		-1UL - prom_prom_taken[iter-1].start_adr;

	/* Sort the other two lists. */
	prom_sortmemlist(prom_phys_total);
	prom_sortmemlist(prom_phys_avail);

	/* Link all the lists into the top-level descriptor. */
	prom_memlist.p1275_totphys=&prom_ptot_ptr;
	prom_memlist.p1275_prommap=&prom_ptak_ptr;
	prom_memlist.p1275_available=&prom_pavl_ptr;
}

/* This returns a pointer to our libraries internal p1275 format
 * memory descriptor.
 */
struct linux_mem_p1275 *
prom_meminfo(void)
{
	return &prom_memlist;
}
