/*
 * Copyright IBM Corp. 2005
 *
 * Author(s): Rolf Adelsberger <adelsberger@de.ibm.com>
 *
 */

#ifndef _S390_KEXEC_H
#define _S390_KEXEC_H

#include <asm/processor.h>
#include <asm/page.h>
/*
 * KEXEC_SOURCE_MEMORY_LIMIT maximum page get_free_page can return.
 * I.e. Maximum page that is mapped directly into kernel memory,
 * and kmap is not required.
 */

/* Maximum physical address we can use pages from */
#define KEXEC_SOURCE_MEMORY_LIMIT (-1UL)

/* Maximum address we can reach in physical address mode */
#define KEXEC_DESTINATION_MEMORY_LIMIT (-1UL)

/* Maximum address we can use for the control pages */
/* Not more than 2GB */
#define KEXEC_CONTROL_MEMORY_LIMIT (1UL<<31)

/* Allocate control page with GFP_DMA */
#define KEXEC_CONTROL_MEMORY_GFP GFP_DMA

/* Maximum address we can use for the crash control pages */
#define KEXEC_CRASH_CONTROL_MEMORY_LIMIT (-1UL)

/* Allocate one page for the pdp and the second for the code */
#define KEXEC_CONTROL_PAGE_SIZE 4096

/* Alignment of crashkernel memory */
#define KEXEC_CRASH_MEM_ALIGN HPAGE_SIZE

/* The native architecture */
#define KEXEC_ARCH KEXEC_ARCH_S390

/* Provide a dummy definition to avoid build failures. */
static inline void crash_setup_regs(struct pt_regs *newregs,
					struct pt_regs *oldregs) { }

#endif /*_S390_KEXEC_H */
