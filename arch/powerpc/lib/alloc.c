#include <linux/types.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/bootmem.h>
#include <linux/string.h>
#include <asm/setup.h>


void * __init_refok zalloc_maybe_bootmem(size_t size, gfp_t mask)
{
	void *p;

	if (mem_init_done)
		p = kzalloc(size, mask);
	else {
		p = memblock_virt_alloc(size, 0);
	}
	return p;
}
