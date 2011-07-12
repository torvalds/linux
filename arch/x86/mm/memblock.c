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
