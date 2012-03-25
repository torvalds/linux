/*
 * License terms: GNU General Public License (GPL) version 2
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/ioport.h>
#include <linux/genalloc.h>
#include <linux/string.h> /* memcpy */
#include <asm/page.h> /* PAGE_SHIFT */
#include <asm/cputype.h>
#include <asm/mach/map.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <mach/memory.h>
#include <plat/sram.h>


/* SRAM section definitions from the linker */
extern char __sram_code_start, __ssram_code_text, __esram_code_text;
extern char __sram_data_start, __ssram_data, __esram_data;

static struct map_desc sram_code_iomap[] __initdata = {
	{
		.virtual	= (unsigned long)SRAM_CODE_OFFSET & PAGE_MASK,
		.pfn		= __phys_to_pfn(0x0),
		.length		=  1024*1024,
		.type		=  MT_MEMORY
	}
};

int __init rk29_sram_init(void)
{
	char *start;
	char *end;
	char *ram;

	iotable_init(sram_code_iomap, 1);

	/*
	 * Normally devicemaps_init() would flush caches and tlb after
	 * mdesc->map_io(), but since we're called from map_io(), we
	 * must do it here.
	 */
	local_flush_tlb_all();
	flush_cache_all();

        memset((char *)SRAM_CODE_OFFSET,0x0,(SRAM_CODE_END - SRAM_CODE_OFFSET + 1));
	memset((char *)SRAM_DATA_OFFSET,0x0,(SRAM_DATA_END - SRAM_DATA_OFFSET + 1));

	/* Copy code from RAM to SRAM CODE */
	start = &__ssram_code_text;
	end   = &__esram_code_text;
	ram   = &__sram_code_start;
	memcpy(start, ram, (end-start));
	flush_icache_range((unsigned long) start, (unsigned long) end);

	printk("CPU SRAM: copied sram code from %p to %p - %p\n", ram, start, end);

	/* Copy data from RAM to SRAM DATA */
	start = &__ssram_data;
	end   = &__esram_data;
	ram   = &__sram_data_start;
	memcpy(start, ram, (end-start));

	printk("CPU SRAM: copied sram data from %p to %p - %p\n", ram,start, end);

	return 0;
}

void __sramfunc sram_printascii(const char *s)
{
	while (*s) {
		sram_printch(*s);
		s++;
	}
}

void __sramfunc sram_printhex(unsigned int hex)
{
	int i = 8;
	sram_printch('0');
	sram_printch('x');
	while (i--) {
		unsigned char c = (hex & 0xF0000000) >> 28;
		sram_printch(c < 0xa ? c + '0' : c - 0xa + 'a');
		hex <<= 4;
	}
}
