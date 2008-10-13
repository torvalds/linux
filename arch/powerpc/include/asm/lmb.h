#ifndef _ASM_POWERPC_LMB_H
#define _ASM_POWERPC_LMB_H

#include <asm/udbg.h>

#define LMB_DBG(fmt...) udbg_printf(fmt)

#ifdef CONFIG_PPC32
extern phys_addr_t lowmem_end_addr;
#define LMB_REAL_LIMIT	lowmem_end_addr
#else
#define LMB_REAL_LIMIT	0
#endif

#endif /* _ASM_POWERPC_LMB_H */
