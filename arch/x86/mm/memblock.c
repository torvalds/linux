#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/range.h>

void __init memblock_x86_reserve_range(u64 start, u64 end, char *name)
{
	if (start == end)
		return;

	if (WARN_ONCE(start > end, "memblock_x86_reserve_range: wrong range [%#llx, %#llx)\n", start, end))
		return;

	memblock_dbg("    memblock_x86_reserve_range: [%#010llx-%#010llx] %16s\n", start, end - 1, name);

	memblock_reserve(start, end - start);
}

void __init memblock_x86_free_range(u64 start, u64 end)
{
	if (start == end)
		return;

	if (WARN_ONCE(start > end, "memblock_x86_free_range: wrong range [%#llx, %#llx)\n", start, end))
		return;

	memblock_dbg("       memblock_x86_free_range: [%#010llx-%#010llx]\n", start, end - 1);

	memblock_free(start, end - start);
}

/*
 * Finds an active region in the address range from start_pfn to last_pfn and
 * returns its range in ei_startpfn and ei_endpfn for the memblock entry.
 */
static int __init memblock_x86_find_active_region(const struct memblock_region *ei,
				  unsigned long start_pfn,
				  unsigned long last_pfn,
				  unsigned long *ei_startpfn,
				  unsigned long *ei_endpfn)
{
	u64 align = PAGE_SIZE;

	*ei_startpfn = round_up(ei->base, align) >> PAGE_SHIFT;
	*ei_endpfn = round_down(ei->base + ei->size, align) >> PAGE_SHIFT;

	/* Skip map entries smaller than a page */
	if (*ei_startpfn >= *ei_endpfn)
		return 0;

	/* Skip if map is outside the node */
	if (*ei_endpfn <= start_pfn || *ei_startpfn >= last_pfn)
		return 0;

	/* Check for overlaps */
	if (*ei_startpfn < start_pfn)
		*ei_startpfn = start_pfn;
	if (*ei_endpfn > last_pfn)
		*ei_endpfn = last_pfn;

	return 1;
}

/*
 * Find the hole size (in bytes) in the memory range.
 * @start: starting address of the memory range to scan
 * @end: ending address of the memory range to scan
 */
u64 __init memblock_x86_hole_size(u64 start, u64 end)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long last_pfn = end >> PAGE_SHIFT;
	unsigned long ei_startpfn, ei_endpfn, ram = 0;
	struct memblock_region *r;

	for_each_memblock(memory, r)
		if (memblock_x86_find_active_region(r, start_pfn, last_pfn,
					   &ei_startpfn, &ei_endpfn))
			ram += ei_endpfn - ei_startpfn;

	return end - start - ((u64)ram << PAGE_SHIFT);
}
