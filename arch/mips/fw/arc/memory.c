/*
 * memory.c: PROM library functions for acquiring/using memory descriptors
 *	     given to us from the ARCS firmware.
 *
 * Copyright (C) 1996 by David S. Miller
 * Copyright (C) 1999, 2000, 2001 by Ralf Baechle
 * Copyright (C) 1999, 2000 by Silicon Graphics, Inc.
 *
 * PROM library functions for acquiring/using memory descriptors given to us
 * from the ARCS firmware.  This is only used when CONFIG_ARC_MEMORY is set
 * because on some machines like SGI IP27 the ARC memory configuration data
 * completly bogus and alternate easier to use mechanisms are available.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/swap.h>

#include <asm/sgialib.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/bootinfo.h>

#undef DEBUG

/*
 * For ARC firmware memory functions the unit of meassuring memory is always
 * a 4k page of memory
 */
#define ARC_PAGE_SHIFT	12

struct linux_mdesc * __init ArcGetMemoryDescriptor(struct linux_mdesc *Current)
{
	return (struct linux_mdesc *) ARC_CALL1(get_mdesc, Current);
}

#ifdef DEBUG /* convenient for debugging */
static char *arcs_mtypes[8] = {
	"Exception Block",
	"ARCS Romvec Page",
	"Free/Contig RAM",
	"Generic Free RAM",
	"Bad Memory",
	"Standalone Program Pages",
	"ARCS Temp Storage Area",
	"ARCS Permanent Storage Area"
};

static char *arc_mtypes[8] = {
	"Exception Block",
	"SystemParameterBlock",
	"FreeMemory",
	"Bad Memory",
	"LoadedProgram",
	"FirmwareTemporary",
	"FirmwarePermanent",
	"FreeContiguous"
};
#define mtypes(a) (prom_flags & PROM_FLAG_ARCS) ? arcs_mtypes[a.arcs] \
						: arc_mtypes[a.arc]
#endif

static inline int memtype_classify_arcs(union linux_memtypes type)
{
	switch (type.arcs) {
	case arcs_fcontig:
	case arcs_free:
		return BOOT_MEM_RAM;
	case arcs_atmp:
		return BOOT_MEM_ROM_DATA;
	case arcs_eblock:
	case arcs_rvpage:
	case arcs_bmem:
	case arcs_prog:
	case arcs_aperm:
		return BOOT_MEM_RESERVED;
	default:
		BUG();
	}
	while(1);				/* Nuke warning.  */
}

static inline int memtype_classify_arc(union linux_memtypes type)
{
	switch (type.arc) {
	case arc_free:
	case arc_fcontig:
		return BOOT_MEM_RAM;
	case arc_atmp:
		return BOOT_MEM_ROM_DATA;
	case arc_eblock:
	case arc_rvpage:
	case arc_bmem:
	case arc_prog:
	case arc_aperm:
		return BOOT_MEM_RESERVED;
	default:
		BUG();
	}
	while(1);				/* Nuke warning.  */
}

static int __init prom_memtype_classify(union linux_memtypes type)
{
	if (prom_flags & PROM_FLAG_ARCS)	/* SGI is ``different'' ... */
		return memtype_classify_arcs(type);

	return memtype_classify_arc(type);
}

void __init prom_meminit(void)
{
	struct linux_mdesc *p;

#ifdef DEBUG
	int i = 0;

	printk("ARCS MEMORY DESCRIPTOR dump:\n");
	p = ArcGetMemoryDescriptor(PROM_NULL_MDESC);
	while(p) {
		printk("[%d,%p]: base<%08lx> pages<%08lx> type<%s>\n",
		       i, p, p->base, p->pages, mtypes(p->type));
		p = ArcGetMemoryDescriptor(p);
		i++;
	}
#endif

	p = PROM_NULL_MDESC;
	while ((p = ArcGetMemoryDescriptor(p))) {
		unsigned long base, size;
		long type;

		base = p->base << ARC_PAGE_SHIFT;
		size = p->pages << ARC_PAGE_SHIFT;
		type = prom_memtype_classify(p->type);

		add_memory_region(base, size, type);
	}
}

void __init prom_free_prom_memory(void)
{
	unsigned long addr;
	int i;

	if (prom_flags & PROM_FLAG_DONT_FREE_TEMP)
		return;

	for (i = 0; i < boot_mem_map.nr_map; i++) {
		if (boot_mem_map.map[i].type != BOOT_MEM_ROM_DATA)
			continue;

		addr = boot_mem_map.map[i].addr;
		free_init_pages("prom memory",
				addr, addr + boot_mem_map.map[i].size);
	}
}
