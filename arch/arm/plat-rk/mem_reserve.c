#include <plat/board.h>
#include <linux/memblock.h>
#include <asm/setup.h>

static size_t reserved_size = 0;
static phys_addr_t reserved_base_end = 0;

phys_addr_t __init board_mem_reserve_add(char *name, size_t size)
{
    phys_addr_t base = 0;

    if(reserved_base_end == 0)
        reserved_base_end  = meminfo.bank[0].start + meminfo.bank[0].size;

    reserved_size += size;
    base  = reserved_base_end - reserved_size;
    pr_info("memory reserve: Memory(base:0x%x size:%dM) reserved for <%s>\n", 
                    base, size/SZ_1M, name);
    return base;
}

void __init board_mem_reserved(void)
{
    phys_addr_t base = reserved_base_end - reserved_size;

    if(reserved_size){
        memblock_remove(base, reserved_size);
	    pr_info("memory reserve: Total reserved %dM\n", reserved_size/SZ_1M);
    }
}
