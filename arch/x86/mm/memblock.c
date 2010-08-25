#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/memblock.h>
#include <linux/bootmem.h>
#include <linux/mm.h>
#include <linux/range.h>

/* Check for already reserved areas */
static inline bool __init bad_addr_size(u64 *addrp, u64 *sizep, u64 align)
{
	struct memblock_region *r;
	u64 addr = *addrp, last;
	u64 size = *sizep;
	bool changed = false;

again:
	last = addr + size;
	for_each_memblock(reserved, r) {
		if (last > r->base && addr < r->base) {
			size = r->base - addr;
			changed = true;
			goto again;
		}
		if (last > (r->base + r->size) && addr < (r->base + r->size)) {
			addr = round_up(r->base + r->size, align);
			size = last - addr;
			changed = true;
			goto again;
		}
		if (last <= (r->base + r->size) && addr >= r->base) {
			(*sizep)++;
			return false;
		}
	}
	if (changed) {
		*addrp = addr;
		*sizep = size;
	}
	return changed;
}

static u64 __init __memblock_x86_find_in_range_size(u64 ei_start, u64 ei_last, u64 start,
			 u64 *sizep, u64 align)
{
	u64 addr, last;

	addr = round_up(ei_start, align);
	if (addr < start)
		addr = round_up(start, align);
	if (addr >= ei_last)
		goto out;
	*sizep = ei_last - addr;
	while (bad_addr_size(&addr, sizep, align) && addr + *sizep <= ei_last)
		;
	last = addr + *sizep;
	if (last > ei_last)
		goto out;

	return addr;

out:
	return MEMBLOCK_ERROR;
}

/*
 * Find next free range after start, and size is returned in *sizep
 */
u64 __init memblock_x86_find_in_range_size(u64 start, u64 *sizep, u64 align)
{
	struct memblock_region *r;

	for_each_memblock(memory, r) {
		u64 ei_start = r->base;
		u64 ei_last = ei_start + r->size;
		u64 addr;

		addr = __memblock_x86_find_in_range_size(ei_start, ei_last, start,
					 sizep, align);

		if (addr != MEMBLOCK_ERROR)
			return addr;
	}

	return MEMBLOCK_ERROR;
}

static __init struct range *find_range_array(int count)
{
	u64 end, size, mem;
	struct range *range;

	size = sizeof(struct range) * count;
	end = memblock.current_limit;

	mem = memblock_find_in_range(0, end, size, sizeof(struct range));
	if (mem == MEMBLOCK_ERROR)
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

#ifdef CONFIG_NO_BOOTMEM
static void __init memblock_x86_subtract_reserved(struct range *range, int az)
{
	u64 final_start, final_end;
	struct memblock_region *r;

	/* Take out region array itself at first*/
	memblock_free_reserved_regions();

	pr_info("Subtract (%ld early reservations)\n", memblock.reserved.cnt);

	for_each_memblock(reserved, r) {
		pr_info("  [%010llx-%010llx]\n", (u64)r->base, (u64)r->base + r->size - 1);
		final_start = PFN_DOWN(r->base);
		final_end = PFN_UP(r->base + r->size);
		if (final_start >= final_end)
			continue;
		subtract_range(range, az, final_start, final_end);
	}

	/* Put region array back ? */
	memblock_reserve_reserved_regions();
}

struct count_data {
	int nr;
};

static int __init count_work_fn(unsigned long start_pfn,
				unsigned long end_pfn, void *datax)
{
	struct count_data *data = datax;

	data->nr++;

	return 0;
}

static int __init count_early_node_map(int nodeid)
{
	struct count_data data;

	data.nr = 0;
	work_with_active_regions(nodeid, count_work_fn, &data);

	return data.nr;
}

int __init get_free_all_memory_range(struct range **rangep, int nodeid)
{
	int count;
	struct range *range;
	int nr_range;

	count = (memblock.reserved.cnt + count_early_node_map(nodeid)) * 2;

	range = find_range_array(count);
	nr_range = 0;

	/*
	 * Use early_node_map[] and memblock.reserved.region to get range array
	 * at first
	 */
	nr_range = add_from_early_node_map(range, count, nr_range, nodeid);
#ifdef CONFIG_X86_32
	subtract_range(range, count, max_low_pfn, -1ULL);
#endif
	memblock_x86_subtract_reserved(range, count);
	nr_range = clean_sort_range(range, count);

	*rangep = range;
	return nr_range;
}
#else
void __init memblock_x86_to_bootmem(u64 start, u64 end)
{
	int count;
	u64 final_start, final_end;
	struct memblock_region *r;

	/* Take out region array itself */
	memblock_free_reserved_regions();

	count  = memblock.reserved.cnt;
	pr_info("(%d early reservations) ==> bootmem [%010llx-%010llx]\n", count, start, end - 1);
	for_each_memblock(reserved, r) {
		pr_info("  [%010llx-%010llx] ", (u64)r->base, (u64)r->base + r->size - 1);
		final_start = max(start, r->base);
		final_end = min(end, r->base + r->size);
		if (final_start >= final_end) {
			pr_cont("\n");
			continue;
		}
		pr_cont(" ==> [%010llx-%010llx]\n", final_start, final_end - 1);
		reserve_bootmem_generic(final_start, final_end - final_start, BOOTMEM_DEFAULT);
	}

	/* Put region array back ? */
	memblock_reserve_reserved_regions();
}
#endif

void __init memblock_x86_reserve_range(u64 start, u64 end, char *name)
{
	if (start == end)
		return;

	if (WARN_ONCE(start > end, "memblock_x86_reserve_range: wrong range [%#llx, %#llx]\n", start, end))
		return;

	memblock_reserve(start, end - start);
}

void __init memblock_x86_free_range(u64 start, u64 end)
{
	if (start == end)
		return;

	if (WARN_ONCE(start > end, "memblock_x86_free_range: wrong range [%#llx, %#llx]\n", start, end))
		return;

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

/* Walk the memblock.memory map and register active regions within a node */
void __init memblock_x86_register_active_regions(int nid, unsigned long start_pfn,
					 unsigned long last_pfn)
{
	unsigned long ei_startpfn;
	unsigned long ei_endpfn;
	struct memblock_region *r;

	for_each_memblock(memory, r)
		if (memblock_x86_find_active_region(r, start_pfn, last_pfn,
					   &ei_startpfn, &ei_endpfn))
			add_active_range(nid, ei_startpfn, ei_endpfn);
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
