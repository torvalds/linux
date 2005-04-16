/* $Id: palloc.c,v 1.4 1996/04/25 06:09:48 davem Exp $
 * palloc.c:  Memory allocation from the Sun PROM.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/openprom.h>
#include <asm/oplib.h>

/* You should not call these routines after memory management
 * has been initialized in the kernel, if fact you should not
 * use these if at all possible in the kernel.  They are mainly
 * to be used for a bootloader for temporary allocations which
 * it will free before jumping into the kernel it has loaded.
 *
 * Also, these routines don't work on V0 proms, only V2 and later.
 */

/* Allocate a chunk of memory of size 'num_bytes' giving a suggestion
 * of virtual_hint as the preferred virtual base address of this chunk.
 * There are no guarantees that you will get the allocation, or that
 * the prom will abide by your "hint".  So check your return value.
 */
char *
prom_alloc(char *virtual_hint, unsigned int num_bytes)
{
	if(prom_vers == PROM_V0) return (char *) 0x0;
	if(num_bytes == 0x0) return (char *) 0x0;
	return (*(romvec->pv_v2devops.v2_dumb_mem_alloc))(virtual_hint, num_bytes);
}

/* Free a previously allocated chunk back to the prom at virtual address
 * 'vaddr' of size 'num_bytes'.  NOTE: This vaddr is not the hint you
 * used for the allocation, but the virtual address the prom actually
 * returned to you.  They may be have been the same, they may have not,
 * doesn't matter.
 */
void
prom_free(char *vaddr, unsigned int num_bytes)
{
	if((prom_vers == PROM_V0) || (num_bytes == 0x0)) return;
	(*(romvec->pv_v2devops.v2_dumb_mem_free))(vaddr, num_bytes);
	return;
}
