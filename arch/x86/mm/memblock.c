#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/range.h>

static __init struct range *find_range_array(int count)
{
	u64 end, size, mem;
	struct range *range;

	size = sizeof(struct range) * count;
	end = memblock.current_limit;

	mem = memblock_find_in_range(0, end, size, sizeof(struct range));
	if (!mem)
		panic("can not find more space for range array");

	/*
	 * This range is tempoaray, so don't reserve it, it will not be
	 * overlapped because We will not alloccate new buffer before
	 * We discard this one
	 */
	range = __va(mem);
	memset(range, 0, size);

	return range;
}

static u64 __init __memblock_x86_memory_in_range(u64 addr, u64 limit, bool get_free)
{
	int i, count;
	struct range *range;
	int nr_range;
	u64 final_start, final_end;
	u64 free_size;
	struct memblock_region *r;

	count = (memblock.reserved.cnt + memblock.memory.cnt) * 2;

	range = find_range_array(count);
	nr_range = 0;

	addr = PFN_UP(addr);
	limit = PFN_DOWN(limit);

	for_each_memblock(memory, r) {
		final_start = PFN_UP(r->base);
		final_end = PFN_DOWN(r->base + r->size);
		if (final_start >= final_end)
			continue;
		if (final_start >= limit || final_end <= addr)
			continue;

		nr_range = add_range(range, count, nr_range, final_start, final_end);
	}
	subtract_range(range, count, 0, addr);
	subtract_range(range, count, limit, -1ULL);

	/* Subtract memblock.reserved.region in range ? */
	if (!get_free)
		goto sort_and_count_them;
	for_each_memblock(reserved, r) {
		final_start = PFN_DOWN(r->base);
		final_end = PFN_UP(r->base + r->size);
		if (final_start >= final_end)
			continue;
		if (final_start >= limit || final_end <= addr)
			continue;

		subtract_range(range, count, final_start, final_end);
	}

sort_and_count_them:
	nr_range = clean_sort_range(range, count);

	free_size = 0;
	for (i = 0; i < nr_range; i++)
		free_size += range[i].end - range[i].start;

	return free_size << PAGE_SHIFT;
}

u64 __init memblock_x86_free_memory_in_range(u64 addr, u64 limit)
{
	return __memblock_x86_memory_in_range(addr, limit, true);
}

u64 __init memblock_x86_memory_in_range(u64 addr, u64 limit)
{
	return __memblock_x86_memory_in_range(addr, limit, false);
}

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
