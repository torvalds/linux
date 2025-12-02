// SPDX-License-Identifier: GPL-2.0

#include <asm/rwonce.h>
#include <asm/page.h>
#include <asm/skey.h>

int skey_regions_initialized;

static inline unsigned long load_real_address(unsigned long address)
{
	unsigned long real;

	asm volatile(
		"	lra	%[real],0(%[address])"
		: [real] "=d" (real)
		: [address] "a" (address)
		: "cc");
	return real;
}

/*
 * Initialize storage keys of registered memory regions with the
 * default key. This is useful for code which is executed with a
 * non-default access key.
 */
void __skey_regions_initialize(void)
{
	unsigned long address, real;
	struct skey_region *r, *end;

	r = __skey_region_start;
	end = __skey_region_end;
	while (r < end) {
		address = r->start & PAGE_MASK;
		do {
			real = load_real_address(address);
			page_set_storage_key(real, PAGE_DEFAULT_KEY, 1);
			address += PAGE_SIZE;
		} while (address < r->end);
		r++;
	}
	/*
	 * Make sure storage keys are initialized before
	 * skey_regions_initialized is changed.
	 */
	barrier();
	WRITE_ONCE(skey_regions_initialized, 1);
}
