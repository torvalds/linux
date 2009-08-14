#include <linux/mm.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

/*
 * Write back the dirty D-caches, but not invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
void __weak __flush_wback_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt -= 8;
	}

	while (cnt) {
		asm volatile("ocbwb	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt--;
	}
}

/*
 * Write back the dirty D-caches and invalidate them.
 *
 * START: Virtual Address (U0, P1, or P3)
 * SIZE: Size of the region.
 */
void __weak __flush_purge_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt -= 8;
	}
	while (cnt) {
		asm volatile("ocbp	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt--;
	}
}

/*
 * No write back please
 */
void __weak __flush_invalidate_region(void *start, int size)
{
	reg_size_t aligned_start, v, cnt, end;

	aligned_start = register_align(start);
	v = aligned_start & ~(L1_CACHE_BYTES-1);
	end = (aligned_start + size + L1_CACHE_BYTES-1)
		& ~(L1_CACHE_BYTES-1);
	cnt = (end - v) / L1_CACHE_BYTES;

	while (cnt >= 8) {
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt -= 8;
	}

	while (cnt) {
		asm volatile("ocbi	@%0" : : "r" (v));
		v += L1_CACHE_BYTES;
		cnt--;
	}
}
