#ifndef _ASM_X86_MACH_GENERIC_MACH_IPI_H
#define _ASM_X86_MACH_GENERIC_MACH_IPI_H

#include <asm/genapic.h>

#define send_IPI_mask (genapic->send_IPI_mask)
#define send_IPI_allbutself (genapic->send_IPI_allbutself)
#define send_IPI_all (genapic->send_IPI_all)

#endif /* _ASM_X86_MACH_GENERIC_MACH_IPI_H */
