#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/string.h>

#include <asm/system.h>

void * __init_refok alloc_maybe_bootmem(size_t size, gfp_t mask)
{
	if (mem_init_done)
		return kmalloc(size, mask);
	else
		return alloc_bootmem(size);
}

void * __init_refok zalloc_maybe_bootmem(size_t size, gfp_t mask)
{
	void *p;

	if (mem_init_done)
		p = kzalloc(size, mask);
	else {
		p = alloc_bootmem(size);
		if (p)
			memset(p, 0, size);
	}
	return p;
}
