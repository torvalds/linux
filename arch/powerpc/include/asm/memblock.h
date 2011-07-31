#ifndef _ASM_POWERPC_MEMBLOCK_H
#define _ASM_POWERPC_MEMBLOCK_H

#include <asm/udbg.h>

#define MEMBLOCK_DBG(fmt...) udbg_printf(fmt)

#ifdef CONFIG_PPC32
extern phys_addr_t lowmem_end_addr;
#define MEMBLOCK_REAL_LIMIT	lowmem_end_addr
#else
#define MEMBLOCK_REAL_LIMIT	0
#endif

#endif /* _ASM_POWERPC_MEMBLOCK_H */
