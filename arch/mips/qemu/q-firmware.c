#include <linux/init.h>
#include <asm/bootinfo.h>

void __init prom_init(void)
{
	add_memory_region(0x0<<20, 0x10<<20, BOOT_MEM_RAM);
}
