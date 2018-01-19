/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC64_SPARSEMEM_H
#define _SPARC64_SPARSEMEM_H

#ifdef __KERNEL__

#include <asm/page.h>

#define SECTION_SIZE_BITS       30
#define MAX_PHYSADDR_BITS       MAX_PHYS_ADDRESS_BITS
#define MAX_PHYSMEM_BITS        MAX_PHYS_ADDRESS_BITS

#endif /* !(__KERNEL__) */

#endif /* !(_SPARC64_SPARSEMEM_H) */
