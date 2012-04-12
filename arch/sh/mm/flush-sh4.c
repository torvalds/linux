#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>
#include <asm/traps.h>

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
static void sh4__flush_wback_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		__ocbwb(v); v += L1_CACHE_BYTES;
		cnt -= 8;
	}

	while (cnt) {
		__ocbwb(v); v += L1_CACHE_BYTES;
		cnt--;
	}
}

/*
 * Write back the dirty D-caches and invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
static void sh4__flush_purge_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		__ocbp(v); v += L1_CACHE_BYTES;
		cnt -= 8;
	}
	while (cnt) {
		__ocbp(v); v += L1_CACHE_BYTES;
		cnt--;
	}
}

/*
 * No write back please
 */
static void sh4__flush_invalidate_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		__ocbi(v); v += L1_CACHE_BYTES;
		cnt -= 8;
	}

	while (cnt) {
		__ocbi(v); v += L1_CACHE_BYTES;
		cnt--;
	}
}

void __init sh4__flush_region_init(void)
{
	__flush_wback_region		= sh4__flush_wback_region;
	__flush_invalidate_region	= sh4__flush_invalidate_region;
	__flush_purge_region		= sh4__flush_purge_region;
}
