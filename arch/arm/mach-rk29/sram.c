/*
 * Copyright (C) 2008-2009 ST-Ericsson AB
 * License terms: GNU General Public License (GPL) version 2
 * TCM memory handling for ARM systems
 *
 * Author: Linus Walleij <linus.walleij@stericsson.com>
 * Author: Rickard Andersson <rickard.andersson@stericsson.com>
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
#include <mach/rk29_iomap.h>


/* SRAM section definitions from the linker */
extern char __sram_code_start, __ssram_code_text, __esram_code_text;
extern char __sram_data_start, __ssram_data, __esram_data;

static struct map_desc sram_code_iomap[] __initdata = {
	{
		.virtual	= SRAM_CODE_OFFSET,
//		.pfn		= __phys_to_pfn(RK29_SRAM_PHYS),
		.pfn		= __phys_to_pfn(0x0),
	//	.length		=  (SRAM_CODE_END - SRAM_CODE_OFFSET + 1),
		.length		=  1024*1024, // (SRAM_CODE_END - SRAM_CODE_OFFSET + 1),
		.type		=  MT_MEMORY//MT_MEMORY //MT_HIGH_VECTORS//MT_MEMORY //MT_UNCACHED
	}
};

static struct map_desc sram_data_iomap[] __initdata = {
	{
		.virtual	= SRAM_DATA_OFFSET,
	//	.pfn		= __phys_to_pfn(RK29_SRAM_PHYS+0x2000),
		.pfn		= __phys_to_pfn(0x2000),
		.length		= (SRAM_DATA_END - SRAM_DATA_OFFSET + 1),
		.type		= MT_HIGH_VECTORS //MT_MEMORY //MT_UNCACHED
	}
};


int __init rk29_sram_init(void)
{
	char *start;
	char *end;
	char *ram;
	
	iotable_init(sram_code_iomap, 1);
//       iotable_init(sram_data_iomap, 1);
	   
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

       printk("CPU SRAM: copied sram code from %p to %p - %p \n", ram, start, end);
	printk("%08x %08x\n", *(u32 *)0xFF400628, *(u32 *)0xFF40062C);
//	extern void DDR_EnterSelfRefresh();
//	DDR_EnterSelfRefresh();

	printk("CPU SRAM: start = [%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x]\n",
		start[0],start[1],start[2],start[3],start[4],start[5],start[6],start[7],start[8],start[9],start[10],start[11],start[12],start[13],start[14],start[15]);
	start += 0x10;
	printk("CPU SRAM: start = [%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x]\n",
		start[0],start[1],start[2],start[3],start[4],start[5],start[6],start[7],start[8],start[9],start[10],start[11],start[12],start[13],start[14],start[15]);

	printk("CPU SRAM: ram = [%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x]\n",
		ram[0],ram[1],ram[2],ram[3],ram[4],ram[5],ram[6],ram[7],ram[8],ram[9],ram[10],ram[11],ram[12],ram[13],ram[14],ram[15]);

	  	
	/* Copy data from RAM to SRAM DATA */
	start = &__ssram_data;
	end   = &__esram_data;
	ram   = &__sram_data_start;
	memcpy(start, ram, (end-start));
	
	printk("CPU SRAM: copied sram data from %p to %p - %p\n", ram,start, end);

	return 0;
}
